#!/usr/bin/env python3
"""Generate markdown summary tables from a benchmark CSV.

Produces speedup tables, efficiency tables, and a backend comparison table
suitable for pasting into docs/benchmark.md.

Usage:
    python scripts/benchmark_summary.py results/full_<jobid>.csv > tables.md
"""
import csv
import statistics
import sys
from collections import defaultdict


def median(xs):
    return statistics.median(xs) if xs else float("nan")


def load(path):
    rows = []
    with open(path, newline="") as f:
        for r in csv.DictReader(f):
            if int(r["success"]) != 1:
                continue
            rows.append({
                "label":   r.get("label", "default"),
                "backend": r["backend"],
                "threads": int(r["threads"]),
                "size":    int(r["rows"]),
                "solve":   float(r["solve_s"]),
            })
    return rows


def aggregate(rows):
    g = defaultdict(list)
    for r in rows:
        g[(r["label"], r["backend"], r["threads"], r["size"])].append(r["solve"])
    return {k: median(v) for k, v in g.items()}


def speedup_table(agg, label):
    sizes = sorted({k[3] for k in agg if k[0] == label})
    threads = sorted({k[2] for k in agg
                      if k[0] == label and k[1] == "omp"})
    if not sizes or not threads:
        return None

    out = []
    out.append(f"### Strong scaling speedup, `{label}`")
    out.append("")
    header = ["Size"] + [f"{t}t" for t in threads]
    out.append("| " + " | ".join(header) + " |")
    out.append("|" + "|".join(["---"] * len(header)) + "|")

    for size in sizes:
        baseline_key = (label, "serial", 1, size)
        if baseline_key not in agg:
            baseline_key = (label, "omp", 1, size)
        if baseline_key not in agg:
            continue
        base = agg[baseline_key]
        row = [f"{size}x{size}"]
        for t in threads:
            key = (label, "omp", t, size)
            if key in agg:
                sp = base / agg[key]
                row.append(f"{sp:.2f}×")
            else:
                row.append("-")
        out.append("| " + " | ".join(row) + " |")
    out.append("")
    return "\n".join(out)


def efficiency_table(agg, label):
    sizes = sorted({k[3] for k in agg if k[0] == label})
    threads = sorted({k[2] for k in agg
                      if k[0] == label and k[1] == "omp"})
    if not sizes or not threads:
        return None

    out = []
    out.append(f"### Parallel efficiency, `{label}`")
    out.append("")
    out.append("Efficiency = speedup / threads. 100% = ideal scaling.")
    out.append("")
    header = ["Size"] + [f"{t}t" for t in threads]
    out.append("| " + " | ".join(header) + " |")
    out.append("|" + "|".join(["---"] * len(header)) + "|")

    for size in sizes:
        baseline_key = (label, "serial", 1, size)
        if baseline_key not in agg:
            baseline_key = (label, "omp", 1, size)
        if baseline_key not in agg:
            continue
        base = agg[baseline_key]
        row = [f"{size}x{size}"]
        for t in threads:
            key = (label, "omp", t, size)
            if key in agg:
                eff = (base / agg[key]) / t
                row.append(f"{eff*100:.0f}%")
            else:
                row.append("-")
        out.append("| " + " | ".join(row) + " |")
    out.append("")
    return "\n".join(out)


def backends_table(agg, label, size):
    backends = sorted({k[1] for k in agg if k[0] == label and k[3] == size})
    threads = sorted({k[2] for k in agg if k[0] == label and k[3] == size})
    if not backends or not threads:
        return None

    out = []
    out.append(f"### Backend solve time (s), `{label}`, {size}×{size}")
    out.append("")
    header = ["Backend"] + [f"{t}t" for t in threads]
    out.append("| " + " | ".join(header) + " |")
    out.append("|" + "|".join(["---"] * len(header)) + "|")
    for b in backends:
        row = [b]
        for t in threads:
            key = (label, b, t, size)
            if key in agg:
                row.append(f"{agg[key]:.3f}")
            else:
                row.append("-")
        out.append("| " + " | ".join(row) + " |")
    out.append("")
    return "\n".join(out)


def peak_table(agg, label):
    """Peak speedup and the thread count at which it occurs."""
    sizes = sorted({k[3] for k in agg if k[0] == label})
    if not sizes:
        return None
    out = []
    out.append(f"### Peak performance, `{label}`")
    out.append("")
    out.append("| Size | Serial (s) | Best parallel (s) | Best threads | Peak speedup |")
    out.append("|---|---|---|---|---|")
    for size in sizes:
        base_key = (label, "serial", 1, size)
        if base_key not in agg:
            continue
        base = agg[base_key]
        best_t, best_time = None, float("inf")
        for k, v in agg.items():
            if k[0] == label and k[1] == "omp" and k[3] == size:
                if v < best_time:
                    best_time = v
                    best_t = k[2]
        if best_t is None:
            continue
        out.append(f"| {size}x{size} | {base:.3f} | {best_time:.3f} "
                   f"| {best_t} | {base / best_time:.2f}× |")
    out.append("")
    return "\n".join(out)


def main():
    if len(sys.argv) < 2:
        print("usage: benchmark_summary.py <csv>", file=sys.stderr)
        sys.exit(2)
    rows = load(sys.argv[1])
    agg = aggregate(rows)
    labels = sorted({k[0] for k in agg.keys()})

    for lab in labels:
        for fn in (speedup_table, efficiency_table, peak_table):
            table = fn(agg, lab)
            if table:
                print(table)
        sizes = sorted({k[3] for k in agg if k[0] == lab})
        for size in sizes:
            t = backends_table(agg, lab, size)
            if t:
                print(t)


if __name__ == "__main__":
    main()
