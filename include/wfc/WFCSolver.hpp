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

    // Throws std::invalid_argument if any field is out of range.
    void validate() const {
        if (rows < 1)         throw std::invalid_argument("SolverOptions: rows < 1");
        if (cols < 1)         throw std::invalid_argument("SolverOptions: cols < 1");
        if (max_attempts < 1) throw std::invalid_argument("SolverOptions: max_attempts < 1");
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
