#include "cli_common.hpp"
#include "wfc/solvers/WFCSolverSerial.hpp"

int main(int argc, char** argv) {
    try {
        auto args = wfc::cli::parse(argc, argv);
        wfc::WFCSolverSerial solver;
        wfc::cli::run(solver, args);
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }
}
