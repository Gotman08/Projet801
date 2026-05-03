#include "wfc/OverlapRules.hpp"

#if defined(WFC_CORE_HAS_OMP)
#include <omp.h>
#endif

namespace wfc {

namespace {

// Two tiles t1 (placed at origin) and t2 (placed at (dx, dy)) are compatible
// when their values agree on the overlap region.
//
// Coordinate convention: dx is horizontal (column shift), dy is vertical
// (row shift). The overlap of an N x N tile at (0, 0) and one at (dx, dy)
// is the rectangle of cells covered by both.
bool compatible(const Tile& t1, const Tile& t2, int N, int dx, int dy) {
    int x_min = std::max(0, dx);
    int x_max = std::min(N, N + dx);
    int y_min = std::max(0, dy);
    int y_max = std::min(N, N + dy);
    for (int y = y_min; y < y_max; ++y) {
        for (int x = x_min; x < x_max; ++x) {
            // (x, y) lies in t1 directly, and in t2 at (x - dx, y - dy).
            Value v1 = t1.data[static_cast<std::size_t>(y) * N + x];
            Value v2 = t2.data[static_cast<std::size_t>(y - dy) * N + (x - dx)];
            if (v1 != v2) return false;
        }
    }
    return true;
}

} // namespace

OverlapRules OverlapRules::build(const TileSet& tiles) {
    OverlapRules rules;
    rules.N_ = tiles.N();
    rules.num_tiles_ = tiles.size();
    rules.stride_ = 2 * rules.N_ - 1;
    rules.offsets_ = rules.stride_ * rules.stride_;
    rules.rules_.assign(
        static_cast<std::size_t>(rules.num_tiles_) * rules.offsets_,
        Bitset(rules.num_tiles_));

    // Each tile t1 writes a disjoint range of `rules.rules_`, so the outer
    // loop is data-parallel. The pragma is ignored when OpenMP is disabled.
    #if defined(WFC_CORE_HAS_OMP)
    #pragma omp parallel for schedule(dynamic)
    #endif
    for (int t1 = 0; t1 < rules.num_tiles_; ++t1) {
        for (int dy = -(rules.N_ - 1); dy <= rules.N_ - 1; ++dy) {
            for (int dx = -(rules.N_ - 1); dx <= rules.N_ - 1; ++dx) {
                Bitset& b = rules.rules_[t1 * rules.offsets_
                                         + rules.offset_index(dx, dy)];
                for (int t2 = 0; t2 < rules.num_tiles_; ++t2) {
                    if (compatible(tiles.tile(t1), tiles.tile(t2),
                                   rules.N_, dx, dy))
                        b.set(t2);
                }
            }
        }
    }
    return rules;
}

} // namespace wfc
