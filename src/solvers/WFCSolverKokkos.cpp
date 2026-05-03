#include "wfc/solvers/WFCSolverKokkos.hpp"

#include "wfc/Bitset.hpp"
#include "wfc/internal/SolverCommon.hpp"

#include <Kokkos_Core.hpp>

#include <cstdint>
#include <vector>

namespace wfc {

namespace {

// Kokkos parallel_for body for one BFS level. Each invocation processes one
// frontier cell: snapshots its wave, AND-folds each neighbor with the
// per-offset union of allowed bitsets, atomically marks dirtied neighbors.
struct PropagateOp {
    Wave* wave;
    const OverlapRules* rules;
    const int* frontier;
    int* next_frontier;       // ring buffer for dirtied cell ids
    int* next_count;          // atomic counter for next_frontier writes
    std::uint8_t* in_queue;   // dedup flags for the current level
    int* contradiction_flag;  // 0/1; set on first empty cell
    int N;
    int num_tiles;

    void operator()(const int idx) const {
        if (Kokkos::atomic_load(contradiction_flag)) return;

        const int c = frontier[idx];
        const int r = wave->row(c);
        const int col = wave->col(c);

        const Bitset src_snapshot{wave->at(c)};
        Bitset allowed(num_tiles);

        for (int dy = -(N - 1); dy <= N - 1; ++dy) {
            for (int dx = -(N - 1); dx <= N - 1; ++dx) {
                if (dx == 0 && dy == 0) continue;
                const int nc = wave->torus_idx(r + dy, col + dx);

                allowed.reset();
                src_snapshot.for_each_set([&](std::size_t t) {
                    allowed.or_with(rules->allowed(static_cast<int>(t), dx, dy));
                });

                BitsetView dst = wave->at(nc);
                std::uint64_t* dwords = dst.words();
                const std::uint64_t* swords = allowed.words();
                bool changed = false;
                for (std::size_t i = 0; i < dst.word_count(); ++i) {
                    const std::uint64_t mask = swords[i];
                    if (mask == ~0ULL) continue;
                    const std::uint64_t old_val =
                        Kokkos::atomic_fetch_and(&dwords[i], mask);
                    if ((old_val & ~mask) != 0ULL) changed = true;
                }
                if (changed) {
                    if (!dst.any()) {
                        Kokkos::atomic_store(contradiction_flag, 1);
                        return;
                    }
                    std::uint8_t expected = 0;
                    if (Kokkos::atomic_compare_exchange(
                            &in_queue[nc], expected,
                            static_cast<std::uint8_t>(1)) == 0) {
                        const int slot = Kokkos::atomic_fetch_add(next_count, 1);
                        next_frontier[slot] = nc;
                    }
                }
            }
        }
    }
};

bool propagate_kokkos(Wave& wave,
                     const OverlapRules& rules,
                     int start_cell,
                     int& propagations) {
    const int N = rules.N();
    const int num_tiles = rules.num_tiles();
    const int total = wave.num_cells();

    std::vector<int> frontier_a(static_cast<std::size_t>(total));
    std::vector<int> frontier_b(static_cast<std::size_t>(total));
    std::vector<std::uint8_t> in_queue(static_cast<std::size_t>(total), 0);
    int contradiction = 0;

    int* cur = frontier_a.data();
    int* nxt = frontier_b.data();
    int cur_size = 1;
    cur[0] = start_cell;

    while (cur_size > 0 && !contradiction) {
        int next_count = 0;
        PropagateOp op{&wave, &rules, cur, nxt, &next_count, in_queue.data(),
                       &contradiction, N, num_tiles};
        Kokkos::parallel_for("wfc_propagate_level", cur_size, op);
        Kokkos::fence();

        propagations += next_count;
        for (int i = 0; i < next_count; ++i)
            in_queue[static_cast<std::size_t>(nxt[i])] = 0;
        std::swap(cur, nxt);
        cur_size = next_count;
    }
    return !contradiction;
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

void WFCSolverKokkos::on_solve_end() {
    if (we_initialised_) {
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

    Kokkos::parallel_for("wfc_min_entropy", n_chunks, [=](const int k) {
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
    return propagate_kokkos(wave, rules, start_cell, stats.propagations);
}

} // namespace wfc
