#!/usr/bin/env bash
# Quick smoke test on Romeo before launching the long full sweep.
# Runs ~30 small configs in <5 minutes to verify build, scaling, and
# determinism on the actual hardware.

#SBATCH --job-name=wfc-smoke
#SBATCH --account=r250127
#SBATCH --partition=short
#SBATCH --exclusive
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=192
#SBATCH --mem=64G
#SBATCH --constraint=x64cpu
#SBATCH --time=00:15:00
#SBATCH --output=results/romeo_smoke_%j.out
#SBATCH --error=results/romeo_smoke_%j.err

set -euo pipefail

cd "$SLURM_SUBMIT_DIR"
mkdir -p results

export OMP_PROC_BIND=close
export OMP_PLACES=cores

echo "=== node info ==="
hostname
nproc
echo ""

BENCH=./build/wfc_benchmark
[[ -x "$BENCH" ]] || { echo "build first"; exit 1; }

OUT=results/romeo_smoke.csv

# Determinism check: serial vs OMP at a few thread counts must match.
echo "=== determinism check ==="
./build/wfc_serial samples/binary_5x5.txt --rows 64 --cols 64 -N 2 \
    --seed 99 --out /tmp/s.txt > /dev/null
for t in 1 4 16 96 192; do
    OMP_NUM_THREADS=$t ./build/wfc_omp samples/binary_5x5.txt \
        --rows 64 --cols 64 -N 2 --seed 99 --threads $t \
        --out /tmp/o_$t.txt > /dev/null
    if diff -q /tmp/s.txt /tmp/o_$t.txt > /dev/null; then
        echo "threads=$t: OK (deterministic)"
    else
        echo "threads=$t: MISMATCH"
    fi
done

echo ""
echo "=== quick sweep ==="
"$BENCH" --sample samples/binary_5x5.txt \
    --N 2 --label "smoke" \
    --sizes "32,64,128" \
    --threads "1,4,16,64,192" \
    --repeats 2 --seed 42 \
    -o "$OUT"

echo ""
echo "=== smoke test done ==="
wc -l "$OUT"
