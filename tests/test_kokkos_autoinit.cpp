// Coverage for the WFCSolverKokkos auto-initialise / auto-finalise path.
//
// Unlike test_solver_kokkos, this binary does NOT call Kokkos::initialize()
// in main(). When the solver is used, on_solve_begin() observes that Kokkos
// is not yet initialised, calls Kokkos::initialize() itself, and sets
// we_initialised_ = true. After solve, on_solve_end() then calls
// Kokkos::finalize().
//
// Once Kokkos has been finalised it cannot be re-initialised in the same
// process, so this binary runs exactly one solver invocation and exits.
// This is why we keep it in a separate test from test_solver_kokkos.

#include "test_helpers.hpp"
#include "wfc/Grid.hpp"
#include "wfc/OverlapRules.hpp"
#include "wfc/TileSet.hpp"
#include "wfc/solvers/WFCSolverKokkos.hpp"

#include <Kokkos_Core.hpp>

using namespace wfc;

int main() {
    // Sanity: Kokkos is NOT yet initialised at process start.
    WFC_CHECK(!Kokkos::is_initialized());

    Grid sample(5, 5);
    int data[] = {
        1, 0, 1, 1, 1,
        1, 0, 1, 1, 1,
        0, 0, 1, 1, 1,
        0, 1, 1, 1, 1,
        0, 0, 0, 0, 0,
    };
    sample.fill_row_major(data, 25);
    TileSet ts = TileSet::from_sample(sample, 2);
    OverlapRules rules = OverlapRules::build(ts);

    SolverOptions opt;
    opt.rows = 16;
    opt.cols = 16;
    opt.seed = 42;
    opt.max_attempts = 5;

    WFCSolverKokkos solver;
    SolverStats stats;
    Grid out = solver.solve(ts, rules, opt, stats);
    (void)out;

    // The auto-init path triggered Kokkos::initialize(), then on_solve_end()
    // called Kokkos::finalize(). Kokkos should now be back in the
    // not-initialised state.
    WFC_CHECK(stats.success);
    WFC_CHECK(!Kokkos::is_initialized());

    return wfc_test::report();
}
