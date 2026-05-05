#!/usr/bin/env bash
set -euo pipefail
cd ~/wfc801
romeo_load_x64cpu_env
spack load /56vu72q   # cmake@3.31.8 %aocc
spack load /tpamt4u   # ninja@1.12.1 %aocc@4.2.0
spack load gcc@14.2.0 2>/dev/null || true
echo "=== toolchain ==="
gcc --version | head -1
cmake --version | head -1
ninja --version
echo ""
echo "=== configure ==="
rm -rf build
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DUSE_OMP=ON
echo ""
echo "=== build ==="
cmake --build build -j 2>&1 | tail -10
echo ""
echo "=== ctest ==="
ctest --test-dir build --output-on-failure 2>&1 | tail -15
