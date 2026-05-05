// Tests for the Kokkos backend, mirrors test_solver_omp coverage:
//  - bit-identical to serial (determinism)
//  - reseed gives different output
//  - failure path (contradiction propagation + max_attempts exhausted)
//  - multivalue parity
//  - backend identifier
//
// Compiled only when USE_KOKKOS=ON.

#include "test_helpers.hpp"
#include "wfc/Grid.hpp"
#include "wfc/OverlapRules.hpp"
#include "wfc/TileSet.hpp"
#include "wfc/solvers/WFCSolverKokkos.hpp"
#include "wfc/solvers/WFCSolverSerial.hpp"

#include <Kokkos_Core.hpp>

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

int main(int argc, char** argv) {
    Kokkos::initialize(argc, argv);
    int rc;
    {
        // === Determinism: kokkos = serial bit-for-bit ===
        // Both sides MUST succeed; otherwise two empty failure grids would
        // compare equal and silently pass.
        {
            Grid sample = make_readme_sample();
            TileSet ts = TileSet::from_sample(sample, 2);
            OverlapRules rules = OverlapRules::build(ts);

            WFCSolverSerial serial;
            SolverStats serial_stats;
            Grid baseline = solve_with(serial, ts, rules, 12345, 24, 24, 5, &serial_stats);
            WFC_CHECK(serial_stats.success);

            WFCSolverKokkos kokkos;
            SolverStats kokkos_stats;
            Grid result = solve_with(kokkos, ts, rules, 12345, 24, 24, 5, &kokkos_stats);
            WFC_CHECK(kokkos_stats.success);
            WFC_CHECK(grids_equal(baseline, result));
        }

        // === Reseed produces different output ===
        {
            Grid sample = make_readme_sample();
            TileSet ts = TileSet::from_sample(sample, 2);
            OverlapRules rules = OverlapRules::build(ts);

            WFCSolverKokkos kokkos;
            SolverStats sa, sb;
            Grid a = solve_with(kokkos, ts, rules, 12345, 24, 24, 5, &sa);
            Grid b = solve_with(kokkos, ts, rules, 67890, 24, 24, 5, &sb);
            WFC_CHECK(sa.success);
            WFC_CHECK(sb.success);
            WFC_CHECK(!grids_equal(a, b));
        }

        // === Multivalue parity ===
        {
            Grid sample = make_multivalue_sample();
            TileSet ts = TileSet::from_sample(sample, 2);
            OverlapRules rules = OverlapRules::build(ts);

            WFCSolverSerial serial;
            WFCSolverKokkos kokkos;
            for (std::uint64_t seed : {1ULL, 7ULL, 42ULL}) {
                SolverStats s_stats, k_stats;
                Grid s_out = solve_with(serial, ts, rules, seed, 16, 16, 5, &s_stats);
                Grid k_out = solve_with(kokkos, ts, rules, seed, 16, 16, 5, &k_stats);
                WFC_CHECK(s_stats.success);
                WFC_CHECK(k_stats.success);
                WFC_CHECK(grids_equal(s_out, k_out));
            }
        }

        // === Failure path: maze N=3 with attempts=1 ===
        // Exercises the contradiction handling in the Kokkos parallel_for,
        // and the failure branch of WFCSolverBase::solve.
        {
            Grid sample = make_maze_sample();
            TileSet ts = TileSet::from_sample(sample, 3);
            OverlapRules rules = OverlapRules::build(ts);

            SolverStats stats;
            WFCSolverKokkos kokkos;
            Grid out = solve_with(kokkos, ts, rules, 1, 8, 8, 1, &stats);
            (void)out;
            WFC_CHECK(!stats.success);
            WFC_CHECK_EQ(stats.attempts, 1);
            WFC_CHECK_EQ(stats.backend, std::string("kokkos"));
        }

        // === Backend identifier ===
        {
            WFCSolverKokkos kokkos;
            WFC_CHECK_EQ(std::string(kokkos.name()), std::string("kokkos"));
        }

        // === Multiple solves on the same instance ===
        {
            Grid sample = make_readme_sample();
            TileSet ts = TileSet::from_sample(sample, 2);
            OverlapRules rules = OverlapRules::build(ts);

            WFCSolverKokkos kokkos;
            // Solve repeatedly to exercise the on_solve_begin/on_solve_end
            // lifecycle.
            for (std::uint64_t seed : {1ULL, 2ULL, 3ULL}) {
                SolverStats stats;
                Grid out = solve_with(kokkos, ts, rules, seed, 12, 12, 5, &stats);
                (void)out;
                WFC_CHECK(stats.success);
            }
        }

        rc = wfc_test::report();
    }
    Kokkos::finalize();
    return rc;
}
