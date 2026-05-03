#!/usr/bin/env python3
"""Render speedup, efficiency, and heatmap plots from a benchmark CSV.

Handles the new `label` column (multiple sweeps in one file) and the
extended thread range (up to 192 cores on Romeo).

Usage:
    python scripts/plot_results.py results/romeo_full.csv [--out docs/figures]
"""

from __future__ import annotations

import argparse
import csv
import os
import statistics
from collections import defaultdict

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np


def read_csv(path):
    rows = []
    with open(path, newline="") as f:
        reader = csv.DictReader(f)
        for r in reader:
            rows.append({
                "label":    r.get("label", "default"),
                "backend":  r["backend"],
                "threads":  int(r["threads"]),
                "rows":     int(r["rows"]),
                "cols":     int(r["cols"]),
                "success":  int(r["success"]) == 1,
                "rules_s":  float(r["rules_s"]),
                "solve_s":  float(r["solve_s"]),
                "total_s":  float(r["total_s"]),
            })
    return rows


def aggregate(rows):
    """Group by (label, backend, threads, size) and return median + stdev."""
    bucket = defaultdict(list)
    for r in rows:
        if not r["success"]:
            continue
        key = (r["label"], r["backend"], r["threads"], r["rows"])
        bucket[key].append(r["solve_s"])
    out = {}
    for k, vs in bucket.items():
        out[k] = {
            "median": statistics.median(vs),
            "min": min(vs),
            "max": max(vs),
            "stdev": statistics.stdev(vs) if len(vs) > 1 else 0.0,
            "n": len(vs),
        }
    return out


def labels(agg):
    return sorted({k[0] for k in agg})


def sizes_for(agg, label):
    return sorted({k[3] for k in agg if k[0] == label})


def threads_for(agg, label, size):
    return sorted({k[2] for k in agg
                   if k[0] == label and k[3] == size and k[1] != "serial"})


def plot_speedup(agg, label, out_dir):
    fig, ax = plt.subplots(figsize=(8.5, 5.5))
    sizes = sizes_for(agg, label)

    max_t = 1
    for size in sizes:
        baseline = agg.get((label, "serial", 1, size))
        if baseline is None:
            continue
        b = baseline["median"]
        ts = threads_for(agg, label, size)
        if not ts:
            continue
        max_t = max(max_t, max(ts))
        speedups = [b / agg[(label, "omp", t, size)]["median"]
                    for t in ts if (label, "omp", t, size) in agg]
        thrs = [t for t in ts if (label, "omp", t, size) in agg]
        ax.plot(thrs, speedups, marker="o", label=f"{size}x{size}")

    ideal = list(range(1, max_t + 1))
    ax.plot(ideal, ideal, "--", color="grey", alpha=0.5, label="ideal")

    ax.set_xscale("log", base=2)
    ax.set_yscale("log", base=2)
    ax.set_xlabel("threads")
    ax.set_ylabel("speedup vs serial")
    ax.set_title(f"WFC parallel speedup — {label}")
    ax.grid(True, which="both", alpha=0.3)
    ax.legend()
    fig.tight_layout()
    fig.savefig(os.path.join(out_dir, f"speedup_{label}.png"), dpi=130)
    plt.close(fig)


def plot_efficiency(agg, label, out_dir):
    fig, ax = plt.subplots(figsize=(8.5, 5.5))
    sizes = sizes_for(agg, label)

    for size in sizes:
        baseline = agg.get((label, "serial", 1, size))
        if baseline is None:
            continue
        b = baseline["median"]
        ts = threads_for(agg, label, size)
        eff = [(b / agg[(label, "omp", t, size)]["median"]) / t
               for t in ts if (label, "omp", t, size) in agg]
        thrs = [t for t in ts if (label, "omp", t, size) in agg]
        ax.plot(thrs, eff, marker="o", label=f"{size}x{size}")

    ax.axhline(1.0, color="grey", linestyle="--", alpha=0.5, label="ideal (100 %)")
    ax.set_xscale("log", base=2)
    ax.set_xlabel("threads")
    ax.set_ylabel("parallel efficiency")
    ax.set_title(f"WFC parallel efficiency — {label}")
    ax.grid(True, which="both", alpha=0.3)
    ax.set_ylim(bottom=0)
    ax.legend()
    fig.tight_layout()
    fig.savefig(os.path.join(out_dir, f"efficiency_{label}.png"), dpi=130)
    plt.close(fig)


def plot_heatmap(agg, label, out_dir):
    """Solve time as a heatmap of size (rows) × threads (cols)."""
    sizes = sizes_for(agg, label)
    threads = sorted({k[2] for k in agg if k[0] == label and k[1] == "omp"})
    if not sizes or not threads:
        return

    matrix = np.full((len(sizes), len(threads)), np.nan)
    for i, s in enumerate(sizes):
        for j, t in enumerate(threads):
            entry = agg.get((label, "omp", t, s))
            if entry is not None:
                matrix[i, j] = entry["median"]

    fig, ax = plt.subplots(figsize=(8.5, 5.0))
    # Solve time spans several orders of magnitude — log color scale.
    from matplotlib.colors import LogNorm
    finite = matrix[np.isfinite(matrix)]
    vmin = max(finite.min(), 1e-4) if finite.size else 1e-4
    vmax = finite.max() if finite.size else 1.0
    im = ax.imshow(matrix, aspect="auto", origin="lower",
                   cmap="viridis_r", norm=LogNorm(vmin=vmin, vmax=vmax))
    ax.set_xticks(range(len(threads)))
    ax.set_xticklabels(threads)
    ax.set_yticks(range(len(sizes)))
    ax.set_yticklabels(sizes)
    ax.set_xlabel("threads")
    ax.set_ylabel("output size")
    ax.set_title(f"WFC solve time (s, median, log scale) — {label}")
    fig.colorbar(im, ax=ax, label="seconds (log)")

    # Annotate each cell with its value for readability.
    for i in range(len(sizes)):
        for j in range(len(threads)):
            v = matrix[i, j]
            if np.isfinite(v):
                txt = f"{v:.2g}" if v < 10 else f"{v:.0f}"
                ax.text(j, i, txt, ha="center", va="center",
                        color="white" if v > vmin * 5 else "black", fontsize=8)
    fig.tight_layout()
    fig.savefig(os.path.join(out_dir, f"heatmap_{label}.png"), dpi=130)
    plt.close(fig)


def plot_speedup_with_errors(agg, label, out_dir):
    """Speedup with min/max bands across repeats."""
    fig, ax = plt.subplots(figsize=(8.5, 5.5))
    sizes = sizes_for(agg, label)
    max_t = 1
    for size in sizes:
        baseline = agg.get((label, "serial", 1, size))
        if baseline is None:
            continue
        b = baseline["median"]
        ts = threads_for(agg, label, size)
        if not ts:
            continue
        max_t = max(max_t, max(ts))
        thrs, med, lo, hi = [], [], [], []
        for t in ts:
            entry = agg.get((label, "omp", t, size))
            if not entry:
                continue
            thrs.append(t)
            med.append(b / entry["median"])
            lo.append(b / entry["max"])
            hi.append(b / entry["min"])
        ax.fill_between(thrs, lo, hi, alpha=0.2)
        ax.plot(thrs, med, marker="o", label=f"{size}x{size}")

    ideal = list(range(1, max_t + 1))
    ax.plot(ideal, ideal, "--", color="grey", alpha=0.5, label="ideal")
    ax.set_xscale("log", base=2)
    ax.set_yscale("log", base=2)
    ax.set_xlabel("threads")
    ax.set_ylabel("speedup vs serial")
    ax.set_title(f"WFC speedup with min/max band — {label}")
    ax.grid(True, which="both", alpha=0.3)
    ax.legend()
    fig.tight_layout()
    fig.savefig(os.path.join(out_dir, f"speedup_band_{label}.png"), dpi=130)
    plt.close(fig)


def main():
    p = argparse.ArgumentParser()
    p.add_argument("csv")
    p.add_argument("--out", default="docs/figures")
    args = p.parse_args()

    os.makedirs(args.out, exist_ok=True)
    rows = read_csv(args.csv)
    agg = aggregate(rows)
    if not agg:
        print("no successful runs in CSV")
        return

    for label in labels(agg):
        plot_speedup(agg, label, args.out)
        plot_efficiency(agg, label, args.out)
        plot_heatmap(agg, label, args.out)
        plot_speedup_with_errors(agg, label, args.out)

    print(f"figures written to {args.out}/")


if __name__ == "__main__":
    main()
