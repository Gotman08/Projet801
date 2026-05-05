// End-to-end tests on the serial solver. Covers:
//  - README example resolution
//  - determinism (same seed → same output) on many sizes
//  - soundness (output uses only input tiles)
//  - reseeding produces different outputs
//  - multi-value samples
//  - varying tile size N
//  - retry on contradiction (stats.attempts behaviour)
//  - basic stats invariants

#include "test_helpers.hpp"
#include "wfc/Grid.hpp"
#include "wfc/OverlapRules.hpp"
#include "wfc/TileSet.hpp"
#include "wfc/solvers/WFCSolverSerial.hpp"

using namespace wfc;

namespace {

Grid make_readme_sample() {
    Grid s(5, 5);
    int data[] = {
        1, 0, 1, 1, 1,
        1, 0, 1, 1, 1,
        0, 0, 1, 1, 1,
        0, 1, 1, 1, 1,
        0, 0, 0, 0, 0,
    };
    s.fill_row_major(data, 25);
    return s;
}

// Three-value sample (water/sand/grass) used to confirm the solver is
// value-agnostic.
Grid make_multivalue_sample() {
    Grid s(6, 6);
    int data[] = {
        0, 0, 0, 0, 0, 0,
        0, 0, 3, 3, 0, 0,
        0, 3, 3, 4, 3, 0,
        0, 3, 4, 4, 3, 0,
        0, 0, 3, 3, 0, 0,
        0, 0, 0, 0, 0, 0,
    };
    s.fill_row_major(data, 36);
    return s;
}

// Highly constrained binary stripes — used to verify the solver works on
// limited tile vocabularies.
Grid make_stripes_sample() {
    Grid s(4, 4);
    int data[] = {
        0, 1, 0, 1,
        0, 1, 0, 1,
        0, 1, 0, 1,
        0, 1, 0, 1,
    };
    s.fill_row_major(data, 16);
    return s;
}

bool grids_equal(const Grid& a, const Grid& b) {
    if (a.rows() != b.rows() || a.cols() != b.cols()) return false;
    for (int r = 0; r < a.rows(); ++r)
        for (int c = 0; c < a.cols(); ++c)
            if (a.at(r, c) != b.at(r, c)) return false;
    return true;
}

bool output_uses_only_input_tiles(const Grid& out, const TileSet& ts) {
    int N = ts.N();
    for (int r = 0; r < out.rows(); ++r) {
        for (int c = 0; c < out.cols(); ++c) {
            Tile t;
            t.N = N;
            t.data.resize(static_cast<std::size_t>(N) * N);
            for (int i = 0; i < N; ++i)
                for (int j = 0; j < N; ++j)
                    t.data[static_cast<std::size_t>(i) * N + j] =
                        out.at_torus(r + i, c + j);
            bool ok = false;
            for (const auto& known : ts.tiles())
                if (known == t) { ok = true; break; }
            if (!ok) return false;
        }
    }
    return true;
}

bool output_values_are_in_alphabet(const Grid& out, const TileSet& ts) {
    Value max_v = ts.max_value();
    for (int r = 0; r < out.rows(); ++r)
        for (int c = 0; c < out.cols(); ++c)
            if (out.at(r, c) > max_v) return false;
    return true;
}

Grid solve_grid(const TileSet& ts, const OverlapRules& rules,
                int rows, int cols, std::uint64_t seed,
                int attempts, SolverStats& stats) {
    SolverOptions opt;
    opt.rows = rows;
    opt.cols = cols;
    opt.seed = seed;
    opt.max_attempts = attempts;
    WFCSolverSerial solver;
    return solver.solve(ts, rules, opt, stats);
}

} // namespace

int main() {
    // === README example: solve, determinism, soundness ===
    {
        Grid sample = make_readme_sample();
        TileSet ts = TileSet::from_sample(sample, 2);
        OverlapRules rules = OverlapRules::build(ts);

        SolverStats s1, s2;
        Grid g1 = solve_grid(ts, rules, 16, 16, 42, 5, s1);
        Grid g2 = solve_grid(ts, rules, 16, 16, 42, 5, s2);

        WFC_CHECK(s1.success);
        WFC_CHECK(s2.success);
        WFC_CHECK(grids_equal(g1, g2));                  // same seed → same output
        WFC_CHECK(output_uses_only_input_tiles(g1, ts)); // soundness
        WFC_CHECK(output_values_are_in_alphabet(g1, ts));
    }

    // === Different seed → different output (very high probability) ===
    {
        Grid sample = make_readme_sample();
        TileSet ts = TileSet::from_sample(sample, 2);
        OverlapRules rules = OverlapRules::build(ts);

        SolverStats s1, s2;
        Grid g1 = solve_grid(ts, rules, 16, 16, 42, 5, s1);
        Grid g2 = solve_grid(ts, rules, 16, 16, 4242, 5, s2);
        WFC_CHECK(s1.success);
        WFC_CHECK(s2.success);
        WFC_CHECK(!grids_equal(g1, g2));
        WFC_CHECK(output_uses_only_input_tiles(g1, ts));
        WFC_CHECK(output_uses_only_input_tiles(g2, ts));
    }

    // === Multiple sizes ===
    {
        Grid sample = make_readme_sample();
        TileSet ts = TileSet::from_sample(sample, 2);
        OverlapRules rules = OverlapRules::build(ts);

        for (int size : {8, 16, 24, 32, 48}) {
            SolverStats stats;
            Grid out = solve_grid(ts, rules, size, size, 7, 5, stats);
            WFC_CHECK(stats.success);
            WFC_CHECK_EQ(out.rows(), size);
            WFC_CHECK_EQ(out.cols(), size);
            WFC_CHECK(output_uses_only_input_tiles(out, ts));
        }
    }

    // === Non-square output ===
    {
        Grid sample = make_readme_sample();
        TileSet ts = TileSet::from_sample(sample, 2);
        OverlapRules rules = OverlapRules::build(ts);

        SolverStats stats;
        Grid out = solve_grid(ts, rules, 12, 30, 42, 5, stats);
        WFC_CHECK(stats.success);
        WFC_CHECK_EQ(out.rows(), 12);
        WFC_CHECK_EQ(out.cols(), 30);
        WFC_CHECK(output_uses_only_input_tiles(out, ts));
    }

    // === Multi-value sample, soundness ===
    {
        Grid mv_sample = make_multivalue_sample();
        TileSet mv_ts = TileSet::from_sample(mv_sample, 2);
        OverlapRules mv_rules = OverlapRules::build(mv_ts);
        WFC_CHECK(static_cast<int>(mv_ts.max_value()) >= 4);

        SolverStats stats;
        Grid out = solve_grid(mv_ts, mv_rules, 16, 16, 11, 5, stats);
        WFC_CHECK(stats.success);
        WFC_CHECK(output_uses_only_input_tiles(out, mv_ts));
        WFC_CHECK(output_values_are_in_alphabet(out, mv_ts));
    }

    // === Multi-value: determinism holds across runs ===
    {
        Grid mv_sample = make_multivalue_sample();
        TileSet mv_ts = TileSet::from_sample(mv_sample, 2);
        OverlapRules mv_rules = OverlapRules::build(mv_ts);

        SolverStats s1, s2;
        Grid g1 = solve_grid(mv_ts, mv_rules, 12, 12, 99, 5, s1);
        Grid g2 = solve_grid(mv_ts, mv_rules, 12, 12, 99, 5, s2);
        WFC_CHECK(s1.success);
        WFC_CHECK(s2.success);
        WFC_CHECK(grids_equal(g1, g2));
    }

    // === Stripes: heavily constrained but solvable ===
    {
        Grid s = make_stripes_sample();
        TileSet ts = TileSet::from_sample(s, 2);
        OverlapRules rules = OverlapRules::build(ts);

        for (std::uint64_t seed : {1ULL, 7ULL, 42ULL, 12345ULL}) {
            SolverStats stats;
            Grid out = solve_grid(ts, rules, 8, 8, seed, 5, stats);
            WFC_CHECK(stats.success);
            WFC_CHECK(output_uses_only_input_tiles(out, ts));
        }
    }

    // === N=3 on README sample (succeed at least once across seeds) ===
    {
        Grid sample = make_readme_sample();
        TileSet ts = TileSet::from_sample(sample, 3);
        OverlapRules rules = OverlapRules::build(ts);

        bool any_success = false;
        for (std::uint64_t seed = 0; seed < 20 && !any_success; ++seed) {
            SolverStats stats;
            Grid out = solve_grid(ts, rules, 12, 12, seed, 10, stats);
            if (stats.success) {
                any_success = true;
                WFC_CHECK(output_uses_only_input_tiles(out, ts));
            }
        }
        WFC_CHECK(any_success);
    }

    // === Stats invariants on a successful run ===
    {
        Grid sample = make_readme_sample();
        TileSet ts = TileSet::from_sample(sample, 2);
        OverlapRules rules = OverlapRules::build(ts);

        SolverStats stats;
        Grid out = solve_grid(ts, rules, 16, 16, 42, 5, stats);
        WFC_CHECK(stats.success);
        WFC_CHECK(stats.attempts >= 1);
        WFC_CHECK(stats.attempts <= 5);
        WFC_CHECK(stats.collapses > 0);
        WFC_CHECK(stats.collapses <= 16 * 16);
        WFC_CHECK(stats.propagations >= 0);
        WFC_CHECK(stats.seconds_total >= 0.0);
        WFC_CHECK(stats.seconds_solve >= 0.0);
        WFC_CHECK(stats.seconds_solve <= stats.seconds_total + 1e-6);
        WFC_CHECK_EQ(stats.backend, std::string("serial"));
        WFC_CHECK_EQ(out.rows(), 16);
        WFC_CHECK_EQ(out.cols(), 16);
    }

    // === SolverOptions::validate rejects bad inputs ===
    {
        WFCSolverSerial solver;
        Grid sample = make_readme_sample();
        TileSet ts = TileSet::from_sample(sample, 2);
        OverlapRules rules = OverlapRules::build(ts);

        SolverOptions opt;
        opt.rows = 0;
        SolverStats stats;
        bool threw = false;
        try { auto g = solver.solve(ts, rules, opt, stats); (void)g; }
        catch (const std::invalid_argument&) { threw = true; }
        WFC_CHECK(threw);

        opt.rows = 8;
        opt.cols = -1;
        threw = false;
        try { auto g = solver.solve(ts, rules, opt, stats); (void)g; }
        catch (const std::invalid_argument&) { threw = true; }
        WFC_CHECK(threw);

        opt.cols = 8;
        opt.max_attempts = 0;
        threw = false;
        try { auto g = solver.solve(ts, rules, opt, stats); (void)g; }
        catch (const std::invalid_argument&) { threw = true; }
        WFC_CHECK(threw);
    }

    // === 1×1 output: trivial, but should still produce a valid result ===
    {
        Grid sample = make_readme_sample();
        TileSet ts = TileSet::from_sample(sample, 2);
        OverlapRules rules = OverlapRules::build(ts);

        SolverStats stats;
        Grid out = solve_grid(ts, rules, 1, 1, 42, 5, stats);
        WFC_CHECK(stats.success);
        WFC_CHECK_EQ(out.rows(), 1);
        WFC_CHECK_EQ(out.cols(), 1);
    }

    // === Many seeds in a row, all should succeed for the README sample ===
    {
        Grid sample = make_readme_sample();
        TileSet ts = TileSet::from_sample(sample, 2);
        OverlapRules rules = OverlapRules::build(ts);

        int success_count = 0;
        for (std::uint64_t seed = 0; seed < 20; ++seed) {
            SolverStats stats;
            Grid out = solve_grid(ts, rules, 16, 16, seed, 5, stats);
            if (stats.success && output_uses_only_input_tiles(out, ts))
                ++success_count;
        }
        // We expect at least 18/20 to succeed for this sample.
        WFC_CHECK(success_count >= 18);
    }

    // === Backend identifier is "serial" ===
    {
        WFCSolverSerial solver;
        WFC_CHECK_EQ(std::string(solver.name()), std::string("serial"));
    }

    return wfc_test::report();
}
