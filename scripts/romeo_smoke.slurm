#!/usr/bin/env bash
#SBATCH --job-name=wfc-smoke
#SBATCH --account=r250127
#SBATCH --partition=instant
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=32
#SBATCH --mem=8G
#SBATCH --constraint=x64cpu
#SBATCH --time=00:15:00
#SBATCH --output=results/smoke_%j.out
#SBATCH --error=results/smoke_%j.err

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
echo "CPUs per task: $SLURM_CPUS_PER_TASK"
nproc
lscpu | grep -E "Model name|Socket|CPU\(s\)|NUMA" | head -8
echo ""

BENCH=./build/wfc_benchmark
[[ -x "$BENCH" ]] || { echo "ERROR: $BENCH missing"; exit 1; }

echo "=== sweep: binary L=11, sizes 32/64/128/256, threads 1/4/16/32 ==="
"$BENCH" --sample samples/binary_5x5.txt \
    --N 2 --label "smoke_binary" \
    --sizes "32,64,128,256" \
    --threads "1,4,16,32" \
    --repeats 3 --seed 42 \
    -o results/smoke_${SLURM_JOB_ID}.csv

echo ""
echo "=== done ==="
date
wc -l results/smoke_${SLURM_JOB_ID}.csv
