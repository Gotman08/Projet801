#!/usr/bin/env bash
# Benchmark just the configs where the OMP optims (NUMA first-touch +
# coalesced atomic + per-thread frontier) are expected to matter.
# Compares against the previous "pass3" baseline already saved on the
# host as results/romeo_combined.csv.

#SBATCH --job-name=wfc-optims
#SBATCH --account=r250127
#SBATCH --partition=short
#SBATCH --exclusive
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=192
#SBATCH --mem=128G
#SBATCH --constraint=x64cpu
#SBATCH --time=01:30:00
#SBATCH --output=results/romeo_optims_%j.out
#SBATCH --error=results/romeo_optims_%j.err

set -euo pipefail
cd "$SLURM_SUBMIT_DIR"
mkdir -p results

# Same OMP placement as the baseline so we compare apples to apples.
export OMP_PROC_BIND=close
export OMP_PLACES=cores

echo "=== node ==="; hostname; date

BENCH=./build/wfc_benchmark
[[ -x "$BENCH" ]] || { echo "build first"; exit 1; }
OUT=results/romeo_optims.csv

# Focus on the post-peak regime (where overhead dominates and our
# optims should help most), plus the peak (to verify we don't regress).
THREADS_TIGHT="1,4,8,16,32,64,96,192"

echo "=== sweep: binary L=11 (the main reference) ==="
"$BENCH" --sample samples/binary_5x5.txt \
    --N 2 --label "binary_L11_optims" \
    --sizes "64,128,256" \
    --threads "$THREADS_TIGHT" \
    --repeats 5 --seed 42 \
    -o "$OUT"

echo "=== sweep: terrain N=2 (multi-value) ==="
"$BENCH" --sample samples/multivalue_terrain.txt \
    --N 2 --label "terrain_N2_optims" \
    --sizes "64,128,256" \
    --threads "1,4,8,16,32,64" \
    --repeats 3 --seed 7 --no-header \
    -o "$OUT"

date
wc -l "$OUT"
