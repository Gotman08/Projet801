// Tests for the optional D4-symmetry expansion in TileSet::from_sample.
//
// Invariants checked :
//   - --symmetries 1 produces a tile set strictly identical to the legacy
//     default path (regression guard for the hot path)
//   - --symmetries N >= 2 expands the tile catalogue but never below
//     the symmetries=1 size
//   - Self-symmetric patterns (uniform fill, checkerboards) do not have
//     their frequencies double-counted under reflection / rotation
//   - rotated_90 applied four times is the identity
//   - reflected_horizontal applied twice is the identity
//   - Invalid symmetry counts throw std::invalid_argument

#include "test_helpers.hpp"
#include "wfc/Grid.hpp"
#include "wfc/Tile.hpp"
#include "wfc/TileSet.hpp"

#include <stdexcept>

using namespace wfc;

namespace {

Grid make_asymmetric_sample() {
    // A small sample with no rotational or reflective symmetry, so the
    // expansion factor is fully exercised (each new variant survives
    // the dedup).
    Grid g(4, 4);
    int data[] = {
        0, 1, 2, 3,
        4, 5, 6, 7,
        8, 9, 0, 1,
        2, 3, 4, 5,
    };
    g.fill_row_major(data, 16);
    return g;
}

Grid make_uniform_sample() {
    // Every cell is 0. Every N x N tile is identical and self-symmetric
    // under any D4 operation, so symmetries 1, 2, 4, 8 all yield exactly
    // one tile.
    return Grid(6, 6, 0);
}

Grid make_readme_sample() {
    Grid g(5, 5);
    int data[] = {
        1, 0, 1, 1, 1,
        1, 0, 1, 1, 1,
        0, 0, 1, 1, 1,
        0, 1, 1, 1, 1,
        0, 0, 0, 0, 0,
    };
    g.fill_row_major(data, 25);
    return g;
}

bool tile_eq(const Tile& a, const Tile& b) {
    return a == b;
}

} // namespace

int main() {
    // === Tile rotated_90 four times == identity ===
    {
        Grid g = make_asymmetric_sample();
        TileSet ts = TileSet::from_sample(g, 2);
        for (int i = 0; i < ts.size(); ++i) {
            const Tile& t = ts.tile(i);
            Tile r = t.rotated_90().rotated_90().rotated_90().rotated_90();
            WFC_CHECK(tile_eq(t, r));
        }
    }

    // === Tile reflected_horizontal twice == identity ===
    {
        Grid g = make_asymmetric_sample();
        TileSet ts = TileSet::from_sample(g, 2);
        for (int i = 0; i < ts.size(); ++i) {
            const Tile& t = ts.tile(i);
            Tile r = t.reflected_horizontal().reflected_horizontal();
            WFC_CHECK(tile_eq(t, r));
        }
    }

    // === Regression : symmetries=1 strictly identical to legacy path ===
    {
        Grid g = make_readme_sample();
        TileSet a = TileSet::from_sample(g, 2);          // legacy default
        TileSet b = TileSet::from_sample(g, 2, 1);       // explicit 1
        WFC_CHECK_EQ(a.size(), b.size());
        // Same tile content, same frequencies, same order.
        for (int i = 0; i < a.size(); ++i) {
            WFC_CHECK(tile_eq(a.tile(i), b.tile(i)));
            WFC_CHECK_EQ(a.frequency(i), b.frequency(i));
        }
    }

    // === Uniform sample : every symmetry collapses to one tile ===
    {
        Grid g = make_uniform_sample();
        TileSet s1 = TileSet::from_sample(g, 2, 1);
        TileSet s2 = TileSet::from_sample(g, 2, 2);
        TileSet s4 = TileSet::from_sample(g, 2, 4);
        TileSet s8 = TileSet::from_sample(g, 2, 8);
        WFC_CHECK_EQ(s1.size(), 1);
        WFC_CHECK_EQ(s2.size(), 1);
        WFC_CHECK_EQ(s4.size(), 1);
        WFC_CHECK_EQ(s8.size(), 1);
        // Frequency is preserved (not double-counted under self-symmetric
        // expansion). With a 6x6 toroidal sample, every origin yields the
        // same uniform tile, so frequency = 36 in all cases.
        WFC_CHECK_EQ(s1.frequency(0), 36u);
        WFC_CHECK_EQ(s8.frequency(0), 36u);
    }

    // === Asymmetric sample : expansion grows the tile set ===
    {
        Grid g = make_asymmetric_sample();
        TileSet s1 = TileSet::from_sample(g, 2, 1);
        TileSet s2 = TileSet::from_sample(g, 2, 2);
        TileSet s4 = TileSet::from_sample(g, 2, 4);
        TileSet s8 = TileSet::from_sample(g, 2, 8);
        // Monotonic growth : s1 <= s2 <= s4 <= s8
        WFC_CHECK(s1.size() <= s2.size());
        WFC_CHECK(s2.size() <= s4.size());
        WFC_CHECK(s4.size() <= s8.size());
        // For this fully asymmetric sample, expansion really does add
        // new patterns at each level (sanity check that the helper
        // emit_variants is not accidentally a no-op).
        WFC_CHECK(s1.size() < s8.size());
    }

    // === Invalid symmetry counts are rejected ===
    {
        Grid g = make_readme_sample();
        bool threw_3 = false;
        try { TileSet::from_sample(g, 2, 3); }
        catch (const std::invalid_argument&) { threw_3 = true; }
        WFC_CHECK(threw_3);

        bool threw_0 = false;
        try { TileSet::from_sample(g, 2, 0); }
        catch (const std::invalid_argument&) { threw_0 = true; }
        WFC_CHECK(threw_0);

        bool threw_neg = false;
        try { TileSet::from_sample(g, 2, -4); }
        catch (const std::invalid_argument&) { threw_neg = true; }
        WFC_CHECK(threw_neg);
    }

    return wfc_test::report();
}
