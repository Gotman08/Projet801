#!/usr/bin/env bash
# Slurm job script for the WFC benchmark sweep on Romeo (HPC AMD EPYC 9654).
#
# Submission:
#   sbatch scripts/romeo_submit.sh
#
# The job reserves one full node (192 cores), pins OMP threads to cores,
# runs three sweeps, and concatenates everything into results/romeo_full.csv.
# Designed to fit within the `short` partition's 1-day limit (we ask 4 h).

#SBATCH --job-name=wfc-bench
#SBATCH --account=r250127
#SBATCH --partition=short
#SBATCH --exclusive
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=192
#SBATCH --mem=128G
#SBATCH --constraint=x64cpu
#SBATCH --time=04:00:00
#SBATCH --output=results/romeo_%j.out
#SBATCH --error=results/romeo_%j.err

set -euo pipefail

cd "$SLURM_SUBMIT_DIR"
mkdir -p results

# OpenMP placement: keep threads close to each other on the socket they
# were spawned on. `cores` = bind to a hardware core, `close` = pack
# threads on neighbouring cores before spilling to the next socket.
export OMP_PROC_BIND=close
export OMP_PLACES=cores

echo "=== node info ==="
hostname
date
echo "Slurm job $SLURM_JOB_ID on $SLURM_NODELIST"
echo "CPUs per task: $SLURM_CPUS_PER_TASK"
nproc
lscpu | grep -E "Model name|Socket|CPU\(s\)|NUMA"
echo ""

BENCH=./build/wfc_benchmark
if [[ ! -x "$BENCH" ]]; then
    echo "ERROR: $BENCH not found. Build first: cmake --build build -j"
    exit 1
fi

# All sweeps append to the same CSV. The first one writes the header,
# the rest use --no-header.
OUT=results/romeo_full.csv

THREADS_FULL="1,2,4,8,16,32,64,96,192"
THREADS_MEDIUM="1,4,16,64,192"
THREADS_SMALL="1,8,32,96,192"

echo "=== sweep 1: binary L=11 ==="
"$BENCH" --sample samples/binary_5x5.txt \
    --N 2 --label "binary_L11" \
    --sizes "32,64,128,256,512" \
    --threads "$THREADS_FULL" \
    --repeats 5 --seed 42 \
    -o "$OUT"

echo ""
echo "=== sweep 2: multivalue terrain N=2 (L~33) ==="
"$BENCH" --sample samples/multivalue_terrain.txt \
    --N 2 --label "terrain_N2" \
    --sizes "32,64,128,256" \
    --threads "$THREADS_MEDIUM" \
    --repeats 3 --seed 7 --no-header \
    -o "$OUT"

echo ""
echo "=== sweep 3: multivalue smooth N=3 (L=12) ==="
"$BENCH" --sample samples/multivalue_smooth.txt \
    --N 3 --label "smooth_N3" \
    --sizes "32,64,128" \
    --threads "$THREADS_SMALL" \
    --repeats 3 --seed 1 --no-header \
    -o "$OUT"

echo ""
echo "=== job done ==="
date
wc -l "$OUT"
echo "Results in $OUT"
