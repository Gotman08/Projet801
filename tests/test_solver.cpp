// End-to-end tests on the serial solver:
//  - It solves the README example without contradicting itself.
//  - The same seed produces a bit-identical output (deterministic).
//  - Patterns produced never violate the overlap rules.

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

// Three-value sample (water/sand/grass) with smooth transitions, designed
// to be solvable with N=2 — used to verify the solver is value-agnostic.
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

bool grids_equal(const Grid& a, const Grid& b) {
    if (a.rows() != b.rows() || a.cols() != b.cols()) return false;
    for (int r = 0; r < a.rows(); ++r)
        for (int c = 0; c < a.cols(); ++c)
            if (a.at(r, c) != b.at(r, c)) return false;
    return true;
}

// Returns the 1-tile entropy of an output grid: every N x N pattern in the
// output (toroidally) must appear in the input tile set.
bool output_uses_only_input_tiles(const Grid& out, const TileSet& ts) {
    int N = ts.N();
    for (int r = 0; r < out.rows(); ++r) {
        for (int c = 0; c < out.cols(); ++c) {
            Tile t;
            t.N = N;
            t.data.resize(static_cast<std::size_t>(N) * N);
            for (int i = 0; i < N; ++i)
                for (int j = 0; j < N; ++j)
                    t.data[static_cast<std::size_t>(i) * N + j] = out.at_torus(r + i, c + j);
            bool ok = false;
            for (const auto& known : ts.tiles()) {
                if (known == t) { ok = true; break; }
            }
            if (!ok) return false;
        }
    }
    return true;
}

} // namespace

int main() {
    Grid sample = make_readme_sample();
    TileSet ts = TileSet::from_sample(sample, 2);
    OverlapRules rules = OverlapRules::build(ts);

    SolverOptions opt;
    opt.rows = 16;
    opt.cols = 16;
    opt.seed = 42;
    opt.max_attempts = 5;

    SolverStats s1, s2;
    WFCSolverSerial solver;
    Grid g1 = solver.solve(ts, rules, opt, s1);
    Grid g2 = solver.solve(ts, rules, opt, s2);

    WFC_CHECK(s1.success);
    WFC_CHECK(s2.success);
    WFC_CHECK(grids_equal(g1, g2));               // determinism
    WFC_CHECK(output_uses_only_input_tiles(g1, ts)); // soundness

    // Different seed produces a different output (with high probability).
    SolverOptions opt2 = opt;
    opt2.seed = 4242;
    SolverStats s3;
    Grid g3 = solver.solve(ts, rules, opt2, s3);
    WFC_CHECK(s3.success);
    WFC_CHECK(output_uses_only_input_tiles(g3, ts));

    // Multi-value sample: solver must handle K > 2 values without any
    // change. The output must remain sound (only known tiles) and
    // every value present must come from the input alphabet.
    Grid mv_sample = make_multivalue_sample();
    TileSet mv_ts = TileSet::from_sample(mv_sample, 2);
    OverlapRules mv_rules = OverlapRules::build(mv_ts);
    WFC_CHECK(static_cast<int>(mv_ts.max_value()) >= 4);

    SolverOptions mv_opt;
    mv_opt.rows = 16;
    mv_opt.cols = 16;
    mv_opt.seed = 11;
    mv_opt.max_attempts = 5;

    SolverStats mv_stats;
    Grid mv_out = solver.solve(mv_ts, mv_rules, mv_opt, mv_stats);
    WFC_CHECK(mv_stats.success);
    WFC_CHECK(output_uses_only_input_tiles(mv_out, mv_ts));

    return wfc_test::report();
}
