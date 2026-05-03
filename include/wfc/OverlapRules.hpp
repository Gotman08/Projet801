#pragma once

#include "wfc/Bitset.hpp"
#include "wfc/TileSet.hpp"
#include <vector>

namespace wfc {

// Precomputed compatibility tables between tile pairs.
//
// For an N x N tile model, the relevant offsets between two tile placements
// are (dx, dy) with -(N-1) <= dx, dy <= (N-1). Two tiles t1 (placed at the
// origin) and t2 (placed at (dx, dy)) are compatible if their values agree
// on the overlap region.
//
// Coordinate convention (matches Wave::torus_idx):
//
//     +---------> dx (column shift)
//     |   . . . . .
//     |   . t1 t1 .
//     |   . t1 t1 .            t2 sits at (dx, dy) from t1's origin.
//     |       t2 t2            For dx=1, dy=1 above, the overlap is
//     |       t2 t2            the bottom-right cell of t1 (= top-left
//     v                        of t2) — both must hold the same value.
//     dy (row shift)
//
// allowed(t, dx, dy) returns the bitset of tile ids that are valid choices
// for t2 when placed at (dx, dy) from t. The compatibility relation is
// symmetric: t2 ∈ allowed(t1, dx, dy) ⇔ t1 ∈ allowed(t2, -dx, -dy).
class OverlapRules {
public:
    static OverlapRules build(const TileSet& tiles);

    int N() const { return N_; }
    int num_tiles() const { return num_tiles_; }
    int offsets() const { return offsets_; }
    int stride() const { return stride_; } // 2N - 1

    // Look up the compatibility bitset for tile `t` at offset (dx, dy).
    const Bitset& allowed(int t, int dx, int dy) const {
        return rules_[t * offsets_ + offset_index(dx, dy)];
    }

    int offset_index(int dx, int dy) const {
        return (dx + N_ - 1) * stride_ + (dy + N_ - 1);
    }

private:
    int N_ = 0;
    int num_tiles_ = 0;
    int stride_ = 0;   // 2*N - 1, cached at build time
    int offsets_ = 0;  // (2*N - 1)^2, cached at build time
    // Flat row-major: rules_[t * offsets_ + offset_index]
    std::vector<Bitset> rules_;
};

} // namespace wfc
