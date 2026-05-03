#pragma once

#include "wfc/internal/WFCSolverBase.hpp"

namespace wfc {

// Kokkos-based parallel solver. Uses Kokkos parallel_for + atomics on the
// propagation frontier, and the serial min-entropy scan for selection (which
// is cheap relative to propagation in the Kokkos backend's typical shape).
class WFCSolverKokkos : public WFCSolverBase {
protected:
    int pick_cell(const Wave& wave,
                  const TileSet& tiles,
                  std::uint64_t seed) override;
    bool propagate(Wave& wave,
                   const OverlapRules& rules,
                   int start_cell,
                   SolverStats& stats) override;
    void on_solve_begin() override;
    void on_solve_end() override;
    const char* backend_name() const override { return "kokkos"; }

private:
    bool we_initialised_ = false; // whether *we* called Kokkos::initialize
};

} // namespace wfc
