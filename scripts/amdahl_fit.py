#!/usr/bin/env python3
"""Fit Amdahl's law against measured speedup curves to demonstrate that
the OMP code is at its theoretical ceiling.

For each (label, size), fit  s(p) = 1 / ((1 - f) + f / p)  to the points
in the *operational window* — the threads where speedup is monotonically
non-decreasing (i.e. up to and including the peak). Beyond the peak,
hardware overheads (NUMA, cache contention) dominate and the model no
longer applies.

Outputs:
    results/amdahl_fits.json
    docs/figures/romeo/amdahl_<label>.png

Usage:
    python scripts/amdahl_fit.py results/romeo/romeo_combined.csv \\
        --out results/amdahl_fits.json \\
        --figures docs/figures/romeo/
"""

from __future__ import annotations

import argparse
import csv
import json
import os
import statistics
from collections import defaultdict

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np
from scipy.optimize import curve_fit


def amdahl(p, f):
    return 1.0 / ((1.0 - f) + f / p)


def read_csv(path):
    rows = []
    with open(path, newline="") as h:
        for r in csv.DictReader(h):
            rows.append({
                "label":   r.get("label", "default"),
                "backend": r["backend"],
                "threads": int(r["threads"]),
                "rows":    int(r["rows"]),
                "success": int(r["success"]) == 1,
                "solve_s": float(r["solve_s"]),
            })
    return rows


def median_table(rows):
    g = defaultdict(list)
    for r in rows:
        if r["success"]:
            g[(r["label"], r["backend"], r["threads"], r["rows"])].append(r["solve_s"])
    return {k: statistics.median(v) for k, v in g.items()}


def operational_window(speedups):
    """Return prefix of (threads, speedup) up to and including the peak."""
    if not speedups:
        return []
    out = [speedups[0]]
    best = speedups[0][1]
    for t, s in speedups[1:]:
        if s >= best * 0.97:  # tolerate 3 % noise around the plateau
            out.append((t, s))
            best = max(best, s)
        else:
            break
    return out


def fit_amdahl(window):
    """Returns (f, R^2) over the window. f bounded to [0, 1]."""
    if len(window) < 2:
        return None, None
    p = np.array([w[0] for w in window], dtype=float)
    s = np.array([w[1] for w in window], dtype=float)
    try:
        popt, _ = curve_fit(amdahl, p, s, p0=[0.9], bounds=(0.0, 0.9999))
    except RuntimeError:
        return None, None
    f = float(popt[0])
    s_pred = amdahl(p, f)
    ss_res = float(np.sum((s - s_pred) ** 2))
    ss_tot = float(np.sum((s - np.mean(s)) ** 2))
    r2 = 1.0 - ss_res / ss_tot if ss_tot > 1e-12 else 1.0
    return f, r2


def plot_one(label, size, all_points, window, f, r2, peak_threads, out_dir):
    fig, ax = plt.subplots(figsize=(8.5, 5.5))

    th_all = [t for t, _ in all_points]
    sp_all = [s for _, s in all_points]
    ax.plot(th_all, sp_all, marker="o", color="C0", label=f"measured ({size}x{size})")

    th_w = [t for t, _ in window]
    sp_w = [s for _, s in window]
    ax.plot(th_w, sp_w, marker="o", linestyle="None", color="C2",
            markersize=10, markerfacecolor="none", markeredgewidth=2,
            label="fit window")

    if f is not None:
        p_fine = np.geomspace(1, max(th_all), 200)
        ax.plot(p_fine, amdahl(p_fine, f), "--", color="C3",
                label=f"Amdahl fit (f = {f:.3f}, R² = {r2:.3f})")
        ceiling = 1.0 / (1.0 - f)
        ax.axhline(ceiling, color="grey", linestyle=":",
                   label=f"Amdahl ceiling = {ceiling:.1f}×")

    ideal = list(range(1, max(th_all) + 1))
    ax.plot(ideal, ideal, alpha=0.3, color="grey", label="ideal")

    ax.set_xscale("log", base=2)
    ax.set_yscale("log", base=2)
    ax.set_xlabel("threads")
    ax.set_ylabel("speedup vs serial")
    ax.set_title(f"Amdahl fit — {label} {size}x{size} (peak at {peak_threads} threads)")
    ax.grid(True, which="both", alpha=0.3)
    ax.legend(loc="upper left")
    fig.tight_layout()
    out = os.path.join(out_dir, f"amdahl_{label}_{size}.png")
    fig.savefig(out, dpi=130)
    plt.close(fig)
    return out


def main():
    p = argparse.ArgumentParser()
    p.add_argument("csv")
    p.add_argument("--out", default="results/amdahl_fits.json")
    p.add_argument("--figures", default="docs/figures/romeo")
    args = p.parse_args()

    os.makedirs(os.path.dirname(args.out) or ".", exist_ok=True)
    os.makedirs(args.figures, exist_ok=True)

    rows = read_csv(args.csv)
    table = median_table(rows)

    # group by (label, size); compute speedup vs serial t=1
    by_label_size = defaultdict(dict)
    for (label, backend, threads, size), val in table.items():
        if backend == "omp":
            by_label_size[(label, size)][threads] = val

    # baseline: serial t=1 if present, else omp t=1
    baselines = {}
    for (label, backend, threads, size), val in table.items():
        key = (label, size)
        if backend == "serial" and threads == 1:
            baselines[key] = val
    for (label, size), threads_dict in by_label_size.items():
        baselines.setdefault((label, size), threads_dict.get(1))

    fits = {}
    figures = []
    for (label, size), threads_dict in sorted(by_label_size.items()):
        baseline = baselines.get((label, size))
        if not baseline:
            continue
        all_points = sorted([(t, baseline / v) for t, v in threads_dict.items()])
        window = operational_window(all_points)
        peak_threads = window[-1][0] if window else None

        f, r2 = fit_amdahl(window)
        ceiling = 1.0 / (1.0 - f) if f is not None and f < 0.9999 else None
        peak_speedup = max((s for _, s in all_points), default=None)
        ratio_to_ceiling = (peak_speedup / ceiling) if (peak_speedup and ceiling) else None

        fits[f"{label}/{size}"] = {
            "label": label,
            "size": size,
            "f": f,
            "r2": r2,
            "ceiling": ceiling,
            "peak_speedup": peak_speedup,
            "peak_threads": peak_threads,
            "ratio_to_ceiling": ratio_to_ceiling,
            "window_threads": [t for t, _ in window],
        }
        fig = plot_one(label, size, all_points, window, f, r2, peak_threads, args.figures)
        figures.append(fig)

    with open(args.out, "w") as h:
        json.dump(fits, h, indent=2)

    print(f"wrote {args.out}")
    print(f"wrote {len(figures)} figures in {args.figures}")
    print("\nSummary:")
    print(f"{'Label':<14} {'Size':>5} {'f':>8} {'R²':>7} "
          f"{'Ceil':>8} {'Peak':>8} {'Ratio':>8}")
    for k, v in fits.items():
        f_s    = f"{v['f']:.3f}"        if v["f"] is not None else "—"
        r2_s   = f"{v['r2']:.3f}"       if v["r2"] is not None else "—"
        ceil_s = f"{v['ceiling']:.1f}×" if v["ceiling"] is not None else "—"
        peak_s = f"{v['peak_speedup']:.2f}×" if v["peak_speedup"] is not None else "—"
        rt_s   = f"{v['ratio_to_ceiling']*100:.0f} %" if v["ratio_to_ceiling"] is not None else "—"
        print(f"{v['label']:<14} {v['size']:>5} {f_s:>8} {r2_s:>7} "
              f"{ceil_s:>8} {peak_s:>8} {rt_s:>8}")


if __name__ == "__main__":
    main()
