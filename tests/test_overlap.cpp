// Validates overlap-rule symmetry and the trivial offset (0, 0).

#include "test_helpers.hpp"
#include "wfc/Grid.hpp"
#include "wfc/OverlapRules.hpp"
#include "wfc/TileSet.hpp"

using namespace wfc;

int main() {
    Grid s(5, 5);
    int data[] = {
        1, 0, 1, 1, 1,
        1, 0, 1, 1, 1,
        0, 0, 1, 1, 1,
        0, 1, 1, 1, 1,
        0, 0, 0, 0, 0,
    };
    s.fill_row_major(data, 25);

    TileSet ts = TileSet::from_sample(s, 2);
    OverlapRules rules = OverlapRules::build(ts);
    WFC_CHECK_EQ(rules.num_tiles(), ts.size());

    // At offset (0, 0) every tile is only compatible with itself.
    for (int t = 0; t < ts.size(); ++t) {
        const Bitset& b = rules.allowed(t, 0, 0);
        WFC_CHECK_EQ(b.count(), 1u);
        WFC_CHECK(b.test(static_cast<std::size_t>(t)));
    }

    // Compatibility is symmetric: t2 in allowed(t1, dx, dy) iff
    // t1 in allowed(t2, -dx, -dy).
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

    return wfc_test::report();
}
