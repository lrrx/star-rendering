#!/usr/bin/env python3
"""
Frametime comparison for the "Render Stars" pass.

Reads the four test-case CSVs from the CURRENT directory and draws a
box-style summary per case: the box spans mean +/- 1 standard deviation,
the center line is the mean, and the whiskers reach the min and max.
Every value is annotated. Far and Near get their own panel because their
scales differ by ~20x.

Run from inside the directory that holds the CSV files:
    python plot_render_stars.py
"""

import csv
import statistics
from pathlib import Path

import matplotlib.pyplot as plt

# ---------------------------------------------------------------------------
# Config -- adjust here if needed
# ---------------------------------------------------------------------------
COLUMN = "Render Stars"          # column to analyze (whitespace is stripped)

# The values look like nanoseconds (they line up with the ns Timestamp column),
# so we divide by 1000 to show microseconds. If your numbers are already in the
# unit you want, set DIVISOR = 1.0 and change UNIT accordingly.
DIVISOR = 1000.0
UNIT = "ms"                 # microseconds

# Panels are grouped by scene; within a panel we compare old vs new.
PANELS = [
    ("Far scene",  [("far-old.csv", "old"),  ("far-new.csv", "new")]),
    ("Near scene", [("near-old.csv", "old"), ("near-new.csv", "new")]),
]

COLORS = {"old": "#d1495b", "new": "#3a86ff"}   # red-ish = old, blue = new
OUTFILE = "render_stars_comparison.png"


# ---------------------------------------------------------------------------
# Data loading
# ---------------------------------------------------------------------------
def load_column(path, column):
    """Return the named column as a list of floats, scaled by DIVISOR."""
    with open(path, newline="") as f:
        reader = csv.reader(f)
        header = [h.strip() for h in next(reader)]
        if column not in header:
            raise ValueError(f"{path}: no '{column}' column (have {header})")
        idx = header.index(column)
        out = []
        for row in reader:
            if len(row) <= idx:
                continue
            cell = row[idx].strip()
            if cell:
                out.append(float(cell) / DIVISOR)
    if not out:
        raise ValueError(f"{path}: column '{column}' had no usable values")
    return out


def summarize(values):
    return {
        "mean": statistics.fmean(values),
        "std": statistics.stdev(values) if len(values) > 1 else 0.0,
        "min": min(values),
        "max": max(values),
        "n": len(values),
    }


# ---------------------------------------------------------------------------
# Drawing
# ---------------------------------------------------------------------------
def draw_box(ax, pos, s, color):
    """Draw one mean/std/min/max box at x=pos and annotate the values."""
    stat = {
        "med": s["mean"],            # center line = mean
        "q1": s["mean"] - s["std"],  # box bottom = mean - sigma
        "q3": s["mean"] + s["std"],  # box top    = mean + sigma
        "whislo": s["min"],
        "whishi": s["max"],
        "fliers": [],
        "mean": s["mean"],
    }
    bp = ax.bxp(
        [stat], positions=[pos], widths=0.45, showfliers=False,
        patch_artist=True, zorder=3,
    )
    for box in bp["boxes"]:
        box.set(facecolor=color, alpha=0.30, edgecolor=color, linewidth=1.6)
    for med in bp["medians"]:
        med.set(color=color, linewidth=2.4)
    for part in bp["whiskers"] + bp["caps"]:
        part.set(color=color, linewidth=1.4)

    # Annotations
    ax.annotate(f"max {s['max']:.1f}", (pos, s["max"]),
                xytext=(0, 6), textcoords="offset points",
                ha="center", va="bottom", fontsize=8.5, color=color)
    ax.annotate(f"min {s['min']:.1f}", (pos, s["min"]),
                xytext=(0, -6), textcoords="offset points",
                ha="center", va="top", fontsize=8.5, color=color)
    ax.annotate(f"\u03bc {s['mean']:.1f}\n\u03c3 {s['std']:.1f}",
                (pos + 0.26, s["mean"]),
                xytext=(4, 0), textcoords="offset points",
                ha="left", va="center", fontsize=9, color="black",
                fontweight="bold")
    return stat


def main():
    here = Path.cwd()
    print(f"Reading CSVs from: {here}")

    fig, axes = plt.subplots(
        1, len(PANELS), figsize=(5.2 * len(PANELS), 5.6)
    )
    if len(PANELS) == 1:
        axes = [axes]

    for ax, (panel_title, cases) in zip(axes, PANELS):
        summaries = {}
        for i, (fname, variant) in enumerate(cases, start=1):
            path = here / fname
            if not path.exists():
                print(f"  ! missing {fname}, skipping")
                continue
            s = summarize(load_column(path, COLUMN))
            summaries[variant] = s
            draw_box(ax, i, s, COLORS.get(variant, "#555555"))
            print(f"  {fname:14s} n={s['n']:>4d}  "
                  f"mean={s['mean']:.1f}  std={s['std']:.1f}  "
                  f"min={s['min']:.1f}  max={s['max']:.1f}  {UNIT}")

        ax.set_xticks(range(1, len(cases) + 1))
        ax.set_xticklabels([v.upper() for _, v in cases])
        ax.set_ylabel(f"{COLUMN} time ({UNIT})")
        ax.set_ylim(bottom=0)
        ax.margins(x=0.25)
        ax.grid(axis="y", linestyle=":", alpha=0.5)
        ax.spines[["top", "right"]].set_visible(False)

        # Headline speedup if both variants are present.
        title = panel_title
        if "old" in summaries and "new" in summaries:
            speedup = summaries["old"]["mean"] / summaries["new"]["mean"]
            title += f"   \u2014   new is {speedup:.1f}\u00d7 faster"
        ax.set_title(title, fontsize=12, fontweight="bold", pad=12)

    fig.suptitle(f"'{COLUMN}' frametime comparison  (lower is better)",
                 fontsize=14, fontweight="bold")
    fig.tight_layout(rect=(0, 0, 1, 0.97))
    fig.savefig(OUTFILE, dpi=200, bbox_inches="tight")
    print(f"\nSaved {OUTFILE}")
    plt.show()


if __name__ == "__main__":
    main()
