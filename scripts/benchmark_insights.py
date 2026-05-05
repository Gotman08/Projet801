#!/usr/bin/env python3
"""Extract key HPC insights from a benchmark CSV.

Outputs a markdown report with:
  - Best speedup per (label, size) and the thread count at which it occurs
  - Strong-scaling efficiency knee (where efficiency drops below 50%)
  - Workload sensitivity (does L = number of tiles affect scaling?)
  - Throughput peaks (Mcells/s)
  - Anomalies (e.g., parallel slower than serial)

Usage:
    python scripts/benchmark_insights.py results/full_<jobid>.csv
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


def best_thread_table(agg):
    print("## Optimal thread count per problem")
    print()
    print("For each (label, size), thread count where OMP achieves its best time.")
    print()
    print("| Label | Size | Best threads | Speedup | Efficiency |")
    print("|---|---|---|---|---|")
    sizes = sorted({(k[0], k[3]) for k in agg.keys()})
    for label, size in sorted(sizes):
        base = agg.get((label, "serial", 1, size))
        if base is None:
            continue
        best_t, best_s = None, float("inf")
        for k, v in agg.items():
            if k[0] == label and k[1] == "omp" and k[3] == size and v < best_s:
                best_s = v
                best_t = k[2]
        if best_t is None:
            continue
        sp = base / best_s
        eff = sp / best_t
        print(f"| {label} | {size}x{size} | {best_t} | {sp:.2f}× | {eff*100:.1f}% |")
    print()


def efficiency_knee(agg):
    print("## Strong-scaling efficiency knee")
    print()
    print("Thread count at which parallel efficiency drops below 50% "
          "(i.e. doubling threads no longer doubles speed).")
    print()
    print("| Label | Size | Knee at | Eff at peak |")
    print("|---|---|---|---|")
    sizes = sorted({(k[0], k[3]) for k in agg.keys()})
    for label, size in sorted(sizes):
        base = agg.get((label, "serial", 1, size))
        if base is None:
            continue
        knee = None
        peak_eff = 0.0
        peak_t = None
        ts = sorted({k[2] for k in agg
                     if k[0] == label and k[1] == "omp" and k[3] == size})
        for t in ts:
            v = agg.get((label, "omp", t, size))
            if v is None:
                continue
            eff = (base / v) / t
            if eff > peak_eff:
                peak_eff = eff
                peak_t = t
            if knee is None and t > 1 and eff < 0.5:
                knee = t
        knee_str = f"{knee}t" if knee else "≥ max"
        print(f"| {label} | {size}x{size} | {knee_str} | {peak_eff*100:.1f}% (at {peak_t}t) |")
    print()


def parallel_slowdown(agg):
    print("## Parallel slowdown (omp t > 1 slower than serial)")
    print()
    print("Configurations where adding threads makes the code slower than "
          "serial. Diagnostic: indicates fork/join + sync cost > parallelizable work.")
    print()
    print("| Label | Size | Threads | Serial (s) | OMP (s) | Slowdown |")
    print("|---|---|---|---|---|---|")
    found = False
    for k, v in sorted(agg.items()):
        label, backend, t, size = k
        if backend != "omp" or t == 1:
            continue
        base = agg.get((label, "serial", 1, size))
        if base is None or v <= base:
            continue
        print(f"| {label} | {size}x{size} | {t} | {base:.2f} | {v:.2f} | {v/base:.2f}× slower |")
        found = True
    if not found:
        print("*(none — every parallel config beats serial)*")
    print()


def workload_sensitivity(agg):
    print("## Workload sensitivity")
    print()
    print("Does L (number of unique tiles) affect parallel efficiency?")
    print()
    labels = sorted({k[0] for k in agg.keys()})
    sizes = sorted({k[3] for k in agg.keys()})
    print("| Size | " + " | ".join(labels) + " |")
    print("|" + "|".join(["---"] * (len(labels) + 1)) + "|")
    for size in sizes:
        row = [f"{size}x{size}"]
        for lab in labels:
            base = agg.get((lab, "serial", 1, size))
            if base is None:
                row.append("—")
                continue
            ts = sorted({k[2] for k in agg
                         if k[0] == lab and k[1] == "omp" and k[3] == size})
            if not ts:
                row.append("—")
                continue
            best = min(agg[(lab, "omp", t, size)] for t in ts
                       if (lab, "omp", t, size) in agg)
            row.append(f"{base/best:.2f}×")
        print("| " + " | ".join(row) + " |")
    print()


def kokkos_anomaly(agg):
    print("## Kokkos thread sensitivity")
    print()
    print("Kokkos solve times across 'thread' counts. The Kokkos backend")
    print("doesn't take a threads argument from `wfc_benchmark` — it uses")
    print("the default Kokkos host execution space concurrency. So all")
    print("'thread' columns should give the same result; if they do, this")
    print("documents that the kokkos sweep is effectively a single-config")
    print("run with statistical replicates rather than a scaling study.")
    print()
    has_kokkos = any(k[1] == "kokkos" for k in agg)
    if not has_kokkos:
        print("*(no kokkos data in this CSV)*")
        return
    sizes = sorted({k[3] for k in agg if k[1] == "kokkos"})
    threads = sorted({k[2] for k in agg if k[1] == "kokkos"})
    labels = sorted({k[0] for k in agg if k[1] == "kokkos"})
    for label in labels:
        print(f"### `{label}`")
        print()
        print("| Size | " + " | ".join(f"{t}t" for t in threads) + " |")
        print("|" + "|".join(["---"] * (len(threads) + 1)) + "|")
        for size in sizes:
            row = [f"{size}x{size}"]
            for t in threads:
                v = agg.get((label, "kokkos", t, size))
                row.append(f"{v:.2f}" if v is not None else "—")
            print("| " + " | ".join(row) + " |")
        print()


def main():
    if len(sys.argv) < 2:
        print("usage: benchmark_insights.py <csv>", file=sys.stderr)
        sys.exit(2)
    rows = load(sys.argv[1])
    agg = aggregate(rows)
    print(f"# Benchmark insights — {len(rows)} rows from {sys.argv[1]}")
    print()
    best_thread_table(agg)
    efficiency_knee(agg)
    parallel_slowdown(agg)
    workload_sensitivity(agg)
    kokkos_anomaly(agg)


if __name__ == "__main__":
    main()
