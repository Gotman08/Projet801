#!/usr/bin/env bash
# Build and install Kokkos (OpenMP backend) into ./external/kokkos/install.
# After running this, configure the project with:
#   cmake -B build -DUSE_KOKKOS=ON -DKokkos_ROOT=$PWD/external/kokkos/install
set -euo pipefail

KOKKOS_VERSION=${KOKKOS_VERSION:-4.4.01}
ROOT=$(pwd)
SRC=$ROOT/external/kokkos/src
BUILD=$ROOT/external/kokkos/build
INSTALL=$ROOT/external/kokkos/install

mkdir -p "$ROOT/external/kokkos"

if [[ ! -d "$SRC" ]]; then
    echo "fetching Kokkos $KOKKOS_VERSION"
    git clone --depth 1 --branch "$KOKKOS_VERSION" \
        https://github.com/kokkos/kokkos.git "$SRC"
fi

cmake -S "$SRC" -B "$BUILD" \
    -DCMAKE_BUILD_TYPE=Release \
    -DKokkos_ENABLE_OPENMP=ON \
    -DKokkos_ENABLE_SERIAL=ON \
    -DCMAKE_INSTALL_PREFIX="$INSTALL"

cmake --build "$BUILD" -j"$(nproc)"
cmake --install "$BUILD"

echo "Kokkos installed to $INSTALL"
echo "Now run: cmake -B build -DUSE_KOKKOS=ON -DKokkos_ROOT=$INSTALL"
