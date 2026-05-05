#!/usr/bin/env bash
# Renders the WFC-paper-style gallery (skyline, plant, rooms) used in
# docs/results.md. Each sample is run at N=3 for finer local motifs;
# parallel-attempts=8 absorbs the higher contradiction rate of N=3
# without retry overhead in wallclock.
#
# Usage : ./scripts/render_gallery.sh [build_dir]
# Default build_dir : ./build (must contain wfc_omp).

set -euo pipefail

BUILD="${1:-build}"
WFC="$BUILD/wfc_omp"
[[ -x "$WFC" || -x "$WFC.exe" ]] || {
    echo "ERROR : $WFC not found (compile with -DUSE_OMP=ON first)"; exit 1
}

OUT_DIR="docs/figures/results/gallery"
mkdir -p "$OUT_DIR"

declare -A SIZES=(
    [rooms]=48
    [plant]=36
    [skyline]=36
)

for sample in rooms plant skyline; do
    size="${SIZES[$sample]}"
    for seed in 1 7 42; do
        out="$OUT_DIR/${sample}_seed${seed}.png"
        echo "[render] $sample @ ${size}x${size} seed=$seed -> $out"
        "$WFC" "samples/${sample}.txt" \
            --rows "$size" --cols "$size" \
            -N 3 --seed "$seed" \
            --attempts 40 --parallel-attempts 8 --threads 8 \
            --scale 8 \
            --png "$out" \
            > /dev/null
    done
done

# Render the input thumbnails too (one per sample). render_input.py
# renders a sample text file directly into a small PNG without going
# through the solver.
INPUTS_DIR="docs/figures/results/inputs"
mkdir -p "$INPUTS_DIR"
for sample in rooms plant skyline; do
    echo "[render] $sample input"
    python3 scripts/render_input.py "samples/${sample}.txt" \
        > /dev/null 2>&1 || python scripts/render_input.py "samples/${sample}.txt" \
        > /dev/null 2>&1 || true
done

echo "[gallery] done"
