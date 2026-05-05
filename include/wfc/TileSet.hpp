#pragma once

#include "wfc/Grid.hpp"
#include "wfc/Tile.hpp"
#include <cstdint>
#include <vector>

namespace wfc {

// Catalogue of unique N x N tiles extracted from a sample grid, with
// occurrence counts used as sampling weights during collapse.
class TileSet {
public:
    // Extract every N x N pattern from `sample` and aggregate frequencies.
    // Sampling is toroidal (wraps around), so each pixel appears in N*N tiles.
    //
    // `symmetries` enables D4 expansion of the tile catalogue :
    //   1 = no expansion (default, identical to legacy behavior)
    //   2 = 2 rotations (0°, 180°)
    //   4 = 4 rotations (0°, 90°, 180°, 270°)
    //   8 = 4 rotations + 4 reflections (full dihedral group D4)
    // Each variant inherits its frequency from the source pattern.
    // Useful for samples with a preferred orientation that you want
    // applied uniformly in the output (e.g. paths that may turn left
    // or right). Ignored for tiles that map to themselves under the
    // chosen symmetry (deduped via the same content hash).
    static TileSet from_sample(const Grid& sample, int N, int symmetries = 1);

    int N() const noexcept { return N_; }
    int size() const noexcept { return static_cast<int>(tiles_.size()); }
    const Tile& tile(int id) const noexcept { return tiles_[id]; }
    std::uint32_t frequency(int id) const noexcept { return freq_[id]; }
    const std::vector<Tile>& tiles() const noexcept { return tiles_; }
    const std::vector<std::uint32_t>& frequencies() const noexcept { return freq_; }

    // Highest distinct value across all tiles (e.g., 1 for binary, K-1 otherwise).
    Value max_value() const noexcept { return max_value_; }

private:
    int N_ = 0;
    Value max_value_ = 0;
    std::vector<Tile> tiles_;
    std::vector<std::uint32_t> freq_;
};

} // namespace wfc
