#pragma once

#include "wfc/Grid.hpp"
#include "wfc/OverlapRules.hpp"
#include "wfc/TileSet.hpp"
#include <chrono>
#include <cstdint>
#include <stdexcept>
#include <string>

namespace wfc {

// Default number of attempts before giving up on a problem instance. WFC
// can hit contradictions on tightly constrained samples; each retry uses a
// derived seed so the random choices differ.
inline constexpr int kDefaultMaxAttempts = 5;

struct SolverOptions {
    int rows = 32;        // output rows
    int cols = 32;        // output cols
    std::uint64_t seed = 0;
    int max_attempts = kDefaultMaxAttempts;

    // Number of attempts run concurrently. 1 = legacy sequential retry. K > 1
    // launches K serial attempts in parallel; the lowest-indexed success wins,
    // so output remains deterministic for a given seed (a serial run with the
    // same max_attempts would have picked the same attempt). Pays off when
    // single attempts have a low success rate (e.g. tightly constrained
    // samples like terrain N=3); wasteful when single attempts almost always
    // succeed.
    int parallel_attempts = 1;

    // Use backtracking instead of restart-on-contradiction. When false
    // (default), a contradiction triggers a fresh attempt with a derived
    // seed. When true, each collapse pushes a decision frame onto a stack
    // (cell, remaining tile choices, wave snapshot); a contradiction pops
    // the top frame and tries the next choice. Useful on tightly-
    // constrained samples where most random seeds run into the same
    // dead-end; wasteful on easy samples where retry would have succeeded
    // on attempt 1. Opt-in : the default path is unchanged. Memory cost
    // ~O(rows*cols*words_per_cell) per stack frame.
    bool use_backtracking = false;

    // Throws std::invalid_argument if any field is out of range.
    void validate() const {
        if (rows < 1)              throw std::invalid_argument("SolverOptions: rows < 1");
        if (cols < 1)              throw std::invalid_argument("SolverOptions: cols < 1");
        if (max_attempts < 1)      throw std::invalid_argument("SolverOptions: max_attempts < 1");
        if (parallel_attempts < 1) throw std::invalid_argument("SolverOptions: parallel_attempts < 1");
    }
};

struct SolverStats {
    bool success = false;
    int attempts = 0;
    int collapses = 0;       // number of cells decided
    int propagations = 0;    // number of propagation events
    double seconds_total = 0.0; // wall-clock from solve() entry to exit
    double seconds_solve = 0.0; // sum of run_attempt() durations
    std::string backend = "serial";
};

// Solver interface. Each implementation (serial/OMP/Kokkos) returns a Grid
// of the requested size and reports timing/status via SolverStats.
class WFCSolver {
public:
    virtual ~WFCSolver() = default;
    [[nodiscard]] virtual Grid solve(const TileSet& tiles,
                                     const OverlapRules& rules,
                                     const SolverOptions& opt,
                                     SolverStats& stats) = 0;

    // Backend identifier ("serial", "omp", "kokkos"). Implemented once in
    // WFCSolverBase by delegating to the protected backend_name() hook.
    virtual const char* name() const = 0;
};

} // namespace wfc
