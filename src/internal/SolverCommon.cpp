#include "wfc/internal/SolverCommon.hpp"

#include "wfc/WFCSolver.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <queue>
#include <vector>

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

namespace {

// One decision point on the backtrack stack. Holds the cell that was
// collapsed, the remaining tile choices to try if the current one
// fails (LIFO via vector::back), and a snapshot of the wave taken
// just before the cell was first decided so that restoring it undoes
// both the collapse and the propagation that followed.
struct BacktrackFrame {
    int cell;
    std::vector<int> tries;
    std::vector<std::uint64_t> wave_snapshot;
};

void snapshot_wave(const Wave& wave, std::vector<std::uint64_t>& out) {
    out.assign(wave.raw_words(), wave.raw_words() + wave.total_words());
}

void restore_wave(Wave& wave, const std::vector<std::uint64_t>& snap) {
    std::copy(snap.begin(), snap.end(), wave.raw_words());
}

} // namespace

bool serial_run_attempt_backtrack(Wave& wave,
                                  const TileSet& tiles,
                                  const OverlapRules& rules,
                                  std::uint64_t seed,
                                  SolverStats& stats) {
    const auto& freq = tiles.frequencies();
    std::vector<BacktrackFrame> stack;

    auto try_top = [&]() -> bool {
        // Pop tile candidates off the top frame until one survives
        // propagation, or the frame is exhausted. Returns true on
        // forward progress, false if the top frame ran out of choices.
        BacktrackFrame& top = stack.back();
        while (!top.tries.empty()) {
            const int t = top.tries.back();
            top.tries.pop_back();
            restore_wave(wave, top.wave_snapshot);
            wave.at(top.cell).set_only(static_cast<std::size_t>(t));
            ++stats.collapses;
            if (serial_propagate(wave, rules, top.cell, stats.propagations))
                return true;
        }
        return false;
    };

    while (true) {
        const int cell = serial_min_entropy(wave, freq, seed).cell;
        if (cell < 0) {
            // Every undecided cell is now collapsed (count <= 1). Sanity
            // check : no cell ended up with count == 0.
            bool ok = true;
            for (int c = 0; c < wave.num_cells(); ++c) {
                if (wave.at(c).count() == 0) { ok = false; break; }
            }
            if (ok) return true;
            // Fall through to backtrack.
        } else {
            // Open a new decision frame at this cell. Snapshot the wave
            // before collapsing so we can restore on contradiction.
            // Order candidates by descending frequency : the back of
            // the vector (popped first) is the most-frequent tile.
            BacktrackFrame frame;
            frame.cell = cell;
            snapshot_wave(wave, frame.wave_snapshot);
            wave.at(cell).for_each_set([&](std::size_t t) {
                frame.tries.push_back(static_cast<int>(t));
            });
            std::sort(frame.tries.begin(), frame.tries.end(),
                      [&](int a, int b) {
                          const std::uint32_t fa = freq[a];
                          const std::uint32_t fb = freq[b];
                          if (fa != fb) return fa < fb;
                          return a > b;
                      });
            stack.push_back(std::move(frame));
            if (try_top()) continue; // forward
        }

        // Backtrack : pop frames until one has a working alternative,
        // or the stack empties (search tree exhausted).
        if (!stack.empty()) stack.pop_back();
        bool recovered = false;
        while (!stack.empty()) {
            if (try_top()) { recovered = true; break; }
            stack.pop_back();
        }
        if (!recovered) return false;
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
