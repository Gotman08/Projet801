#pragma once

#include <cstdint>
#include <stdexcept>
#include <vector>
#include <cassert>

namespace wfc {

using Value = std::uint8_t;

class Grid {
public:
    Grid() = default;
    Grid(int rows, int cols, Value fill = 0)
        : rows_(rows), cols_(cols), data_(static_cast<std::size_t>(rows) * cols, fill) {}

    int rows() const { return rows_; }
    int cols() const { return cols_; }
    std::size_t size() const { return data_.size(); }

    Value& at(int r, int c) {
        assert(r >= 0 && r < rows_ && c >= 0 && c < cols_);
        return data_[static_cast<std::size_t>(r) * cols_ + c];
    }
    Value at(int r, int c) const {
        assert(r >= 0 && r < rows_ && c >= 0 && c < cols_);
        return data_[static_cast<std::size_t>(r) * cols_ + c];
    }

    // Toroidal access: wraps around the borders.
    Value at_torus(int r, int c) const {
        int rr = ((r % rows_) + rows_) % rows_;
        int cc = ((c % cols_) + cols_) % cols_;
        return data_[static_cast<std::size_t>(rr) * cols_ + cc];
    }

    // Read-only view of the row-major buffer. Mutation goes through `at()`
    // so the rows_/cols_ invariant cannot be broken from outside.
    const std::vector<Value>& data() const { return data_; }

    // Fill the grid from a row-major buffer of `rows() * cols()` ints.
    // Throws std::invalid_argument on size mismatch or out-of-range value.
    void fill_row_major(const int* values, std::size_t count) {
        if (count != data_.size())
            throw std::invalid_argument("Grid::fill_row_major: size mismatch");
        for (std::size_t i = 0; i < count; ++i) {
            const int v = values[i];
            if (v < 0 || v > 255)
                throw std::invalid_argument("Grid::fill_row_major: value out of range");
            data_[i] = static_cast<Value>(v);
        }
    }

private:
    int rows_ = 0;
    int cols_ = 0;
    std::vector<Value> data_;
};

} // namespace wfc
