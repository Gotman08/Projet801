// Tests for the OMP backend:
//  - bit-identical to serial across thread counts (determinism)
//  - reseeding produces a different output
//  - failure path is exercised on a constrained sample
//  - multivalue parity with serial
//
// Compiled only when USE_OMP=ON.

#include "test_helpers.hpp"
#include "wfc/Grid.hpp"
#include "wfc/OverlapRules.hpp"
#include "wfc/TileSet.hpp"
#include "wfc/solvers/WFCSolverOMP.hpp"
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

Grid make_maze_sample() {
    Grid s(11, 12);
    int data[] = {
        1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
        1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1,
        1, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0, 1,
        1, 0, 1, 0, 0, 0, 6, 0, 1, 0, 0, 1,
        1, 0, 1, 0, 1, 1, 1, 1, 1, 0, 1, 1,
        1, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 1,
        1, 1, 6, 1, 1, 0, 1, 1, 1, 0, 1, 1,
        1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1,
        1, 0, 1, 1, 1, 0, 6, 0, 1, 1, 0, 1,
        1, 0, 0, 0, 1, 0, 1, 0, 0, 0, 0, 1,
        1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    };
    s.fill_row_major(data, 132);
    return s;
}

bool grids_equal(const Grid& a, const Grid& b) {
    if (a.rows() != b.rows() || a.cols() != b.cols()) return false;
    for (int r = 0; r < a.rows(); ++r)
        for (int c = 0; c < a.cols(); ++c)
            if (a.at(r, c) != b.at(r, c)) return false;
    return true;
}

Grid solve_with(WFCSolver& s, const TileSet& tiles, const OverlapRules& rules,
                std::uint64_t seed, int rows = 24, int cols = 24,
                int max_attempts = 5, SolverStats* opt_stats = nullptr) {
    SolverOptions opt;
    opt.rows = rows;
    opt.cols = cols;
    opt.seed = seed;
    opt.max_attempts = max_attempts;
    SolverStats stats;
    Grid g = s.solve(tiles, rules, opt, stats);
    if (opt_stats) *opt_stats = stats;
    return g;
}

} // namespace

int main() {
    // === Determinism: OMP at 1, 2, 4, 8 threads matches serial ===
    // Both sides MUST succeed before comparing — otherwise two zero-filled
    // failure grids would compare equal and the test would silently pass
    // even if both backends were broken.
    {
        Grid sample = make_readme_sample();
        TileSet ts = TileSet::from_sample(sample, 2);
        OverlapRules rules = OverlapRules::build(ts);

        SolverStats baseline_stats;
        WFCSolverSerial serial;
        Grid baseline = solve_with(serial, ts, rules, 12345, 24, 24, 5, &baseline_stats);
        WFC_CHECK(baseline_stats.success);

        for (int threads : {1, 2, 4, 8}) {
            WFCSolverOMP omp(threads);
            SolverStats omp_stats;
            Grid result = solve_with(omp, ts, rules, 12345, 24, 24, 5, &omp_stats);
            WFC_CHECK(omp_stats.success);
            WFC_CHECK(grids_equal(baseline, result));
        }
    }

    // === Reseeding produces a different output ===
    {
        Grid sample = make_readme_sample();
        TileSet ts = TileSet::from_sample(sample, 2);
        OverlapRules rules = OverlapRules::build(ts);

        WFCSolverOMP omp(4);
        SolverStats sa, sb;
        Grid a = solve_with(omp, ts, rules, 12345, 24, 24, 5, &sa);
        Grid b = solve_with(omp, ts, rules, 67890, 24, 24, 5, &sb);
        WFC_CHECK(sa.success);
        WFC_CHECK(sb.success);
        WFC_CHECK(!grids_equal(a, b));
    }

    // === Multivalue parity with serial ===
    {
        Grid sample = make_multivalue_sample();
        TileSet ts = TileSet::from_sample(sample, 2);
        OverlapRules rules = OverlapRules::build(ts);

        WFCSolverSerial serial;
        WFCSolverOMP omp(4);
        for (std::uint64_t seed : {1ULL, 7ULL, 42ULL}) {
            SolverStats s_stats, o_stats;
            Grid s_out = solve_with(serial, ts, rules, seed, 16, 16, 5, &s_stats);
            Grid o_out = solve_with(omp,    ts, rules, seed, 16, 16, 5, &o_stats);
            WFC_CHECK(s_stats.success);
            WFC_CHECK(o_stats.success);
            WFC_CHECK(grids_equal(s_out, o_out));
        }
    }

    // === Failure path: maze N=3 with attempts=1 should fail ===
    // This exercises the contradiction handling inside propagate_tasks
    // and the all-attempts-failed branch of WFCSolverBase::solve.
    {
        Grid sample = make_maze_sample();
        TileSet ts = TileSet::from_sample(sample, 3);
        OverlapRules rules = OverlapRules::build(ts);

        SolverStats stats;
        WFCSolverOMP omp(4);
        Grid out = solve_with(omp, ts, rules, 1, 8, 8, 1, &stats);
        (void)out;
        WFC_CHECK(!stats.success);
        WFC_CHECK_EQ(stats.attempts, 1);
        WFC_CHECK_EQ(stats.backend, std::string("omp"));
    }

    // === OMP backend identifier ===
    {
        WFCSolverOMP omp(2);
        WFC_CHECK_EQ(std::string(omp.name()), std::string("omp"));
    }

    // === OMP with 0 threads (default) still works ===
    {
        Grid sample = make_readme_sample();
        TileSet ts = TileSet::from_sample(sample, 2);
        OverlapRules rules = OverlapRules::build(ts);

        WFCSolverOMP omp(0); // 0 = use default
        SolverStats stats;
        Grid out = solve_with(omp, ts, rules, 42, 16, 16, 5, &stats);
        (void)out;
        WFC_CHECK(stats.success);
    }

    // === OMP determinism with multivalue across threads ===
    {
        Grid sample = make_multivalue_sample();
        TileSet ts = TileSet::from_sample(sample, 2);
        OverlapRules rules = OverlapRules::build(ts);

        WFCSolverOMP omp1(1);
        WFCSolverOMP omp4(4);
        WFCSolverOMP omp8(8);
        SolverStats s1, s4, s8;
        Grid r1 = solve_with(omp1, ts, rules, 99, 16, 16, 5, &s1);
        Grid r4 = solve_with(omp4, ts, rules, 99, 16, 16, 5, &s4);
        Grid r8 = solve_with(omp8, ts, rules, 99, 16, 16, 5, &s8);
        WFC_CHECK(s1.success);
        WFC_CHECK(s4.success);
        WFC_CHECK(s8.success);
        WFC_CHECK(grids_equal(r1, r4));
        WFC_CHECK(grids_equal(r4, r8));
    }

    // === OMP retry path: same maze sample with N=2, more attempts ===
    {
        Grid sample = make_maze_sample();
        TileSet ts = TileSet::from_sample(sample, 2);
        OverlapRules rules = OverlapRules::build(ts);

        SolverStats stats;
        WFCSolverOMP omp(4);
        Grid out = solve_with(omp, ts, rules, 7, 24, 24, 10, &stats);
        (void)out;
        WFC_CHECK(stats.attempts >= 1);
        WFC_CHECK(stats.attempts <= 10);
    }

    return wfc_test::report();
}
