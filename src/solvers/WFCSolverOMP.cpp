#include "wfc/solvers/WFCSolverOMP.hpp"

#include "wfc/Bitset.hpp"
#include "wfc/internal/SolverCommon.hpp"

#include <atomic>
#include <cstdint>
#include <vector>

#include <omp.h>

namespace wfc {

namespace {

// Word-level atomic intersection of `dst` with `src`. AND is associative
// and commutative, so the final state across concurrent writers is fully
// defined regardless of observed ordering. Returns true if this call cleared
// at least one bit (used to decide whether the neighbor entered the next
// BFS frontier).
//
// Optimisation: a relaxed load before the `lock and` instruction skips the
// expensive RMW when no bits would change. The lock prefix triggers a
// cache-line invalidation broadcast on x86 (~80-3000 ns depending on the
// NUMA distance); the relaxed load only requires the line in shared state,
// which is cheap when the line is already cached. This is a no-op for
// correctness but cuts O(50 %) of the atomic traffic on workloads where the
// neighbor's wave is already a subset of the mask.
bool atomic_and_with(BitsetView dst, ConstBitsetView src) {
    bool changed = false;
    std::uint64_t* d = dst.words();
    const std::uint64_t* s = src.words();
    const std::size_t n = dst.word_count();
    for (std::size_t i = 0; i < n; ++i) {
        const std::uint64_t mask = s[i];
        if (mask == ~0ULL) continue;

        // Cheap relaxed read first: if (current & ~mask) is empty, the AND
        // would be a no-op and we can skip the expensive lock prefix.
        const std::uint64_t cur =
            __atomic_load_n(&d[i], __ATOMIC_RELAXED);
        if ((cur & ~mask) == 0ULL) continue;

        const std::uint64_t old_val =
            __atomic_fetch_and(&d[i], mask, __ATOMIC_RELAXED);
        if ((old_val & ~mask) != 0ULL) changed = true;
    }
    return changed;
}

// Per-task scratch: one allowed-bitset per (dx, dy) offset slot. Reused
// across cells of the chunk to avoid reallocation.
//
// `alignas(64)` keeps every thread's scratch on its own 64-byte cache line
// so two adjacent scratches in `thread_scratch[]` cannot induce false
// sharing during BFS propagation.
struct alignas(64) OffsetScratch {
    int N;
    int offsets;       // (2*N-1)^2
    int num_tiles;
    std::vector<Bitset> allowed; // size = offsets, indexed by offset_idx

    OffsetScratch(int N_, int num_tiles_)
        : N(N_), offsets((2 * N_ - 1) * (2 * N_ - 1)),
          num_tiles(num_tiles_),
          allowed(static_cast<std::size_t>(offsets), Bitset(num_tiles_)) {}

    int offset_idx(int dx, int dy) const {
        return (dx + N - 1) * (2 * N - 1) + (dy + N - 1);
    }
    void reset_all() {
        for (auto& b : allowed) b.reset();
    }
};
// alignas(64) on the struct guarantees both alignment AND that sizeof is a
// multiple of 64 (C++17), so std::vector<OffsetScratch> elements never
// share a cache line.
static_assert(alignof(OffsetScratch) >= 64,
              "OffsetScratch must be cache-line aligned");

// Process one frontier cell in a single pass over its current wave snapshot:
// for every set bit `t` we OR `rules.allowed(t, dx, dy)` into the per-offset
// scratch bitset; then we apply each scratch atomically to the corresponding
// neighbor. This iterates the snapshot once instead of (2N-1)^2-1 times.
void process_cell(int c,
                  Wave& wave,
                  const OverlapRules& rules,
                  OffsetScratch& scratch,
                  std::vector<int>& local_next,
                  std::atomic<int>& propagations,
                  std::atomic<bool>& contradiction) {
    if (contradiction.load(std::memory_order_relaxed)) return;

    const int N = scratch.N;
    const int r = wave.row(c);
    const int col = wave.col(c);
    // Copy of this cell's wave, so concurrent writers don't shift the bits
    // we accumulate from. Cheap (1 word for L<=64).
    const Bitset src_snapshot{wave.at(c)};

    scratch.reset_all();

    // Single pass over the snapshot — accumulate all neighbor unions at once.
    src_snapshot.for_each_set([&](std::size_t t) {
        for (int dy = -(N - 1); dy <= N - 1; ++dy) {
            for (int dx = -(N - 1); dx <= N - 1; ++dx) {
                if (dx == 0 && dy == 0) continue;
                scratch.allowed[scratch.offset_idx(dx, dy)]
                    .or_with(rules.allowed(static_cast<int>(t), dx, dy));
            }
        }
    });

    // Apply each accumulated union to the corresponding neighbor.
    for (int dy = -(N - 1); dy <= N - 1; ++dy) {
        for (int dx = -(N - 1); dx <= N - 1; ++dx) {
            if (dx == 0 && dy == 0) continue;
            const int nc = wave.torus_idx(r + dy, col + dx);
            const Bitset& mask = scratch.allowed[scratch.offset_idx(dx, dy)];

            if (atomic_and_with(wave.at(nc), mask)) {
                propagations.fetch_add(1, std::memory_order_relaxed);
                if (!wave.at(nc).any()) {
                    contradiction.store(true, std::memory_order_relaxed);
                    return;
                }
                local_next.push_back(nc);
            }
        }
    }
}

// Each worker thread has its own frontier append buffer. `alignas(64)`
// keeps consecutive entries on different cache lines so concurrent
// `push_back` calls don't ping-pong the vector header bytes.
struct alignas(64) ThreadFrontier {
    std::vector<int> cells;
};
static_assert(alignof(ThreadFrontier) >= 64,
              "ThreadFrontier must be cache-line aligned");

bool propagate_tasks(Wave& wave,
                    const OverlapRules& rules,
                    int start_cell,
                    std::atomic<int>& propagations,
                    std::atomic<bool>& contradiction) {
    const int N = rules.N();
    const int num_tiles = rules.num_tiles();
    const int total = wave.num_cells();

    // Two pre-allocated frontier buffers; we ping-pong between them
    // across BFS levels. The "next" buffer is filled by concatenating
    // per-thread append vectors at the end of each level — no atomic
    // counter, no lock.
    std::vector<int> buf_a(static_cast<std::size_t>(total));
    std::vector<int> buf_b(static_cast<std::size_t>(total));
    int* frontier = buf_a.data();
    int* next = buf_b.data();
    int frontier_size = 1;
    int next_size = 0;
    frontier[0] = start_cell;

    // Per-cell flag set when a cell joins the next frontier; cleared
    // after the swap. CAS protects the dedup decision but the storage
    // is local to each thread.
    std::vector<std::uint8_t> in_queue(static_cast<std::size_t>(total), 0);

    bool finished = false;
    const int max_threads = omp_get_max_threads();

    // Pre-allocated per-thread state. Both arrays live for the whole
    // propagate call so first-touch happens once.
    std::vector<OffsetScratch> thread_scratch;
    thread_scratch.reserve(static_cast<std::size_t>(max_threads));
    for (int i = 0; i < max_threads; ++i) thread_scratch.emplace_back(N, num_tiles);

    std::vector<ThreadFrontier> thread_next(static_cast<std::size_t>(max_threads));
    for (auto& tf : thread_next) tf.cells.reserve(256);

    // Frontier threshold below which we run the level serially in the
    // master thread instead of spawning tasks. Tasks have ~10 µs of
    // wakeup+barrier overhead per level on 192-core EPYC; for short
    // levels (BFS often produces frontiers of 1-50 cells) this overhead
    // dominates the work. Empirically, a threshold proportional to the
    // task pool gives the best results: parallelise only if there are at
    // least max(64, max_threads) cells to distribute.
    const int kSerialFallback = std::max(64, max_threads);

    #pragma omp parallel shared(frontier, next, frontier_size, next_size, \
                                wave, rules, in_queue, propagations, \
                                contradiction, finished, thread_scratch, \
                                thread_next)
    {
        while (!finished) {
            const int chunk = std::max(1, frontier_size / (4 * max_threads));
            const bool use_tasks = frontier_size >= kSerialFallback;

            #pragma omp single
            {
                if (!use_tasks) {
                    // Serial fast path for short BFS levels. Avoids the
                    // task creation + barrier overhead that dominates at
                    // high thread counts on small frontiers.
                    OffsetScratch& scratch = thread_scratch[0];
                    std::vector<int>& my_next = thread_next[0].cells;
                    std::vector<int> local_next;
                    local_next.reserve(static_cast<std::size_t>(
                        frontier_size * scratch.offsets));
                    for (int i = 0; i < frontier_size; ++i) {
                        process_cell(frontier[i], wave, rules, scratch,
                                     local_next, propagations, contradiction);
                        if (contradiction.load(std::memory_order_relaxed)) break;
                    }
                    for (int nc : local_next) {
                        std::uint8_t expected = 0;
                        if (__atomic_compare_exchange_n(
                                &in_queue[static_cast<std::size_t>(nc)],
                                &expected, static_cast<std::uint8_t>(1),
                                false,
                                __ATOMIC_RELAXED, __ATOMIC_RELAXED)) {
                            my_next.push_back(nc);
                        }
                    }
                } else {
                for (int start = 0; start < frontier_size; start += chunk) {
                    const int end = std::min(start + chunk, frontier_size);
                    #pragma omp task firstprivate(start, end) \
                        shared(frontier, wave, rules, in_queue, \
                               propagations, contradiction, \
                               thread_scratch, thread_next)
                    {
                        const int tid = omp_get_thread_num();
                        OffsetScratch& scratch =
                            thread_scratch[static_cast<std::size_t>(tid)];
                        std::vector<int>& my_next =
                            thread_next[static_cast<std::size_t>(tid)].cells;
                        std::vector<int> local_next;
                        local_next.reserve(static_cast<std::size_t>(
                            (end - start) * scratch.offsets));

                        for (int i = start; i < end; ++i) {
                            process_cell(frontier[i], wave, rules, scratch,
                                         local_next, propagations, contradiction);
                            if (contradiction.load(std::memory_order_relaxed)) break;
                        }

                        // Dedup via CAS on in_queue, append to thread-local
                        // buffer (no shared atomic counter).
                        for (int nc : local_next) {
                            std::uint8_t expected = 0;
                            if (__atomic_compare_exchange_n(
                                    &in_queue[static_cast<std::size_t>(nc)],
                                    &expected, static_cast<std::uint8_t>(1),
                                    false,
                                    __ATOMIC_RELAXED, __ATOMIC_RELAXED)) {
                                my_next.push_back(nc);
                            }
                        }
                    }
                }
                }
            } // implicit barrier + all tasks done

            #pragma omp single
            {
                // Concatenate per-thread buffers into the global `next`.
                // Ordering across threads is non-deterministic, but the
                // BFS level result is order-independent (the propagation
                // operations are idempotent and commutative).
                next_size = 0;
                for (auto& tf : thread_next) {
                    const std::size_t k = tf.cells.size();
                    if (k > 0) {
                        std::copy(tf.cells.begin(), tf.cells.end(),
                                  next + next_size);
                        next_size += static_cast<int>(k);
                        tf.cells.clear();
                    }
                }

                for (int i = 0; i < next_size; ++i)
                    in_queue[static_cast<std::size_t>(next[i])] = 0;
                std::swap(frontier, next);
                frontier_size = next_size;
                next_size = 0;
                if (frontier_size == 0 ||
                    contradiction.load(std::memory_order_relaxed))
                    finished = true;
            }
        }
    }

    return !contradiction.load(std::memory_order_relaxed);
}

// Parallel min-entropy reduction using explicit OMP tasks. Each task scans
// a contiguous slice of the wave and emits one MinEntropyResult; the final
// reduction iterates partials in chunk order so the result is deterministic.
MinEntropyResult parallel_min_entropy(const Wave& wave,
                                      const std::vector<std::uint32_t>& freq,
                                      std::uint64_t seed,
                                      int max_threads) {
    const int total = wave.num_cells();
    const int chunk = std::max(64, total / (4 * std::max(1, max_threads)));
    const int n_chunks = (total + chunk - 1) / chunk;

    std::vector<MinEntropyResult> partials(static_cast<std::size_t>(n_chunks));

    #pragma omp parallel shared(wave, freq, partials)
    {
        #pragma omp single
        {
            for (int k = 0; k < n_chunks; ++k) {
                const int start = k * chunk;
                const int end = std::min(start + chunk, total);
                #pragma omp task firstprivate(k, start, end, seed) \
                    shared(wave, freq, partials)
                {
                    MinEntropyResult best;
                    for (int c = start; c < end; ++c) {
                        if (wave.at(c).count() <= 1) continue;
                        const double key = weighted_entropy(wave.at(c), freq)
                                         + cell_jitter(c, seed);
                        if (key < best.value) { best.value = key; best.cell = c; }
                    }
                    partials[static_cast<std::size_t>(k)] = best;
                }
            }
        }
    }

    MinEntropyResult global;
    for (const auto& p : partials)
        if (p.cell != -1 && p.value < global.value) global = p;
    return global;
}

} // namespace

void WFCSolverOMP::on_solve_begin() {
    if (num_threads_ > 0) omp_set_num_threads(num_threads_);
}

int WFCSolverOMP::pick_cell(const Wave& wave,
                            const TileSet& tiles,
                            std::uint64_t seed) {
    return parallel_min_entropy(wave, tiles.frequencies(), seed,
                                omp_get_max_threads()).cell;
}

bool WFCSolverOMP::propagate(Wave& wave,
                             const OverlapRules& rules,
                             int start_cell,
                             SolverStats& stats) {
    std::atomic<int> propagations{stats.propagations};
    std::atomic<bool> contradiction{false};
    const bool ok = propagate_tasks(wave, rules, start_cell,
                                    propagations, contradiction);
    stats.propagations = propagations.load();
    return ok;
}

} // namespace wfc
