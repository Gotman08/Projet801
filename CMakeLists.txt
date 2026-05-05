cmake_minimum_required(VERSION 3.16)
project(wfc CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(USE_OMP "Build the OpenMP-parallel solver" ON)
option(USE_KOKKOS "Build the Kokkos-parallel solver" OFF)
option(BUILD_TESTING "Build unit tests" ON)

# Optional UE5-facing dungeon CLI. Off by default so the HPC build pipeline
# (serial / OMP / Kokkos benchmarks) is unaffected. The dungeon target uses
# only WFCSolverSerial via the public solver interface, no impact on the
# parallel hot path. See docs/ue5_integration.md.
option(BUILD_DUNGEON "Build the wfc_dungeon CLI for the UE5 plugin" OFF)

# Link-Time Optimization. Verified +3.9% speedup vs no-LTO on the binary
# 128x128 N=2 workload (median of 11 alternating runs). Doubles link time
# from ~1s to ~3s. Off by default to keep the dev edit-build-test loop
# fast; enable for release builds. Disabled with sanitizers (incompatible).
option(USE_LTO "Enable Link-Time Optimization (Release only)" OFF)

# Sanitizer options. Mutually exclusive (TSAN does not coexist with ASAN).
option(USE_TSAN "Build with ThreadSanitizer (race detection)" OFF)
option(USE_ASAN "Build with AddressSanitizer + UndefinedBehaviorSanitizer" OFF)

if(USE_LTO AND NOT (USE_TSAN OR USE_ASAN))
  include(CheckIPOSupported)
  check_ipo_supported(RESULT ipo_ok OUTPUT ipo_msg)
  if(ipo_ok)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)
    message(STATUS "Link-Time Optimization: ON")
  else()
    message(WARNING "USE_LTO=ON but compiler does not support IPO: ${ipo_msg}")
  endif()
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
  # -O3 -march=native only in Release, so Debug builds remain debuggable.
  add_compile_options($<$<CONFIG:Release>:-O3>
                     $<$<CONFIG:Release>:-march=native>)
  add_compile_options($<$<CONFIG:Debug>:-O0>
                     $<$<CONFIG:Debug>:-g3>)

  if(USE_TSAN)
    add_compile_options(-fsanitize=thread -g)
    add_link_options(-fsanitize=thread)
  elseif(USE_ASAN)
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -g)
    add_link_options(-fsanitize=address,undefined)
  endif()
elseif(CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
  add_compile_options(/W4 $<$<CONFIG:Release>:/O2>)
endif()

add_library(wfc_core
  src/Grid.cpp
  src/TileSet.cpp
  src/OverlapRules.cpp
  src/Wave.cpp
  src/GridIO.cpp
  src/internal/SolverCommon.cpp
  src/internal/WFCSolverBase.cpp
  src/solvers/WFCSolverSerial.cpp
)
target_include_directories(wfc_core
  PUBLIC  ${CMAKE_SOURCE_DIR}/include
  PRIVATE ${CMAKE_SOURCE_DIR}/third_party
)

# stb_image_write.h triggers a flood of harmless missing-field-initializer
# warnings under -Wextra. Suppress only for the file that compiles it.
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  set_source_files_properties(src/GridIO.cpp PROPERTIES
    COMPILE_OPTIONS "-Wno-missing-field-initializers")
endif()

add_executable(wfc_serial apps/wfc_serial.cpp)
target_link_libraries(wfc_serial PRIVATE wfc_core)

# Optional UE5 demo target. Strictly additive: skipped by default, never
# linked into the benchmark or test suites. Built only when the user
# explicitly requests -DBUILD_DUNGEON=ON.
if(BUILD_DUNGEON)
  add_executable(wfc_dungeon apps/wfc_dungeon.cpp)
  target_link_libraries(wfc_dungeon PRIVATE wfc_core)
endif()

if(USE_OMP)
  find_package(OpenMP REQUIRED)
  add_library(wfc_omp_lib src/solvers/WFCSolverOMP.cpp)
  target_include_directories(wfc_omp_lib PUBLIC ${CMAKE_SOURCE_DIR}/include)
  target_link_libraries(wfc_omp_lib PUBLIC wfc_core OpenMP::OpenMP_CXX)

  # Make wfc_core able to use OpenMP for parallel rule construction.
  target_link_libraries(wfc_core PUBLIC OpenMP::OpenMP_CXX)
  target_compile_definitions(wfc_core PUBLIC WFC_CORE_HAS_OMP=1)

  add_executable(wfc_omp apps/wfc_omp.cpp)
  target_link_libraries(wfc_omp PRIVATE wfc_omp_lib)
endif()

if(USE_KOKKOS)
  find_package(Kokkos REQUIRED)
  add_library(wfc_kokkos_lib src/solvers/WFCSolverKokkos.cpp)
  target_include_directories(wfc_kokkos_lib PUBLIC ${CMAKE_SOURCE_DIR}/include)
  target_link_libraries(wfc_kokkos_lib PUBLIC wfc_core Kokkos::kokkos)

  add_executable(wfc_kokkos apps/wfc_kokkos.cpp)
  target_link_libraries(wfc_kokkos PRIVATE wfc_kokkos_lib)
endif()

add_executable(wfc_benchmark apps/benchmark.cpp)
target_link_libraries(wfc_benchmark PRIVATE wfc_core)
if(USE_OMP)
  target_link_libraries(wfc_benchmark PRIVATE wfc_omp_lib)
  target_compile_definitions(wfc_benchmark PRIVATE WFC_HAS_OMP=1)
endif()
if(USE_KOKKOS)
  target_link_libraries(wfc_benchmark PRIVATE wfc_kokkos_lib)
  target_compile_definitions(wfc_benchmark PRIVATE WFC_HAS_KOKKOS=1)
endif()

if(BUILD_TESTING)
  enable_testing()
  add_executable(test_bitset            tests/test_bitset.cpp)
  add_executable(test_grid              tests/test_grid.cpp)
  add_executable(test_grid_io           tests/test_grid_io.cpp)
  add_executable(test_tileset           tests/test_tileset.cpp)
  add_executable(test_overlap           tests/test_overlap.cpp)
  add_executable(test_wave              tests/test_wave.cpp)
  add_executable(test_solver_common     tests/test_solver_common.cpp)
  add_executable(test_solver            tests/test_solver.cpp)
  add_executable(test_edge_cases        tests/test_edge_cases.cpp)
  add_executable(test_parallel_attempts tests/test_parallel_attempts.cpp)
  add_executable(test_symmetries        tests/test_symmetries.cpp)
  add_executable(test_backtrack         tests/test_backtrack.cpp)
  foreach(t test_bitset test_grid test_grid_io test_tileset test_overlap
            test_wave test_solver_common test_solver test_edge_cases
            test_parallel_attempts test_symmetries test_backtrack)
    target_link_libraries(${t} PRIVATE wfc_core)
    add_test(NAME ${t} COMMAND ${t})
  endforeach()
  if(USE_OMP)
    target_link_libraries(test_parallel_attempts PRIVATE wfc_omp_lib)
    target_compile_definitions(test_parallel_attempts PRIVATE WFC_TEST_HAS_OMP=1)
  endif()

  if(USE_OMP)
    add_executable(test_solver_omp tests/test_solver_omp.cpp)
    target_link_libraries(test_solver_omp PRIVATE wfc_omp_lib)
    add_test(NAME test_solver_omp COMMAND test_solver_omp)
  endif()

  if(USE_KOKKOS)
    add_executable(test_solver_kokkos tests/test_solver_kokkos.cpp)
    target_link_libraries(test_solver_kokkos PRIVATE wfc_kokkos_lib)
    add_test(NAME test_solver_kokkos COMMAND test_solver_kokkos)

    add_executable(test_kokkos_autoinit tests/test_kokkos_autoinit.cpp)
    target_link_libraries(test_kokkos_autoinit PRIVATE wfc_kokkos_lib)
    add_test(NAME test_kokkos_autoinit COMMAND test_kokkos_autoinit)
  endif()
endif()
