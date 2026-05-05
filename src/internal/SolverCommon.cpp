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

// Delta-encoded snapshot : records the cells whose words changed during
// one collapse + propagate, alongside their pre-modification values.
// Memory cost per frame is O(touched_cells * words_per_cell) instead of
// O(num_cells * words_per_cell) for a full wave snapshot. On a 64x64
// binary problem this is typically ~50 cells touched (vs 4096 total)
// per propagate, ~80x less memory.
struct WaveDelta {
    int words_per_cell = 0;
    std::vector<int> cells;
    std::vector<std::uint64_t> before;
};

void capture_cell(WaveDelta& delta,
                  std::vector<std::uint8_t>& touched,
                  const Wave& wave,
                  int cell_idx) {
    if (touched[cell_idx]) return;
    touched[cell_idx] = 1;
    delta.cells.push_back(cell_idx);
    const std::uint64_t* src = wave.raw_words()
        + static_cast<std::size_t>(cell_idx) * delta.words_per_cell;
    for (int w = 0; w < delta.words_per_cell; ++w)
        delta.before.push_back(src[w]);
}

void apply_delta(Wave& wave, const WaveDelta& delta) {
    const int W = delta.words_per_cell;
    for (std::size_t k = 0; k < delta.cells.size(); ++k) {
        std::uint64_t* dst = wave.raw_words()
            + static_cast<std::size_t>(delta.cells[k]) * W;
        const std::uint64_t* src = delta.before.data() + k * W;
        for (int w = 0; w < W; ++w) dst[w] = src[w];
    }
}

void clear_delta_and_touched(WaveDelta& delta,
                             std::vector<std::uint8_t>& touched) {
    for (int c : delta.cells) touched[c] = 0;
    delta.cells.clear();
    delta.before.clear();
}

void union_allowed_for_delta(Bitset& out,
                             ConstBitsetView cell_wave,
                             const OverlapRules& rules,
                             int dx, int dy) {
    out.reset();
    cell_wave.for_each_set([&](std::size_t t) {
        out.or_with(rules.allowed(static_cast<int>(t), dx, dy));
    });
}

// Variant of serial_propagate that records, in `delta`, every cell
// whose words are mutated by the propagation. The caller passes a
// `touched` flag vector that is reset (zeroed for the captured indices
// only) by clear_delta_and_touched after the propagate. We capture the
// pre-state ONLY on the first successful mutation of a cell, which
// avoids storing snapshots of cells that the propagation visits but
// never modifies (the common case).
bool serial_propagate_with_delta(Wave& wave,
                                 const OverlapRules& rules,
                                 int start_cell,
                                 int& propagations,
                                 WaveDelta& delta,
                                 std::vector<std::uint8_t>& touched) {
    const int N = rules.N();
    const int num_tiles = rules.num_tiles();
    const int W = delta.words_per_cell;
    std::queue<int> q;
    q.push(start_cell);

    Bitset allowed(num_tiles);
    // Small stack scratch for the pre-state read. Sized to the static
    // upper bound used by the Kokkos backend, so any L <= 512 fits.
    constexpr int kMaxW = 8;
    std::uint64_t before_words[kMaxW];

    while (!q.empty()) {
        const int c = q.front(); q.pop();
        const int r = wave.row(c);
        const int col = wave.col(c);

        for (int dy = -(N - 1); dy <= N - 1; ++dy) {
            for (int dx = -(N - 1); dx <= N - 1; ++dx) {
                if (dx == 0 && dy == 0) continue;
                const int nc = wave.torus_idx(r + dy, col + dx);

                union_allowed_for_delta(allowed, wave.at(c), rules, dx, dy);

                // Snapshot the pre-state in a stack scratch ; we'll only
                // commit it to the delta if and_with actually mutates.
                if (!touched[nc]) {
                    const std::uint64_t* src = wave.raw_words()
                        + static_cast<std::size_t>(nc) * W;
                    for (int w = 0; w < W; ++w) before_words[w] = src[w];
                }
                BitsetView dst = wave.at(nc);
                if (dst.and_with(allowed)) {
                    if (!touched[nc]) {
                        touched[nc] = 1;
                        delta.cells.push_back(nc);
                        for (int w = 0; w < W; ++w)
                            delta.before.push_back(before_words[w]);
                    }
                    ++propagations;
                    if (!dst.any()) return false;
                    q.push(nc);
                }
            }
        }
    }
    return true;
}

// One decision point on the backtrack stack. Stores the cell that
// was collapsed, the remaining tile choices, and a delta describing
// what the *current* try at this frame changed. When the frame is
// popped or the try fails, applying `delta` rolls the wave back to
// its state at frame-open time.
struct BacktrackFrame {
    int cell;
    std::vector<int> tries;
    WaveDelta delta;
};

} // namespace

bool serial_run_attempt_backtrack(Wave& wave,
                                  const TileSet& tiles,
                                  const OverlapRules& rules,
                                  std::uint64_t seed,
                                  SolverStats& stats) {
    const auto& freq = tiles.frequencies();
    const int W = static_cast<int>(wave.words_per_cell());
    const int total_cells = wave.num_cells();
    std::vector<BacktrackFrame> stack;
    // Reused across all propagate calls to avoid reallocation. Reset
    // (zeroed for the cells we touched) at the end of each capture.
    std::vector<std::uint8_t> touched(static_cast<std::size_t>(total_cells), 0);

    auto try_top = [&]() -> bool {
        // Try the next candidate at the top frame. Each try captures
        // a fresh delta ; on failure the delta is applied to roll the
        // wave back before trying the next candidate. On forward
        // progress the delta stays attached to the frame so that a
        // later backtrack to this frame can reverse this try.
        BacktrackFrame& top = stack.back();
        while (!top.tries.empty()) {
            const int t = top.tries.back();
            top.tries.pop_back();

            top.delta.words_per_cell = W;
            // Capture the cell before set_only so that restoring undoes
            // the collapse too. The propagate that follows will record
            // every neighbour it modifies into the same delta.
            capture_cell(top.delta, touched, wave, top.cell);
            wave.at(top.cell).set_only(static_cast<std::size_t>(t));
            ++stats.collapses;

            const bool ok = serial_propagate_with_delta(
                wave, rules, top.cell, stats.propagations,
                top.delta, touched);
            // touched is reset relative to this delta either way ; the
            // delta's own `cells` list is what we need to roll back. The
            // touched flags are zeroed back below by clear_delta_and_touched
            // so the next propagate starts clean.
            if (ok) {
                // Successful try : delta stays in the frame, but touched
                // must be zeroed so the next propagate sees a clean slate.
                for (int c : top.delta.cells) touched[c] = 0;
                return true;
            }
            // Contradiction : roll back this try, leave the frame open.
            apply_delta(wave, top.delta);
            clear_delta_and_touched(top.delta, touched);
        }
        return false;
    };

    while (true) {
        const int cell = serial_min_entropy(wave, freq, seed).cell;
        if (cell < 0) {
            // Every undecided cell has count <= 1. Sanity check : no
            // cell ended up with count == 0 (would mean the propagate
            // missed a contradiction).
            bool ok = true;
            for (int c = 0; c < total_cells; ++c) {
                if (wave.at(c).count() == 0) { ok = false; break; }
            }
            if (ok) return true;
            // Fall through to backtrack.
        } else {
            // Open a new decision frame at this cell. Order candidates
            // by descending frequency : the back of the vector (popped
            // first by try_top) is the most-frequent tile.
            BacktrackFrame frame;
            frame.cell = cell;
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

        // Backtrack : roll back successful frames until one has a
        // viable alternative, or the stack empties (search exhausted).
        if (!stack.empty()) {
            // Top frame's current try has been recorded ; undo it.
            apply_delta(wave, stack.back().delta);
            clear_delta_and_touched(stack.back().delta, touched);
            stack.pop_back();
        }
        bool recovered = false;
        while (!stack.empty()) {
            // The previous top's delta still represents that frame's
            // successful try. Undo it before exploring next candidate.
            apply_delta(wave, stack.back().delta);
            clear_delta_and_touched(stack.back().delta, touched);
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
