#!/usr/bin/env python3
"""Compare median solve times across multiple benchmark CSVs.

Usage:
    python scripts/compare_benchmarks.py LABEL1=file1.csv LABEL2=file2.csv ...
"""
from __future__ import annotations
import csv
import statistics
import sys
from collections import defaultdict


def median_table(path):
    bucket = defaultdict(list)
    with open(path, newline="") as f:
        for r in csv.DictReader(f):
            if int(r["success"]) != 1:
                continue
            key = (r["backend"], int(r["threads"]), int(r["rows"]))
            bucket[key].append(float(r["solve_s"]))
    return {k: statistics.median(v) for k, v in bucket.items()}


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(1)
    runs = []
    for arg in sys.argv[1:]:
        label, path = arg.split("=", 1)
        runs.append((label, median_table(path)))

    keys = sorted({k for _, t in runs for k in t})
    label_w = max(8, max(len(l) for l, _ in runs))
    header = f"{'backend':>10s} {'threads':>3s} {'size':>5s}"
    for label, _ in runs:
        header += f" {label:>{label_w + 2}s}"
    if len(runs) > 1:
        header += f" {'delta%':>8s}"
    print(header)
    print("-" * len(header))

    for k in keys:
        backend, threads, size = k
        line = f"{backend:>10s} {threads:>3d} {size:>5d}"
        values = []
        for _, table in runs:
            v = table.get(k)
            values.append(v)
            line += f" {('---' if v is None else f'{v:.3f}'):>{label_w + 2}s}"
        if len(runs) >= 2 and values[0] and values[-1]:
            delta = (values[-1] - values[0]) / values[0] * 100.0
            line += f" {delta:+7.1f}%"
        print(line)


if __name__ == "__main__":
    main()
