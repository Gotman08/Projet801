#!/usr/bin/env python3
"""Auto-diagnostic over a benchmark CSV: sweet spot per size, NUMA break,
high-variance configs, failed runs, Amdahl fit. Output markdown to stdout
or to --out FILE.

Usage:
    python scripts/analyze_results.py results/romeo_full.csv \
        --out docs/romeo_diagnostic.md
"""

from __future__ import annotations

import argparse
import csv
import math
import statistics
import sys
from collections import defaultdict


def read(path):
    rows = []
    with open(path, newline="") as f:
        reader = csv.DictReader(f)
        for r in reader:
            rows.append({
                "label":    r.get("label", "default"),
                "backend":  r["backend"],
                "threads":  int(r["threads"]),
                "rows":     int(r["rows"]),
                "success":  int(r["success"]) == 1,
                "attempts": int(r["attempts"]),
                "solve_s":  float(r["solve_s"]),
                "rules_s":  float(r["rules_s"]),
                "collapses":   int(r["collapses"]),
                "propagations":int(r["propagations"]),
            })
    return rows


def group(rows):
    g = defaultdict(list)
    for r in rows:
        if not r["success"]:
            continue
        g[(r["label"], r["backend"], r["threads"], r["rows"])].append(r["solve_s"])
    out = {}
    for k, v in g.items():
        out[k] = {
            "median": statistics.median(v),
            "mean":   statistics.mean(v),
            "stdev":  statistics.stdev(v) if len(v) > 1 else 0.0,
            "min":    min(v),
            "max":    max(v),
            "n":      len(v),
        }
    return out


def amdahl_fraction_from(speedup_p, p):
    # s = 1 / ((1-f) + f/p)  =>  f = (p*(s-1)) / (s*(p-1))
    if p == 1 or speedup_p <= 1:
        return None
    return (p * (speedup_p - 1)) / (speedup_p * (p - 1))


def write_md(out, rows, agg):
    print = lambda *a, **kw: out.write(" ".join(str(x) for x in a) + kw.get("end", "\n"))

    print("# WFC benchmark — diagnostic Romeo")
    print()

    # --- summary ---
    print("## Sommaire")
    total = len(rows)
    fails = sum(1 for r in rows if not r["success"])
    print(f"- Runs totaux : **{total}**")
    print(f"- Échecs (`success=0`) : **{fails}**" + ("  ⚠️" if fails else ""))
    print(f"- Configurations agrégées : **{len(agg)}**")
    print()

    # --- failures ---
    if fails:
        print("## Échecs")
        seen = set()
        for r in rows:
            if r["success"]:
                continue
            key = (r["label"], r["backend"], r["threads"], r["rows"], r["attempts"])
            if key in seen:
                continue
            seen.add(key)
            print(f"- `{r['label']}` {r['backend']} t={r['threads']} {r['rows']}×{r['rows']}"
                  f" → contradiction après {r['attempts']} tentatives")
        print()

    # --- variance flags ---
    print("## Variance")
    high_var = []
    for k, v in agg.items():
        if v["n"] >= 3 and v["median"] > 0:
            cv = v["stdev"] / v["median"]
            if cv > 0.10:
                high_var.append((cv, k, v))
    high_var.sort(reverse=True)
    if high_var:
        print("Configs avec coefficient de variation > 10 % (à examiner) :")
        print()
        print("| Label | Backend | Threads | Size | CV | Median (s) | Min | Max |")
        print("|-------|---------|---------|------|-----|------------|-----|-----|")
        for cv, (label, backend, t, size), v in high_var[:15]:
            print(f"| {label} | {backend} | {t} | {size}×{size} |"
                  f" {cv*100:.1f} % | {v['median']:.3f} |"
                  f" {v['min']:.3f} | {v['max']:.3f} |")
    else:
        print("Toutes les configs ont CV ≤ 10 % — bonne reproductibilité.")
    print()

    # --- per-label analysis ---
    labels = sorted({k[0] for k in agg})
    for label in labels:
        print(f"## Label : `{label}`")

        # Sweet spot per size
        sizes = sorted({k[3] for k in agg if k[0] == label})
        threads_seen = sorted({k[2] for k in agg if k[0] == label and k[1] == "omp"})
        print()
        print("### Sweet spot par taille")
        print()
        print("Le \"sweet spot\" est le nombre de threads qui maximise le speedup")
        print("(t ≥ 2 ; t=1 OMP est exclu — il représente l'overhead de la version")
        print("parallèle sans bénéfice).")
        print()
        print("| Size | Threads sweet | Speedup | Efficacité | Solve (s) |")
        print("|------|---------------|---------|------------|-----------|")
        for s in sizes:
            base = agg.get((label, "serial", 1, s))
            if not base:
                continue
            best_sp = 0.0
            best_t, best_eff, best_med = None, None, None
            for t in threads_seen:
                if t < 2:
                    continue
                e = agg.get((label, "omp", t, s))
                if not e:
                    continue
                sp = base["median"] / e["median"]
                if sp > best_sp:
                    best_sp = sp
                    best_t = t
                    best_eff = sp / t
                    best_med = e["median"]
            if best_t is not None:
                print(f"| {s}×{s} | {best_t} | {best_sp:.2f}× |"
                      f" {best_eff*100:.0f} % | {best_med:.3f} |")
        print()

        # Max speedup per size
        print("### Speedup maximum par taille")
        print()
        print("| Size | Threads | Speedup max | Solve (s) | Efficacité |")
        print("|------|---------|-------------|-----------|------------|")
        for s in sizes:
            base = agg.get((label, "serial", 1, s))
            if not base:
                continue
            best_sp = 0.0
            best_t, best_med = None, None
            for t in threads_seen:
                e = agg.get((label, "omp", t, s))
                if not e:
                    continue
                sp = base["median"] / e["median"]
                if sp > best_sp:
                    best_sp, best_t, best_med = sp, t, e["median"]
            if best_t is not None:
                eff = best_sp / best_t
                print(f"| {s}×{s} | {best_t} | {best_sp:.2f}× |"
                      f" {best_med:.3f} | {eff*100:.0f} % |")
        print()

        # NUMA break: speedup at 96 vs 192. Above 96 = crossing sockets.
        if 96 in threads_seen and 192 in threads_seen:
            print("### Effet NUMA (96 → 192 threads, traverse les sockets)")
            print()
            print("| Size | Speedup @ 96 | Speedup @ 192 | Gain double-socket |")
            print("|------|--------------|---------------|--------------------|")
            for s in sizes:
                base = agg.get((label, "serial", 1, s))
                if not base:
                    continue
                e96  = agg.get((label, "omp",  96, s))
                e192 = agg.get((label, "omp", 192, s))
                if not e96 or not e192:
                    continue
                sp96  = base["median"] / e96["median"]
                sp192 = base["median"] / e192["median"]
                ratio = sp192 / sp96
                marker = " ✓" if ratio > 1.3 else (" ⚠️" if ratio < 1.0 else "")
                print(f"| {s}×{s} | {sp96:.2f}× | {sp192:.2f}× |"
                      f" ×{ratio:.2f}{marker} |")
            print()

        # Amdahl fit using the best speedup
        print("### Loi d'Amdahl — fraction parallélisable estimée")
        print()
        print("| Size | Speedup max | Threads | Fraction parallélisable estimée |")
        print("|------|-------------|---------|--------------------------------|")
        for s in sizes:
            base = agg.get((label, "serial", 1, s))
            if not base:
                continue
            best_sp, best_t = 0.0, None
            for t in threads_seen:
                e = agg.get((label, "omp", t, s))
                if not e:
                    continue
                sp = base["median"] / e["median"]
                if sp > best_sp:
                    best_sp, best_t = sp, t
            f = amdahl_fraction_from(best_sp, best_t) if best_t else None
            f_str = f"{f*100:.1f} %" if f is not None else "—"
            print(f"| {s}×{s} | {best_sp:.2f}× | {best_t} | {f_str} |")
        print()

    # --- propagation stats sanity ---
    print("## Sanity check — propagations / collapses")
    print()
    print("Le ratio propagations/collapses indique la \"profondeur\" moyenne du BFS")
    print("après chaque collapse. Il devrait être stable pour une même config")
    print("(seed différent → ordre différent mais ordre de grandeur conservé).")
    print()
    summary = defaultdict(list)
    for r in rows:
        if r["success"] and r["collapses"] > 0:
            summary[(r["label"], r["rows"])].append(r["propagations"] / r["collapses"])
    print("| Label | Size | Ratio prop/collapse (median) | min | max |")
    print("|-------|------|------------------------------|-----|-----|")
    for (label, size), vs in sorted(summary.items()):
        print(f"| {label} | {size}×{size} | {statistics.median(vs):.1f} |"
              f" {min(vs):.1f} | {max(vs):.1f} |")
    print()


def main():
    p = argparse.ArgumentParser()
    p.add_argument("csv")
    p.add_argument("--out", default="-",
                   help="output markdown path, '-' = stdout")
    args = p.parse_args()

    rows = read(args.csv)
    agg = group(rows)

    if args.out == "-":
        write_md(sys.stdout, rows, agg)
    else:
        with open(args.out, "w", encoding="utf-8") as f:
            write_md(f, rows, agg)
        print(f"diagnostic written to {args.out}", file=sys.stderr)


if __name__ == "__main__":
    main()
