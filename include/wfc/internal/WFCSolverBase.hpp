#pragma once

#include "wfc/WFCSolver.hpp"
#include "wfc/Wave.hpp"
#include "wfc/internal/SolverCommon.hpp"

#include <random>

namespace wfc {

// Template Method base for WFC backends. Implements the retry/timing/loop
// orchestration once; subclasses only override the two hot operations
// (`pick_cell` and `propagate`) plus optional setup/teardown hooks.
class WFCSolverBase : public WFCSolver {
public:
    [[nodiscard]] Grid solve(const TileSet& tiles,
                             const OverlapRules& rules,
                             const SolverOptions& opt,
                             SolverStats& stats) override final;

    // Forwards to backend_name(); subclasses only define backend_name().
    const char* name() const override final { return backend_name(); }

protected:
    // Select the next cell to collapse. Returns -1 if every cell is already
    // decided (or has a 0-count, i.e. contradiction — the caller verifies).
    virtual int pick_cell(const Wave& wave,
                          const TileSet& tiles,
                          std::uint64_t seed) = 0;

    // Propagate the constraint introduced by collapsing `start_cell`. Returns
    // false if propagation produced a contradiction.
    virtual bool propagate(Wave& wave,
                           const OverlapRules& rules,
                           int start_cell,
                           SolverStats& stats) = 0;

    // Optional hooks for backends that need init/teardown around the solve
    // (Kokkos initialise/finalize is the main use case).
    virtual void on_solve_begin() {}
    virtual void on_solve_end() {}

    // Backend identifier copied into stats. Pure-virtual so each subclass
    // declares its own identity explicitly.
    virtual const char* backend_name() const = 0;

private:
    // Single WFC attempt: pick → collapse → propagate until done or a
    // contradiction. Returns true on success.
    bool run_attempt(Wave& wave,
                     const TileSet& tiles,
                     const OverlapRules& rules,
                     std::uint64_t seed,
                     std::mt19937_64& rng,
                     SolverStats& stats);
};

} // namespace wfc
