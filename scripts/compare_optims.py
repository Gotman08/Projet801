#!/usr/bin/env python3
"""Compare two benchmark CSVs (baseline vs optimised) and report the
delta per (sweep, size, threads).

Usage:
    python scripts/compare_optims.py \\
        --baseline results/romeo/romeo_combined.csv \\
        --baseline-label binary_L11 \\
        --optims results/romeo_optims.csv \\
        --optims-label binary_L11_optims
"""

from __future__ import annotations
import argparse
import csv
import statistics
from collections import defaultdict


def read_medians(path, label_filter=None):
    g = defaultdict(list)
    with open(path, newline="") as f:
        for r in csv.DictReader(f):
            if int(r["success"]) != 1:
                continue
            label = r.get("label", "default")
            if label_filter and label != label_filter:
                continue
            key = (r["backend"], int(r["threads"]), int(r["rows"]))
            g[key].append(float(r["solve_s"]))
    return {k: statistics.median(v) for k, v in g.items()}


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--baseline", required=True)
    p.add_argument("--baseline-label", required=True)
    p.add_argument("--optims",   required=True)
    p.add_argument("--optims-label", required=True)
    args = p.parse_args()

    base = read_medians(args.baseline, args.baseline_label)
    opt  = read_medians(args.optims,   args.optims_label)

    common = sorted(set(base) & set(opt))
    if not common:
        print("no common configs found")
        return

    print(f"# Comparison: baseline `{args.baseline_label}` vs optims "
          f"`{args.optims_label}`")
    print()
    print(f"{'backend':>8s} {'thr':>4s} {'size':>5s} "
          f"{'baseline':>10s} {'optims':>10s} {'delta':>8s} "
          f"{'speedup':>9s}")
    print("-" * 65)
    for k in common:
        backend, threads, size = k
        b = base[k]; o = opt[k]
        delta_pct = (o - b) / b * 100.0
        speedup_ratio = b / o
        marker = ""
        if speedup_ratio >= 1.10: marker = " ★"   # > 10 % faster
        elif speedup_ratio <= 0.90: marker = " ⚠"  # > 10 % slower
        print(f"{backend:>8s} {threads:>4d} {size:>5d} "
              f"{b:>10.3f} {o:>10.3f} {delta_pct:>+7.1f}% "
              f"{speedup_ratio:>8.2f}x{marker}")

    print()
    # Aggregate stats by size
    print("## Aggregate by size (omp threads >= 2)")
    print()
    print(f"{'size':>5s} {'best_speedup':>14s} {'avg_gain':>10s}")
    by_size = defaultdict(list)
    for k in common:
        backend, threads, size = k
        if backend == "omp" and threads >= 2:
            ratio = base[k] / opt[k]
            by_size[size].append((threads, ratio))
    for size in sorted(by_size):
        ratios = [r for _, r in by_size[size]]
        best = max(ratios)
        avg  = sum(ratios) / len(ratios)
        print(f"{size:>5d} {best:>13.2f}x {(avg-1)*100:>+9.1f}%")


if __name__ == "__main__":
    main()
