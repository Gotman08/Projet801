#include "wfc/internal/SolverCommon.hpp"

#include "wfc/WFCSolver.hpp"

#include <cmath>
#include <cstddef>
#include <queue>

namespace wfc {

double weighted_entropy(ConstBitsetView cell_wave,
                        const std::vector<std::uint32_t>& freq) {
    double sum = 0.0;
    double sum_log = 0.0;
    cell_wave.for_each_set([&](std::size_t t) {
        const double f = static_cast<double>(freq[t]);
        sum += f;
        sum_log += f * std::log(f);
    });
    if (sum <= 0.0) return 0.0;
    return std::log(sum) - sum_log / sum;
}

int weighted_pick(ConstBitsetView cell_wave,
                  const std::vector<std::uint32_t>& freq,
                  std::mt19937_64& rng) {
    double total = 0.0;
    cell_wave.for_each_set([&](std::size_t t) {
        total += static_cast<double>(freq[t]);
    });

    std::uniform_real_distribution<double> dist(0.0, total);
    const double r = dist(rng);
    double acc = 0.0;
    int last = -1;
    int picked = -1;
    cell_wave.for_each_set([&](std::size_t t) {
        if (picked != -1) return;
        last = static_cast<int>(t);
        acc += static_cast<double>(freq[t]);
        if (r <= acc) picked = last;
    });
    return picked != -1 ? picked : last;
}

MinEntropyResult serial_min_entropy(const Wave& wave,
                                    const std::vector<std::uint32_t>& freq,
                                    std::uint64_t seed) {
    MinEntropyResult best;
    const int total = wave.num_cells();
    for (int c = 0; c < total; ++c) {
        if (wave.at(c).count() <= 1) continue; // already decided
        const double key = weighted_entropy(wave.at(c), freq) + cell_jitter(c, seed);
        if (key < best.value) {
            best.value = key;
            best.cell = c;
        }
    }
    return best;
}

namespace {

void union_allowed(Bitset& out,
                   ConstBitsetView cell_wave,
                   const OverlapRules& rules,
                   int dx, int dy) {
    out.reset();
    cell_wave.for_each_set([&](std::size_t t) {
        out.or_with(rules.allowed(static_cast<int>(t), dx, dy));
    });
}

} // namespace

bool serial_propagate(Wave& wave,
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
                    if (!dst.any()) return false;
                    q.push(nc);
                }
            }
        }
    }
    return true;
}

bool serial_run_attempt(Wave& wave,
                        const TileSet& tiles,
                        const OverlapRules& rules,
                        std::uint64_t seed,
                        std::mt19937_64& rng,
                        SolverStats& stats) {
    while (true) {
        const int cell = serial_min_entropy(wave, tiles.frequencies(), seed).cell;
        if (cell < 0) {
            for (int c = 0; c < wave.num_cells(); ++c) {
                if (wave.at(c).count() == 0) return false;
            }
            return true;
        }
        const int t = weighted_pick(wave.at(cell), tiles.frequencies(), rng);
        wave.at(cell).set_only(static_cast<std::size_t>(t));
        ++stats.collapses;
        if (!serial_propagate(wave, rules, cell, stats.propagations))
            return false;
    }
}

Grid build_output(const Wave& wave, const TileSet& tiles) {
    Grid g(wave.rows(), wave.cols());
    // Output pixel (r, c) = top-left value of the tile collapsed at (r, c).
    for (int r = 0; r < wave.rows(); ++r) {
        for (int c = 0; c < wave.cols(); ++c) {
            const ConstBitsetView b = wave.at(r, c);
            const std::size_t t = b.first_set();
            g.at(r, c) = (t < b.size())
                ? tiles.tile(static_cast<int>(t)).at(0, 0)
                : Value{0};
        }
    }
    return g;
}

} // namespace wfc
