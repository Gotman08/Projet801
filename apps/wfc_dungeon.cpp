/**
 * @file wfc_dungeon.cpp
 * @brief CLI front-end that runs the serial WFC solver and emits a
 *        structured JSON file describing the synthesised dungeon for
 *        the Unreal Engine plugin to consume.
 *
 * Reuses the existing 2D solver verbatim, the dungeon is "2.5D" in
 * the sense that UE5 will extrude each cell into a 3D mesh based on
 * the cell's tile id and its four cardinal neighbours.
 */

#include "wfc/Grid.hpp"
#include "wfc/GridIO.hpp"
#include "wfc/OverlapRules.hpp"
#include "wfc/TileSet.hpp"
#include "wfc/solvers/WFCSolverSerial.hpp"

#include "json_writer.hpp"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <queue>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace {

/**
 * @brief Default tile id → readable name mapping.
 *
 * Mirrors the convention used by the sample files (`samples/*.txt`):
 *   - 0 = floor (open ground)
 *   - 1 = wall (full wall block)
 *   - 2-5 = terrain types (water/sand/grass/rock/...)
 *   - 6 = door
 *
 * Any tile id not in the map is named `tile_<id>` in the JSON. The
 * user can rename in UE5 via the `UTileMappingDataAsset`.
 */
const char* default_tile_name(int id) {
    static const std::unordered_map<int, const char*> kNames = {
        {0, "floor"},
        {1, "wall"},
        {2, "water"},
        {3, "sand"},
        {4, "grass"},
        {5, "rock"},
        {6, "door"},
    };
    auto it = kNames.find(id);
    return (it != kNames.end()) ? it->second : nullptr;
}

struct Args {
    std::string sample;
    std::string out_path;
    int rows = 32;
    int cols = 32;
    int N = 2;
    std::uint64_t seed = 0;
    int attempts = 5;
    double cell_size_cm = 200.0;
    double wall_height_cm = 300.0;
    std::string out_txt;

    /**
     * Comma-separated list of tile ids treated as walkable for the
     * post-solver connectivity check. Default matches the convention
     * `0=floor, 6=door` (terrain ids 2-5 are not in here because they
     * may or may not be walkable depending on the user's intent).
     */
    std::string walkable_ids = "0,6";

    /**
     * Number of times we re-roll the seed if the solver succeeds but
     * the resulting dungeon has disconnected walkable regions. The
     * solver's own `--attempts` budget retries on contradictions; this
     * is a separate, outer budget for connectivity-only failures.
     * Set to 1 to disable re-rolling (still emits a warning).
     */
    int connectivity_attempts = 5;
};

void usage(const char* prog) {
    std::cerr <<
        "usage: " << prog << " <sample.txt> -o <dungeon.json> [options]\n"
        "  --rows R                  output rows (default 32)\n"
        "  --cols C                  output cols (default 32)\n"
        "  -N N                      tile size for WFC (default 2)\n"
        "  --seed S                  rng seed (default 0)\n"
        "  --attempts A              WFC retry budget on contradiction (default 5)\n"
        "  --connectivity-attempts A re-roll budget if walkable cells aren't\n"
        "                            all connected after a successful solve (default 5)\n"
        "  --walkable IDS            comma-separated walkable tile ids for the\n"
        "                            connectivity check (default \"0,6\")\n"
        "  --cell-size CM            UE5 cell size in cm (default 200)\n"
        "  --wall-height CM          UE5 wall height in cm (default 300)\n"
        "  --txt FILE.txt            also dump grid as text (debug)\n"
        "  -o FILE.json              JSON output path (required)\n";
}

Args parse(int argc, char** argv) {
    Args a;
    int i = 1;
    if (i >= argc) { usage(argv[0]); throw std::runtime_error("missing sample path"); }
    a.sample = argv[i++];
    while (i < argc) {
        std::string k = argv[i++];
        auto next = [&](const char* name) -> const char* {
            if (i >= argc) { usage(argv[0]); throw std::runtime_error(std::string("missing value for ") + name); }
            return argv[i++];
        };
        if      (k == "--rows")        a.rows           = std::stoi(next("--rows"));
        else if (k == "--cols")        a.cols           = std::stoi(next("--cols"));
        else if (k == "-N")            a.N              = std::stoi(next("-N"));
        else if (k == "--seed")        a.seed           = std::stoull(next("--seed"));
        else if (k == "--attempts")    a.attempts       = std::stoi(next("--attempts"));
        else if (k == "--cell-size")   a.cell_size_cm   = std::stod(next("--cell-size"));
        else if (k == "--wall-height") a.wall_height_cm = std::stod(next("--wall-height"));
        else if (k == "--walkable")    a.walkable_ids   = next("--walkable");
        else if (k == "--connectivity-attempts")
            a.connectivity_attempts = std::stoi(next("--connectivity-attempts"));
        else if (k == "--txt")         a.out_txt        = next("--txt");
        else if (k == "-o")            a.out_path       = next("-o");
        else if (k == "-h" || k == "--help") { usage(argv[0]); std::exit(0); }
        else { usage(argv[0]); throw std::runtime_error("unknown option: " + k); }
    }
    if (a.out_path.empty()) {
        usage(argv[0]);
        throw std::runtime_error("-o <output.json> is required");
    }
    return a;
}

/**
 * @brief Toroidal-clamped neighbour value lookup.
 *
 * Returns the tile id of the cell at `(r, c)`. Out-of-bounds reads
 * return -1, signalling "no neighbour", the UE5 plugin treats that
 * as a free edge (no wall to spawn).
 */
int neighbour_id(const wfc::Grid& g, int r, int c) {
    if (r < 0 || r >= g.rows() || c < 0 || c >= g.cols()) return -1;
    return static_cast<int>(g.at(r, c));
}

/**
 * @brief Parse a comma-separated list of integers into an unordered_set.
 *
 * Tolerates whitespace and empty entries. Used for the `--walkable` CLI
 * flag. Throws on a non-numeric entry so the user gets a clear error
 * instead of silently treating their typo as "no walkable tiles".
 */
std::unordered_set<int> parse_int_set(const std::string& csv) {
    std::unordered_set<int> out;
    std::stringstream ss(csv);
    std::string tok;
    while (std::getline(ss, tok, ',')) {
        // Trim whitespace.
        const auto a = tok.find_first_not_of(" \t");
        const auto b = tok.find_last_not_of(" \t");
        if (a == std::string::npos) continue;
        const std::string trimmed = tok.substr(a, b - a + 1);
        if (trimmed.empty()) continue;
        try {
            out.insert(std::stoi(trimmed));
        } catch (...) {
            throw std::runtime_error("invalid integer in CSV list: '" + trimmed + "'");
        }
    }
    return out;
}

/**
 * @brief BFS flood-fill connectivity check on the collapsed grid.
 *
 * Walks all walkable cells reachable from the first walkable cell found
 * (top-left scan order), counting how many were reached. If every
 * walkable cell in the grid was reached, the dungeon is connected.
 *
 * Cardinal-neighbours only (no diagonals), matches the wall placement
 * convention of the UE5 plugin: two walkable cells touching only by a
 * corner are NOT considered connected because a player cannot walk
 * through a corner without going around.
 *
 * Cost: O(rows × cols) time, O(rows × cols) extra memory for the
 * visited bitmap. Negligible compared to a single propagation step.
 */
bool is_connected(const wfc::Grid& g,
                  const std::unordered_set<int>& walkable,
                  int* out_total_walkable = nullptr,
                  int* out_reached = nullptr) {
    const int rows = g.rows();
    const int cols = g.cols();
    std::vector<std::uint8_t> visited(static_cast<std::size_t>(rows) * cols, 0);

    int start_r = -1, start_c = -1;
    int total = 0;
    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < cols; ++c) {
            if (walkable.count(static_cast<int>(g.at(r, c)))) {
                if (start_r < 0) { start_r = r; start_c = c; }
                ++total;
            }
        }
    }
    if (out_total_walkable) *out_total_walkable = total;

    if (total == 0) {
        if (out_reached) *out_reached = 0;
        return true; // Trivially connected, no walkable cells.
    }

    int reached = 1;
    visited[static_cast<std::size_t>(start_r) * cols + start_c] = 1;
    std::queue<std::pair<int, int>> q;
    q.emplace(start_r, start_c);

    static const int dr[4] = {-1, 1, 0, 0};
    static const int dc[4] = { 0, 0, 1, -1};
    while (!q.empty()) {
        const auto [r, c] = q.front(); q.pop();
        for (int d = 0; d < 4; ++d) {
            const int nr = r + dr[d];
            const int nc = c + dc[d];
            if (nr < 0 || nr >= rows || nc < 0 || nc >= cols) continue;
            const std::size_t idx = static_cast<std::size_t>(nr) * cols + nc;
            if (visited[idx]) continue;
            if (!walkable.count(static_cast<int>(g.at(nr, nc)))) continue;
            visited[idx] = 1;
            ++reached;
            q.emplace(nr, nc);
        }
    }

    if (out_reached) *out_reached = reached;
    return reached == total;
}

/**
 * @brief Emit the full dungeon JSON for a single solved floor.
 *
 * Sections: version, metadata (sample/seed/stats + connectivity),
 * grid (size + UE units), tile_alphabet (id->name), and the per-cell
 * array with cardinal neighbours pre-computed.
 */
void write_dungeon_json(std::ostream& os,
                        const wfc::Grid& out,
                        const wfc::TileSet& tiles,
                        const wfc::SolverStats& stats,
                        bool connected,
                        int total_walkable,
                        int reached,
                        const Args& args) {
    using namespace wfc::json;
    Writer w(os);

    Object root(w);

    w.key_value("version", 1);

    w.key("metadata");
    {
        Object meta(w);
        w.key_value("sample",          args.sample);
        w.key_value("seed",            static_cast<unsigned long long>(args.seed));
        w.key_value("tile_size_n",     args.N);
        w.key_value("backend",         stats.backend);
        w.key_value("success",         stats.success);
        w.key_value("attempts",        stats.attempts);
        w.key_value("collapses",       stats.collapses);
        w.key_value("propagations",    stats.propagations);
        w.key_value("solve_seconds",   stats.seconds_solve);
        w.key_value("connected",       connected);
        w.key_value("walkable_total",  total_walkable);
        w.key_value("walkable_reached", reached);
    }

    w.key("grid");
    {
        Object grid(w);
        w.key_value("rows",            out.rows());
        w.key_value("cols",            out.cols());
        w.key_value("cell_size_cm",    args.cell_size_cm);
        w.key_value("wall_height_cm",  args.wall_height_cm);
    }

    w.key("tile_alphabet");
    {
        Array alphabet(w);
        const int max_v = static_cast<int>(tiles.max_value());
        for (int id = 0; id <= max_v; ++id) {
            Object t(w);
            w.key_value("id", id);
            const char* nm = default_tile_name(id);
            if (nm) {
                w.key_value("name", nm);
            } else {
                w.key_value("name", "tile_" + std::to_string(id));
            }
        }
    }

    w.key("cells");
    {
        Array cells(w);
        for (int r = 0; r < out.rows(); ++r) {
            for (int c = 0; c < out.cols(); ++c) {
                Object cell(w);
                w.key_value("r", r);
                w.key_value("c", c);
                w.key_value("tile_id", static_cast<int>(out.at(r, c)));
                w.key("neighbors");
                {
                    Object nb(w);
                    w.key_value("n", neighbour_id(out, r - 1, c));
                    w.key_value("s", neighbour_id(out, r + 1, c));
                    w.key_value("e", neighbour_id(out, r, c + 1));
                    w.key_value("w", neighbour_id(out, r, c - 1));
                }
            }
        }
    }
}

}

int main(int argc, char** argv) {
    Args args;
    try {
        args = parse(argc, argv);
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }

    try {
        wfc::Grid sample = wfc::read_grid_txt(args.sample);
        const auto t0 = std::chrono::steady_clock::now();
        wfc::TileSet tiles = wfc::TileSet::from_sample(sample, args.N);
        wfc::OverlapRules rules = wfc::OverlapRules::build(tiles);
        const double rules_s = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - t0).count();

        wfc::SolverOptions opt;
        opt.rows = args.rows;
        opt.cols = args.cols;
        opt.max_attempts = args.attempts;

        const std::unordered_set<int> walkable = parse_int_set(args.walkable_ids);
        const int conn_budget = std::max(1, args.connectivity_attempts);

        // Single-floor solve with the connectivity-aware retry loop:
        // re-roll the seed if WFC succeeds but the dungeon is split
        // into disconnected walkable regions.
        wfc::WFCSolverSerial solver;
        wfc::SolverStats stats;
        wfc::Grid out;
        bool connected = false;
        int total_walkable = 0;
        int reached = 0;
        std::uint64_t try_seed = args.seed;
        for (int attempt = 0; attempt < conn_budget; ++attempt) {
            opt.seed = try_seed;
            out = solver.solve(tiles, rules, opt, stats);
            if (!stats.success) break;
            connected = is_connected(out, walkable, &total_walkable, &reached);
            if (connected) break;
            std::cerr << "[connectivity] attempt " << (attempt + 1) << "/" << conn_budget
                      << " disconnected (" << reached << "/" << total_walkable
                      << "), re-rolling with seed " << (try_seed + 1) << "\n";
            ++try_seed;
        }

        std::cerr << "sample=" << args.sample
                  << " tiles=" << tiles.size()
                  << " success=" << (stats.success ? "yes" : "no")
                  << " connected=" << (connected ? "yes" : "no")
                  << " walkable=" << reached << "/" << total_walkable
                  << " collapses=" << stats.collapses
                  << " rules_s=" << rules_s
                  << " solve_s=" << stats.seconds_solve << "\n";

        if (!args.out_txt.empty()) wfc::write_grid_txt(args.out_txt, out);

        std::ofstream json_out(args.out_path);
        if (!json_out) throw std::runtime_error("cannot write " + args.out_path);
        write_dungeon_json(json_out, out, tiles, stats, connected,
                           total_walkable, reached, args);
        json_out << '\n';

        std::cerr << "wrote " << args.out_path << "\n";
        // Exit codes: 0 = success+connected, 2 = WFC failure,
        // 3 = WFC ok but disconnected after exhausting retries.
        if (!stats.success) return 2;
        if (!connected) return 3;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }
}
