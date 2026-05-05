// Comprehensive tests for TileSet: extraction, frequencies, deduplication,
// edge cases (N=1, multi-value, N matching grid size).

#include "test_helpers.hpp"
#include "wfc/Grid.hpp"
#include "wfc/TileSet.hpp"

#include <set>

using namespace wfc;

namespace {

Grid make_grid(int rows, int cols, std::initializer_list<int> values) {
    Grid g(rows, cols);
    int data[256]; // way bigger than we need for tests
    int i = 0;
    for (int v : values) data[i++] = v;
    g.fill_row_major(data, static_cast<std::size_t>(rows * cols));
    return g;
}

bool tile_equals(const Tile& t, std::initializer_list<int> expected) {
    if (t.data.size() != expected.size()) return false;
    int i = 0;
    for (int v : expected)
        if (static_cast<int>(t.data[i++]) != v) return false;
    return true;
}

bool tileset_contains(const TileSet& ts, std::initializer_list<int> expected) {
    for (int id = 0; id < ts.size(); ++id)
        if (tile_equals(ts.tile(id), expected)) return true;
    return false;
}

int find_tile(const TileSet& ts, std::initializer_list<int> expected) {
    for (int id = 0; id < ts.size(); ++id)
        if (tile_equals(ts.tile(id), expected)) return id;
    return -1;
}

} // namespace

int main() {
    // === README example, N=2 ===
    {
        Grid s = make_grid(5, 5, {
            1, 0, 1, 1, 1,
            1, 0, 1, 1, 1,
            0, 0, 1, 1, 1,
            0, 1, 1, 1, 1,
            0, 0, 0, 0, 0,
        });
        TileSet ts = TileSet::from_sample(s, 2);

        // Toroidal sampling produces 25 tiles total (rows*cols).
        std::uint32_t total = 0;
        for (auto f : ts.frequencies()) total += f;
        WFC_CHECK_EQ(static_cast<int>(total), 25);
        WFC_CHECK_EQ(static_cast<int>(ts.max_value()), 1);
        WFC_CHECK_EQ(ts.N(), 2);

        // Specific patterns expected.
        WFC_CHECK(tileset_contains(ts, {1, 0, 1, 0}));
        WFC_CHECK(tileset_contains(ts, {0, 1, 0, 1}));
        WFC_CHECK(tileset_contains(ts, {1, 1, 1, 1}));
        WFC_CHECK(tileset_contains(ts, {1, 1, 0, 0}));
        // {0,0,0,0} does NOT appear in S: the only all-zero row is the
        // bottom one; toroidally, the row "below" is row 0 (`1 0 1 1 1`),
        // so no 2×2 tile is fully zero.
        WFC_CHECK(!tileset_contains(ts, {0, 0, 0, 0}));
    }

    // === Total frequency property: holds for any grid/N ===
    {
        Grid s = make_grid(4, 4, {
            0, 1, 0, 1,
            1, 0, 1, 0,
            0, 1, 0, 1,
            1, 0, 1, 0,
        });
        for (int N : {1, 2, 3, 4}) {
            TileSet ts = TileSet::from_sample(s, N);
            std::uint32_t total = 0;
            for (auto f : ts.frequencies()) total += f;
            WFC_CHECK_EQ(static_cast<int>(total), 16);
        }
    }

    // === N=1 degenerate case: each tile is a single cell ===
    {
        Grid s = make_grid(3, 3, {
            0, 1, 2,
            1, 2, 0,
            2, 0, 1,
        });
        TileSet ts = TileSet::from_sample(s, 1);
        WFC_CHECK_EQ(ts.N(), 1);
        // Three distinct values → three unique tiles.
        WFC_CHECK_EQ(ts.size(), 3);
        // Total frequency = rows*cols = 9.
        std::uint32_t total = 0;
        for (auto f : ts.frequencies()) total += f;
        WFC_CHECK_EQ(static_cast<int>(total), 9);
        // Each tile has exactly one cell, all values present.
        std::set<int> seen;
        for (int id = 0; id < ts.size(); ++id) {
            const Tile& t = ts.tile(id);
            WFC_CHECK_EQ(t.N, 1);
            WFC_CHECK_EQ(t.data.size(), 1u);
            seen.insert(static_cast<int>(t.at(0, 0)));
        }
        WFC_CHECK(seen.count(0) == 1);
        WFC_CHECK(seen.count(1) == 1);
        WFC_CHECK(seen.count(2) == 1);
        // Each value appears exactly 3 times in the grid.
        for (int id = 0; id < ts.size(); ++id)
            WFC_CHECK_EQ(static_cast<int>(ts.frequency(id)), 3);
    }

    // === Multi-value tiles, N=2 ===
    {
        Grid s = make_grid(4, 4, {
            0, 0, 3, 3,
            0, 0, 3, 3,
            5, 5, 7, 7,
            5, 5, 7, 7,
        });
        TileSet ts = TileSet::from_sample(s, 2);
        WFC_CHECK_EQ(static_cast<int>(ts.max_value()), 7);
        // Should include {0,0,0,0}, {3,3,3,3}, {5,5,5,5}, {7,7,7,7}, plus
        // some transition tiles.
        WFC_CHECK(tileset_contains(ts, {0, 0, 0, 0}));
        WFC_CHECK(tileset_contains(ts, {3, 3, 3, 3}));
        WFC_CHECK(tileset_contains(ts, {5, 5, 5, 5}));
        WFC_CHECK(tileset_contains(ts, {7, 7, 7, 7}));
        WFC_CHECK(tileset_contains(ts, {0, 3, 0, 3}));  // vertical seam left/right
        WFC_CHECK(tileset_contains(ts, {0, 0, 5, 5}));  // horizontal seam top/bottom
    }

    // === Uniform grid: only one unique tile, frequency = rows*cols ===
    {
        Grid s = make_grid(4, 4, {
            7, 7, 7, 7,
            7, 7, 7, 7,
            7, 7, 7, 7,
            7, 7, 7, 7,
        });
        TileSet ts = TileSet::from_sample(s, 2);
        WFC_CHECK_EQ(ts.size(), 1);
        WFC_CHECK_EQ(static_cast<int>(ts.frequency(0)), 16);
        WFC_CHECK(tile_equals(ts.tile(0), {7, 7, 7, 7}));
        WFC_CHECK_EQ(static_cast<int>(ts.max_value()), 7);
    }

    // === Striped pattern: limited unique tiles ===
    {
        Grid s = make_grid(4, 4, {
            0, 1, 0, 1,
            0, 1, 0, 1,
            0, 1, 0, 1,
            0, 1, 0, 1,
        });
        TileSet ts = TileSet::from_sample(s, 2);
        // Only two distinct 2x2 patterns: {0,1,0,1} and {1,0,1,0}.
        WFC_CHECK_EQ(ts.size(), 2);
        WFC_CHECK(tileset_contains(ts, {0, 1, 0, 1}));
        WFC_CHECK(tileset_contains(ts, {1, 0, 1, 0}));
        // Each appears 8 times (half of 16).
        for (int id = 0; id < ts.size(); ++id)
            WFC_CHECK_EQ(static_cast<int>(ts.frequency(id)), 8);
    }

    // === N=3 on 5×5: extraction works ===
    {
        Grid s = make_grid(5, 5, {
            1, 0, 1, 1, 1,
            1, 0, 1, 1, 1,
            0, 0, 1, 1, 1,
            0, 1, 1, 1, 1,
            0, 0, 0, 0, 0,
        });
        TileSet ts = TileSet::from_sample(s, 3);
        WFC_CHECK_EQ(ts.N(), 3);
        std::uint32_t total = 0;
        for (auto f : ts.frequencies()) total += f;
        WFC_CHECK_EQ(static_cast<int>(total), 25);
        // Each tile has 9 cells.
        for (int id = 0; id < ts.size(); ++id)
            WFC_CHECK_EQ(ts.tile(id).data.size(), 9u);
    }

    // === No duplicate tiles in tiles_ ===
    {
        Grid s = make_grid(5, 5, {
            1, 0, 1, 1, 1,
            1, 0, 1, 1, 1,
            0, 0, 1, 1, 1,
            0, 1, 1, 1, 1,
            0, 0, 0, 0, 0,
        });
        TileSet ts = TileSet::from_sample(s, 2);
        for (int i = 0; i < ts.size(); ++i)
            for (int j = i + 1; j < ts.size(); ++j)
                WFC_CHECK(!(ts.tile(i) == ts.tile(j)));
    }

    // === Frequency consistency: matches manual count ===
    {
        // Pattern with one cell different in the middle.
        Grid s = make_grid(3, 3, {
            0, 0, 0,
            0, 1, 0,
            0, 0, 0,
        });
        TileSet ts = TileSet::from_sample(s, 2);
        // Toroidal: 9 tiles total, 4 of them contain the '1'.
        std::uint32_t total = 0;
        std::uint32_t with_one = 0;
        for (int id = 0; id < ts.size(); ++id) {
            total += ts.frequency(id);
            const Tile& t = ts.tile(id);
            for (auto v : t.data) if (v == 1) { with_one += ts.frequency(id); break; }
        }
        WFC_CHECK_EQ(static_cast<int>(total), 9);
        WFC_CHECK_EQ(static_cast<int>(with_one), 4);
    }

    // === Tile id stability: find_tile returns valid id ===
    {
        Grid s = make_grid(4, 4, {
            0, 1, 0, 1,
            0, 1, 0, 1,
            0, 1, 0, 1,
            0, 1, 0, 1,
        });
        TileSet ts = TileSet::from_sample(s, 2);
        int id1 = find_tile(ts, {0, 1, 0, 1});
        int id2 = find_tile(ts, {1, 0, 1, 0});
        WFC_CHECK(id1 >= 0);
        WFC_CHECK(id2 >= 0);
        WFC_CHECK(id1 != id2);
        WFC_CHECK(id1 < ts.size());
        WFC_CHECK(id2 < ts.size());
    }

    // === High value (close to uint8_t max) ===
    {
        Grid s = make_grid(2, 2, {
            250, 251,
            252, 253,
        });
        TileSet ts = TileSet::from_sample(s, 1);
        WFC_CHECK_EQ(ts.size(), 4);
        WFC_CHECK_EQ(static_cast<int>(ts.max_value()), 253);
    }

    return wfc_test::report();
}
