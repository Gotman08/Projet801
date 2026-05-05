#!/usr/bin/env bash
# Targeted A/B test: did the frontier-threshold optim help OMP at high
# thread counts on 256×256 ?
#
# We compare the WFCSolverOMP.cpp BEFORE the optim (just the smoke run
# 542693 / sweep 1 of 543692) to AFTER. Same configurations, same seed.
#
# Sweep: 256×256 binary with thread counts that previously regressed
# heavily (32, 64, 96, 192) plus the previous peak (16) for sanity.
#
#SBATCH --job-name=wfc-optim
#SBATCH --account=r250127
#SBATCH --partition=instant
#SBATCH --exclusive
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=192
#SBATCH --mem=64G
#SBATCH --constraint=x64cpu
#SBATCH --time=00:50:00
#SBATCH --output=results/optim_%j.out
#SBATCH --error=results/optim_%j.err

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
nproc
echo ""

OUT=results/optim_${SLURM_JOB_ID}.csv

# Larger sweep: includes 128×128 and 256×256, threads where regression
# was severe before. Same seed/repeats as 543692.
./build/wfc_benchmark --sample samples/binary_5x5.txt \
    --N 2 --label "binary_optim" \
    --sizes "128,256" \
    --threads "1,2,4,8,16,32,64,96,192" \
    --backends "serial,omp" \
    --repeats 3 --seed 42 \
    -o "$OUT"

echo ""
echo "=== done ==="
date
wc -l "$OUT"
