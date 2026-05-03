#include "test_helpers.hpp"
#include "wfc/Grid.hpp"

using namespace wfc;

int main() {
    Grid g(3, 4, 0);
    WFC_CHECK_EQ(g.rows(), 3);
    WFC_CHECK_EQ(g.cols(), 4);
    WFC_CHECK_EQ(g.size(), 12u);

    g.at(1, 2) = 7;
    WFC_CHECK_EQ(static_cast<int>(g.at(1, 2)), 7);

    // Toroidal access wraps cleanly in both directions.
    g.at(0, 0) = 11;
    g.at(2, 3) = 22;
    WFC_CHECK_EQ(static_cast<int>(g.at_torus(3, 4)),  11);  // wraps to (0, 0)
    WFC_CHECK_EQ(static_cast<int>(g.at_torus(-1, -1)), 22); // wraps to (2, 3)

    return wfc_test::report();
}
