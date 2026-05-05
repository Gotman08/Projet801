#include "cli_common.hpp"
#include "wfc/solvers/WFCSolverKokkos.hpp"

#include <Kokkos_Core.hpp>

int main(int argc, char** argv) {
    // Kokkos::initialize consumes --kokkos-* flags and removes them
    // from argc/argv before our own parser sees them.
    // ScopeGuard ensures Kokkos::finalize() runs on any path out (including
    // exceptions thrown while formatting an error message).
    Kokkos::ScopeGuard kokkos_guard(argc, argv);
    try {
        auto args = wfc::cli::parse(argc, argv);
        wfc::WFCSolverKokkos solver;
        wfc::cli::run(solver, args);
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }
}
