#include "wfc/TileSet.hpp"
#include <stdexcept>
#include <unordered_map>

namespace wfc {

namespace {

// Generate the requested D4 variants of `t` and call `emit` on each
// distinct one. `symmetries` controls how many variants are produced :
//   1 = identity only (no-op, hot path)
//   2 = identity + 180° rotation
//   4 = identity + 90° + 180° + 270° rotations
//   8 = the four rotations and their horizontal reflections
//
// Variants are deduped against `seen` by content hash so a tile that
// maps to itself under a given operation does not double-count its
// frequency. The cost of expansion is paid once at extraction time
// (a few microseconds even on the largest realistic samples) and
// never appears in the solver hot path.
template <typename Emit>
void emit_variants(const Tile& t,
                   int symmetries,
                   std::unordered_map<Tile, int, TileHash>& seen,
                   Emit emit) {
    auto try_emit = [&](Tile&& v) {
        if (seen.find(v) != seen.end()) return;
        seen.emplace(v, 0);
        emit(std::move(v));
    };

    // Identity is always emitted (and required for symmetries=1, which
    // matches the legacy from_sample behavior bit-for-bit).
    try_emit(Tile{t});
    if (symmetries == 1) return;

    Tile r90  = t.rotated_90();
    Tile r180 = r90.rotated_90();
    Tile r270 = r180.rotated_90();

    if (symmetries >= 2) try_emit(std::move(r180));
    if (symmetries >= 4) {
        try_emit(std::move(r90));
        try_emit(std::move(r270));
    }
    if (symmetries >= 8) {
        // Reflections : compute on demand from the four rotations.
        try_emit(t.reflected_horizontal());
        try_emit(t.rotated_90().reflected_horizontal());
        try_emit(t.rotated_90().rotated_90().reflected_horizontal());
        try_emit(t.rotated_90().rotated_90().rotated_90().reflected_horizontal());
    }
}

} // namespace

TileSet TileSet::from_sample(const Grid& sample, int N, int symmetries) {
    if (symmetries != 1 && symmetries != 2 && symmetries != 4 && symmetries != 8) {
        throw std::invalid_argument(
            "TileSet::from_sample : symmetries must be 1, 2, 4, or 8");
    }

    TileSet set;
    set.N_ = N;

    // Toroidal sampling : each (r0, c0) origin yields one tile by reading
    // N x N values with wrap-around. This guarantees every value of S is
    // covered uniformly and that the tile distribution is shift-invariant.
    std::unordered_map<Tile, int, TileHash> index;
    Value max_v = 0;
    for (int r0 = 0; r0 < sample.rows(); ++r0) {
        for (int c0 = 0; c0 < sample.cols(); ++c0) {
            Tile t;
            t.N = N;
            t.data.resize(static_cast<std::size_t>(N) * N);
            for (int i = 0; i < N; ++i) {
                for (int j = 0; j < N; ++j) {
                    Value v = sample.at_torus(r0 + i, c0 + j);
                    t.data[static_cast<std::size_t>(i) * N + j] = v;
                    if (v > max_v) max_v = v;
                }
            }

            if (symmetries == 1) {
                // Hot path : no variant generation, no per-pattern dedup
                // map beyond `index`. Identical to the legacy code.
                auto [it, inserted] = index.try_emplace(
                    std::move(t), static_cast<int>(set.tiles_.size()));
                if (inserted) {
                    set.tiles_.push_back(it->first);
                    set.freq_.push_back(1);
                } else {
                    ++set.freq_[it->second];
                }
            } else {
                // Symmetry expansion : every D4 variant of this pattern
                // shares its frequency contribution. We dedup against
                // the per-source `seen` so a self-symmetric pattern
                // does not contribute its frequency twice.
                std::unordered_map<Tile, int, TileHash> seen;
                emit_variants(t, symmetries, seen, [&](Tile v) {
                    auto [it, inserted] = index.try_emplace(
                        std::move(v), static_cast<int>(set.tiles_.size()));
                    if (inserted) {
                        set.tiles_.push_back(it->first);
                        set.freq_.push_back(1);
                    } else {
                        ++set.freq_[it->second];
                    }
                });
            }
        }
    }
    set.max_value_ = max_v;
    return set;
}

} // namespace wfc
