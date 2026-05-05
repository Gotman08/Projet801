#!/usr/bin/env bash
# Post-SLURM-job pipeline: pull CSV from Romeo, run all analyses,
# generate figures and tables for benchmark.md.
#
# Usage: bash scripts/post_job_pipeline.sh <jobid>

set -euo pipefail

JOBID=${1:?usage: post_job_pipeline.sh <jobid>}
PROJECT_DIR=$(cd "$(dirname "$0")/.." && pwd)
cd "$PROJECT_DIR"

CSV_REMOTE="~/wfc801/results/full_${JOBID}.csv"
CSV_LOCAL="results/full_${JOBID}.csv"
OUT_REMOTE="~/wfc801/results/full_${JOBID}.out"
OUT_LOCAL="results/full_${JOBID}.out"

echo "=== fetching CSV from Romeo ==="
scp -q "romeo:${CSV_REMOTE}" "${CSV_LOCAL}"
scp -q "romeo:${OUT_REMOTE}" "${OUT_LOCAL}" 2>/dev/null || echo "(no .out file)"
ls -la "${CSV_LOCAL}"
echo ""

echo "=== generating scaling figures (plot_results.py) ==="
mkdir -p docs/figures/scaling
python scripts/plot_results.py "${CSV_LOCAL}" --out docs/figures/scaling/

echo ""
echo "=== generating Amdahl fit ==="
mkdir -p docs/figures/amdahl results
python scripts/amdahl_fit.py "${CSV_LOCAL}" \
    --out "results/amdahl_${JOBID}.json" \
    --figures docs/figures/amdahl/ || echo "(amdahl_fit failed; not critical)"

echo ""
echo "=== generating tables ==="
python scripts/benchmark_summary.py "${CSV_LOCAL}" > "results/tables_${JOBID}.md"
echo "tables -> results/tables_${JOBID}.md"

echo ""
echo "=== generating insights ==="
python scripts/benchmark_insights.py "${CSV_LOCAL}" > "results/insights_${JOBID}.md"
echo "insights -> results/insights_${JOBID}.md"

echo ""
echo "=== summary ==="
echo "Figures generated:"
ls docs/figures/scaling/ 2>/dev/null | head -20
echo ""
echo "Now manually merge results/tables_${JOBID}.md and"
echo "results/insights_${JOBID}.md into docs/benchmark.md."
