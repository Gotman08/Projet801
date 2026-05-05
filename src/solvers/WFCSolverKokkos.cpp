#include "wfc/solvers/WFCSolverKokkos.hpp"

#include "wfc/Bitset.hpp"
#include "wfc/internal/SolverCommon.hpp"

#include <Kokkos_Core.hpp>

#include <cstdint>
#include <cstring>
#include <vector>

namespace wfc {

namespace {

// Maximum number of u64 words per cell bitset, bounded statically so that
// the parallel_for body can use stack-allocated scratch (snapshot, allowed)
// rather than heap allocation. Eight words = 512 tiles, well above any
// realistic L for this problem (binary L=11, terrain L=33, smooth L=12).
// If a sample produces more tiles, we fall back to the host-only OMP solver
// at the call site (asserted in propagate_kokkos).
constexpr std::size_t MAX_WORDS_PER_CELL = 8;

// Default execution space and memory space. On a CPU build (Kokkos OpenMP
// or Serial backend), this is HostSpace, so the View<u64*> we allocate
// lives in regular host memory and is directly accessible from host code.
// On a CUDA / HIP build, this is the device memory space; we deep_copy
// from host buffers (Wave's std::vector, the flattened OverlapRules) at
// solve entry and back at exit. The same source code compiles for both.
using ExecSpace = Kokkos::DefaultExecutionSpace;
using MemSpace = ExecSpace::memory_space;
using U64View = Kokkos::View<std::uint64_t*, MemSpace>;
using ConstU64View = Kokkos::View<const std::uint64_t*, MemSpace>;
using IntView = Kokkos::View<int*, MemSpace>;
using U8View = Kokkos::View<std::uint8_t*, MemSpace>;

// True when the default Kokkos memory space is the host. In that case
// the parallel_for body can read/write Wave's std::vector directly via
// an UnmanagedView wrapping wave.raw_words(), keeping the refactor
// zero-overhead vs the pre-refactor host-only Kokkos backend. On a
// CUDA / HIP build this is false and we materialise the views with
// managed allocations + deep_copy at solve entry and exit.
constexpr bool kHostOnly = std::is_same<MemSpace, Kokkos::HostSpace>::value;

// Portable count-trailing-zeros for u64. Kokkos 4.x has Kokkos::countr_zero
// but we avoid pinning to a specific Kokkos version. The host code path
// inlines to BSF/TZCNT on x86; on CUDA the compiler maps to __ffsll.
KOKKOS_INLINE_FUNCTION int ctz64(std::uint64_t x) {
#if defined(__CUDA_ARCH__) || defined(__HIP_DEVICE_COMPILE__)
    // __ffsll returns 1-based bit index, 0 if x == 0.
    return __ffsll(static_cast<long long>(x)) - 1;
#elif defined(__GNUC__) || defined(__clang__)
    return __builtin_ctzll(x);
#else
    int n = 0;
    while ((x & 1ULL) == 0) { x >>= 1; ++n; }
    return n;
#endif
}

// Device-friendly POD view over Wave's flat word buffer. Kokkos::View is
// trivially copyable, so capturing this struct by value into a parallel_for
// functor is safe on both CPU and GPU.
struct WaveView_K {
    U64View words;          // flat row-major: cell c at words[c * W .. c * W + W)
    int rows;
    int cols;
    int num_tiles;
    int words_per_cell;     // W

    KOKKOS_INLINE_FUNCTION int idx(int r, int c) const noexcept {
        return r * cols + c;
    }
    KOKKOS_INLINE_FUNCTION int row(int cell) const noexcept {
        return cell / cols;
    }
    KOKKOS_INLINE_FUNCTION int col(int cell) const noexcept {
        return cell % cols;
    }
    // Branch-only toroidal index (callers pass offsets bounded by ±(N-1),
    // always smaller than rows/cols on realistic inputs).
    KOKKOS_INLINE_FUNCTION int torus_idx(int r, int c) const noexcept {
        if (r < 0)              r += rows;
        else if (r >= rows)     r -= rows;
        if (c < 0)              c += cols;
        else if (c >= cols)     c -= cols;
        return r * cols + c;
    }
};

// Device-friendly view over a flattened OverlapRules. The underlying
// `data` buffer concatenates one bitset per (tile, offset) pair, each of
// `words_per_rule` u64 words. Indexing via offset_index(dx, dy) matches the
// host OverlapRules layout exactly, so the rules are bit-for-bit identical
// between solvers.
struct RulesView_K {
    ConstU64View data;
    int num_tiles;
    int N;
    int stride;             // 2*N - 1
    int offsets_count;      // (2*N - 1)^2
    int words_per_rule;     // = wave.words_per_cell

    KOKKOS_INLINE_FUNCTION int offset_index(int dx, int dy) const noexcept {
        return (dx + N - 1) * stride + (dy + N - 1);
    }
    KOKKOS_INLINE_FUNCTION std::size_t rule_offset(int t, int dx, int dy) const noexcept {
        return static_cast<std::size_t>(t * offsets_count + offset_index(dx, dy))
             * static_cast<std::size_t>(words_per_rule);
    }
};

// parallel_for body for one BFS level. Each invocation processes a single
// frontier cell; concurrency is `cur_size`. Captured by value, so the
// functor is device-callable on CUDA / HIP. Optimisations preserved /
// added relative to the previous host-only Kokkos version:
//
//  1. Stack snapshot of the cell's wave (anti-race), same idea as the
//     OMP backend's `Bitset src_snapshot{wave.at(c)}` but using a fixed
//     u64 array instead of heap-allocated Bitset. Avoids per-iteration
//     heap allocation, which is a hard requirement on GPU and a measurable
//     win on CPU too.
//
//  2. Stack `allowed` accumulator per (dx, dy) (size MAX_WORDS_PER_CELL,
//     trivially zero-initialised each iteration via memset).
//
//  3. Relaxed load before the atomic AND, mirroring the OMP optimisation
//     (50% reduction in lock-cycle traffic on workloads where the neighbour
//     wave is already a subset of the mask). Newly added in this refactor;
//     previous Kokkos version unconditionally issued atomic_fetch_and.
//
//  4. Skip atomic when mask == all-ones (no bits would be cleared).
//
//  5. atomic_compare_exchange dedup of in_queue, single atomic_fetch_add
//     onto next_count for the writer slot.
struct PropagateOp {
    WaveView_K wave;
    RulesView_K rules;
    IntView frontier;
    IntView next_frontier;
    IntView next_count;            // size 1
    U8View in_queue;
    IntView contradiction_flag;    // size 1

    KOKKOS_INLINE_FUNCTION
    void operator()(const int idx) const {
        if (Kokkos::atomic_load(&contradiction_flag(0))) return;

        const int c = frontier(idx);
        const int r = wave.row(c);
        const int col = wave.col(c);
        const int W = wave.words_per_cell;
        const std::size_t cell_off = static_cast<std::size_t>(c)
                                   * static_cast<std::size_t>(W);

        // Stack snapshot, see optim (1).
        std::uint64_t snap[MAX_WORDS_PER_CELL];
        for (int w = 0; w < W; ++w) snap[w] = wave.words(cell_off + w);

        for (int dy = -(rules.N - 1); dy <= rules.N - 1; ++dy) {
            for (int dx = -(rules.N - 1); dx <= rules.N - 1; ++dx) {
                if (dx == 0 && dy == 0) continue;
                const int nc = wave.torus_idx(r + dy, col + dx);

                // Stack allowed accumulator, see optim (2).
                std::uint64_t allowed[MAX_WORDS_PER_CELL];
                for (int w = 0; w < W; ++w) allowed[w] = 0ULL;

                // For each set tile id in the snapshot, OR in its rule's
                // bitset for this offset. Manual ctz loop instead of
                // for_each_set lambda since lambdas don't go through
                // KOKKOS_INLINE_FUNCTION uniformly.
                for (int w = 0; w < W; ++w) {
                    std::uint64_t bits = snap[w];
                    const int base = w * 64;
                    while (bits) {
                        const int bit = ctz64(bits);
                        const int t = base + bit;
                        const std::size_t rule_off = rules.rule_offset(t, dx, dy);
                        for (int i = 0; i < W; ++i) {
                            allowed[i] |= rules.data(rule_off + i);
                        }
                        bits &= bits - 1;
                    }
                }

                // Apply allowed mask to neighbour with atomic AND. Optim (3):
                // relaxed read first to skip the atomic when the mask would
                // be a no-op (`cur & ~mask == 0`). On x86 a Kokkos atomic
                // AND maps to `lock and` (~80-3000 ns inter-NUMA); the
                // relaxed load only needs the cache line in shared state.
                // On CUDA the atomic_fetch_and goes through global atomic
                // units; the relaxed load checks shared memory first. This
                // is correctness-preserving (the AND is idempotent and the
                // race window only loses the chance to skip a pending no-op).
                const std::size_t nc_off = static_cast<std::size_t>(nc)
                                         * static_cast<std::size_t>(W);
                bool changed = false;
                for (int i = 0; i < W; ++i) {
                    const std::uint64_t mask = allowed[i];
                    if (mask == ~0ULL) continue;
                    const std::uint64_t cur =
                        Kokkos::atomic_load(&wave.words(nc_off + i));
                    if ((cur & ~mask) == 0ULL) continue;
                    const std::uint64_t old_val =
                        Kokkos::atomic_fetch_and(&wave.words(nc_off + i), mask);
                    if ((old_val & ~mask) != 0ULL) changed = true;
                }

                if (changed) {
                    // Re-read all words to check for contradiction. Racy with
                    // concurrent writers but the same race shape as the OMP
                    // backend's `wave.at(nc).any()` check, eventually
                    // consistent because contradictions don't go away.
                    bool any_set = false;
                    for (int i = 0; i < W; ++i) {
                        if (Kokkos::atomic_load(&wave.words(nc_off + i)) != 0ULL) {
                            any_set = true;
                            break;
                        }
                    }
                    if (!any_set) {
                        Kokkos::atomic_store(&contradiction_flag(0), 1);
                        return;
                    }
                    // Optim (5): CAS-based dedup, single fetch-add for slot.
                    std::uint8_t expected = 0;
                    if (Kokkos::atomic_compare_exchange(
                            &in_queue(nc), expected,
                            static_cast<std::uint8_t>(1)) == 0) {
                        const int slot = Kokkos::atomic_fetch_add(&next_count(0), 1);
                        next_frontier(slot) = nc;
                    }
                }
            }
        }
    }
};

// Build a flat rules buffer once and copy it into a device view. Reused
// across propagate calls (rules are constant per solve).
class RulesCache {
public:
    void rebuild(const OverlapRules& rules) {
        const int num_tiles = rules.num_tiles();
        const int offsets = rules.offsets();
        words_per_rule_ = static_cast<int>(detail::words_for(static_cast<std::size_t>(num_tiles)));

        const std::size_t total = static_cast<std::size_t>(num_tiles)
                                * static_cast<std::size_t>(offsets)
                                * static_cast<std::size_t>(words_per_rule_);
        // Reuse buffer if same size; otherwise reallocate.
        if (data_.extent(0) != total) {
            data_ = U64View("wfc_rules_flat", total);
        }
        // Stage in a host buffer, then deep_copy. On HostSpace this is a
        // memcpy; on CudaSpace this is a real H2D copy.
        std::vector<std::uint64_t> staging(total, 0ULL);
        for (int t = 0; t < num_tiles; ++t) {
            for (int dy = -(rules.N() - 1); dy <= rules.N() - 1; ++dy) {
                for (int dx = -(rules.N() - 1); dx <= rules.N() - 1; ++dx) {
                    const Bitset& b = rules.allowed(t, dx, dy);
                    const std::size_t base =
                        (static_cast<std::size_t>(t) * offsets
                         + static_cast<std::size_t>(rules.offset_index(dx, dy)))
                        * static_cast<std::size_t>(words_per_rule_);
                    const std::uint64_t* src = b.words();
                    for (int i = 0; i < words_per_rule_; ++i) {
                        staging[base + i] = src[i];
                    }
                }
            }
        }
        Kokkos::View<const std::uint64_t*, Kokkos::HostSpace, Kokkos::MemoryTraits<Kokkos::Unmanaged>>
            host_view(staging.data(), total);
        Kokkos::deep_copy(data_, host_view);

        num_tiles_ = num_tiles;
        N_ = rules.N();
        stride_ = rules.stride();
        offsets_ = offsets;
    }

    RulesView_K make_view() const {
        return RulesView_K{
            data_, num_tiles_, N_, stride_, offsets_, words_per_rule_,
        };
    }

private:
    U64View data_;
    int num_tiles_ = 0;
    int N_ = 0;
    int stride_ = 0;
    int offsets_ = 0;
    int words_per_rule_ = 0;
};

// Forward declarations for the cleanup helpers.
struct KokkosScratch;
KokkosScratch& kokkos_scratch();

// Register a Kokkos finalize hook that clears the static caches before
// Kokkos tears down its memory subsystem. Without this hook, the static
// thread_local destructors run at process exit (after Kokkos::finalize)
// and trigger "View deallocated after finalize" errors.
void install_finalize_hook_once();

// Thread-local rules cache. Persistence across propagate calls means we
// flatten the rules just once per solve.
RulesCache& rules_cache() {
    static thread_local RulesCache c;
    install_finalize_hook_once();
    return c;
}

void clear_rules_cache() {
    rules_cache() = RulesCache{};
}

bool propagate_kokkos(Wave& wave,
                     const OverlapRules& /*rules*/,
                     int start_cell,
                     int& propagations,
                     U64View& wave_view,
                     IntView& frontier_a,
                     IntView& frontier_b,
                     IntView& next_count,
                     U8View& in_queue,
                     IntView& contradiction_view) {
    const int W = static_cast<int>(wave.words_per_cell());

    // Sync host wave → device. On host-only builds the wave_view IS
    // already wrapping wave.raw_words() (set up by ensure_scratch when
    // kHostOnly), so no copy needed. On a GPU build we deep_copy the
    // host wave into the managed device buffer here.
    if constexpr (!kHostOnly) {
        Kokkos::View<const std::uint64_t*, Kokkos::HostSpace,
                     Kokkos::MemoryTraits<Kokkos::Unmanaged>>
            host_wave(wave.raw_words(), wave.total_words());
        Kokkos::deep_copy(wave_view, host_wave);
    }

    // Reset frontier / dedup / contradiction flag for this propagate call.
    Kokkos::deep_copy(in_queue, std::uint8_t{0});
    Kokkos::deep_copy(contradiction_view, 0);
    {
        // Set frontier_a[0] = start_cell. Use a single-element subview so
        // the extents match (deep_copy fails if View shapes differ).
        int start = start_cell;
        auto first = Kokkos::subview(frontier_a, std::pair<int, int>(0, 1));
        Kokkos::View<int*, Kokkos::HostSpace,
                     Kokkos::MemoryTraits<Kokkos::Unmanaged>>
            init(&start, 1);
        Kokkos::deep_copy(first, init);
    }

    WaveView_K wv{wave_view, wave.rows(), wave.cols(),
                  wave.num_tiles(), W};
    RulesView_K rv = rules_cache().make_view();
    int cur_size = 1;
    auto* cur = &frontier_a;
    auto* nxt = &frontier_b;

    while (cur_size > 0) {
        // Reset next_count to zero for this BFS level.
        Kokkos::deep_copy(next_count, 0);

        PropagateOp op{wv, rv, *cur, *nxt, next_count, in_queue, contradiction_view};
        Kokkos::parallel_for("wfc_propagate_level", cur_size, op);
        Kokkos::fence();

        // Pull next_count and contradiction back to host.
        auto nc_host = Kokkos::create_mirror_view(next_count);
        Kokkos::deep_copy(nc_host, next_count);
        auto cf_host = Kokkos::create_mirror_view(contradiction_view);
        Kokkos::deep_copy(cf_host, contradiction_view);
        const int next_count_host = nc_host(0);
        const int contradiction_host = cf_host(0);

        propagations += next_count_host;
        if (contradiction_host) break;

        // Clear dedup flags for the next BFS level. Single deep_copy is
        // a memset on HostSpace and a single kernel launch on CUDA. We
        // could optimise by tracking which cells were touched and clearing
        // only those, but the per-level cost is dominated by parallel_for.
        if (next_count_host > 0) Kokkos::deep_copy(in_queue, std::uint8_t{0});

        std::swap(cur, nxt);
        cur_size = next_count_host;
    }

    // Sync device wave → host so subsequent host-side pick_cell sees the
    // updates. No-op on host-only builds (wave_view already aliases
    // wave.raw_words()).
    if constexpr (!kHostOnly) {
        Kokkos::View<std::uint64_t*, Kokkos::HostSpace,
                     Kokkos::MemoryTraits<Kokkos::Unmanaged>>
            host_wave(wave.raw_words(), wave.total_words());
        Kokkos::deep_copy(host_wave, wave_view);
    }

    auto cf_host = Kokkos::create_mirror_view(contradiction_view);
    Kokkos::deep_copy(cf_host, contradiction_view);
    return !cf_host(0);
}

} // namespace

void WFCSolverKokkos::on_solve_begin() {
    if (!Kokkos::is_initialized()) {
        Kokkos::initialize();
        we_initialised_ = true;
    } else {
        we_initialised_ = false;
    }
}

// Forward declarations for the static caches; defined in the anonymous
// namespace below. Cleared before Kokkos::finalize so the View destructors
// can deallocate while Kokkos is still alive.
namespace {
void clear_rules_cache();
void clear_kokkos_scratch();
} // namespace

void WFCSolverKokkos::on_solve_end() {
    if (we_initialised_) {
        // Release Kokkos-owned buffers (rules, wave, frontier, ...) before
        // calling finalize. Otherwise the Views' destructors run during
        // static thread_local destruction at process exit when Kokkos is
        // no longer initialised, triggering a runtime error.
        clear_rules_cache();
        clear_kokkos_scratch();
        Kokkos::finalize();
        we_initialised_ = false;
    }
}

namespace {

// Kokkos parallel min-entropy reduction. Uses the same chunked-then-ordered
// merge strategy as the OMP backend so the result is fully deterministic.
// The Kokkos Min/Max reducers are non-deterministic with regard to which
// equal-key element wins, so we collect partials into a host array and do
// the final reduction sequentially.
//
// This still operates on the host Wave (passed by reference) because the
// min-entropy scan is cheap relative to propagate and re-syncing the wave
// just for the scan would dominate the cost.
MinEntropyResult kokkos_min_entropy(const Wave& wave,
                                    const std::vector<std::uint32_t>& freq,
                                    std::uint64_t seed) {
    const int total = wave.num_cells();
    const int max_threads = Kokkos::DefaultHostExecutionSpace().concurrency();
    const int chunk = std::max(64, total / (4 * std::max(1, max_threads)));
    const int n_chunks = (total + chunk - 1) / chunk;

    std::vector<MinEntropyResult> partials(static_cast<std::size_t>(n_chunks));
    const Wave* wave_ptr = &wave;
    const std::vector<std::uint32_t>* freq_ptr = &freq;
    MinEntropyResult* partials_data = partials.data();

    // Force this parallel_for onto the host execution space, Wave's data
    // lives in std::vector and is not addressable from the device. The
    // GPU-portable version of this would mirror the wave to a device
    // view (as in propagate_kokkos) and run the scan on the device, but
    // that's a future optimisation; on CPU OpenMP the host scan is
    // already optimal.
    Kokkos::parallel_for("wfc_min_entropy",
        Kokkos::RangePolicy<Kokkos::DefaultHostExecutionSpace>(0, n_chunks),
        [=](const int k) {
        const int start = k * chunk;
        const int end = std::min(start + chunk, total);
        MinEntropyResult best;
        for (int c = start; c < end; ++c) {
            if (wave_ptr->at(c).count() <= 1) continue;
            const double key = weighted_entropy(wave_ptr->at(c), *freq_ptr)
                             + cell_jitter(c, seed);
            if (key < best.value) { best.value = key; best.cell = c; }
        }
        partials_data[k] = best;
    });
    Kokkos::fence();

    MinEntropyResult global;
    for (const auto& p : partials)
        if (p.cell != -1 && p.value < global.value) global = p;
    return global;
}

// Per-solver scratch, reallocated per (rows, cols, num_tiles) signature so
// that consecutive solves of the same shape reuse the buffers.
struct KokkosScratch {
    int rows = 0, cols = 0, num_tiles = 0, words_per_cell = 0;
    U64View wave_view;
    IntView frontier_a;
    IntView frontier_b;
    IntView next_count;
    U8View in_queue;
    IntView contradiction;
};

KokkosScratch& kokkos_scratch() {
    static thread_local KokkosScratch s;
    install_finalize_hook_once();
    return s;
}

void clear_kokkos_scratch() {
    kokkos_scratch() = KokkosScratch{};
}

void install_finalize_hook_once() {
    static thread_local bool installed = []() {
        // Push a callback that runs during Kokkos::finalize, before the
        // memory subsystem tears down. Resetting the caches here releases
        // the View<>'s underlying buffers while Kokkos is still alive.
        Kokkos::push_finalize_hook([]() {
            clear_rules_cache();
            clear_kokkos_scratch();
        });
        return true;
    }();
    (void)installed;
}

void ensure_scratch(KokkosScratch& s, Wave& wave) {
    const int total = wave.num_cells();
    const int W = static_cast<int>(wave.words_per_cell());
    const std::size_t total_words = wave.total_words();
    const bool resize = s.rows != wave.rows() || s.cols != wave.cols()
                     || s.num_tiles != wave.num_tiles()
                     || s.words_per_cell != W;
    if (resize) {
        // Wave view: on host, wrap the existing buffer (zero-copy);
        // on device, allocate a managed buffer that we'll deep_copy into.
        if constexpr (kHostOnly) {
            s.wave_view = U64View(wave.raw_words(), total_words);
        } else {
            s.wave_view = U64View("wfc_wave", total_words);
        }
        s.frontier_a    = IntView("wfc_frontier_a", total);
        s.frontier_b    = IntView("wfc_frontier_b", total);
        s.next_count    = IntView("wfc_next_count", 1);
        s.in_queue      = U8View("wfc_in_queue", total);
        s.contradiction = IntView("wfc_contradiction", 1);
        s.rows = wave.rows();
        s.cols = wave.cols();
        s.num_tiles = wave.num_tiles();
        s.words_per_cell = W;
    } else if constexpr (kHostOnly) {
        // Same shape as last solve, but the underlying Wave object may
        // be a fresh instance, re-wrap so we point at the current data.
        if (s.wave_view.data() != wave.raw_words()) {
            s.wave_view = U64View(wave.raw_words(), total_words);
        }
    }
}

} // namespace

int WFCSolverKokkos::pick_cell(const Wave& wave,
                               const TileSet& tiles,
                               std::uint64_t seed) {
    return kokkos_min_entropy(wave, tiles.frequencies(), seed).cell;
}

bool WFCSolverKokkos::propagate(Wave& wave,
                                const OverlapRules& rules,
                                int start_cell,
                                SolverStats& stats) {
    const int W = static_cast<int>(wave.words_per_cell());
    if (W > static_cast<int>(MAX_WORDS_PER_CELL)) {
        // Tile count exceeds the static bound (>512 tiles). The current
        // refactored Kokkos backend cannot handle this configuration on
        // GPU, fall back to a host-only implementation. We keep the
        // assertion tight here so the solver fails loudly rather than
        // silently producing wrong answers.
        throw std::runtime_error(
            "WFCSolverKokkos: words_per_cell exceeds MAX_WORDS_PER_CELL "
            "(>512 tiles). Increase MAX_WORDS_PER_CELL in WFCSolverKokkos.cpp "
            "and recompile.");
    }

    KokkosScratch& s = kokkos_scratch();
    ensure_scratch(s, wave);

    // Rebuild the flattened rules every propagate. Suboptimal but
    // correct, caching by pointer identity is broken when different
    // OverlapRules instances happen to reuse the same heap address
    // across solves. TODO: cache by rules content hash if perf matters.
    rules_cache().rebuild(rules);

    return propagate_kokkos(wave, rules, start_cell,
                            stats.propagations,
                            s.wave_view, s.frontier_a, s.frontier_b,
                            s.next_count, s.in_queue, s.contradiction);
}

} // namespace wfc
