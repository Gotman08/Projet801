#include "wfc/internal/SolverCommon.hpp"

#include <cmath>
#include <cstddef>

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
