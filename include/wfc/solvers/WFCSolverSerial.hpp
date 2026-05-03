#pragma once

#include "wfc/internal/WFCSolverBase.hpp"

namespace wfc {

class WFCSolverSerial : public WFCSolverBase {
protected:
    int pick_cell(const Wave& wave,
                  const TileSet& tiles,
                  std::uint64_t seed) override;
    bool propagate(Wave& wave,
                   const OverlapRules& rules,
                   int start_cell,
                   SolverStats& stats) override;
    const char* backend_name() const override { return "serial"; }
};

} // namespace wfc
