#!/usr/bin/env bash
#SBATCH --job-name=wfc-full
#SBATCH --account=r250127
#SBATCH --partition=short
#SBATCH --exclusive
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=192
#SBATCH --mem=64G
#SBATCH --constraint=x64cpu
#SBATCH --time=01:00:00
#SBATCH --output=results/full_%j.out
#SBATCH --error=results/full_%j.err

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
lscpu | grep -E "Model name|Socket|CPU\(s\)|NUMA" | head -8
echo ""

BENCH=./build/wfc_benchmark
[[ -x "$BENCH" ]] || { echo "ERROR: $BENCH missing"; exit 1; }

OUT=results/full_${SLURM_JOB_ID}.csv
THREADS_BENCH="1,2,4,8,16,32,64,96,192"

echo "=== sweep 1: binary L=11, sizes 32-256, all backends ==="
"$BENCH" --sample samples/binary_5x5.txt \
    --N 2 --label "binary_L11" \
    --sizes "32,64,128,256" \
    --threads "$THREADS_BENCH" \
    --backends "serial,omp,kokkos" \
    --repeats 3 --seed 42 \
    -o "$OUT"

echo ""
echo "=== sweep 2: multivalue terrain L~33, sizes 32-128 ==="
"$BENCH" --sample samples/multivalue_terrain.txt \
    --N 2 --label "terrain_L33" \
    --sizes "32,64,128" \
    --threads "$THREADS_BENCH" \
    --backends "serial,omp,kokkos" \
    --repeats 3 --seed 7 --no-header \
    -o "$OUT"

echo ""
echo "=== sweep 3: multivalue smooth L=12, N=3, sizes 32-128 ==="
"$BENCH" --sample samples/multivalue_smooth.txt \
    --N 3 --label "smooth_N3" \
    --sizes "32,64,128" \
    --threads "1,4,16,64,192" \
    --backends "serial,omp,kokkos" \
    --repeats 3 --seed 1 --no-header \
    -o "$OUT"

echo ""
echo "=== done ==="
date
wc -l "$OUT"
