// Comprehensive tests for Grid: construction, accessors, toroidal access,
// fill_row_major validation, edge cases (1x1, large grids).

#include "test_helpers.hpp"
#include "wfc/Grid.hpp"

#include <stdexcept>

using namespace wfc;

namespace {

template <typename F>
bool throws_invalid_argument(F&& f) {
    try { f(); }
    catch (const std::invalid_argument&) { return true; }
    catch (...) { return false; }
    return false;
}

} // namespace

int main() {
    // --- Construction ---
    {
        Grid g; // default
        WFC_CHECK_EQ(g.rows(), 0);
        WFC_CHECK_EQ(g.cols(), 0);
        WFC_CHECK_EQ(g.size(), 0u);
    }
    {
        Grid g(3, 4, 0);
        WFC_CHECK_EQ(g.rows(), 3);
        WFC_CHECK_EQ(g.cols(), 4);
        WFC_CHECK_EQ(g.size(), 12u);
        // All cells initialised to 0.
        for (int r = 0; r < 3; ++r)
            for (int c = 0; c < 4; ++c)
                WFC_CHECK_EQ(static_cast<int>(g.at(r, c)), 0);
    }
    {
        // Non-zero default fill applies to every cell.
        Grid g(2, 5, 7);
        for (int r = 0; r < 2; ++r)
            for (int c = 0; c < 5; ++c)
                WFC_CHECK_EQ(static_cast<int>(g.at(r, c)), 7);
    }

    // --- 1×1 edge case ---
    {
        Grid g(1, 1, 42);
        WFC_CHECK_EQ(g.rows(), 1);
        WFC_CHECK_EQ(g.cols(), 1);
        WFC_CHECK_EQ(g.size(), 1u);
        WFC_CHECK_EQ(static_cast<int>(g.at(0, 0)), 42);
        // Toroidal access on 1×1 always returns the same cell.
        WFC_CHECK_EQ(static_cast<int>(g.at_torus(5, -3)), 42);
        WFC_CHECK_EQ(static_cast<int>(g.at_torus(-100, 1000)), 42);
    }

    // --- Read/write ---
    {
        Grid g(3, 4, 0);
        g.at(1, 2) = 7;
        WFC_CHECK_EQ(static_cast<int>(g.at(1, 2)), 7);
        g.at(1, 2) = 99;
        WFC_CHECK_EQ(static_cast<int>(g.at(1, 2)), 99);
        // Other cells unchanged.
        WFC_CHECK_EQ(static_cast<int>(g.at(0, 0)), 0);
        WFC_CHECK_EQ(static_cast<int>(g.at(2, 3)), 0);
    }

    // --- Toroidal access basic wrap ---
    {
        Grid g(3, 4, 0);
        g.at(0, 0) = 11;
        g.at(2, 3) = 22;
        WFC_CHECK_EQ(static_cast<int>(g.at_torus(3, 4)),    11);  // wraps to (0, 0)
        WFC_CHECK_EQ(static_cast<int>(g.at_torus(-1, -1)),  22);  // wraps to (2, 3)
        WFC_CHECK_EQ(static_cast<int>(g.at_torus(0, 0)),    11);  // identity
        WFC_CHECK_EQ(static_cast<int>(g.at_torus(2, 3)),    22);  // identity
    }

    // --- Toroidal access with multi-period offsets ---
    {
        Grid g(3, 4, 0);
        g.at(1, 2) = 5;
        // Multiple wraps in both dims.
        WFC_CHECK_EQ(static_cast<int>(g.at_torus(1 + 6, 2 + 8)), 5);
        WFC_CHECK_EQ(static_cast<int>(g.at_torus(1 - 9, 2 - 12)), 5);
        WFC_CHECK_EQ(static_cast<int>(g.at_torus(1 + 30, 2 + 40)), 5);
    }

    // --- fill_row_major: success path ---
    {
        Grid g(2, 3);
        int data[] = {0, 1, 2, 3, 4, 5};
        g.fill_row_major(data, 6);
        WFC_CHECK_EQ(static_cast<int>(g.at(0, 0)), 0);
        WFC_CHECK_EQ(static_cast<int>(g.at(0, 1)), 1);
        WFC_CHECK_EQ(static_cast<int>(g.at(0, 2)), 2);
        WFC_CHECK_EQ(static_cast<int>(g.at(1, 0)), 3);
        WFC_CHECK_EQ(static_cast<int>(g.at(1, 1)), 4);
        WFC_CHECK_EQ(static_cast<int>(g.at(1, 2)), 5);
    }

    // --- fill_row_major: size mismatch throws ---
    {
        Grid g(2, 3);
        int small[] = {1, 2, 3};
        int big[]   = {1, 2, 3, 4, 5, 6, 7};
        WFC_CHECK(throws_invalid_argument([&]{ g.fill_row_major(small, 3); }));
        WFC_CHECK(throws_invalid_argument([&]{ g.fill_row_major(big, 7); }));
    }

    // --- fill_row_major: out-of-range value throws ---
    {
        Grid g(2, 2);
        int neg[]    = {0, -1, 2, 3};
        int big[]    = {0, 1, 256, 3};
        int huge[]   = {0, 1, 2, 99999};
        WFC_CHECK(throws_invalid_argument([&]{ g.fill_row_major(neg, 4); }));
        WFC_CHECK(throws_invalid_argument([&]{ g.fill_row_major(big, 4); }));
        WFC_CHECK(throws_invalid_argument([&]{ g.fill_row_major(huge, 4); }));
    }

    // --- fill_row_major: boundary values 0 and 255 accepted ---
    {
        Grid g(1, 2);
        int data[] = {0, 255};
        g.fill_row_major(data, 2);
        WFC_CHECK_EQ(static_cast<int>(g.at(0, 0)), 0);
        WFC_CHECK_EQ(static_cast<int>(g.at(0, 1)), 255);
    }

    // --- data() returns a row-major view consistent with at(r, c) ---
    {
        Grid g(2, 3);
        int data[] = {10, 20, 30, 40, 50, 60};
        g.fill_row_major(data, 6);
        const auto& buf = g.data();
        WFC_CHECK_EQ(buf.size(), 6u);
        WFC_CHECK_EQ(static_cast<int>(buf[0]), 10);
        WFC_CHECK_EQ(static_cast<int>(buf[5]), 60);
        for (int r = 0; r < 2; ++r)
            for (int c = 0; c < 3; ++c)
                WFC_CHECK_EQ(static_cast<int>(buf[r * 3 + c]),
                             static_cast<int>(g.at(r, c)));
    }

    // --- Larger grid, dense write/read pattern ---
    {
        Grid g(20, 30, 0);
        // Each cell gets (r * 7 + c * 3) mod 256.
        for (int r = 0; r < 20; ++r)
            for (int c = 0; c < 30; ++c)
                g.at(r, c) = static_cast<Value>((r * 7 + c * 3) & 0xFF);
        for (int r = 0; r < 20; ++r)
            for (int c = 0; c < 30; ++c)
                WFC_CHECK_EQ(static_cast<int>(g.at(r, c)),
                             static_cast<int>((r * 7 + c * 3) & 0xFF));
    }

    // --- Const access yields the same values ---
    {
        Grid g(3, 3);
        int data[] = {1, 2, 3, 4, 5, 6, 7, 8, 9};
        g.fill_row_major(data, 9);
        const Grid& cg = g;
        for (int r = 0; r < 3; ++r)
            for (int c = 0; c < 3; ++c)
                WFC_CHECK_EQ(static_cast<int>(cg.at(r, c)),
                             static_cast<int>(g.at(r, c)));
    }

    // --- Asymmetric shape: cols >> rows ---
    {
        Grid g(2, 50, 9);
        WFC_CHECK_EQ(g.size(), 100u);
        for (int c = 0; c < 50; ++c)
            WFC_CHECK_EQ(static_cast<int>(g.at(1, c)), 9);
    }

    // --- Asymmetric shape: rows >> cols ---
    {
        Grid g(50, 2, 1);
        WFC_CHECK_EQ(g.size(), 100u);
        for (int r = 0; r < 50; ++r)
            WFC_CHECK_EQ(static_cast<int>(g.at(r, 1)), 1);
    }

    return wfc_test::report();
}
