// Unit tests for the helpers in SolverCommon.hpp/.cpp:
// weighted_entropy, weighted_pick, cell_jitter, attempt_seed,
// serial_min_entropy, build_output. These functions sit between the core
// data structures and the solvers, so testing them in isolation catches
// regressions before they cascade into the integration tests.

#include "test_helpers.hpp"
#include "wfc/Bitset.hpp"
#include "wfc/Grid.hpp"
#include "wfc/OverlapRules.hpp"
#include "wfc/TileSet.hpp"
#include "wfc/Wave.hpp"
#include "wfc/internal/SolverCommon.hpp"

#include <cmath>
#include <cstdint>
#include <random>
#include <set>
#include <vector>

using namespace wfc;

namespace {

// Build a small TileSet so we can exercise build_output without doing a full
// solve. Returns also the underlying Grid for reference.
struct ToyContext {
    Grid sample;
    TileSet tiles;
};

ToyContext make_toy_binary() {
    ToyContext ctx;
    ctx.sample = Grid(5, 5);
    int data[] = {
        1, 0, 1, 1, 1,
        1, 0, 1, 1, 1,
        0, 0, 1, 1, 1,
        0, 1, 1, 1, 1,
        0, 0, 0, 0, 0,
    };
    ctx.sample.fill_row_major(data, 25);
    ctx.tiles = TileSet::from_sample(ctx.sample, 2);
    return ctx;
}

} // namespace

int main() {
    // === weighted_entropy: cell with one candidate has zero entropy ===
    {
        Bitset b(8);
        b.set(3);
        std::vector<std::uint32_t> freq = {1, 1, 1, 5, 1, 1, 1, 1};
        double e = weighted_entropy(b, freq);
        WFC_CHECK(std::abs(e) < 1e-12);
    }

    // === weighted_entropy: empty cell returns 0 (no candidates) ===
    {
        Bitset b(8);
        std::vector<std::uint32_t> freq = {1, 1, 1, 1, 1, 1, 1, 1};
        double e = weighted_entropy(b, freq);
        // Production code returns the literal 0.0 in this branch, so exact
        // equality is safe, but use epsilon for symmetry with the rest.
        WFC_CHECK(std::abs(e) < 1e-12);
    }

    // === weighted_entropy: uniform freq, n candidates → log(n) ===
    // entropy = log(Σf) - Σ(f log f) / Σf
    // with uniform f = 1: log(n) - (n * 0)/n = log(n)
    {
        Bitset b(4);
        b.set(0); b.set(1); b.set(2); b.set(3);
        std::vector<std::uint32_t> freq = {1, 1, 1, 1};
        double e = weighted_entropy(b, freq);
        WFC_CHECK(std::abs(e - std::log(4.0)) < 1e-9);
    }

    // === weighted_entropy: more candidates → larger entropy ===
    {
        std::vector<std::uint32_t> freq = {1, 1, 1, 1, 1, 1, 1, 1};
        Bitset b1(8); b1.set(0); b1.set(1);
        Bitset b2(8); b2.set(0); b2.set(1); b2.set(2); b2.set(3);
        WFC_CHECK(weighted_entropy(b1, freq) < weighted_entropy(b2, freq));
    }

    // === weighted_entropy: skewed distribution (one freq dominates) ===
    // entropy is lower than the uniform case for the same number of candidates.
    {
        std::vector<std::uint32_t> uniform = {1, 1, 1, 1};
        std::vector<std::uint32_t> skewed  = {1, 1, 1, 100};
        Bitset b(4); b.set(0); b.set(1); b.set(2); b.set(3);
        double e_uniform = weighted_entropy(b, uniform);
        double e_skewed  = weighted_entropy(b, skewed);
        WFC_CHECK(e_skewed < e_uniform);
    }

    // === weighted_entropy: always finite and non-negative ===
    {
        std::vector<std::uint32_t> freq = {1, 2, 3, 4, 5, 6, 7, 8};
        for (std::size_t mask = 1; mask < (1u << 8); ++mask) {
            Bitset b(8);
            for (int i = 0; i < 8; ++i)
                if (mask & (1u << i)) b.set(static_cast<std::size_t>(i));
            double e = weighted_entropy(b, freq);
            WFC_CHECK(std::isfinite(e));
            WFC_CHECK(e >= -1e-12);
        }
    }

    // === weighted_pick: deterministic with the same RNG state ===
    {
        Bitset b(8);
        b.set(1); b.set(3); b.set(5); b.set(7);
        std::vector<std::uint32_t> freq = {1, 2, 3, 4, 5, 6, 7, 8};

        std::mt19937_64 rng1(42);
        std::mt19937_64 rng2(42);
        for (int i = 0; i < 100; ++i) {
            int p1 = weighted_pick(b, freq, rng1);
            int p2 = weighted_pick(b, freq, rng2);
            WFC_CHECK_EQ(p1, p2);
        }
    }

    // === weighted_pick: result is always one of the candidates ===
    {
        Bitset b(16);
        std::set<int> candidates = {2, 5, 9, 13};
        for (int c : candidates) b.set(static_cast<std::size_t>(c));
        std::vector<std::uint32_t> freq(16, 1);

        std::mt19937_64 rng(7);
        for (int i = 0; i < 200; ++i) {
            int p = weighted_pick(b, freq, rng);
            WFC_CHECK(candidates.count(p) == 1);
        }
    }

    // === weighted_pick: roughly respects frequency proportions ===
    // Two candidates, freq ratio 1:9 → expect ~10% / ~90% with a strong RNG.
    // Tolerance ±5% over 5000 trials gives extremely high confidence.
    {
        Bitset b(2);
        b.set(0); b.set(1);
        std::vector<std::uint32_t> freq = {1, 9};
        std::mt19937_64 rng(12345);
        int count0 = 0;
        const int trials = 5000;
        for (int i = 0; i < trials; ++i) {
            if (weighted_pick(b, freq, rng) == 0) ++count0;
        }
        double frac0 = static_cast<double>(count0) / trials;
        WFC_CHECK(frac0 > 0.05);  // expect ~0.10
        WFC_CHECK(frac0 < 0.15);
    }

    // === weighted_pick: single candidate always wins ===
    {
        Bitset b(8);
        b.set(4);
        std::vector<std::uint32_t> freq(8, 1);
        std::mt19937_64 rng(99);
        for (int i = 0; i < 50; ++i)
            WFC_CHECK_EQ(weighted_pick(b, freq, rng), 4);
    }

    // === weighted_pick: all-zero frequencies → falls through to last seen.
    //     Edge case: with total = 0, dist(0, 0) returns 0, and the
    //     `r <= acc` check (0 <= 0) hits on the first candidate. We just
    //     verify the result is one of the candidates and it's deterministic. ===
    {
        Bitset b(8);
        b.set(2); b.set(5); b.set(7);
        std::vector<std::uint32_t> zero_freq(8, 0);
        std::mt19937_64 rng(123);
        std::set<int> picked;
        for (int i = 0; i < 20; ++i)
            picked.insert(weighted_pick(b, zero_freq, rng));
        // Result must be one of the candidate ids, even with zero weights.
        for (int p : picked) WFC_CHECK(p == 2 || p == 5 || p == 7);
    }

    // === weighted_pick: result respects the candidate set even when one
    //     non-candidate has a huge frequency ===
    {
        Bitset b(4);
        b.set(0); b.set(1);
        std::vector<std::uint32_t> freq = {1, 1, 100000, 100000};
        std::mt19937_64 rng(7);
        for (int i = 0; i < 50; ++i) {
            int p = weighted_pick(b, freq, rng);
            WFC_CHECK(p == 0 || p == 1);
        }
    }

    // === cell_jitter: range [0, 1e-6) ===
    {
        for (std::uint64_t s : {0ULL, 1ULL, 42ULL, 0xCAFEBABEULL}) {
            for (int c = 0; c < 1000; ++c) {
                double j = cell_jitter(c, s);
                WFC_CHECK(j >= 0.0);
                WFC_CHECK(j < 1e-6);
            }
        }
    }

    // === cell_jitter: deterministic ===
    {
        for (int c = 0; c < 100; ++c) {
            double j1 = cell_jitter(c, 12345);
            double j2 = cell_jitter(c, 12345);
            WFC_CHECK_EQ(j1, j2);
        }
    }

    // === cell_jitter: produces distinct values for different cells (most of them) ===
    {
        std::set<double> seen;
        for (int c = 0; c < 1000; ++c) seen.insert(cell_jitter(c, 42));
        // Some collisions are possible due to the [0, 1e-6) range with finite
        // floating-point precision, but the bulk should be distinct.
        WFC_CHECK(seen.size() > 900);
    }

    // === cell_jitter: different seeds produce different values for the same cell ===
    {
        int distinct = 0;
        for (std::uint64_t s = 0; s < 100; ++s) {
            std::set<double> seen;
            for (std::uint64_t s2 = s; s2 < s + 10; ++s2)
                seen.insert(cell_jitter(7, s2));
            if (seen.size() > 1) ++distinct;
        }
        WFC_CHECK(distinct > 90);
    }

    // === attempt_seed: distinct for different attempt indices ===
    {
        std::uint64_t base = 42;
        std::set<std::uint64_t> seeds;
        for (int i = 0; i < 100; ++i) seeds.insert(attempt_seed(base, i));
        WFC_CHECK_EQ(static_cast<int>(seeds.size()), 100);
    }

    // === attempt_seed: monotonic structure (consistent with golden ratio addition) ===
    {
        std::uint64_t base = 0;
        WFC_CHECK_EQ(attempt_seed(base, 0), 0ULL);
        WFC_CHECK_EQ(attempt_seed(base, 1) - attempt_seed(base, 0),
                     attempt_seed(base, 2) - attempt_seed(base, 1));
    }

    // === attempt_seed: different base => different seed sequence ===
    {
        for (int i = 0; i < 10; ++i)
            WFC_CHECK(attempt_seed(0, i) != attempt_seed(1, i));
    }

    // === serial_min_entropy: returns -1 when all cells are decided ===
    {
        Wave w(3, 3, 5);
        // Decide every cell.
        for (int c = 0; c < w.num_cells(); ++c)
            w.at(c).set_only(static_cast<std::size_t>(c % 5));
        std::vector<std::uint32_t> freq(5, 1);
        auto best = serial_min_entropy(w, freq, 42);
        WFC_CHECK_EQ(best.cell, -1);
    }

    // === serial_min_entropy: ignores cells with count <= 1 ===
    {
        Wave w(3, 3, 5);
        // Most cells decided; one cell has 3 candidates.
        for (int c = 0; c < w.num_cells(); ++c)
            w.at(c).set_only(0);
        BitsetView mid = w.at(1, 1);
        mid.reset(); mid.set(0); mid.set(1); mid.set(2);
        std::vector<std::uint32_t> freq = {1, 1, 1, 1, 1};
        auto best = serial_min_entropy(w, freq, 42);
        WFC_CHECK_EQ(best.cell, 4); // (1,1) at index 4 in row-major
    }

    // === serial_min_entropy: deterministic ===
    {
        Wave w(4, 4, 8);
        std::vector<std::uint32_t> freq(8, 1);
        auto a = serial_min_entropy(w, freq, 42);
        auto b = serial_min_entropy(w, freq, 42);
        WFC_CHECK_EQ(a.cell, b.cell);
        WFC_CHECK_EQ(a.value, b.value);
    }

    // === serial_min_entropy: different seeds may pick different cells (jitter) ===
    {
        Wave w(4, 4, 8);
        std::vector<std::uint32_t> freq(8, 1);
        std::set<int> cells_picked;
        for (std::uint64_t s = 0; s < 50; ++s)
            cells_picked.insert(serial_min_entropy(w, freq, s).cell);
        // Different seeds should pick from different cells (at least 2 distinct).
        WFC_CHECK(cells_picked.size() >= 2u);
    }

    // === build_output: reconstructs values from collapsed wave ===
    {
        ToyContext ctx = make_toy_binary();
        const TileSet& ts = ctx.tiles;
        Wave w(3, 3, ts.size());
        // Find the all-ones tile in ts.
        int tile_id = -1;
        for (int id = 0; id < ts.size(); ++id) {
            const Tile& t = ts.tile(id);
            if (t.data[0] == 1 && t.data[1] == 1 && t.data[2] == 1 && t.data[3] == 1) {
                tile_id = id; break;
            }
        }
        WFC_CHECK(tile_id >= 0);
        for (int c = 0; c < w.num_cells(); ++c)
            w.at(c).set_only(static_cast<std::size_t>(tile_id));
        Grid out = build_output(w, ts);
        // Every output pixel should equal the top-left value of that tile (=1).
        for (int r = 0; r < out.rows(); ++r)
            for (int c = 0; c < out.cols(); ++c)
                WFC_CHECK_EQ(static_cast<int>(out.at(r, c)), 1);
    }

    // === build_output: handles uncollapsed cells deterministically ===
    {
        ToyContext ctx = make_toy_binary();
        const TileSet& ts = ctx.tiles;
        Wave w(2, 2, ts.size());
        // Leave wave fully open. build_output picks first_set (= tile 0).
        Grid out = build_output(w, ts);
        WFC_CHECK_EQ(out.rows(), 2);
        WFC_CHECK_EQ(out.cols(), 2);
        const int expected = static_cast<int>(ts.tile(0).at(0, 0));
        for (int r = 0; r < 2; ++r)
            for (int c = 0; c < 2; ++c)
                WFC_CHECK_EQ(static_cast<int>(out.at(r, c)), expected);
    }

    // === build_output: empty cells produce 0 (sentinel) ===
    {
        ToyContext ctx = make_toy_binary();
        const TileSet& ts = ctx.tiles;
        Wave w(2, 2, ts.size());
        for (int c = 0; c < w.num_cells(); ++c)
            w.at(c).reset();
        Grid out = build_output(w, ts);
        for (int r = 0; r < 2; ++r)
            for (int c = 0; c < 2; ++c)
                WFC_CHECK_EQ(static_cast<int>(out.at(r, c)), 0);
    }

    // === MinEntropyResult default invariant ===
    {
        MinEntropyResult m;
        WFC_CHECK_EQ(m.cell, -1);
        WFC_CHECK(m.value > 1e200);
    }

    return wfc_test::report();
}
