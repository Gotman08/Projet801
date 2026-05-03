#!/usr/bin/env bash
# Follow-up job: cover the gaps left by the timed-out main sweep.
#  - multivalue_terrain (N=2) + multivalue_smooth (N=3) sweeps
#  - finish 512x512 binary at higher thread counts (16, 32, 64)

#SBATCH --job-name=wfc-followup
#SBATCH --account=r250127
#SBATCH --partition=short
#SBATCH --exclusive
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=192
#SBATCH --mem=128G
#SBATCH --constraint=x64cpu
#SBATCH --time=01:30:00
#SBATCH --output=results/romeo_followup_%j.out
#SBATCH --error=results/romeo_followup_%j.err

set -euo pipefail
cd "$SLURM_SUBMIT_DIR"
mkdir -p results

export OMP_PROC_BIND=close
export OMP_PLACES=cores

echo "=== node ==="; hostname; date

BENCH=./build/wfc_benchmark
[[ -x "$BENCH" ]] || { echo "build first"; exit 1; }
OUT=results/romeo_followup.csv

echo "=== sweep: multivalue_terrain N=2 (L~33) ==="
"$BENCH" --sample samples/multivalue_terrain.txt \
    --N 2 --label "terrain_N2" \
    --sizes "32,64,128,256" \
    --threads "1,2,4,8,16,32,64" \
    --repeats 3 --seed 7 \
    -o "$OUT"

echo "=== sweep: multivalue_smooth N=3 (L=12) ==="
"$BENCH" --sample samples/multivalue_smooth.txt \
    --N 3 --label "smooth_N3" \
    --sizes "32,64,128" \
    --threads "1,2,4,8,16,32" \
    --repeats 3 --seed 1 --no-header \
    -o "$OUT"

echo "=== fill: binary L=11 at 512x512 with 16,32,64 threads ==="
"$BENCH" --sample samples/binary_5x5.txt \
    --N 2 --label "binary_L11" \
    --sizes "512" \
    --threads "16,32,64" \
    --backends "omp" \
    --repeats 3 --seed 42 --no-header \
    -o "$OUT"

date
wc -l "$OUT"
