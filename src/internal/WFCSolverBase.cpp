#include "wfc/internal/WFCSolverBase.hpp"

#include "wfc/internal/SolverCommon.hpp"

#include <chrono>

namespace wfc {

namespace {
using Clock = std::chrono::steady_clock;
}

bool WFCSolverBase::run_attempt(Wave& wave,
                                const TileSet& tiles,
                                const OverlapRules& rules,
                                std::uint64_t seed,
                                std::mt19937_64& rng,
                                SolverStats& stats) {
    while (true) {
        const int cell = pick_cell(wave, tiles, seed);
        if (cell < 0) {
            // No undecided cell left; verify no contradiction was missed.
            for (int c = 0; c < wave.num_cells(); ++c) {
                if (wave.at(c).count() == 0) return false;
            }
            return true;
        }

        const int t = weighted_pick(wave.at(cell), tiles.frequencies(), rng);
        wave.at(cell).set_only(static_cast<std::size_t>(t));
        ++stats.collapses;

        if (!propagate(wave, rules, cell, stats)) return false;
    }
}

Grid WFCSolverBase::solve(const TileSet& tiles,
                          const OverlapRules& rules,
                          const SolverOptions& opt,
                          SolverStats& stats) {
    opt.validate();

    const auto t_total_start = Clock::now();
    stats.backend = backend_name();

    on_solve_begin();

    for (int attempt = 1; attempt <= opt.max_attempts; ++attempt) {
        stats.attempts = attempt;
        const std::uint64_t seed = attempt_seed(opt.seed, attempt - 1);
        std::mt19937_64 rng(seed);
        Wave wave(opt.rows, opt.cols, rules.num_tiles());

        const auto t_solve_start = Clock::now();
        const bool ok = run_attempt(wave, tiles, rules, seed, rng, stats);
        stats.seconds_solve += std::chrono::duration<double>(
            Clock::now() - t_solve_start).count();

        if (ok) {
            stats.success = true;
            stats.seconds_total = std::chrono::duration<double>(
                Clock::now() - t_total_start).count();
            on_solve_end();
            return build_output(wave, tiles);
        }
    }

    stats.success = false;
    stats.seconds_total = std::chrono::duration<double>(
        Clock::now() - t_total_start).count();
    on_solve_end();
    return Grid(opt.rows, opt.cols);
}

} // namespace wfc
