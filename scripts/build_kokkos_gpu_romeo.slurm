#!/usr/bin/env bash
# Build Kokkos+CUDA on Romeo armgpu and run tests.
#SBATCH --job-name=wfc-gpu-build
#SBATCH --account=r250127
#SBATCH --partition=instant
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=8
#SBATCH --mem=16G
#SBATCH --constraint=armgpu
#SBATCH --gpus-per-node=1
#SBATCH --time=00:55:00
#SBATCH --output=results/gpu_build_%j.out
#SBATCH --error=results/gpu_build_%j.err

set -euo pipefail
cd "$SLURM_SUBMIT_DIR"
mkdir -p results

romeo_load_armgpu_env
echo "=== environment ==="
hostname
nproc
nvidia-smi --query-gpu=name,memory.total --format=csv 2>&1 | head -5
echo ""

# Use exact spack hashes from `spack find --long`. armgpu env doesn't
# have gcc 14.2, only gcc 11.4 (which is what cmake/ninja were built
# against) or clang 19.1.
spack load /phhwuh3   # cmake@3.30.5
spack load /vabslno   # ninja@1.12.1
spack load /vziqyqt   # gcc@11.4.1 (one of three; nvcc just needs a host gcc)
spack load /iw66xwz   # cuda@12.9.0

which cmake gcc nvcc 2>&1
cmake --version | head -1
nvcc --version 2>&1 | tail -2
echo ""

# Build Kokkos with CUDA backend if not already done. We disable LTO
# because nvcc + LTO has known compatibility issues across compilers.
KOKKOS_INSTALL="$PWD/external/kokkos/install_cuda"
if [[ ! -d "$KOKKOS_INSTALL" ]]; then
    echo "=== building Kokkos+CUDA ==="
    rm -rf external/kokkos/build_cuda
    cmake -S external/kokkos/src -B external/kokkos/build_cuda -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DKokkos_ENABLE_CUDA=ON \
        -DKokkos_ENABLE_SERIAL=ON \
        -DKokkos_ARCH_HOPPER90=ON \
        -DCMAKE_CXX_COMPILER="$PWD/external/kokkos/src/bin/nvcc_wrapper" \
        -DCMAKE_INSTALL_PREFIX="$KOKKOS_INSTALL"
    cmake --build external/kokkos/build_cuda -j 8 2>&1 | tail -10
    cmake --install external/kokkos/build_cuda 2>&1 | tail -3
fi

echo ""
echo "=== building project with Kokkos+CUDA ==="
rm -rf build_cuda
cmake -B build_cuda -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DUSE_OMP=ON -DUSE_KOKKOS=ON \
    -DCMAKE_CXX_COMPILER="$PWD/external/kokkos/src/bin/nvcc_wrapper" \
    -DKokkos_ROOT="$KOKKOS_INSTALL" 2>&1 | tail -5
cmake --build build_cuda -j 8 2>&1 | tail -10

echo ""
echo "=== run tests ==="
ctest --test-dir build_cuda --output-on-failure 2>&1 | tail -20

echo ""
echo "=== quick wfc_kokkos run on GPU ==="
./build_cuda/wfc_kokkos samples/binary_5x5.txt --rows 64 --cols 64 -N 2 \
    --seed 42 2>&1 | tail -10

echo ""
echo "=== done ==="
date
