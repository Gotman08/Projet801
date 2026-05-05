// Round-trip and image-format header checks for GridIO. Cross-platform:
// uses std::filesystem::temp_directory_path() rather than hard-coded /tmp.

#include "test_helpers.hpp"
#include "wfc/Grid.hpp"
#include "wfc/GridIO.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace wfc;

namespace {

std::string tmp_path(const char* suffix) {
    static int counter = 0;
    auto base = std::filesystem::temp_directory_path();
    auto p = base / ("wfc_test_" + std::to_string(++counter) + suffix);
    return p.string();
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

template <typename F>
bool throws_runtime(F&& f) {
    try { f(); }
    catch (const std::runtime_error&) { return true; }
    catch (...) { return false; }
    return false;
}

void write_text(const std::string& path, const std::string& content) {
    std::ofstream out(path);
    out << content;
}

} // namespace

int main() {
    // === Round-trip txt: write a small grid, read it back, compare ===
    {
        Grid g(3, 4);
        int data[] = {0, 1, 2, 3,
                      4, 5, 6, 7,
                      8, 9, 10, 11};
        g.fill_row_major(data, 12);

        const std::string p = tmp_path(".txt");
        write_grid_txt(p, g);
        Grid read_back = read_grid_txt(p);
        WFC_CHECK(grids_equal(g, read_back));
        std::remove(p.c_str());
    }

    // === Round-trip multivalue ===
    {
        Grid g(3, 3);
        int data[] = {0, 50, 100,
                      150, 200, 250,
                      255, 0, 7};
        g.fill_row_major(data, 9);

        const std::string p = tmp_path(".txt");
        write_grid_txt(p, g);
        Grid read_back = read_grid_txt(p);
        WFC_CHECK(grids_equal(g, read_back));
        std::remove(p.c_str());
    }

    // === Round-trip 1×1 ===
    {
        Grid g(1, 1);
        int data[] = {42};
        g.fill_row_major(data, 1);

        const std::string p = tmp_path(".txt");
        write_grid_txt(p, g);
        Grid read_back = read_grid_txt(p);
        WFC_CHECK(grids_equal(g, read_back));
        std::remove(p.c_str());
    }

    // === Comments are skipped ===
    {
        const std::string p = tmp_path(".txt");
        write_text(p,
            "# this is a comment\n"
            "# another comment\n"
            "1 0 1\n"
            "0 1 0\n"
            "1 0 1\n"
            "# trailing comment\n");
        Grid g = read_grid_txt(p);
        WFC_CHECK_EQ(g.rows(), 3);
        WFC_CHECK_EQ(g.cols(), 3);
        WFC_CHECK_EQ(static_cast<int>(g.at(0, 0)), 1);
        WFC_CHECK_EQ(static_cast<int>(g.at(2, 2)), 1);
        std::remove(p.c_str());
    }

    // === Empty lines are skipped ===
    {
        const std::string p = tmp_path(".txt");
        write_text(p,
            "\n"
            "  \n"
            "1 2\n"
            "\n"
            "3 4\n"
            "\n");
        Grid g = read_grid_txt(p);
        WFC_CHECK_EQ(g.rows(), 2);
        WFC_CHECK_EQ(g.cols(), 2);
        WFC_CHECK_EQ(static_cast<int>(g.at(0, 1)), 2);
        WFC_CHECK_EQ(static_cast<int>(g.at(1, 0)), 3);
        std::remove(p.c_str());
    }

    // === Inconsistent row width throws ===
    {
        const std::string p = tmp_path(".txt");
        write_text(p,
            "1 2 3\n"
            "4 5\n"
            "6 7 8\n");
        WFC_CHECK(throws_runtime([&]{ read_grid_txt(p); }));
        std::remove(p.c_str());
    }

    // === Empty file (after stripping comments/whitespace) throws ===
    {
        const std::string p = tmp_path(".txt");
        write_text(p, "# only comments\n# nothing else\n");
        WFC_CHECK(throws_runtime([&]{ read_grid_txt(p); }));
        std::remove(p.c_str());
    }

    // === Truly empty file throws ===
    {
        const std::string p = tmp_path(".txt");
        write_text(p, "");
        WFC_CHECK(throws_runtime([&]{ read_grid_txt(p); }));
        std::remove(p.c_str());
    }

    // === Non-existent file throws ===
    {
        const std::string p = tmp_path(".does_not_exist.txt");
        // Make sure it doesn't exist.
        std::remove(p.c_str());
        WFC_CHECK(throws_runtime([&]{ read_grid_txt(p); }));
    }

    // === Out-of-range value throws ===
    {
        const std::string p = tmp_path(".txt");
        write_text(p, "1 2 999\n");
        WFC_CHECK(throws_runtime([&]{ read_grid_txt(p); }));
        std::remove(p.c_str());
    }

    // === Negative value throws ===
    {
        const std::string p = tmp_path(".txt");
        write_text(p, "1 2\n-1 4\n");
        WFC_CHECK(throws_runtime([&]{ read_grid_txt(p); }));
        std::remove(p.c_str());
    }

    // === Whitespace tolerance: tabs and multiple spaces ===
    {
        const std::string p = tmp_path(".txt");
        write_text(p, "1\t2\t3\n   4    5    6\n");
        Grid g = read_grid_txt(p);
        WFC_CHECK_EQ(g.rows(), 2);
        WFC_CHECK_EQ(g.cols(), 3);
        WFC_CHECK_EQ(static_cast<int>(g.at(0, 1)), 2);
        WFC_CHECK_EQ(static_cast<int>(g.at(1, 2)), 6);
        std::remove(p.c_str());
    }

    // === PPM header: "P6\n<W> <H>\n255\n", W = cols * scale ===
    {
        Grid g(3, 4);
        int data[] = {0, 1, 2, 3,
                      4, 5, 6, 7,
                      8, 9, 10, 11};
        g.fill_row_major(data, 12);

        const std::string p = tmp_path(".ppm");
        write_grid_ppm(p, g, /*scale=*/2);
        auto bytes = read_bytes(p, 32);
        WFC_CHECK(bytes.size() >= 3);
        WFC_CHECK_EQ(bytes[0], static_cast<unsigned char>('P'));
        WFC_CHECK_EQ(bytes[1], static_cast<unsigned char>('6'));
        std::string header(bytes.begin(), bytes.end());
        WFC_CHECK(header.find("8 6") != std::string::npos);   // 4 cols * 2, 3 rows * 2
        WFC_CHECK(header.find("255") != std::string::npos);
        std::remove(p.c_str());
    }

    // === PPM at scale 1 ===
    {
        Grid g(2, 3, 1);
        const std::string p = tmp_path(".ppm");
        write_grid_ppm(p, g, /*scale=*/1);
        auto bytes = read_bytes(p, 32);
        std::string header(bytes.begin(), bytes.end());
        WFC_CHECK(header.find("3 2") != std::string::npos);
        std::remove(p.c_str());
    }

    // === PNG signature + IHDR width/height check ===
    // Magic-bytes-only check would let any random file slip through. We
    // also parse the IHDR chunk (bytes 8..32) to verify the reported
    // image dimensions match cols*scale × rows*scale.
    {
        Grid g(3, 4);
        int data[] = {0, 1, 2, 3,
                      4, 5, 6, 7,
                      8, 9, 10, 11};
        g.fill_row_major(data, 12);

        const std::string p = tmp_path(".png");
        write_grid_png(p, g, /*scale=*/2);

        auto bytes = read_bytes(p, 33);
        // Signature.
        static const unsigned char kPngSig[8] = {0x89,0x50,0x4E,0x47,0x0D,0x0A,0x1A,0x0A};
        WFC_CHECK(bytes.size() >= 33u);
        for (int i = 0; i < 8; ++i) WFC_CHECK_EQ(bytes[i], kPngSig[i]);

        // IHDR chunk layout (PNG spec):
        //   bytes 8..11   = chunk length (always 13 for IHDR)
        //   bytes 12..15  = "IHDR"
        //   bytes 16..19  = width (big-endian uint32)
        //   bytes 20..23  = height (big-endian uint32)
        WFC_CHECK_EQ(bytes[12], static_cast<unsigned char>('I'));
        WFC_CHECK_EQ(bytes[13], static_cast<unsigned char>('H'));
        WFC_CHECK_EQ(bytes[14], static_cast<unsigned char>('D'));
        WFC_CHECK_EQ(bytes[15], static_cast<unsigned char>('R'));
        auto be32 = [&](int off) {
            return (static_cast<unsigned int>(bytes[off]) << 24)
                 | (static_cast<unsigned int>(bytes[off + 1]) << 16)
                 | (static_cast<unsigned int>(bytes[off + 2]) << 8)
                 |  static_cast<unsigned int>(bytes[off + 3]);
        };
        WFC_CHECK_EQ(be32(16), 8u);  // 4 cols * 2 scale = 8
        WFC_CHECK_EQ(be32(20), 6u);  // 3 rows * 2 scale = 6
        std::remove(p.c_str());
    }

    // === PPM: parse the header explicitly (not just substring search) ===
    // The previous version asserted "8 6" appears as a substring, which
    // could match raw pixel data starting with bytes '8',' ','6'. Read
    // the header line by line.
    {
        Grid g(3, 4);
        int data[] = {0, 1, 2, 3,
                      4, 5, 6, 7,
                      8, 9, 10, 11};
        g.fill_row_major(data, 12);

        const std::string p = tmp_path(".ppm");
        write_grid_ppm(p, g, /*scale=*/2);
        std::ifstream in(p, std::ios::binary);
        std::string magic, dims, max;
        std::getline(in, magic);
        std::getline(in, dims);
        std::getline(in, max);
        WFC_CHECK_EQ(magic, std::string("P6"));
        WFC_CHECK_EQ(dims,  std::string("8 6"));   // 4 cols * 2, 3 rows * 2
        WFC_CHECK_EQ(max,   std::string("255"));
        std::remove(p.c_str());
    }

    // === default_color: known values ===
    {
        auto c0 = default_color(0);
        WFC_CHECK_EQ(c0[0], static_cast<unsigned char>(0));
        WFC_CHECK_EQ(c0[1], static_cast<unsigned char>(0));
        WFC_CHECK_EQ(c0[2], static_cast<unsigned char>(0));

        auto c1 = default_color(1);
        WFC_CHECK_EQ(c1[0], static_cast<unsigned char>(255));
        WFC_CHECK_EQ(c1[1], static_cast<unsigned char>(255));
        WFC_CHECK_EQ(c1[2], static_cast<unsigned char>(255));

        // Palette is 16 entries, value 16 wraps to value 0.
        auto c16 = default_color(16);
        WFC_CHECK(c16 == c0);
    }

    // === default_color: same color for value+16k (palette wrap) ===
    {
        for (int v = 0; v < 16; ++v) {
            auto c = default_color(static_cast<Value>(v));
            auto c2 = default_color(static_cast<Value>(v + 16));
            WFC_CHECK(c == c2);
        }
    }

    // === Empty grid IO: write should not crash, read of empty file throws ===
    {
        // Cannot create a "real" empty grid easily; use 1x1 instead and
        // check the txt has a single value plus newline.
        Grid g(1, 1, 5);
        const std::string p = tmp_path(".txt");
        write_grid_txt(p, g);
        std::ifstream in(p);
        std::string line;
        std::getline(in, line);
        WFC_CHECK(line.find('5') != std::string::npos);
        std::remove(p.c_str());
    }

    // === Larger round-trip (10×10 multivalue) ===
    {
        Grid g(10, 10);
        int data[100];
        for (int i = 0; i < 100; ++i) data[i] = (i * 17) & 0xFF;
        g.fill_row_major(data, 100);

        const std::string p = tmp_path(".txt");
        write_grid_txt(p, g);
        Grid read_back = read_grid_txt(p);
        WFC_CHECK(grids_equal(g, read_back));
        std::remove(p.c_str());
    }

    return wfc_test::report();
}
