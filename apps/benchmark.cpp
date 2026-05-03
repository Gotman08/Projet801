// Benchmark harness for the WFC solvers.
//
// CSV columns:
//   label,backend,threads,sample,N,rows,cols,seed,repeat,success,attempts,
//   collapses,propagations,rules_s,solve_s,total_s
//
// CLI:
//   --sample <path>          (required)
//   --sizes "32,64,128"      output rows=cols, default "32,64,128"
//   --threads "1,2,4,8"      OMP thread counts, default "1,2,4,8"
//   --repeats N              repetitions per (size,thread), default 3
//   --seed S                 base seed (each repeat adds an offset), default 42
//   --N tile_size            default 2
//   --attempts A             default 5
//   --label NAME             label column in CSV, default "default"
//   --backends "serial,omp"  which backends to run, default both
//   --no-header              skip CSV header (useful when appending)
//   -o output.csv            output path, default results/benchmark.csv

#include "wfc/Grid.hpp"
#include "wfc/GridIO.hpp"
#include "wfc/OverlapRules.hpp"
#include "wfc/TileSet.hpp"
#include "wfc/solvers/WFCSolverSerial.hpp"

#if defined(WFC_HAS_OMP)
#include "wfc/solvers/WFCSolverOMP.hpp"
#include <omp.h>
#endif

#if defined(WFC_HAS_KOKKOS)
#include "wfc/solvers/WFCSolverKokkos.hpp"
#include <Kokkos_Core.hpp>
#endif

#include <chrono>
#include <fstream>
#include <iostream>
#include <memory>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace wfc;

namespace {

struct Config {
    std::string sample;
    std::string label = "default";
    std::vector<int> sizes = {32, 64, 128};
    std::vector<int> threads = {1, 2, 4, 8};
    int repeats = 3;
    std::uint64_t seed = 42;
    int N = 2;
    int attempts = 5;
    std::set<std::string> backends = {"serial", "omp"};
    bool write_header = true;
    std::string out_path = "results/benchmark.csv";
};

std::vector<int> parse_int_list(const std::string& s) {
    std::vector<int> out;
    std::stringstream ss(s);
    std::string tok;
    while (std::getline(ss, tok, ',')) {
        if (!tok.empty()) out.push_back(std::stoi(tok));
    }
    return out;
}

std::set<std::string> parse_str_set(const std::string& s) {
    std::set<std::string> out;
    std::stringstream ss(s);
    std::string tok;
    while (std::getline(ss, tok, ',')) {
        if (!tok.empty()) out.insert(tok);
    }
    return out;
}

void usage(const char* prog) {
    std::cerr <<
        "usage: " << prog << " --sample PATH [options]\n"
        "  --sizes \"32,64,128\"        output sizes\n"
        "  --threads \"1,2,4,8\"        OMP thread counts\n"
        "  --repeats N                  repetitions per config (default 3)\n"
        "  --seed S                     base seed (default 42)\n"
        "  --N <tile_size>              tile size (default 2)\n"
        "  --attempts A                 max attempts on contradiction (default 5)\n"
        "  --label NAME                 label column (default \"default\")\n"
        "  --backends \"serial,omp\"    which backends (default both)\n"
        "  --no-header                  skip CSV header\n"
        "  -o output.csv                output path\n";
}

Config parse_args(int argc, char** argv) {
    Config c;
    int i = 1;
    while (i < argc) {
        std::string k = argv[i++];
        auto next_arg = [&]() -> const char* {
            if (i >= argc) {
                usage(argv[0]);
                throw std::runtime_error("missing value for " + k);
            }
            return argv[i++];
        };
        if      (k == "--sample")    c.sample = next_arg();
        else if (k == "--sizes")     c.sizes = parse_int_list(next_arg());
        else if (k == "--threads")   c.threads = parse_int_list(next_arg());
        else if (k == "--repeats")   c.repeats = std::stoi(next_arg());
        else if (k == "--seed")      c.seed = std::stoull(next_arg());
        else if (k == "--N")         c.N = std::stoi(next_arg());
        else if (k == "--attempts")  c.attempts = std::stoi(next_arg());
        else if (k == "--label")     c.label = next_arg();
        else if (k == "--backends")  c.backends = parse_str_set(next_arg());
        else if (k == "--no-header") c.write_header = false;
        else if (k == "-o")          c.out_path = next_arg();
        else if (k == "-h" || k == "--help") { usage(argv[0]); std::exit(0); }
        else { usage(argv[0]); throw std::runtime_error("unknown option: " + k); }
    }
    if (c.sample.empty()) {
        usage(argv[0]);
        throw std::runtime_error("--sample is required");
    }
    return c;
}

void write_header(std::ostream& out) {
    out << "label,backend,threads,sample,N,rows,cols,seed,repeat,success,"
        << "attempts,collapses,propagations,rules_s,solve_s,total_s\n";
}

void run_one(const Config& cfg,
             const TileSet& tiles,
             const OverlapRules& rules,
             double rules_s,
             const std::string& backend,
             int threads,
             int size,
             std::ostream& csv) {
    std::unique_ptr<WFCSolver> solver;
    if (backend == "serial") {
        solver = std::make_unique<WFCSolverSerial>();
    }
#if defined(WFC_HAS_OMP)
    else if (backend == "omp") {
        solver = std::make_unique<WFCSolverOMP>(threads);
    }
#endif
#if defined(WFC_HAS_KOKKOS)
    else if (backend == "kokkos") {
        solver = std::make_unique<WFCSolverKokkos>();
    }
#endif
    if (!solver) {
        std::cerr << "skipping " << backend << " (not built)\n";
        return;
    }

    for (int rep = 0; rep < cfg.repeats; ++rep) {
        SolverOptions opt;
        opt.rows = size;
        opt.cols = size;
        opt.seed = cfg.seed + static_cast<std::uint64_t>(rep);
        opt.max_attempts = cfg.attempts;

        SolverStats stats;
        Grid out = solver->solve(tiles, rules, opt, stats);
        (void)out;

        csv << cfg.label << ',' << backend << ',' << threads << ',' << cfg.sample
            << ',' << cfg.N << ',' << size << ',' << size << ',' << opt.seed
            << ',' << rep << ',' << (stats.success ? 1 : 0) << ',' << stats.attempts
            << ',' << stats.collapses << ',' << stats.propagations << ','
            << rules_s << ',' << stats.seconds_solve << ',' << stats.seconds_total
            << '\n';
        csv.flush();

        std::cerr << "[" << cfg.label << "] " << backend << " t=" << threads
                  << " " << size << "x" << size << " rep=" << rep
                  << " solve=" << stats.seconds_solve << "s ok=" << stats.success
                  << '\n';
    }
}

} // namespace

int main(int argc, char** argv) {
    Config cfg;
    try {
        cfg = parse_args(argc, argv);
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }

#if defined(WFC_HAS_KOKKOS)
    Kokkos::initialize(argc, argv);
#endif

    Grid sample = read_grid_txt(cfg.sample);
    auto t0 = std::chrono::steady_clock::now();
    TileSet tiles = TileSet::from_sample(sample, cfg.N);
    OverlapRules rules = OverlapRules::build(tiles);
    const double rules_s =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();

    std::cerr << "sample=" << cfg.sample << " N=" << cfg.N
              << " tiles=" << tiles.size() << " max_value="
              << static_cast<int>(tiles.max_value())
              << " rules_s=" << rules_s << "\n";

    std::ofstream csv;
    std::ostream* out = &std::cout;
    if (cfg.out_path != "-") {
        csv.open(cfg.out_path, cfg.write_header ? std::ios::out : std::ios::app);
        if (!csv) {
            std::cerr << "cannot open " << cfg.out_path << "\n";
#if defined(WFC_HAS_KOKKOS)
            Kokkos::finalize();
#endif
            return 1;
        }
        out = &csv;
    }
    if (cfg.write_header) write_header(*out);

    int total_runs = 0;
    for (int size : cfg.sizes) {
        for (const std::string& backend : cfg.backends) {
            if (backend == "serial") {
                run_one(cfg, tiles, rules, rules_s, backend, 1, size, *out);
                ++total_runs;
            } else {
                for (int t : cfg.threads) {
                    run_one(cfg, tiles, rules, rules_s, backend, t, size, *out);
                    ++total_runs;
                }
            }
        }
    }

    std::cerr << "completed " << total_runs << " run groups -> " << cfg.out_path << "\n";

#if defined(WFC_HAS_KOKKOS)
    Kokkos::finalize();
#endif
    return 0;
}
