#pragma once

#include "wfc/Grid.hpp"
#include "wfc/GridIO.hpp"
#include "wfc/OverlapRules.hpp"
#include "wfc/TileSet.hpp"
#include "wfc/WFCSolver.hpp"

#include <chrono>
#include <iostream>
#include <stdexcept>
#include <string>

namespace wfc::cli {

struct Args {
    std::string sample;
    int rows = 32;
    int cols = 32;
    int N = 2;
    std::uint64_t seed = 0;
    int attempts = 5;
    int threads = 0;
    int scale = 8;
    std::string out_txt;
    std::string out_ppm;
    std::string out_png;
    bool help_only = false;  // set by parse() if --help was passed
};

inline void print_usage(const char* prog) {
    std::cerr <<
        "usage: " << prog << " <sample.txt> [options]\n"
        "  --rows R           output rows (default 32)\n"
        "  --cols C           output cols (default 32)\n"
        "  -N N               tile size (default 2)\n"
        "  --seed S           rng seed (default 0)\n"
        "  --attempts A       max attempts on contradiction (default 5)\n"
        "  --threads T        parallel threads (default: backend default)\n"
        "  --scale S          render scale for ppm/png (default 8)\n"
        "  --out FILE.txt     write text output\n"
        "  --ppm FILE.ppm     write PPM image\n"
        "  --png FILE.png     write PNG image\n";
}

inline Args parse(int argc, char** argv) {
    Args a;
    int i = 1;
    // Allow --help / -h as the very first arg, with no sample path.
    if (i < argc) {
        const std::string first = argv[i];
        if (first == "-h" || first == "--help") {
            print_usage(argv[0]);
            a.help_only = true;
            return a;
        }
    }
    if (i >= argc) { print_usage(argv[0]); throw std::runtime_error("missing sample path"); }
    a.sample = argv[i++];
    while (i < argc) {
        std::string k = argv[i++];
        auto next = [&](const char* name) -> const char* {
            if (i >= argc) { print_usage(argv[0]); throw std::runtime_error(std::string("missing value for ") + name); }
            return argv[i++];
        };
        if      (k == "--rows")     a.rows     = std::stoi(next("--rows"));
        else if (k == "--cols")     a.cols     = std::stoi(next("--cols"));
        else if (k == "-N")         a.N        = std::stoi(next("-N"));
        else if (k == "--seed")     a.seed     = std::stoull(next("--seed"));
        else if (k == "--attempts") a.attempts = std::stoi(next("--attempts"));
        else if (k == "--threads")  a.threads  = std::stoi(next("--threads"));
        else if (k == "--scale")    a.scale    = std::stoi(next("--scale"));
        else if (k == "--out")      a.out_txt  = next("--out");
        else if (k == "--ppm")      a.out_ppm  = next("--ppm");
        else if (k == "--png")      a.out_png  = next("--png");
        else if (k == "-h" || k == "--help") { print_usage(argv[0]); a.help_only = true; return a; }
        else { print_usage(argv[0]); throw std::runtime_error("unknown option: " + k); }
    }
    return a;
}

inline void run(WFCSolver& solver, const Args& a) {
    if (a.help_only) return;  // --help was handled by parse(), nothing to run
    Grid sample = read_grid_txt(a.sample);
    auto t0 = std::chrono::steady_clock::now();
    TileSet tiles = TileSet::from_sample(sample, a.N);
    OverlapRules rules = OverlapRules::build(tiles);
    auto t1 = std::chrono::steady_clock::now();
    double rules_s = std::chrono::duration<double>(t1 - t0).count();

    SolverOptions opt;
    opt.rows = a.rows;
    opt.cols = a.cols;
    opt.seed = a.seed;
    opt.max_attempts = a.attempts;

    SolverStats stats;
    Grid out = solver.solve(tiles, rules, opt, stats);

    std::cout << "backend     : " << stats.backend << "\n"
              << "tiles       : " << tiles.size() << " (max value " << static_cast<int>(tiles.max_value()) << ")\n"
              << "rules build : " << rules_s        << " s\n"
              << "solve       : " << stats.seconds_solve << " s\n"
              << "total       : " << (rules_s + stats.seconds_total) << " s\n"
              << "success     : " << (stats.success ? "yes" : "no") << "\n"
              << "attempts    : " << stats.attempts    << "\n"
              << "collapses   : " << stats.collapses   << "\n"
              << "propagations: " << stats.propagations << "\n";

    if (!stats.success) {
        std::cerr << "warning: solver failed after " << stats.attempts << " attempts\n";
    }

    if (!a.out_txt.empty()) write_grid_txt(a.out_txt, out);
    if (!a.out_ppm.empty()) write_grid_ppm(a.out_ppm, out, a.scale);
    if (!a.out_png.empty()) write_grid_png(a.out_png, out, a.scale);
}

} // namespace wfc::cli
