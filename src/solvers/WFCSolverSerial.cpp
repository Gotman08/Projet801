#include "wfc/solvers/WFCSolverSerial.hpp"

#include "wfc/internal/SolverCommon.hpp"

namespace wfc {

int WFCSolverSerial::pick_cell(const Wave& wave,
                               const TileSet& tiles,
                               std::uint64_t seed) {
    return serial_min_entropy(wave, tiles.frequencies(), seed).cell;
}

bool WFCSolverSerial::propagate(Wave& wave,
                                const OverlapRules& rules,
                                int start_cell,
                                SolverStats& stats) {
    return serial_propagate(wave, rules, start_cell, stats.propagations);
}

} // namespace wfc
