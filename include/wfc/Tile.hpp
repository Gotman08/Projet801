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
