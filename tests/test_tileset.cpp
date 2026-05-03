// Validates tile extraction against the worked example from the README.

#include "test_helpers.hpp"
#include "wfc/Grid.hpp"
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

    // Toroidal sampling produces 25 tiles total (rows*cols).
    std::uint32_t total = 0;
    for (auto f : ts.frequencies()) total += f;
    WFC_CHECK_EQ(static_cast<int>(total), 25);
    WFC_CHECK_EQ(static_cast<int>(ts.max_value()), 1);

    // The README only counts non-toroidal tiles (16 of them) but the unique
    // patterns must be present and correctly hashed.
    auto contains = [&](std::initializer_list<int> pattern) {
        for (int id = 0; id < ts.size(); ++id) {
            const Tile& t = ts.tile(id);
            bool eq = true;
            int k = 0;
            for (int v : pattern) {
                if (static_cast<int>(t.data[k++]) != v) { eq = false; break; }
            }
            if (eq) return true;
        }
        return false;
    };
    WFC_CHECK(contains({1, 0, 1, 0}));
    WFC_CHECK(contains({0, 1, 0, 1}));
    WFC_CHECK(contains({1, 1, 1, 1}));
    WFC_CHECK(contains({1, 1, 0, 0}));
    WFC_CHECK(!contains({0, 0, 0, 0})); // pure-zero 2x2 never appears in S

    return wfc_test::report();
}
