#!/usr/bin/env bash
# Run the WFC benchmark sweep and produce results/benchmark.csv.
#
# Usage: ./scripts/run_benchmark.sh [build_dir] [sample.txt]
set -euo pipefail

BUILD_DIR=${1:-build}
SAMPLE=${2:-samples/binary_5x5.txt}
OUT=results/benchmark.csv

if [[ ! -x "$BUILD_DIR/wfc_benchmark" ]]; then
    echo "wfc_benchmark not found in $BUILD_DIR. Build first:"
    echo "  cmake -B $BUILD_DIR -DCMAKE_BUILD_TYPE=Release"
    echo "  cmake --build $BUILD_DIR -j"
    exit 1
fi

mkdir -p results

echo "running benchmark on $SAMPLE -> $OUT"
"$BUILD_DIR/wfc_benchmark" "$SAMPLE" -o "$OUT"

echo "done. preview:"
head -1 "$OUT"
tail -n +2 "$OUT" | head -n 20
