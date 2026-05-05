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
#include <random>
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

    /**
     * Number of stacked dungeon floors. Each floor is solved
     * independently with `seed + floor_index * kSeedStride` so the
     * floors look different. After all floors are solved, the CLI
     * post-processes them to insert stair tiles linking adjacent
     * floors at a common walkable cell.
     */
    int levels = 1;

    /**
     * Vertical spacing between floors in centimetres. Plumbed straight
     * into the JSON `grid.floor_height_cm`; the UE5 plugin uses it as
     * the Z multiplier when stacking levels.
     */
    double floor_height_cm = 400.0;

    /**
     * Tile id used to mark a stair cell post-WFC. Picked outside the
     * usual `0-6` range so it cannot accidentally clash with a tile
     * the user already employs in their samples.
     */
    int stair_tile_id = 9;
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
        "  --levels N                number of stacked floors (default 1)\n"
        "  --floor-height CM         vertical spacing between floors (default 400)\n"
        "  --stair-id I              tile id used for stair cells (default 9)\n"
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
        else if (k == "--levels")       a.levels          = std::stoi(next("--levels"));
        else if (k == "--floor-height") a.floor_height_cm = std::stod(next("--floor-height"));
        else if (k == "--stair-id")     a.stair_tile_id   = std::stoi(next("--stair-id"));
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
 * @brief One solved floor plus the diagnostic counters that came out
 *        of the connectivity check.
 *
 * Held in a vector by `main` so the post-WFC stair pass can mutate the
 * grids in place before we serialise them.
 */
struct LevelGrid {
    int level = 0;
    wfc::Grid grid;
    wfc::SolverStats stats;
    bool connected = false;
    int reached = 0;
    int total_walkable = 0;
};

/**
 * @brief Solve a single floor with the connectivity-aware retry loop.
 *
 * Each floor uses `base_seed + level_idx * kSeedStride` as its starting
 * seed so different floors look different even with the same sample.
 * The same connectivity budget is applied per floor: if the floor
 * stays disconnected after `conn_budget` re-rolls the function returns
 * the last grid produced (the caller can decide whether to abort).
 */
LevelGrid solve_one_level(int level_idx,
                          std::uint64_t base_seed,
                          const wfc::TileSet& tiles,
                          const wfc::OverlapRules& rules,
                          wfc::WFCSolverSerial& solver,
                          wfc::SolverOptions opt,
                          const std::unordered_set<int>& walkable,
                          int conn_budget) {
    // A large prime stride keeps consecutive floors from sharing the
    // same RNG state, even when `--connectivity-attempts` re-rolls one
    // of them by +1.
    constexpr std::uint64_t kSeedStride = 1000003ULL;

    LevelGrid lg;
    lg.level = level_idx;

    std::uint64_t try_seed =
        base_seed + static_cast<std::uint64_t>(level_idx) * kSeedStride;

    for (int attempt = 0; attempt < std::max(1, conn_budget); ++attempt) {
        opt.seed = try_seed;
        lg.grid = solver.solve(tiles, rules, opt, lg.stats);
        if (!lg.stats.success) break;

        lg.connected = is_connected(
            lg.grid, walkable, &lg.total_walkable, &lg.reached);
        if (lg.connected) break;

        std::cerr << "[connectivity] level " << level_idx
                  << " attempt " << (attempt + 1) << "/" << conn_budget
                  << " disconnected (" << lg.reached << "/" << lg.total_walkable
                  << "), re-rolling with seed " << (try_seed + 1) << "\n";
        ++try_seed;
    }
    return lg;
}

/**
 * @brief Replace one cell on every adjacent pair of floors with a stair
 *        tile, choosing positions that are walkable on both floors.
 *
 * Strategy: for each pair `(i, i+1)`, intersect the walkable cells at
 * matching `(r, c)` positions on both floors. If the intersection is
 * empty, no stair is placed (the dungeon will be effectively cut at
 * that boundary, a warning is logged). Otherwise one cell is picked
 * uniformly and overwritten with `stair_id` on both floors.
 *
 * The mutation is intentional: the caller's `LevelGrid::grid` now
 * carries the stair tile id at the chosen cell, so it appears in the
 * JSON cells array unchanged. The UE5 plugin then treats `stair_id` as
 * a normal walkable tile that happens to spawn a `StairMesh`.
 *
 * RNG: derived from `base_seed` mixed with a constant so the choice is
 * reproducible for a given seed but distinct from any solver RNG state.
 */
void place_stairs(std::vector<LevelGrid>& levels,
                  const std::unordered_set<int>& walkable,
                  int stair_id,
                  std::uint64_t base_seed) {
    if (levels.size() < 2) return;

    std::mt19937_64 rng(base_seed ^ 0xDEADBEEFCAFEULL);

    for (std::size_t i = 0; i + 1 < levels.size(); ++i) {
        wfc::Grid& a = levels[i].grid;
        wfc::Grid& b = levels[i + 1].grid;
        if (a.rows() != b.rows() || a.cols() != b.cols()) {
            std::cerr << "[stairs] skip: levels " << i << "/" << (i + 1)
                      << " have different dimensions ("
                      << a.rows() << "x" << a.cols() << " vs "
                      << b.rows() << "x" << b.cols() << ")\n";
            continue;
        }

        std::vector<std::pair<int, int>> common;
        common.reserve(static_cast<std::size_t>(a.rows()) * a.cols() / 4);
        for (int r = 0; r < a.rows(); ++r) {
            for (int c = 0; c < a.cols(); ++c) {
                if (walkable.count(static_cast<int>(a.at(r, c))) &&
                    walkable.count(static_cast<int>(b.at(r, c)))) {
                    common.emplace_back(r, c);
                }
            }
        }

        if (common.empty()) {
            std::cerr << "[stairs] no walkable cell shared by levels "
                      << i << " and " << (i + 1)
                      << ", floors will be unreachable from each other\n";
            continue;
        }

        std::uniform_int_distribution<std::size_t> pick(0, common.size() - 1);
        const auto [r, c] = common[pick(rng)];
        // Only paint the stair on the LOWER floor of the pair. The
        // upper floor keeps its walkable tile so the player lands on
        // a normal floor when arriving from below, the stair mesh
        // itself is responsible for visually spanning the gap.
        a.at(r, c) = static_cast<wfc::Value>(stair_id);
        std::cerr << "[stairs] placed at level " << i << "/" << (i + 1)
                  << " r=" << r << " c=" << c << "\n";
    }
}

/**
 * @brief Resolve a tile id to a printable name, with the stair id taken
 *        from `args` so users who shift the default still get a
 *        readable JSON.
 */
std::string tile_name_for(int id, const Args& args) {
    if (id == args.stair_tile_id) return "stair";
    if (const char* nm = default_tile_name(id)) return nm;
    return "tile_" + std::to_string(id);
}

/**
 * @brief Emit the full dungeon JSON.
 *
 * Sections (in order): version, metadata (sample/seed/stats),
 * grid (size + UE units), tile_alphabet (id→name), and the per-cell
 * array with cardinal neighbours pre-computed.
 *
 * Multi-floor dungeons use a top-level `levels` array; single-floor
 * dungeons keep emitting `cells` at the root for backward compatibility
 * with older UE5 plugin builds.
 */
void write_dungeon_json(std::ostream& os,
                        const std::vector<LevelGrid>& levels_data,
                        const wfc::TileSet& tiles,
                        const Args& args) {
    using namespace wfc::json;
    if (levels_data.empty()) {
        throw std::runtime_error("write_dungeon_json: no levels to emit");
    }
    Writer w(os);

    Object root(w);

    // Bump the version when we use the multi-floor layout so older
    // UE5 plugin builds (which only know `cells:` at the root) can
    // detect and reject, or upgrade, instead of silently mis-parsing.
    const bool multi_floor = levels_data.size() > 1;
    w.key_value("version", multi_floor ? 2 : 1);

    // Aggregate stats reported in metadata come from level 0; the
    // per-level counters live next to each level's cells block.
    const wfc::SolverStats& stats0 = levels_data.front().stats;
    w.key("metadata");
    {
        Object meta(w);
        w.key_value("sample",        args.sample);
        w.key_value("seed",          static_cast<unsigned long long>(args.seed));
        w.key_value("tile_size_n",   args.N);
        w.key_value("backend",       stats0.backend);
        w.key_value("success",       stats0.success);
        w.key_value("attempts",      stats0.attempts);
        w.key_value("collapses",     stats0.collapses);
        w.key_value("propagations",  stats0.propagations);
        w.key_value("solve_seconds", stats0.seconds_solve);
        w.key_value("levels",        static_cast<int>(levels_data.size()));
        w.key_value("stair_tile_id", args.stair_tile_id);
    }

    const wfc::Grid& g0 = levels_data.front().grid;
    w.key("grid");
    {
        Object grid(w);
        w.key_value("rows",             g0.rows());
        w.key_value("cols",             g0.cols());
        w.key_value("cell_size_cm",     args.cell_size_cm);
        w.key_value("wall_height_cm",   args.wall_height_cm);
        w.key_value("floor_height_cm",  args.floor_height_cm);
    }

    // Emit alphabet entries up to max(tiles.max_value(), stair_tile_id)
    // so multi-floor dungeons always advertise "stair" even if the
    // sample never used that id.
    w.key("tile_alphabet");
    {
        Array alphabet(w);
        const int max_v = std::max(
            static_cast<int>(tiles.max_value()), args.stair_tile_id);
        for (int id = 0; id <= max_v; ++id) {
            Object t(w);
            w.key_value("id", id);
            w.key_value("name", tile_name_for(id, args));
        }
    }

    auto emit_cells_for = [&](const wfc::Grid& g) {
        Array cells(w);
        for (int r = 0; r < g.rows(); ++r) {
            for (int c = 0; c < g.cols(); ++c) {
                Object cell(w);
                w.key_value("r", r);
                w.key_value("c", c);
                w.key_value("tile_id", static_cast<int>(g.at(r, c)));
                w.key("neighbors");
                {
                    Object nb(w);
                    w.key_value("n", neighbour_id(g, r - 1, c));
                    w.key_value("s", neighbour_id(g, r + 1, c));
                    w.key_value("e", neighbour_id(g, r, c + 1));
                    w.key_value("w", neighbour_id(g, r, c - 1));
                }
            }
        }
    };

    if (!multi_floor) {
        // Backward-compatible single-floor layout: `cells:` at the root.
        w.key("cells");
        emit_cells_for(g0);
    } else {
        // Multi-floor layout: `levels: [{ level, connected, cells }, ...]`.
        // Single Z-stride for now (`floor_height_cm`), per-level
        // overrides could be added later without breaking this format.
        w.key("levels");
        {
            Array levels_arr(w);
            for (const LevelGrid& lg : levels_data) {
                Object level(w);
                w.key_value("level",          lg.level);
                w.key_value("connected",      lg.connected);
                w.key_value("walkable_total", lg.total_walkable);
                w.key_value("walkable_reached", lg.reached);
                w.key("cells");
                emit_cells_for(lg.grid);
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

        std::unordered_set<int> walkable = parse_int_set(args.walkable_ids);
        // Stair cells are walkable by definition, they exist only to
        // let the player move between floors. Adding the id here keeps
        // the connectivity check honest after `place_stairs` mutates
        // the grids in place.
        walkable.insert(args.stair_tile_id);
        const int conn_budget = std::max(1, args.connectivity_attempts);
        const int n_levels = std::max(1, args.levels);

        wfc::WFCSolverSerial solver;
        std::vector<LevelGrid> levels_data;
        levels_data.reserve(static_cast<std::size_t>(n_levels));

        bool any_failed = false;
        bool any_disconnected = false;
        for (int li = 0; li < n_levels; ++li) {
            LevelGrid lg = solve_one_level(
                li, args.seed, tiles, rules, solver, opt, walkable, conn_budget);
            if (!lg.stats.success) any_failed = true;
            if (lg.stats.success && !lg.connected) any_disconnected = true;
            levels_data.push_back(std::move(lg));
        }

        // Insert stairs after every floor has been solved so the post
        // pass can choose any walkable cell, the stairs never appear
        // in the WFC alphabet, they are simply painted on top.
        if (n_levels > 1) {
            place_stairs(levels_data, walkable, args.stair_tile_id, args.seed);
        }

        // One-line summary covering all floors. The per-level
        // connectivity stats live inside the JSON for tooling.
        std::cerr << "sample=" << args.sample
                  << " tiles=" << tiles.size()
                  << " levels=" << n_levels
                  << " success=" << (any_failed ? "no" : "yes")
                  << " connected=" << (any_disconnected ? "no" : "yes")
                  << " rules_s=" << rules_s;
        for (const LevelGrid& lg : levels_data) {
            std::cerr << " L" << lg.level
                      << "[walk=" << lg.reached << "/" << lg.total_walkable
                      << ",collapses=" << lg.stats.collapses
                      << ",solve_s=" << lg.stats.seconds_solve
                      << "]";
        }
        std::cerr << "\n";

        // The optional --txt dump only makes sense for the first
        // floor; users wanting all floors can re-run with --levels 1
        // per floor or post-process the JSON.
        if (!args.out_txt.empty()) {
            wfc::write_grid_txt(args.out_txt, levels_data.front().grid);
        }

        std::ofstream json_out(args.out_path);
        if (!json_out) throw std::runtime_error("cannot write " + args.out_path);
        write_dungeon_json(json_out, levels_data, tiles, args);
        json_out << '\n';

        std::cerr << "wrote " << args.out_path << "\n";
        // Exit codes: 0 = success+connected, 2 = WFC failure on any
        // level, 3 = WFC ok but at least one level disconnected after
        // exhausting --connectivity-attempts (callers can tell the two
        // failure modes apart from the exit code alone).
        if (any_failed) return 2;
        if (any_disconnected) return 3;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }
}
