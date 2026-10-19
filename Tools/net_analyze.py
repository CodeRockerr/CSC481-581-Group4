#!/usr/bin/env python3
"""Summarize and plot the Part 1C networking experiments.

    python3 Tools/net_analyze.py Docs/Results/net_results.csv [OUT_DIR]

For each case, a run's value is the mean over its clients (one client in E1). Across the REPS runs
we report the arithmetic mean and the SAMPLE variance (n - 1) of the elapsed time for the fixed
number of iterations, plus mean bytes per snapshot. Writes summary.csv, summary.md and one PNG per
experiment into OUT_DIR (default: the CSV's folder).
"""
import csv
import os
import statistics
import sys
from collections import defaultdict

EXPERIMENTS = {
    "E1": ("server_objects", "Moving objects in scene (1 client)"),
    "E2": ("clients", "Connected clients (100 moving objects)"),
    "E3": ("owned_per_client", "Objects owned by each client (2 clients)"),
}


def load(path):
    runs = defaultdict(list)  # (exp, fmt, x, rep) -> [elapsed_ms per client]
    sizes = defaultdict(list)
    iterations = None
    with open(path, newline="") as f:
        for row in csv.DictReader(f):
            exp = row["experiment"]
            x = int(row[EXPERIMENTS[exp][0]])
            key = (exp, row["format"], x, int(row["rep"]))
            runs[key].append(float(row["elapsed_ms"]))
            sizes[(exp, row["format"], x)].append(float(row["bytes_per_snapshot"]))
            iterations = int(row["iterations"])
    return runs, sizes, iterations


def summarize(runs, sizes):
    per_case = defaultdict(list)
    for (exp, fmt, x, _rep), values in runs.items():
        per_case[(exp, fmt, x)].append(statistics.fmean(values))
    rows = []
    for (exp, fmt, x), values in sorted(per_case.items()):
        rows.append({
            "experiment": exp,
            "format": fmt,
            "x": x,
            "runs": len(values),
            "mean_ms": statistics.fmean(values),
            "variance_ms2": statistics.variance(values) if len(values) > 1 else 0.0,
            "stdev_ms": statistics.stdev(values) if len(values) > 1 else 0.0,
            "bytes_per_snapshot": statistics.fmean(sizes[(exp, fmt, x)]),
        })
    return rows


def write_tables(rows, iterations, out_dir):
    with open(os.path.join(out_dir, "summary.csv"), "w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=list(rows[0].keys()))
        writer.writeheader()
        writer.writerows(rows)

    lines = [f"Elapsed time for {iterations} loop iterations: mean and sample variance over runs.", ""]
    for exp, (_col, label) in EXPERIMENTS.items():
        lines += [f"## {exp}: {label}", "",
                  "| x | format | runs | mean (ms) | variance (ms²) | std dev (ms) | bytes/snapshot |",
                  "|---|---|---|---|---|---|---|"]
        for r in rows:
            if r["experiment"] == exp:
                lines.append(f"| {r['x']} | {r['format']} | {r['runs']} | {r['mean_ms']:.1f} | "
                             f"{r['variance_ms2']:.1f} | {r['stdev_ms']:.1f} | {r['bytes_per_snapshot']:.0f} |")
        lines.append("")
    with open(os.path.join(out_dir, "summary.md"), "w") as f:
        f.write("\n".join(lines))


def plot(rows, iterations, out_dir):
    try:
        import matplotlib
        matplotlib.use("Agg")
        import matplotlib.pyplot as plt
    except ImportError:
        print("matplotlib not installed; skipping charts")
        return
    colors = {"binary": "#2a6fdb", "text": "#e07a1f"}
    for n, (exp, (_col, label)) in enumerate(EXPERIMENTS.items(), start=1):
        fig, ax = plt.subplots(figsize=(6.4, 4.0), dpi=150)
        for fmt in ("binary", "text"):
            data = sorted((r["x"], r["mean_ms"], r["stdev_ms"]) for r in rows
                          if r["experiment"] == exp and r["format"] == fmt)
            if not data:
                continue
            xs, ys, es = zip(*data)
            ax.errorbar(xs, ys, yerr=es, marker="o", capsize=4, linewidth=2, color=colors[fmt], label=fmt)
        ax.set_title(f"Figure {n}. {exp}: {label}")
        ax.set_xlabel(label)
        ax.set_ylabel(f"Time for {iterations} iterations (ms)")
        xs_all = sorted({r["x"] for r in rows if r["experiment"] == exp})
        ax.set_xticks(xs_all)
        ax.grid(True, alpha=0.3)
        ax.legend(title="Wire format")
        fig.tight_layout()
        fig.savefig(os.path.join(out_dir, f"figure{n}_{exp}.png"))
        plt.close(fig)


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 1
    path = sys.argv[1]
    out_dir = sys.argv[2] if len(sys.argv) > 2 else os.path.dirname(os.path.abspath(path))
    os.makedirs(out_dir, exist_ok=True)
    runs, sizes, iterations = load(path)
    rows = summarize(runs, sizes)
    write_tables(rows, iterations, out_dir)
    plot(rows, iterations, out_dir)
    print(open(os.path.join(out_dir, "summary.md")).read())
    return 0


if __name__ == "__main__":
    sys.exit(main())
