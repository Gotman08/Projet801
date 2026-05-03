#include "cli_common.hpp"
#include "wfc/solvers/WFCSolverKokkos.hpp"

#include <Kokkos_Core.hpp>

int main(int argc, char** argv) {
    // Kokkos::initialize consumes --kokkos-* flags and removes them
    // from argc/argv before our own parser sees them.
    Kokkos::initialize(argc, argv);
    int rc = 0;
    try {
        auto args = wfc::cli::parse(argc, argv);
        wfc::WFCSolverKokkos solver;
        wfc::cli::run(solver, args);
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
        rc = 1;
    }
    Kokkos::finalize();
    return rc;
}
