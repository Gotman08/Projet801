// Unit tests for Wave: the superposition state holding a Bitset per cell
// over a flat u64 buffer. Covers initialisation, accessors, toroidal
// indexing, cell independence, and varied tile counts (single-word vs
// multi-word per cell).

#include "test_helpers.hpp"
#include "wfc/Bitset.hpp"
#include "wfc/Wave.hpp"

using namespace wfc;

namespace {

// Count cells whose bitset has at least one bit set.
int active_cells(const Wave& w) {
    int count = 0;
    for (int c = 0; c < w.num_cells(); ++c)
        if (w.at(c).any()) ++count;
    return count;
}

} // namespace

int main() {
    // === Construction: every cell starts with all tiles possible ===
    {
        Wave w(3, 4, /*num_tiles=*/5);
        WFC_CHECK_EQ(w.rows(), 3);
        WFC_CHECK_EQ(w.cols(), 4);
        WFC_CHECK_EQ(w.num_cells(), 12);
        WFC_CHECK_EQ(w.num_tiles(), 5);
        for (int c = 0; c < w.num_cells(); ++c) {
            WFC_CHECK_EQ(w.at(c).count(), 5u);
            WFC_CHECK(w.at(c).any());
        }
    }

    // === at(r, c) and at(cell) point to the same view ===
    {
        Wave w(3, 4, 8);
        for (int r = 0; r < 3; ++r) {
            for (int c = 0; c < 4; ++c) {
                int cell = r * 4 + c;
                BitsetView a = w.at(r, c);
                BitsetView b = w.at(cell);
                WFC_CHECK_EQ(a.size(), b.size());
                WFC_CHECK_EQ(a.words(), b.words());
            }
        }
    }

    // === Modifications persist on subsequent reads ===
    {
        Wave w(2, 3, 10);
        w.at(0, 0).set_only(3);
        w.at(1, 2).set_only(7);
        WFC_CHECK_EQ(w.at(0, 0).count(), 1u);
        WFC_CHECK(w.at(0, 0).test(3));
        WFC_CHECK_EQ(w.at(1, 2).count(), 1u);
        WFC_CHECK(w.at(1, 2).test(7));
        // Other cells unchanged.
        WFC_CHECK_EQ(w.at(0, 1).count(), 10u);
        WFC_CHECK_EQ(w.at(1, 0).count(), 10u);
    }

    // === Cells are independent (no aliasing) ===
    {
        Wave w(4, 4, 16);
        // Modify each cell uniquely.
        for (int c = 0; c < w.num_cells(); ++c)
            w.at(c).set_only(static_cast<std::size_t>(c % 16));
        for (int c = 0; c < w.num_cells(); ++c) {
            WFC_CHECK_EQ(w.at(c).count(), 1u);
            WFC_CHECK(w.at(c).test(static_cast<std::size_t>(c % 16)));
        }
    }

    // === row(cell) / col(cell) consistent with at(r, c) ===
    {
        Wave w(5, 7, 4);
        for (int r = 0; r < 5; ++r) {
            for (int c = 0; c < 7; ++c) {
                int cell = r * 7 + c;
                WFC_CHECK_EQ(w.row(cell), r);
                WFC_CHECK_EQ(w.col(cell), c);
            }
        }
    }

    // === torus_idx wraps in both directions ===
    {
        Wave w(3, 4, 4);
        // Identity within bounds.
        WFC_CHECK_EQ(w.torus_idx(0, 0), 0);
        WFC_CHECK_EQ(w.torus_idx(2, 3), 11);
        // Negative wraps to positive.
        WFC_CHECK_EQ(w.torus_idx(-1, 0), w.torus_idx(2, 0));
        WFC_CHECK_EQ(w.torus_idx(0, -1), w.torus_idx(0, 3));
        WFC_CHECK_EQ(w.torus_idx(-1, -1), w.torus_idx(2, 3));
        // Beyond rows/cols wraps back.
        WFC_CHECK_EQ(w.torus_idx(3, 0), w.torus_idx(0, 0));
        WFC_CHECK_EQ(w.torus_idx(0, 4), w.torus_idx(0, 0));
        WFC_CHECK_EQ(w.torus_idx(3, 4), w.torus_idx(0, 0));
    }

    // === Single-word per cell (num_tiles <= 64) ===
    {
        Wave w(2, 2, 11); // README example
        for (int c = 0; c < w.num_cells(); ++c) {
            WFC_CHECK_EQ(w.at(c).count(), 11u);
            WFC_CHECK_EQ(w.at(c).size(), 11u);
        }
    }

    // === Multi-word per cell (num_tiles > 64) ===
    {
        Wave w(3, 3, 200);
        for (int c = 0; c < w.num_cells(); ++c) {
            WFC_CHECK_EQ(w.at(c).count(), 200u);
            WFC_CHECK_EQ(w.at(c).size(), 200u);
        }
    }

    // === All-active count matches num_cells initially ===
    {
        Wave w(4, 5, 6);
        WFC_CHECK_EQ(active_cells(w), 20);
        // Collapse one cell to zero candidates → it's no longer active.
        w.at(0, 0).reset();
        WFC_CHECK_EQ(active_cells(w), 19);
    }

    // === 1×1 wave ===
    {
        Wave w(1, 1, 4);
        WFC_CHECK_EQ(w.num_cells(), 1);
        WFC_CHECK_EQ(w.at(0).count(), 4u);
        // Toroidal index for a 1×1 wave: torus_idx is single-step branch
        // correction, only valid for offsets within ±rows_/±cols_. For 1×1
        // it stays correct only for r,c ∈ {0, ±1}; we test those.
        WFC_CHECK_EQ(w.torus_idx(0, 0), 0);
        WFC_CHECK_EQ(w.torus_idx(1, 0), 0);
        WFC_CHECK_EQ(w.torus_idx(0, 1), 0);
        WFC_CHECK_EQ(w.torus_idx(-1, 0), 0);
        WFC_CHECK_EQ(w.torus_idx(0, -1), 0);
    }

    // === Default-constructed wave is empty but valid ===
    {
        Wave w;
        WFC_CHECK_EQ(w.rows(), 0);
        WFC_CHECK_EQ(w.cols(), 0);
        WFC_CHECK_EQ(w.num_cells(), 0);
        WFC_CHECK_EQ(w.num_tiles(), 0);
    }

    // === At various tile counts, full bitset count = num_tiles ===
    {
        for (int nt : {1, 2, 11, 33, 63, 64, 65, 100, 128, 200, 500}) {
            Wave w(2, 2, nt);
            for (int c = 0; c < w.num_cells(); ++c) {
                WFC_CHECK_EQ(w.at(c).count(), static_cast<std::size_t>(nt));
                WFC_CHECK_EQ(w.at(c).size(), static_cast<std::size_t>(nt));
                WFC_CHECK(w.at(c).any());
            }
        }
    }

    // === BFS-style propagation pattern: clear bits and re-check ===
    {
        Wave w(3, 3, 8);
        // Constrain center cell to a single tile, then verify.
        w.at(1, 1).set_only(3);
        WFC_CHECK_EQ(w.at(1, 1).count(), 1u);
        WFC_CHECK(w.at(1, 1).test(3));
        // Constrain neighbours partially.
        for (int r = 0; r < 3; ++r) {
            for (int c = 0; c < 3; ++c) {
                if (r == 1 && c == 1) continue;
                BitsetView v = w.at(r, c);
                // Keep only odd-numbered tiles.
                Bitset mask(8);
                for (int t = 1; t < 8; t += 2) mask.set(t);
                v.and_with(mask);
            }
        }
        // Verify center untouched, neighbours have 4 tiles each.
        WFC_CHECK_EQ(w.at(1, 1).count(), 1u);
        for (int r = 0; r < 3; ++r)
            for (int c = 0; c < 3; ++c)
                if (r != 1 || c != 1)
                    WFC_CHECK_EQ(w.at(r, c).count(), 4u);
    }

    // === Sequential indexing: at(cell) for cell = r*cols+c is at(r, c) ===
    {
        Wave w(4, 6, 12);
        for (int cell = 0; cell < w.num_cells(); ++cell) {
            int r = w.row(cell), c = w.col(cell);
            WFC_CHECK_EQ(w.at(cell).words(), w.at(r, c).words());
        }
    }

    // === Const access (ConstBitsetView) ===
    {
        Wave w(2, 2, 8);
        w.at(0, 0).set_only(3);
        const Wave& cw = w;
        ConstBitsetView v = cw.at(0, 0);
        WFC_CHECK_EQ(v.count(), 1u);
        WFC_CHECK(v.test(3));
    }

    // === torus_idx wrap consistency with multi-period offsets
    //     torus_idx is a single-step branch correction; offsets up to
    //     ±(N-1) are guaranteed within ±rows. We test the boundary cases. ===
    {
        Wave w(5, 5, 4);
        // Within ±1 step: should give expected wraps.
        for (int r = 0; r < 5; ++r) {
            for (int c = 0; c < 5; ++c) {
                // (r-1, c) wraps to (4, c) when r=0
                int got = w.torus_idx(r - 1, c);
                int expected_r = (r - 1 + 5) % 5;
                WFC_CHECK_EQ(got, expected_r * 5 + c);
            }
        }
    }

    // === Large wave: stress test storage layout ===
    {
        Wave w(64, 64, 11);
        WFC_CHECK_EQ(w.num_cells(), 4096);
        // Set cell (i, j) to tile i*j mod 11.
        for (int i = 0; i < 64; ++i)
            for (int j = 0; j < 64; ++j)
                w.at(i, j).set_only(static_cast<std::size_t>((i * j) % 11));
        // Verify.
        for (int i = 0; i < 64; ++i)
            for (int j = 0; j < 64; ++j) {
                WFC_CHECK_EQ(w.at(i, j).count(), 1u);
                WFC_CHECK(w.at(i, j).test(static_cast<std::size_t>((i * j) % 11)));
            }
    }

    return wfc_test::report();
}
