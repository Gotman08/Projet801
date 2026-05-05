#!/usr/bin/env bash
# Targeted complement to job 543692:
#   - serial 256×256 (missing, we only had omp/kokkos)
#   - sweep 2: terrain_L33 (missed entirely)
#   - sweep 3: smooth_N3 (missed entirely)
#
# Threads sweep limited to 1, 2, 4, 8, 16, 32 (skip 64/96/192 where
# regression is already documented in 543692). Kokkos now runs once per
# (size, repeat) thanks to the bug fix in apps/benchmark.cpp, so the
# whole sweep should fit easily in the `instant` 1-hour partition.

#SBATCH --job-name=wfc-cpl
#SBATCH --account=r250127
#SBATCH --partition=instant
#SBATCH --exclusive
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=192
#SBATCH --mem=64G
#SBATCH --constraint=x64cpu
#SBATCH --time=00:55:00
#SBATCH --output=results/cpl_%j.out
#SBATCH --error=results/cpl_%j.err

set -euo pipefail
cd "$SLURM_SUBMIT_DIR"
mkdir -p results

romeo_load_x64cpu_env
spack load /56vu72q
spack load /tpamt4u
spack load gcc@14.2.0

export OMP_PROC_BIND=close
export OMP_PLACES=cores

echo "=== node info ==="
hostname
date
echo "Slurm job $SLURM_JOB_ID on $SLURM_NODELIST"
nproc
echo ""

BENCH=./build/wfc_benchmark
[[ -x "$BENCH" ]] || { echo "ERROR: $BENCH missing"; exit 1; }

OUT=results/cpl_${SLURM_JOB_ID}.csv
THREADS="1,2,4,8,16,32"

echo "=== sweep 1b: serial 256x256 (complement to 543692) ==="
"$BENCH" --sample samples/binary_5x5.txt \
    --N 2 --label "binary_L11" \
    --sizes "256" \
    --threads "1" \
    --backends "serial" \
    --repeats 3 --seed 42 \
    -o "$OUT"

echo ""
echo "=== sweep 2: multivalue terrain L=33, N=2 ==="
"$BENCH" --sample samples/multivalue_terrain.txt \
    --N 2 --label "terrain_L33" \
    --sizes "32,64,128" \
    --threads "$THREADS" \
    --backends "serial,omp,kokkos" \
    --repeats 3 --seed 7 --no-header \
    -o "$OUT"

echo ""
echo "=== sweep 3: multivalue smooth, N=3 ==="
"$BENCH" --sample samples/multivalue_smooth.txt \
    --N 3 --label "smooth_N3" \
    --sizes "32,64,128" \
    --threads "$THREADS" \
    --backends "serial,omp,kokkos" \
    --repeats 3 --seed 1 --no-header \
    -o "$OUT"

echo ""
echo "=== done ==="
date
wc -l "$OUT"
