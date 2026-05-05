#pragma once

// Helpers shared by every WFC backend (serial / OMP / Kokkos). Pulled out
// here to keep a single source of truth for entropy, sampling, and output
// reconstruction, the three solvers used to duplicate ~150 lines of these.

#include "wfc/Grid.hpp"
#include "wfc/OverlapRules.hpp"
#include "wfc/TileSet.hpp"
#include "wfc/Wave.hpp"
#include "wfc/Bitset.hpp"

#include <cstdint>
#include <random>
#include <vector>

namespace wfc {

// Mixers from SplitMix64 (D. Stafford, 2013). Picked because the algorithm
// has full avalanche (every output bit depends on every input bit), is
// stateless (no sequence to maintain, perfect for parallel hashing of
// cell ids), and runs in three multiplications without branches. Used here
// only to derive a tiny tie-break jitter; the main RNG remains mt19937_64.
inline constexpr std::uint64_t kSplitMix64Mul1 = 0xFF51AFD7ED558CCDULL;
inline constexpr std::uint64_t kSplitMix64Mul2 = 0xC4CEB9FE1A85EC53ULL;

// Reciprocal of the golden ratio (Knuth), a high-quality additive offset
// for deriving distinct seeds from a base + step.
inline constexpr std::uint64_t kGoldenRatio64 = 0x9E3779B97F4A7C15ULL;

// (entropy_value, cell_index) pair returned by min-entropy reductions.
struct MinEntropyResult {
    double value = 1e300;
    int cell = -1;
};

// Tiny deterministic per-cell noise (in [0, 1e-6)) used only to break
// entropy ties. Identical across all backends so that "same seed → same
// output" holds bit-for-bit between serial and parallel runs.
inline double cell_jitter(int cell, std::uint64_t seed) {
    std::uint64_t x = seed ^ static_cast<std::uint64_t>(cell);
    x ^= x >> 33;  x *= kSplitMix64Mul1;
    x ^= x >> 33;  x *= kSplitMix64Mul2;
    x ^= x >> 33;
    return (x & 0xFFFFFFu) * (1e-6 / static_cast<double>(0x1000000));
}

// Derive the seed used for the n-th attempt (0-based) from a base seed.
inline std::uint64_t attempt_seed(std::uint64_t base, int attempt_index) {
    return base + kGoldenRatio64 * static_cast<std::uint64_t>(attempt_index);
}

// Shannon-like entropy weighted by tile frequencies. The README mentions
// using frequencies for selection; doing so here produces more visually
// natural patterns than uniform sampling.
double weighted_entropy(ConstBitsetView cell_wave,
                        const std::vector<std::uint32_t>& freq);

// Pick a tile id from `cell_wave` with probability proportional to
// frequency. Returns -1 only on numeric edge cases (no candidate set).
int weighted_pick(ConstBitsetView cell_wave,
                  const std::vector<std::uint32_t>& freq,
                  std::mt19937_64& rng);

// Serial scan: cell with minimum (entropy + jitter), ignoring already-decided
// cells. Used by the serial and Kokkos solvers; the OMP solver has its own
// task-based reduction.
MinEntropyResult serial_min_entropy(const Wave& wave,
                                    const std::vector<std::uint32_t>& freq,
                                    std::uint64_t seed);

// Single-thread BFS propagation. Pulled out of WFCSolverSerial so the
// parallel-attempts orchestration can reuse it without recursing into a
// backend's own (potentially nested) parallel propagate.
bool serial_propagate(Wave& wave,
                      const OverlapRules& rules,
                      int start_cell,
                      int& propagations);

struct SolverStats; // forward decl from WFCSolver.hpp

// One full WFC attempt, end to end, single-threaded. Used by the
// parallel-attempts batch orchestrator: K of these run concurrently on
// independent waves; the lowest-indexed success wins.
bool serial_run_attempt(Wave& wave,
                        const TileSet& tiles,
                        const OverlapRules& rules,
                        std::uint64_t seed,
                        std::mt19937_64& rng,
                        SolverStats& stats);

// Backtracking variant of serial_run_attempt. Each collapse pushes a
// frame (cell, remaining-choices, wave-snapshot) onto a stack ; on
// contradiction the top frame is popped and the next choice is tried.
// Returns true if any consistent solution exists in the search tree
// rooted at the initial wave. Choices are ordered by descending
// frequency (most frequent tile tried first) for stable behavior.
bool serial_run_attempt_backtrack(Wave& wave,
                                  const TileSet& tiles,
                                  const OverlapRules& rules,
                                  std::uint64_t seed,
                                  SolverStats& stats);

// Reconstruct the output grid from a fully collapsed wave: pixel (r, c)
// receives the top-left value of the tile selected at (r, c).
Grid build_output(const Wave& wave, const TileSet& tiles);

} // namespace wfc
