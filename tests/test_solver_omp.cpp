// Determinism check across the OMP backend: for a given seed, the OMP
// solver must produce a bit-identical grid to the serial solver, regardless
// of thread count. Compiled only when USE_OMP=ON.

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
    opt.max_attempts = 5;
    SolverStats stats;
    return s.solve(tiles, rules, opt, stats);
}

} // namespace

int main() {
    Grid sample = make_readme_sample();
    TileSet ts = TileSet::from_sample(sample, 2);
    OverlapRules rules = OverlapRules::build(ts);

    WFCSolverSerial serial;
    Grid baseline = solve_with(serial, ts, rules, 12345);

    // OMP must match serial bit-for-bit at every reasonable thread count.
    for (int threads : {1, 2, 4, 8}) {
        WFCSolverOMP omp(threads);
        Grid result = solve_with(omp, ts, rules, 12345);
        WFC_CHECK(grids_equal(baseline, result));
    }

    // And reseeding produces a different deterministic output.
    WFCSolverOMP omp(4);
    Grid alt = solve_with(omp, ts, rules, 67890);
    WFC_CHECK(!grids_equal(baseline, alt));

    return wfc_test::report();
}
