#pragma once

#include "wfc/Grid.hpp"
#include <array>
#include <string>
#include <vector>

namespace wfc {

// Whitespace-separated integer grid: optional "rows cols" header,
// then row-major values separated by spaces or newlines.
Grid read_grid_txt(const std::string& path);
void write_grid_txt(const std::string& path, const Grid& grid);

// Default RGB palette indexed by Value. Up to 256 entries; uses a fixed
// human-readable scheme (0=black, 1=white, then qualitative colors).
std::array<std::uint8_t, 3> default_color(Value v);

void write_grid_ppm(const std::string& path, const Grid& grid, int scale = 1);
void write_grid_png(const std::string& path, const Grid& grid, int scale = 1);

} // namespace wfc
