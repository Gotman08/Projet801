// Round-trip and image-format header checks for GridIO.
//
//  - read_grid_txt(write_grid_txt(g)) == g
//  - PPM output starts with the P6 magic and reports the right dimensions
//  - PNG output starts with the well-known signature bytes

#include "test_helpers.hpp"
#include "wfc/Grid.hpp"
#include "wfc/GridIO.hpp"

#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

using namespace wfc;

namespace {

std::string tmp_path(const char* suffix) {
    // std::filesystem would also work but adds an include for one call.
    static int counter = 0;
    return "/tmp/wfc_test_" + std::to_string(++counter) + suffix;
}

std::vector<unsigned char> read_bytes(const std::string& path, std::size_t n) {
    std::ifstream in(path, std::ios::binary);
    std::vector<unsigned char> buf(n);
    in.read(reinterpret_cast<char*>(buf.data()), static_cast<std::streamsize>(n));
    buf.resize(static_cast<std::size_t>(in.gcount()));
    return buf;
}

bool grids_equal(const Grid& a, const Grid& b) {
    if (a.rows() != b.rows() || a.cols() != b.cols()) return false;
    for (int r = 0; r < a.rows(); ++r)
        for (int c = 0; c < a.cols(); ++c)
            if (a.at(r, c) != b.at(r, c)) return false;
    return true;
}

} // namespace

int main() {
    // Round-trip: write a small grid, read it back, compare.
    Grid g(3, 4);
    int data[] = {0, 1, 2, 3,
                  4, 5, 6, 7,
                  8, 9, 10, 11};
    g.fill_row_major(data, 12);

    const std::string txt = tmp_path(".txt");
    write_grid_txt(txt, g);
    Grid read_back = read_grid_txt(txt);
    WFC_CHECK(grids_equal(g, read_back));
    std::remove(txt.c_str());

    // PPM header: "P6\n<W> <H>\n255\n" where W = cols * scale, H = rows * scale.
    const std::string ppm = tmp_path(".ppm");
    write_grid_ppm(ppm, g, /*scale=*/2);
    auto ppm_bytes = read_bytes(ppm, 32);
    WFC_CHECK(ppm_bytes.size() >= 3);
    WFC_CHECK_EQ(ppm_bytes[0], static_cast<unsigned char>('P'));
    WFC_CHECK_EQ(ppm_bytes[1], static_cast<unsigned char>('6'));
    std::string header(ppm_bytes.begin(), ppm_bytes.end());
    WFC_CHECK(header.find("8 6") != std::string::npos);   // 4 cols * 2, 3 rows * 2
    WFC_CHECK(header.find("255") != std::string::npos);
    std::remove(ppm.c_str());

    // PNG signature: 89 50 4E 47 0D 0A 1A 0A
    const std::string png = tmp_path(".png");
    write_grid_png(png, g, /*scale=*/2);
    auto png_bytes = read_bytes(png, 8);
    static const unsigned char kPngSig[8] = {0x89,0x50,0x4E,0x47,0x0D,0x0A,0x1A,0x0A};
    WFC_CHECK_EQ(png_bytes.size(), 8u);
    for (int i = 0; i < 8; ++i) WFC_CHECK_EQ(png_bytes[i], kPngSig[i]);
    std::remove(png.c_str());

    return wfc_test::report();
}
