#pragma once

#include "wfc/internal/WFCSolverBase.hpp"

namespace wfc {

class WFCSolverOMP : public WFCSolverBase {
public:
    explicit WFCSolverOMP(int num_threads = 0) : num_threads_(num_threads) {}

    int num_threads() const { return num_threads_; }
    void set_num_threads(int n) { num_threads_ = n; }

protected:
    int pick_cell(const Wave& wave,
                  const TileSet& tiles,
                  std::uint64_t seed) override;
    bool propagate(Wave& wave,
                   const OverlapRules& rules,
                   int start_cell,
                   SolverStats& stats) override;
    void on_solve_begin() override;
    const char* backend_name() const override { return "omp"; }

private:
    int num_threads_ = 0; // 0 -> use OMP default
};

} // namespace wfc
