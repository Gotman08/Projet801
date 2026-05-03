#include "wfc/solvers/WFCSolverSerial.hpp"

#include "wfc/Bitset.hpp"
#include "wfc/internal/SolverCommon.hpp"

#include <queue>

namespace wfc {

namespace {

// Compute the union over `cell_wave`'s set bits of `rules.allowed(t, dx, dy)`.
// Uses for_each_set, which skips empty 64-bit words and visits only the set
// bits — much faster than a per-bit `test()` loop when the wave is sparse.
void union_allowed(Bitset& out,
                   ConstBitsetView cell_wave,
                   const OverlapRules& rules,
                   int dx, int dy) {
    out.reset();
    cell_wave.for_each_set([&](std::size_t t) {
        out.or_with(rules.allowed(static_cast<int>(t), dx, dy));
    });
}

bool propagate_serial(Wave& wave,
                      const OverlapRules& rules,
                      int start_cell,
                      int& propagations) {
    const int N = rules.N();
    const int num_tiles = rules.num_tiles();
    std::queue<int> q;
    q.push(start_cell);

    Bitset allowed(num_tiles);
    while (!q.empty()) {
        const int c = q.front(); q.pop();
        const int r = wave.row(c);
        const int col = wave.col(c);

        for (int dy = -(N - 1); dy <= N - 1; ++dy) {
            for (int dx = -(N - 1); dx <= N - 1; ++dx) {
                if (dx == 0 && dy == 0) continue;
                const int nc = wave.torus_idx(r + dy, col + dx);

                union_allowed(allowed, wave.at(c), rules, dx, dy);

                BitsetView dst = wave.at(nc);
                if (dst.and_with(allowed)) {
                    ++propagations;
                    if (!dst.any()) return false; // contradiction
                    q.push(nc);
                }
            }
        }
    }
    return true;
}

} // namespace

int WFCSolverSerial::pick_cell(const Wave& wave,
                               const TileSet& tiles,
                               std::uint64_t seed) {
    return serial_min_entropy(wave, tiles.frequencies(), seed).cell;
}

bool WFCSolverSerial::propagate(Wave& wave,
                                const OverlapRules& rules,
                                int start_cell,
                                SolverStats& stats) {
    return propagate_serial(wave, rules, start_cell, stats.propagations);
}

} // namespace wfc
