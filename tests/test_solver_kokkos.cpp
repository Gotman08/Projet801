// Determinism check for the Kokkos backend, mirroring test_solver_omp.
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

bool grids_equal(const Grid& a, const Grid& b) {
    if (a.rows() != b.rows() || a.cols() != b.cols()) return false;
    for (int r = 0; r < a.rows(); ++r)
        for (int c = 0; c < a.cols(); ++c)
            if (a.at(r, c) != b.at(r, c)) return false;
    return true;
}

Grid solve_with(WFCSolver& s, const TileSet& tiles, const OverlapRules& rules,
                std::uint64_t seed) {
    SolverOptions opt;
    opt.rows = 24;
    opt.cols = 24;
    opt.seed = seed;
    SolverStats stats;
    return s.solve(tiles, rules, opt, stats);
}

} // namespace

int main(int argc, char** argv) {
    Kokkos::initialize(argc, argv);
    int rc;
    {
        Grid sample = make_readme_sample();
        TileSet ts = TileSet::from_sample(sample, 2);
        OverlapRules rules = OverlapRules::build(ts);

        WFCSolverSerial serial;
        Grid baseline = solve_with(serial, ts, rules, 12345);

        WFCSolverKokkos kokkos;
        Grid result = solve_with(kokkos, ts, rules, 12345);
        WFC_CHECK(grids_equal(baseline, result));

        Grid alt = solve_with(kokkos, ts, rules, 67890);
        WFC_CHECK(!grids_equal(baseline, alt));

        rc = wfc_test::report();
    }
    Kokkos::finalize();
    return rc;
}
