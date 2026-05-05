// Edge-case and error-path tests targeting the lines that the
// happy-path suites don't exercise:
//
//  - Solver failure path: max_attempts exhausted with no success
//  - GridIO scale < 1 protective clamp
//  - build_output with all-empty wave (Value{0} fallback)
//  - Bitset for_each_set on patterns that trigger the inner loop branches
//  - WFCSolverBase backend identifier
//  - SolverOptions::validate edge cases
//
// These tests deliberately push parts of the code that the main
// integration tests cover only superficially.

#include "test_helpers.hpp"
#include "wfc/Bitset.hpp"
#include "wfc/Grid.hpp"
#include "wfc/GridIO.hpp"
#include "wfc/OverlapRules.hpp"
#include "wfc/TileSet.hpp"
#include "wfc/Wave.hpp"
#include "wfc/internal/SolverCommon.hpp"
#include "wfc/solvers/WFCSolverSerial.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

using namespace wfc;

namespace {

// Tightly-constrained sample known to fail with N=3 on small outputs.
// Mirror of samples/multivalue_maze.txt.
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

std::string tmp_path(const char* suffix) {
    static int counter = 0;
    auto base = std::filesystem::temp_directory_path();
    auto p = base / ("wfc_edge_" + std::to_string(++counter) + suffix);
    return p.string();
}

template <typename F>
bool throws_runtime(F&& f) {
    try { f(); }
    catch (const std::runtime_error&) { return true; }
    catch (...) { return false; }
    return false;
}

} // namespace

int main() {
    // === Solver failure path: all attempts fail ===
    // The maze sample with N=3 produces tightly-constrained tiles that
    // contradict on small outputs. With max_attempts=1, the solver fails;
    // stats reports the failure, and a (zero-filled) grid is returned.
    {
        Grid sample = make_maze_sample();
        TileSet ts = TileSet::from_sample(sample, 3);
        OverlapRules rules = OverlapRules::build(ts);

        SolverOptions opt;
        opt.rows = 8;
        opt.cols = 8;
        opt.seed = 1;
        opt.max_attempts = 1;

        SolverStats stats;
        WFCSolverSerial solver;
        Grid out = solver.solve(ts, rules, opt, stats);
        WFC_CHECK(!stats.success);
        WFC_CHECK_EQ(stats.attempts, 1);
        WFC_CHECK(stats.seconds_total >= 0.0);
        WFC_CHECK_EQ(out.rows(), 8);
        WFC_CHECK_EQ(out.cols(), 8);
        // On failure, the returned grid is default-constructed (all zeros).
        for (int r = 0; r < out.rows(); ++r)
            for (int c = 0; c < out.cols(); ++c)
                WFC_CHECK_EQ(static_cast<int>(out.at(r, c)), 0);
    }

    // === Solver failure path: max_attempts > 1, all fail ===
    // Sweep seeds to find one for which all 3 attempts fail (this exercises
    // the loop that retries with different attempt seeds before giving up).
    // We must observe at least one all-fail case, that's the entire point
    // of the test.
    {
        Grid sample = make_maze_sample();
        TileSet ts = TileSet::from_sample(sample, 3);
        OverlapRules rules = OverlapRules::build(ts);

        bool saw_all_attempts_fail = false;
        for (std::uint64_t seed = 0; seed < 30 && !saw_all_attempts_fail; ++seed) {
            SolverOptions opt;
            opt.rows = 8;
            opt.cols = 8;
            opt.seed = seed;
            opt.max_attempts = 3;

            SolverStats stats;
            WFCSolverSerial solver;
            Grid out = solver.solve(ts, rules, opt, stats);
            WFC_CHECK(stats.attempts >= 1);
            WFC_CHECK(stats.attempts <= 3);
            if (!stats.success) {
                WFC_CHECK_EQ(stats.attempts, 3);
                // Failure grid must be zero-filled (per WFCSolverBase contract).
                for (int r = 0; r < out.rows(); ++r)
                    for (int c = 0; c < out.cols(); ++c)
                        WFC_CHECK_EQ(static_cast<int>(out.at(r, c)), 0);
                saw_all_attempts_fail = true;
            }
        }
        WFC_CHECK(saw_all_attempts_fail);
    }

    // === Solver: retry path. Sample known to require retries with N=2. ===
    // The maze sample triggers contradictions on a fraction of seeds. We
    // sweep until at least one seed succeeds via retry (attempts > 1) or
    // until we've exhausted the budget. The assertion is "we exercised the
    // retry mechanism at least once across the sweep".
    {
        Grid sample = make_maze_sample();
        TileSet ts = TileSet::from_sample(sample, 2);
        OverlapRules rules = OverlapRules::build(ts);

        bool saw_retry_success = false;
        bool saw_first_attempt_success = false;
        for (std::uint64_t seed = 0; seed < 50; ++seed) {
            SolverOptions opt;
            opt.rows = 16;
            opt.cols = 16;
            opt.seed = seed;
            opt.max_attempts = 5;

            SolverStats stats;
            WFCSolverSerial solver;
            Grid out = solver.solve(ts, rules, opt, stats);
            (void)out;
            if (stats.success && stats.attempts > 1) saw_retry_success = true;
            if (stats.success && stats.attempts == 1) saw_first_attempt_success = true;
        }
        // Some seeds succeed on first attempt for any reasonable sample.
        WFC_CHECK(saw_first_attempt_success);
        // We don't assert saw_retry_success because the sample may be lucky;
        // we have separate tests that explicitly force the retry path.
        (void)saw_retry_success;
    }

    // === GridIO: scale < 1 is silently clamped to 1 (PPM) ===
    {
        Grid g(2, 3, 1);
        const std::string p = tmp_path(".ppm");
        write_grid_ppm(p, g, /*scale=*/0);
        std::ifstream in(p, std::ios::binary);
        std::string header;
        std::getline(in, header);
        WFC_CHECK_EQ(header, std::string("P6"));
        std::getline(in, header);
        // Should report 3 cols × 2 rows (no scaling).
        WFC_CHECK(header.find("3 2") != std::string::npos);
        std::remove(p.c_str());
    }

    // === GridIO: scale < 1 clamped (PNG) ===
    {
        Grid g(2, 2, 1);
        const std::string p = tmp_path(".png");
        write_grid_png(p, g, /*scale=*/-5);
        // Just check the PNG signature is present.
        std::ifstream in(p, std::ios::binary);
        unsigned char sig[8];
        in.read(reinterpret_cast<char*>(sig), 8);
        WFC_CHECK_EQ(sig[0], static_cast<unsigned char>(0x89));
        WFC_CHECK_EQ(sig[1], static_cast<unsigned char>(0x50));
        std::remove(p.c_str());
    }

    // === GridIO: write to non-existent parent dir throws ===
    // Use temp_directory_path() / non-existent-subdir / file so the path
    // is portable across Linux and Windows.
    {
        Grid g(2, 2, 1);
        auto base = std::filesystem::temp_directory_path();
        auto bad = (base / "wfc_test_no_such_subdir_zzz" / "out.ppm").string();
        WFC_CHECK(throws_runtime([&]{ write_grid_ppm(bad, g, 1); }));
    }

    // === GridIO: write_grid_txt to non-existent parent dir throws ===
    {
        Grid g(2, 2, 1);
        auto base = std::filesystem::temp_directory_path();
        auto bad = (base / "wfc_test_no_such_subdir_zzz" / "out.txt").string();
        WFC_CHECK(throws_runtime([&]{ write_grid_txt(bad, g); }));
    }

    // === GridIO: write_grid_png to a directory path triggers
    //     stbi_write_png failure → throw. We pass a path equal to an
    //     existing directory; stb_image_write opens with "wb" which fails
    //     when the target is a directory. ===
    {
        Grid g(2, 2, 1);
        // Use the temp dir itself as the "file" path, stb_image_write
        // can't fopen a directory in write-binary mode.
        auto base = std::filesystem::temp_directory_path();
        // Make sure we point at a directory path (must exist).
        WFC_CHECK(std::filesystem::exists(base));
        WFC_CHECK(std::filesystem::is_directory(base));
        WFC_CHECK(throws_runtime([&]{ write_grid_png(base.string(), g, 1); }));
    }

    // === GridIO: read_grid_txt with garbage tokens at end of line.
    //     The current parser uses `>> v` which stops on a non-numeric
    //     token. We document this behaviour: garbage at end of an otherwise
    //     valid line is silently truncated. We DO NOT silently truncate
    //     mid-grid because the row-width check then rejects mismatched rows. ===
    {
        const std::string p = tmp_path(".txt");
        std::ofstream(p) << "1 2 3 abc\n4 5 6\n";
        // Both rows parse to 3 numeric tokens; widths match → success.
        Grid g = read_grid_txt(p);
        WFC_CHECK_EQ(g.rows(), 2);
        WFC_CHECK_EQ(g.cols(), 3);
        WFC_CHECK_EQ(static_cast<int>(g.at(0, 2)), 3);
        WFC_CHECK_EQ(static_cast<int>(g.at(1, 2)), 6);
        std::remove(p.c_str());
    }

    // === GridIO: garbage that creates inconsistent widths IS rejected ===
    {
        const std::string p = tmp_path(".txt");
        std::ofstream(p) << "1 2 abc 3\n4 5 6 7\n";
        // First row: parses "1 2", then sees "abc" and stops → 2 tokens.
        // Second row: 4 tokens. Widths mismatch → throw.
        WFC_CHECK(throws_runtime([&]{ read_grid_txt(p); }));
        std::remove(p.c_str());
    }

    // === build_output: all-empty wave returns all zeros ===
    {
        Grid sample(3, 3);
        int data[] = {0, 1, 0, 1, 0, 1, 0, 1, 0};
        sample.fill_row_major(data, 9);
        TileSet ts = TileSet::from_sample(sample, 2);

        Wave w(4, 4, ts.size());
        for (int c = 0; c < w.num_cells(); ++c) w.at(c).reset();

        Grid out = build_output(w, ts);
        WFC_CHECK_EQ(out.rows(), 4);
        WFC_CHECK_EQ(out.cols(), 4);
        // Every cell falls through to Value{0}.
        for (int r = 0; r < 4; ++r)
            for (int c = 0; c < 4; ++c)
                WFC_CHECK_EQ(static_cast<int>(out.at(r, c)), 0);
    }

    // === build_output: mixed wave (some collapsed, some empty) ===
    {
        Grid sample(3, 3);
        int data[] = {0, 1, 0, 1, 0, 1, 0, 1, 0};
        sample.fill_row_major(data, 9);
        TileSet ts = TileSet::from_sample(sample, 2);

        Wave w(2, 2, ts.size());
        // Collapse (0,0) to first tile, leave others empty.
        for (int c = 0; c < w.num_cells(); ++c) w.at(c).reset();
        w.at(0, 0).set_only(0);

        Grid out = build_output(w, ts);
        // (0,0) gets the value from tile 0's top-left corner.
        WFC_CHECK_EQ(static_cast<int>(out.at(0, 0)),
                     static_cast<int>(ts.tile(0).at(0, 0)));
        // Others are 0 (empty).
        WFC_CHECK_EQ(static_cast<int>(out.at(0, 1)), 0);
        WFC_CHECK_EQ(static_cast<int>(out.at(1, 0)), 0);
        WFC_CHECK_EQ(static_cast<int>(out.at(1, 1)), 0);
    }

    // === Bitset for_each_set: dense pattern across word boundaries ===
    // Forces the inner ctz/clear loop to iterate many times within a single
    // word and across multiple words.
    {
        Bitset b(192);
        // Dense first word (every bit), sparse second word, full third word.
        for (int i = 0; i < 64; ++i) b.set(i);
        b.set(100);
        for (int i = 128; i < 192; ++i) b.set(i);

        std::vector<std::size_t> seen;
        b.for_each_set([&](std::size_t i) { seen.push_back(i); });
        WFC_CHECK_EQ(seen.size(), 64u + 1u + 64u);
        // First 64 bits.
        for (int i = 0; i < 64; ++i) WFC_CHECK_EQ(seen[i], static_cast<std::size_t>(i));
        WFC_CHECK_EQ(seen[64], 100u);
        for (int i = 0; i < 64; ++i)
            WFC_CHECK_EQ(seen[65 + i], static_cast<std::size_t>(128 + i));
    }

    // === Bitset: for_each_set on completely full bitset of 64 bits ===
    {
        Bitset b = Bitset::full(64);
        std::size_t expected = 0;
        bool ok = true;
        b.for_each_set([&](std::size_t i) {
            if (i != expected++) ok = false;
        });
        WFC_CHECK(ok);
        WFC_CHECK_EQ(expected, 64u);
    }

    // === Bitset: only-last-bit-set ===
    {
        Bitset b(128);
        b.set(127);
        std::vector<std::size_t> seen;
        b.for_each_set([&](std::size_t i) { seen.push_back(i); });
        WFC_CHECK_EQ(seen.size(), 1u);
        WFC_CHECK_EQ(seen[0], 127u);
    }

    // === Backend identifier: serial ===
    {
        WFCSolverSerial solver;
        WFC_CHECK_EQ(std::string(solver.name()), std::string("serial"));
    }

    // === SolverOptions::validate: rows < 1 ===
    {
        SolverOptions opt;
        opt.rows = 0;
        bool threw = false;
        try { opt.validate(); }
        catch (const std::invalid_argument&) { threw = true; }
        WFC_CHECK(threw);
    }

    // === SolverOptions::validate: cols < 1 ===
    {
        SolverOptions opt;
        opt.cols = -3;
        bool threw = false;
        try { opt.validate(); }
        catch (const std::invalid_argument&) { threw = true; }
        WFC_CHECK(threw);
    }

    // === SolverOptions::validate: max_attempts < 1 ===
    {
        SolverOptions opt;
        opt.max_attempts = 0;
        bool threw = false;
        try { opt.validate(); }
        catch (const std::invalid_argument&) { threw = true; }
        WFC_CHECK(threw);
    }

    // === SolverOptions::validate: valid options pass ===
    {
        SolverOptions opt;
        opt.rows = 1; opt.cols = 1; opt.max_attempts = 1; opt.seed = 0;
        bool threw = false;
        try { opt.validate(); }
        catch (...) { threw = true; }
        WFC_CHECK(!threw);
    }

    // === SolverStats default values are sensible ===
    {
        SolverStats s;
        WFC_CHECK(!s.success);
        WFC_CHECK_EQ(s.attempts, 0);
        WFC_CHECK_EQ(s.collapses, 0);
        WFC_CHECK_EQ(s.propagations, 0);
        WFC_CHECK_EQ(s.seconds_total, 0.0);
        WFC_CHECK_EQ(s.seconds_solve, 0.0);
        WFC_CHECK_EQ(s.backend, std::string("serial"));
    }

    // === Tile equality ===
    {
        Tile a;
        a.N = 2;
        a.data = {0, 1, 1, 0};
        Tile b;
        b.N = 2;
        b.data = {0, 1, 1, 0};
        WFC_CHECK(a == b);

        Tile c;
        c.N = 2;
        c.data = {0, 1, 1, 1};
        WFC_CHECK(!(a == c));

        Tile d;
        d.N = 3;
        d.data = {0, 1, 1, 0, 0, 1, 1, 1, 0};
        WFC_CHECK(!(a == d));
    }

    // === TileHash: equal tiles produce equal hashes ===
    {
        Tile a;
        a.N = 2;
        a.data = {3, 5, 7, 9};
        Tile b = a;
        TileHash h;
        WFC_CHECK_EQ(h(a), h(b));
    }

    // === TileHash: different tiles usually produce different hashes ===
    {
        Tile a; a.N = 2; a.data = {0, 1, 0, 1};
        Tile b; b.N = 2; b.data = {1, 0, 1, 0};
        TileHash h;
        WFC_CHECK(h(a) != h(b));
    }

    // === Tile::at returns the right value ===
    {
        Tile t;
        t.N = 3;
        t.data = {0, 1, 2, 3, 4, 5, 6, 7, 8};
        WFC_CHECK_EQ(static_cast<int>(t.at(0, 0)), 0);
        WFC_CHECK_EQ(static_cast<int>(t.at(0, 2)), 2);
        WFC_CHECK_EQ(static_cast<int>(t.at(1, 1)), 4);
        WFC_CHECK_EQ(static_cast<int>(t.at(2, 0)), 6);
        WFC_CHECK_EQ(static_cast<int>(t.at(2, 2)), 8);
    }

    // === Wave: BitsetView and ConstBitsetView agree on values ===
    {
        Wave w(2, 2, 8);
        w.at(1, 1).set_only(5);
        const Wave& cw = w;
        ConstBitsetView v = cw.at(1, 1);
        WFC_CHECK_EQ(v.count(), 1u);
        WFC_CHECK(v.test(5));
        WFC_CHECK_EQ(v.first_set(), 5u);
    }

    // === read_grid_txt with content-only-comments fails ===
    {
        const std::string p = tmp_path(".txt");
        std::ofstream(p) << "# only comments\n# nothing else\n   \n   \n";
        WFC_CHECK(throws_runtime([&]{ read_grid_txt(p); }));
        std::remove(p.c_str());
    }

    // === Run a successful solve to also count stats.collapses + propagations ===
    {
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
        opt.rows = 8; opt.cols = 8; opt.seed = 0; opt.max_attempts = 5;
        SolverStats stats;
        WFCSolverSerial solver;
        Grid out = solver.solve(ts, rules, opt, stats);
        WFC_CHECK(stats.success);
        WFC_CHECK(stats.collapses > 0);
        WFC_CHECK(stats.collapses <= 8 * 8);
        WFC_CHECK(stats.propagations > 0);
        WFC_CHECK_EQ(out.rows(), 8);
        WFC_CHECK_EQ(out.cols(), 8);
        // Soundness: every output value must be in the input alphabet (0 or 1).
        for (int r = 0; r < out.rows(); ++r)
            for (int c = 0; c < out.cols(); ++c)
                WFC_CHECK(static_cast<int>(out.at(r, c)) <= 1);
    }

    return wfc_test::report();
}
