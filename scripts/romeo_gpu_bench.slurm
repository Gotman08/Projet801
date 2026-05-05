#!/usr/bin/env bash
# GPU benchmark on Romeo armgpu (GH200).
#SBATCH --job-name=wfc-gpu-bench
#SBATCH --account=r250127
#SBATCH --partition=instant
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=8
#SBATCH --mem=16G
#SBATCH --constraint=armgpu
#SBATCH --gpus-per-node=1
#SBATCH --time=00:25:00
#SBATCH --output=results/gpu_bench_%j.out
#SBATCH --error=results/gpu_bench_%j.err

set -euo pipefail
cd "$SLURM_SUBMIT_DIR"
mkdir -p results

romeo_load_armgpu_env
spack load /phhwuh3
spack load /vabslno
spack load /vziqyqt
spack load /iw66xwz

export OMP_PROC_BIND=close
export OMP_PLACES=cores

echo "=== environment ==="
hostname
nvidia-smi --query-gpu=name,memory.total --format=csv 2>&1 | head -3
echo ""

OUT=results/gpu_bench_${SLURM_JOB_ID}.csv
BENCH=./build_cuda/wfc_benchmark
[[ -x "$BENCH" ]] || { echo "ERROR: $BENCH missing, run scripts/build_kokkos_gpu_romeo.slurm first"; exit 1; }

# Multiple sizes, GPU only (kokkos with CUDA backend means kokkos calls
# launch CUDA kernels). serial/omp use CPU as before.
"$BENCH" --sample samples/binary_5x5.txt \
    --N 2 --label "binary_gpu" \
    --sizes "32,64,128,256" \
    --threads "1,4,16" \
    --backends "serial,omp,kokkos" \
    --repeats 3 --seed 42 \
    -o "$OUT"

"$BENCH" --sample samples/multivalue_terrain.txt \
    --N 2 --label "terrain_gpu" \
    --sizes "32,64,128" \
    --threads "1,4,16" \
    --backends "serial,omp,kokkos" \
    --repeats 3 --seed 7 --no-header \
    -o "$OUT"

echo ""
echo "=== done ==="
date
wc -l "$OUT"
