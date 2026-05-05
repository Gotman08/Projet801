#!/usr/bin/env bash
# Build Kokkos 4.4.01 (OpenMP backend) on Romeo x86_64 partition.
set -euo pipefail
cd ~/wfc801
romeo_load_x64cpu_env
spack load /56vu72q   # cmake@3.31.8
spack load /tpamt4u   # ninja@1.12.1
spack load gcc@14.2.0 2>/dev/null || true

KOKKOS_VERSION=4.4.01
mkdir -p external/kokkos
if [[ ! -d external/kokkos/src ]]; then
    git clone --depth 1 --branch "$KOKKOS_VERSION" \
        https://github.com/kokkos/kokkos.git external/kokkos/src
fi

cmake -S external/kokkos/src -B external/kokkos/build -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DKokkos_ENABLE_OPENMP=ON \
    -DKokkos_ENABLE_SERIAL=ON \
    -DCMAKE_INSTALL_PREFIX="$PWD/external/kokkos/install"

cmake --build external/kokkos/build -j
cmake --install external/kokkos/build

echo "=== Rebuild project with USE_KOKKOS=ON + USE_LTO=ON ==="
rm -rf build
cmake -B build -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DUSE_OMP=ON -DUSE_KOKKOS=ON -DUSE_LTO=ON \
    -DKokkos_ROOT="$PWD/external/kokkos/install"
cmake --build build -j 2>&1 | tail -10

echo "=== ctest with Kokkos ==="
ctest --test-dir build --output-on-failure 2>&1 | tail -20
