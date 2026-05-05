#include "wfc/internal/WFCSolverBase.hpp"

#include "wfc/internal/SolverCommon.hpp"

#include <atomic>
#include <chrono>
#include <vector>

#ifdef _OPENMP
#include <omp.h>
#endif

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

Grid WFCSolverBase::solve_sequential(const TileSet& tiles,
                                     const OverlapRules& rules,
                                     const SolverOptions& opt,
                                     SolverStats& stats) {
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
            return build_output(wave, tiles);
        }
    }
    stats.success = false;
    return Grid(opt.rows, opt.cols);
}

Grid WFCSolverBase::solve_parallel(const TileSet& tiles,
                                   const OverlapRules& rules,
                                   const SolverOptions& opt,
                                   SolverStats& stats) {
    // Strategy: process attempts in batches of K = parallel_attempts. Within
    // a batch, every attempt runs concurrently on its own Wave with its own
    // seed (= attempt_seed(base, idx)). The lowest-indexed success in a batch
    // wins (matches what the sequential retry loop would have produced).
    // If every attempt in a batch fails, advance to the next batch.
    const int K = opt.parallel_attempts;
    const int num_tiles = rules.num_tiles();

    // Pre-allocated waves, reused across batches.
    std::vector<Wave> waves;
    waves.reserve(static_cast<std::size_t>(K));
    for (int k = 0; k < K; ++k) waves.emplace_back(opt.rows, opt.cols, num_tiles);

    // Per-attempt scratch.
    std::vector<int> success(static_cast<std::size_t>(K), 0);
    std::vector<int> collapses(static_cast<std::size_t>(K), 0);
    std::vector<int> propagations(static_cast<std::size_t>(K), 0);

    int total_attempts_run = 0;
    int batch_start = 0;
    while (batch_start < opt.max_attempts) {
        const int batch_size =
            std::min(K, opt.max_attempts - batch_start);

        // Reset per-batch state. Wave needs to be re-initialised because a
        // previous batch may have collapsed it.
        for (int k = 0; k < batch_size; ++k) {
            success[k] = 0;
            collapses[k] = 0;
            propagations[k] = 0;
            waves[k] = Wave(opt.rows, opt.cols, num_tiles);
        }

        // Cooperative bail-out: as soon as any attempt succeeds, lower-indexed
        // attempts that are still running stay relevant (we may pick them
        // instead) but higher-indexed attempts that haven't started yet skip
        // their work — the lowest-indexed success will dominate anyway.
        std::atomic<int> lowest_success{batch_size};

        const auto t_solve_start = Clock::now();

        #ifdef _OPENMP
        #pragma omp parallel for schedule(dynamic, 1) \
            num_threads(std::min(batch_size, omp_get_max_threads()))
        #endif
        for (int k = 0; k < batch_size; ++k) {
            if (k >= lowest_success.load(std::memory_order_relaxed)) continue;
            const std::uint64_t seed =
                attempt_seed(opt.seed, batch_start + k);
            std::mt19937_64 rng(seed);
            SolverStats local;
            const bool ok = serial_run_attempt(waves[k], tiles, rules,
                                               seed, rng, local);
            collapses[k] = local.collapses;
            propagations[k] = local.propagations;
            success[k] = ok ? 1 : 0;
            if (ok) {
                int cur = lowest_success.load(std::memory_order_relaxed);
                while (k < cur && !lowest_success.compare_exchange_weak(
                           cur, k, std::memory_order_relaxed)) {}
            }
        }

        stats.seconds_solve += std::chrono::duration<double>(
            Clock::now() - t_solve_start).count();
        total_attempts_run += batch_size;

        // Lowest-indexed success wins.
        int picked = -1;
        for (int k = 0; k < batch_size; ++k) {
            if (success[k]) { picked = k; break; }
        }
        if (picked >= 0) {
            stats.attempts = batch_start + picked + 1;
            stats.collapses = collapses[picked];
            stats.propagations = propagations[picked];
            stats.success = true;
            return build_output(waves[picked], tiles);
        }

        batch_start += batch_size;
    }

    stats.attempts = total_attempts_run;
    stats.success = false;
    return Grid(opt.rows, opt.cols);
}

Grid WFCSolverBase::solve(const TileSet& tiles,
                          const OverlapRules& rules,
                          const SolverOptions& opt,
                          SolverStats& stats) {
    opt.validate();

    const auto t_total_start = Clock::now();
    stats.backend = backend_name();

    on_solve_begin();

    Grid result = (opt.parallel_attempts > 1)
                  ? solve_parallel(tiles, rules, opt, stats)
                  : solve_sequential(tiles, rules, opt, stats);

    stats.seconds_total = std::chrono::duration<double>(
        Clock::now() - t_total_start).count();
    on_solve_end();
    return result;
}

} // namespace wfc
