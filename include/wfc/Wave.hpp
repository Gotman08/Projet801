#pragma once

#include "wfc/Bitset.hpp"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace wfc {

// Superposition state of the output grid.
//
// Each cell (r, c) stores the bitset of tile ids that could still be placed
// with their top-left corner at this cell. Storage is one contiguous
// `uint64_t` buffer for all cells, indexed by row-major cell × words_per_cell.
// Cell access goes through BitsetView so the hot loops never dereference a
// std::vector — better cache behaviour than per-cell vectors.
//
// NUMA first-touch policy: the buffer is `resize()`d uninitialised, then
// filled in parallel. With OMP_PROC_BIND=close on a NUMA system, each thread
// touches only the slice of words it will subsequently access during
// propagation, so the OS allocates physical pages on the local NUMA node.
// Without this, thread 0 would first-touch everything and 7/8 of accesses
// on a 192-core EPYC would cross NUMA boundaries.
class Wave {
public:
    Wave() = default;
    Wave(int rows, int cols, int num_tiles)
        : rows_(rows), cols_(cols), num_tiles_(num_tiles),
          words_per_cell_(detail::words_for(static_cast<std::size_t>(num_tiles))),
          words_(static_cast<std::size_t>(rows) * cols * words_per_cell_) {
        const std::size_t total_cells = static_cast<std::size_t>(rows) * cols;
        const std::size_t tail = static_cast<std::size_t>(num_tiles) & 63;
        const std::uint64_t tail_mask = tail ? ((1ULL << tail) - 1ULL) : ~0ULL;

        // Parallel first-touch: each thread initialises the cells it will
        // most likely process, mapping physical pages to its NUMA node.
        // The pragma is silently ignored when the core lib is built without
        // OpenMP (header-only path), preserving correctness.
        #if defined(_OPENMP)
        #pragma omp parallel for schedule(static)
        #endif
        for (std::ptrdiff_t c = 0; c < static_cast<std::ptrdiff_t>(total_cells); ++c) {
            const std::size_t base = static_cast<std::size_t>(c) * words_per_cell_;
            for (std::size_t w = 0; w + 1 < words_per_cell_; ++w) {
                words_[base + w] = ~0ULL;
            }
            if (words_per_cell_ > 0) {
                words_[base + (words_per_cell_ - 1)] = tail_mask;
            }
        }
    }

    int rows() const noexcept { return rows_; }
    int cols() const noexcept { return cols_; }
    int num_cells() const noexcept { return rows_ * cols_; }
    int num_tiles() const noexcept { return num_tiles_; }

    // Direct access to the flat underlying buffer. Used by GPU-portable
    // backends (Kokkos) that need to wrap or deep-copy the storage into a
    // device-accessible View. Layout is row-major: offset of cell (r, c)
    // is (r * cols + c) * words_per_cell.
    std::size_t words_per_cell() const noexcept { return words_per_cell_; }
    std::size_t total_words() const noexcept { return words_.size(); }
    std::uint64_t* raw_words() noexcept { return words_.data(); }
    const std::uint64_t* raw_words() const noexcept { return words_.data(); }

    BitsetView at(int r, int c) noexcept {
        return {cell_words(idx(r, c)), static_cast<std::size_t>(num_tiles_)};
    }
    ConstBitsetView at(int r, int c) const noexcept {
        return {cell_words(idx(r, c)), static_cast<std::size_t>(num_tiles_)};
    }
    BitsetView at(int cell) noexcept {
        return {cell_words(cell), static_cast<std::size_t>(num_tiles_)};
    }
    ConstBitsetView at(int cell) const noexcept {
        return {cell_words(cell), static_cast<std::size_t>(num_tiles_)};
    }

    int row(int cell) const noexcept { return cell / cols_; }
    int col(int cell) const noexcept { return cell % cols_; }

    // Toroidal cell index. Branch-only correction (no modulo): callers in
    // WFC pass offsets bounded by ±(N-1), which is always smaller than
    // rows_/cols_ in any practical configuration.
    int torus_idx(int r, int c) const noexcept {
        if (r < 0)        r += rows_;
        else if (r >= rows_) r -= rows_;
        if (c < 0)        c += cols_;
        else if (c >= cols_) c -= cols_;
        return r * cols_ + c;
    }

private:
    int idx(int r, int c) const noexcept { return r * cols_ + c; }

    std::uint64_t* cell_words(int cell) noexcept {
        return words_.data() + static_cast<std::size_t>(cell) * words_per_cell_;
    }
    const std::uint64_t* cell_words(int cell) const noexcept {
        return words_.data() + static_cast<std::size_t>(cell) * words_per_cell_;
    }

    int rows_ = 0;
    int cols_ = 0;
    int num_tiles_ = 0;
    std::size_t words_per_cell_ = 0;
    // Flat row-major buffer: words_[(r*cols + c) * words_per_cell + w]
    std::vector<std::uint64_t> words_;
};

} // namespace wfc
