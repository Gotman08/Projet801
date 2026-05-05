// Validates overlap rules: identity at (0,0), symmetry, multivalue,
// non-trivial offsets, and structural invariants.

#include "test_helpers.hpp"
#include "wfc/Grid.hpp"
#include "wfc/OverlapRules.hpp"
#include "wfc/TileSet.hpp"

#include <set>

using namespace wfc;

namespace {

Grid make_grid(int rows, int cols, std::initializer_list<int> values) {
    Grid g(rows, cols);
    int data[256];
    int i = 0;
    for (int v : values) data[i++] = v;
    g.fill_row_major(data, static_cast<std::size_t>(rows * cols));
    return g;
}

// For every tile pair (t1, t2) and every offset (dx, dy), verify the
// symmetry: t2 ∈ allow(t1, dx, dy) ⇔ t1 ∈ allow(t2, -dx, -dy).
void check_symmetry(const TileSet& ts, const OverlapRules& rules) {
    int N = ts.N();
    for (int t1 = 0; t1 < ts.size(); ++t1) {
        for (int dy = -(N - 1); dy <= N - 1; ++dy) {
            for (int dx = -(N - 1); dx <= N - 1; ++dx) {
                const Bitset& b = rules.allowed(t1, dx, dy);
                for (std::size_t t2 = 0; t2 < b.size(); ++t2) {
                    if (b.test(t2)) {
                        WFC_CHECK(rules.allowed(static_cast<int>(t2), -dx, -dy)
                                  .test(static_cast<std::size_t>(t1)));
                    }
                }
            }
        }
    }
}

// (0, 0) is identity: allowed(t, 0, 0) = {t}.
void check_identity(const TileSet& ts, const OverlapRules& rules) {
    for (int t = 0; t < ts.size(); ++t) {
        const Bitset& b = rules.allowed(t, 0, 0);
        WFC_CHECK_EQ(b.count(), 1u);
        WFC_CHECK(b.test(static_cast<std::size_t>(t)));
    }
}

// At max offset (N-1, N-1), only one cell of t1 overlaps with one cell of t2.
// So t2 is compatible iff t2[0][0] == t1[N-1][N-1].
void check_max_offset(const TileSet& ts, const OverlapRules& rules) {
    int N = ts.N();
    if (N <= 1) return;
    int dx = N - 1, dy = N - 1;
    for (int t1 = 0; t1 < ts.size(); ++t1) {
        const Bitset& b = rules.allowed(t1, dx, dy);
        Value corner = ts.tile(t1).at(N - 1, N - 1);
        for (int t2 = 0; t2 < ts.size(); ++t2) {
            const bool expected = (ts.tile(t2).at(0, 0) == corner);
            const bool actual = b.test(static_cast<std::size_t>(t2));
            WFC_CHECK_EQ(expected, actual);
        }
    }
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
        OverlapRules rules = OverlapRules::build(ts);

        WFC_CHECK_EQ(rules.num_tiles(), ts.size());
        WFC_CHECK_EQ(rules.N(), 2);
        WFC_CHECK_EQ(rules.stride(), 3);    // 2N - 1
        WFC_CHECK_EQ(rules.offsets(), 9);   // (2N-1)^2

        check_identity(ts, rules);
        check_symmetry(ts, rules);
        check_max_offset(ts, rules);
    }

    // === N=3 sample, larger offsets ===
    {
        Grid s = make_grid(5, 5, {
            1, 0, 1, 1, 1,
            1, 0, 1, 1, 1,
            0, 0, 1, 1, 1,
            0, 1, 1, 1, 1,
            0, 0, 0, 0, 0,
        });
        TileSet ts = TileSet::from_sample(s, 3);
        OverlapRules rules = OverlapRules::build(ts);

        WFC_CHECK_EQ(rules.N(), 3);
        WFC_CHECK_EQ(rules.stride(), 5);    // 2*3 - 1
        WFC_CHECK_EQ(rules.offsets(), 25);

        check_identity(ts, rules);
        check_symmetry(ts, rules);
        check_max_offset(ts, rules);
    }

    // === Multi-value sample ===
    {
        Grid s = make_grid(4, 4, {
            0, 0, 3, 3,
            0, 0, 3, 3,
            5, 5, 7, 7,
            5, 5, 7, 7,
        });
        TileSet ts = TileSet::from_sample(s, 2);
        OverlapRules rules = OverlapRules::build(ts);

        check_identity(ts, rules);
        check_symmetry(ts, rules);
        check_max_offset(ts, rules);
    }

    // === Uniform grid: a single tile that's compatible with itself
    //     at every offset ===
    {
        Grid s = make_grid(3, 3, {
            5, 5, 5,
            5, 5, 5,
            5, 5, 5,
        });
        TileSet ts = TileSet::from_sample(s, 2);
        OverlapRules rules = OverlapRules::build(ts);

        WFC_CHECK_EQ(ts.size(), 1);
        // At every offset, the single tile is compatible with itself.
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                const Bitset& b = rules.allowed(0, dx, dy);
                WFC_CHECK_EQ(b.count(), 1u);
                WFC_CHECK(b.test(0));
            }
        }
    }

    // === Striped pattern: only two tiles, alternating compatibility ===
    {
        Grid s = make_grid(4, 4, {
            0, 1, 0, 1,
            0, 1, 0, 1,
            0, 1, 0, 1,
            0, 1, 0, 1,
        });
        TileSet ts = TileSet::from_sample(s, 2);
        OverlapRules rules = OverlapRules::build(ts);

        WFC_CHECK_EQ(ts.size(), 2);
        check_identity(ts, rules);
        check_symmetry(ts, rules);
        check_max_offset(ts, rules);

        // Vertical neighbor (dx=0, dy=1): the bottom row of t1 must equal
        // the top row of t2. For the striped pattern, only one tile matches
        // (the same pattern type), so each tile has exactly 1 vertical
        // compatible.
        for (int t1 = 0; t1 < ts.size(); ++t1) {
            const Bitset& b = rules.allowed(t1, 0, 1);
            WFC_CHECK_EQ(b.count(), 1u);
            WFC_CHECK(b.test(static_cast<std::size_t>(t1)));
        }
    }

    // === Each `allowed(t, dx, dy)` must be non-empty for at least the
    //     "trivial" case where t is compatible with itself or some other
    //     tile that exists in S, otherwise propagation could deadlock.
    //     Specifically, allowed(t, 0, 0) always contains t. ===
    {
        Grid s = make_grid(3, 3, {
            0, 0, 1,
            0, 1, 0,
            1, 0, 0,
        });
        TileSet ts = TileSet::from_sample(s, 2);
        OverlapRules rules = OverlapRules::build(ts);
        for (int t = 0; t < ts.size(); ++t) {
            const Bitset& b = rules.allowed(t, 0, 0);
            WFC_CHECK(b.test(static_cast<std::size_t>(t)));
        }
    }

    // === offset_index is reversible ===
    {
        Grid s = make_grid(3, 3, {
            0, 1, 0,
            1, 0, 1,
            0, 1, 0,
        });
        TileSet ts = TileSet::from_sample(s, 2);
        OverlapRules rules = OverlapRules::build(ts);
        std::set<int> seen_indices;
        int N = ts.N();
        for (int dy = -(N - 1); dy <= N - 1; ++dy) {
            for (int dx = -(N - 1); dx <= N - 1; ++dx) {
                int idx = rules.offset_index(dx, dy);
                WFC_CHECK(idx >= 0);
                WFC_CHECK(idx < rules.offsets());
                WFC_CHECK(seen_indices.insert(idx).second); // unique
            }
        }
        WFC_CHECK_EQ(static_cast<int>(seen_indices.size()), rules.offsets());
    }

    // === Symmetric grid → at offset (1, 0) and (-1, 0), counts must match
    //     (by the symmetry property of the rule table). ===
    {
        Grid s = make_grid(4, 4, {
            0, 1, 0, 1,
            1, 0, 1, 0,
            0, 1, 0, 1,
            1, 0, 1, 0,
        });
        TileSet ts = TileSet::from_sample(s, 2);
        OverlapRules rules = OverlapRules::build(ts);
        // Count compatible pairs (t1, t2) at offsets (dx, 0) for dx in {-1, 1}.
        // By symmetry, total counts must match.
        int count_pos = 0, count_neg = 0;
        for (int t1 = 0; t1 < ts.size(); ++t1) {
            count_pos += static_cast<int>(rules.allowed(t1,  1, 0).count());
            count_neg += static_cast<int>(rules.allowed(t1, -1, 0).count());
        }
        WFC_CHECK_EQ(count_pos, count_neg);
    }

    // === Vertical symmetry: (0, 1) and (0, -1) total counts match ===
    {
        Grid s = make_grid(4, 4, {
            0, 1, 0, 1,
            1, 0, 1, 0,
            0, 1, 0, 1,
            1, 0, 1, 0,
        });
        TileSet ts = TileSet::from_sample(s, 2);
        OverlapRules rules = OverlapRules::build(ts);
        int count_pos = 0, count_neg = 0;
        for (int t1 = 0; t1 < ts.size(); ++t1) {
            count_pos += static_cast<int>(rules.allowed(t1, 0,  1).count());
            count_neg += static_cast<int>(rules.allowed(t1, 0, -1).count());
        }
        WFC_CHECK_EQ(count_pos, count_neg);
    }

    return wfc_test::report();
}
