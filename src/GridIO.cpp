#include "wfc/GridIO.hpp"

#include <cctype>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <vector>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

namespace wfc {

Grid read_grid_txt(const std::string& path) {
    std::ifstream in(path);
    if (!in) throw std::runtime_error("cannot open " + path);

    std::vector<std::vector<int>> rows;
    std::string line;
    while (std::getline(in, line)) {
        // Skip lines starting with '#' or empty lines.
        std::size_t i = 0;
        while (i < line.size() && std::isspace(static_cast<unsigned char>(line[i]))) ++i;
        if (i >= line.size() || line[i] == '#') continue;

        std::istringstream ss(line);
        std::vector<int> row;
        int v;
        while (ss >> v) row.push_back(v);
        if (!row.empty()) rows.push_back(std::move(row));
    }
    if (rows.empty()) throw std::runtime_error("empty grid file: " + path);

    int cols = static_cast<int>(rows[0].size());
    for (const auto& r : rows) {
        if (static_cast<int>(r.size()) != cols)
            throw std::runtime_error("inconsistent row width in " + path);
    }
    Grid g(static_cast<int>(rows.size()), cols);
    for (int r = 0; r < g.rows(); ++r) {
        for (int c = 0; c < g.cols(); ++c) {
            int v = rows[r][c];
            if (v < 0 || v > 255)
                throw std::runtime_error("value out of range in " + path);
            g.at(r, c) = static_cast<Value>(v);
        }
    }
    return g;
}

void write_grid_txt(const std::string& path, const Grid& grid) {
    std::ofstream out(path);
    if (!out) throw std::runtime_error("cannot write " + path);
    for (int r = 0; r < grid.rows(); ++r) {
        for (int c = 0; c < grid.cols(); ++c) {
            if (c) out << ' ';
            out << static_cast<int>(grid.at(r, c));
        }
        out << '\n';
    }
}

namespace {

// 16-color qualitative palette. Index 0 reserved for "background black",
// index 1 for "white". Indices >=2 cycle through visually distinct colors.
constexpr std::array<std::array<std::uint8_t, 3>, 16> kPalette = {{
    {0, 0, 0},        // 0  black
    {255, 255, 255},  // 1  white
    {31, 119, 180},   // 2  water blue
    {255, 127, 14},   // 3  sand orange
    {44, 160, 44},    // 4  grass green
    {148, 103, 189},  // 5  rock purple
    {214, 39, 40},    // 6  red
    {140, 86, 75},    // 7  brown
    {227, 119, 194},  // 8  pink
    {127, 127, 127},  // 9  grey
    {188, 189, 34},   // 10 olive
    {23, 190, 207},   // 11 cyan
    {174, 199, 232},  // 12 light blue
    {255, 187, 120},  // 13 light orange
    {152, 223, 138},  // 14 light green
    {197, 176, 213},  // 15 light purple
}};

} // namespace

std::array<std::uint8_t, 3> default_color(Value v) {
    return kPalette[v % kPalette.size()];
}

static std::vector<std::uint8_t> render_rgb(const Grid& grid, int scale) {
    if (scale < 1) scale = 1;
    int W = grid.cols() * scale;
    int H = grid.rows() * scale;
    std::vector<std::uint8_t> rgb(static_cast<std::size_t>(W) * H * 3);
    for (int r = 0; r < grid.rows(); ++r) {
        for (int c = 0; c < grid.cols(); ++c) {
            auto color = default_color(grid.at(r, c));
            for (int dr = 0; dr < scale; ++dr) {
                int row = r * scale + dr;
                for (int dc = 0; dc < scale; ++dc) {
                    int col = c * scale + dc;
                    std::size_t k = (static_cast<std::size_t>(row) * W + col) * 3;
                    rgb[k + 0] = color[0];
                    rgb[k + 1] = color[1];
                    rgb[k + 2] = color[2];
                }
            }
        }
    }
    return rgb;
}

void write_grid_ppm(const std::string& path, const Grid& grid, int scale) {
    if (scale < 1) scale = 1;
    int W = grid.cols() * scale;
    int H = grid.rows() * scale;
    std::ofstream out(path, std::ios::binary);
    if (!out) throw std::runtime_error("cannot write " + path);
    out << "P6\n" << W << ' ' << H << "\n255\n";
    auto rgb = render_rgb(grid, scale);
    out.write(reinterpret_cast<const char*>(rgb.data()),
              static_cast<std::streamsize>(rgb.size()));
}

void write_grid_png(const std::string& path, const Grid& grid, int scale) {
    if (scale < 1) scale = 1;
    int W = grid.cols() * scale;
    int H = grid.rows() * scale;
    auto rgb = render_rgb(grid, scale);
    if (!stbi_write_png(path.c_str(), W, H, 3, rgb.data(), W * 3))
        throw std::runtime_error("stbi_write_png failed for " + path);
}

} // namespace wfc
