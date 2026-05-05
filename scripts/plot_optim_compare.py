#!/usr/bin/env python3
"""Plot before/after comparison of the OMP frontier-threshold optim.

Compares `binary_L11` (job 543692, original code) vs `binary_optim`
(job 544356, with kSerialFallback fallback) on the 128x128 workload.
"""
import csv
import statistics
from collections import defaultdict
from pathlib import Path

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt


def load(path):
    rows = []
    with open(path) as f:
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


def median_by(rows, label):
    g = defaultdict(list)
    for r in rows:
        if r["label"] != label or r["backend"] != "omp":
            continue
        g[(r["threads"], r["size"])].append(r["solve"])
    return {k: statistics.median(v) for k, v in g.items()}


def main():
    rows = load("results/all_combined.csv")
    before = median_by(rows, "binary_L11")
    after = median_by(rows, "binary_optim")

    # Use 128 size (the only one in optim job)
    threads = sorted({k[0] for k in before if k[1] == 128} &
                     {k[0] for k in after if k[1] == 128})

    out_dir = Path("docs/figures/optim")
    out_dir.mkdir(parents=True, exist_ok=True)

    fig, (ax_time, ax_gain) = plt.subplots(1, 2, figsize=(13, 5))

    bef = [before[(t, 128)] for t in threads]
    aft = [after[(t, 128)] for t in threads]

    ax_time.plot(threads, bef, "o-", label="before (no fallback)", linewidth=2)
    ax_time.plot(threads, aft, "s-", label="after (frontier threshold)", linewidth=2)
    ax_time.set_xscale("log", base=2)
    ax_time.set_yscale("log")
    ax_time.set_xlabel("threads")
    ax_time.set_ylabel("solve time (s)")
    ax_time.set_title("OMP solve time on 128x128 binary, before vs after")
    ax_time.grid(True, which="both", alpha=0.3)
    ax_time.legend()

    gain = [(b - a) / b * 100.0 for b, a in zip(bef, aft)]
    colors = ["tab:green" if g > 0 else "tab:red" for g in gain]
    ax_gain.bar([str(t) for t in threads], gain, color=colors)
    ax_gain.axhline(0, color="black", linewidth=0.5)
    ax_gain.set_xlabel("threads")
    ax_gain.set_ylabel("speedup vs before (%)")
    ax_gain.set_title("Frontier-threshold optim, gain per thread count")
    ax_gain.grid(True, axis="y", alpha=0.3)
    for i, g in enumerate(gain):
        ax_gain.text(i, g + (1 if g > 0 else -2),
                     f"{g:+.0f}%", ha="center",
                     fontsize=9, fontweight="bold")

    fig.tight_layout()
    out = out_dir / "frontier_threshold_compare.png"
    fig.savefig(out, dpi=130)
    plt.close(fig)
    print(f"wrote {out}")


if __name__ == "__main__":
    main()
