#pragma once

#include "wfc/Grid.hpp"
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

namespace wfc {

// A square N x N pattern of values extracted from a sample grid.
// Stored row-major in a flat vector. Hashable for deduplication.
//
// Kept as a `struct` (POD-like) because Tile is constructed and compared
// by value in many places (extraction, hashing, output verification).
// The public `data` member is intentional, callers fill it once during
// construction; afterwards, it's read via `at()` from the rest of the code.
struct Tile {
    int N = 0;
    std::vector<Value> data;

    Value at(int r, int c) const {
        assert(r >= 0 && r < N && c >= 0 && c < N);
        return data[static_cast<std::size_t>(r) * N + c];
    }

    bool operator==(const Tile& other) const {
        return N == other.N && data == other.data;
    }

    // Returns the tile rotated 90 degrees clockwise. New (r, c) reads
    // from the original (N - 1 - c, r). Used by TileSet::from_sample
    // when symmetry expansion is enabled (--symmetries 4 or 8).
    Tile rotated_90() const {
        Tile t;
        t.N = N;
        t.data.resize(static_cast<std::size_t>(N) * N);
        for (int r = 0; r < N; ++r) {
            for (int c = 0; c < N; ++c) {
                t.data[static_cast<std::size_t>(r) * N + c] =
                    at(N - 1 - c, r);
            }
        }
        return t;
    }

    // Returns the tile reflected horizontally (left-right flip).
    // Combined with the four rotations, this generates the full
    // dihedral group D4 of 8 symmetries (--symmetries 8).
    Tile reflected_horizontal() const {
        Tile t;
        t.N = N;
        t.data.resize(static_cast<std::size_t>(N) * N);
        for (int r = 0; r < N; ++r) {
            for (int c = 0; c < N; ++c) {
                t.data[static_cast<std::size_t>(r) * N + c] =
                    at(r, N - 1 - c);
            }
        }
        return t;
    }
};

struct TileHash {
    std::size_t operator()(const Tile& t) const noexcept {
        // FNV-1a 64-bit hash over the tile bytes.
        std::size_t h = 1469598103934665603ULL;
        for (Value v : t.data) {
            h ^= static_cast<std::size_t>(v);
            h *= 1099511628211ULL;
        }
        h ^= static_cast<std::size_t>(t.N);
        return h;
    }
};

} // namespace wfc
