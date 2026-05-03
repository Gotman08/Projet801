#include "wfc/TileSet.hpp"
#include <unordered_map>

namespace wfc {

TileSet TileSet::from_sample(const Grid& sample, int N) {
    TileSet set;
    set.N_ = N;

    // Toroidal sampling: each (r0, c0) origin yields one tile by reading
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
            auto [it, inserted] = index.try_emplace(std::move(t), static_cast<int>(set.tiles_.size()));
            if (inserted) {
                set.tiles_.push_back(it->first);
                set.freq_.push_back(1);
            } else {
                ++set.freq_[it->second];
            }
        }
    }
    set.max_value_ = max_v;
    return set;
}

} // namespace wfc
