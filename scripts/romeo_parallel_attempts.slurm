#!/usr/bin/env bash
# A/B test: --parallel-attempts on tightly-constrained workloads.
# terrain_N3 has a low per-attempt success rate (~10%), so K parallel
# attempts give a near-K× wallclock win until the success rate × K
# saturates. We sweep K = {1, 4, 8, 16, 32} on a fixed batch of seeds.
#
# Output is a small text log (wallclock + winning_attempt for each
# config), not a CSV — the structure isn't useful for plotting.
#
#SBATCH --job-name=wfc-pa
#SBATCH --account=r250127
#SBATCH --partition=instant
#SBATCH --exclusive
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=192
#SBATCH --mem=32G
#SBATCH --constraint=x64cpu
#SBATCH --time=00:30:00
#SBATCH --output=results/pa_%j.out
#SBATCH --error=results/pa_%j.err

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

BIN=./build/wfc_omp
[[ -x "$BIN" ]] || { echo "ERROR: $BIN missing"; exit 1; }

OUT=results/pa_${SLURM_JOB_ID}.log
SEEDS="2 7 13 21 42 99 100 200"
SIZE=32
N=3

for K in 1 4 8 16 32; do
    THREADS=$K
    echo "=== K=$K threads=$THREADS terrain_N3 ${SIZE}x${SIZE} ==="  | tee -a "$OUT"
    total=0
    for s in $SEEDS; do
        # Use bash time-piped-to-grep to capture wallclock without depending
        # on /usr/bin/time format. Sum manually below.
        start=$(date +%s%N)
        "$BIN" samples/multivalue_terrain.txt \
            --rows $SIZE --cols $SIZE -N $N --seed $s \
            --attempts 80 --parallel-attempts $K --threads $THREADS \
            > /dev/null 2>&1 || true
        end=$(date +%s%N)
        elapsed_ms=$(( (end - start) / 1000000 ))
        total=$(( total + elapsed_ms ))
        echo "  seed=$s elapsed=${elapsed_ms}ms" | tee -a "$OUT"
    done
    echo "  TOTAL: ${total}ms" | tee -a "$OUT"
    echo "" | tee -a "$OUT"
done

echo "=== done ===" | tee -a "$OUT"
date
