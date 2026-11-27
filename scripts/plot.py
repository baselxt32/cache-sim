#!/usr/bin/env python3
from __future__ import annotations

import argparse
from collections import defaultdict
import csv
from html import escape
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read_rows(path: Path) -> list[dict]:
    with path.open(newline="") as file:
        return [{key: (value if key in ("workload", "policy") else float(value))
                 for key, value in row.items()} for row in csv.DictReader(file)]


def line_plot(path: Path, title: str, x_labels: list[str], series: list[tuple[str, list[float], str]],
              y_label: str) -> None:
    width, height = 760, 480
    left, right, top, bottom = 82, 30, 65, 70
    plot_w, plot_h = width - left - right, height - top - bottom
    values = [value for _, points, _ in series for value in points]
    max_y = max(values) * 1.08 if values and max(values) else 1
    parts = [f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" viewBox="0 0 {width} {height}">',
             '<rect width="100%" height="100%" fill="white"/>',
             f'<text x="{width/2}" y="30" text-anchor="middle" font-size="20" font-family="sans-serif">{escape(title)}</text>',
             f'<text x="18" y="{top + plot_h/2}" transform="rotate(-90 18 {top + plot_h/2})" text-anchor="middle" font-size="14" font-family="sans-serif">{escape(y_label)}</text>']
    for tick in range(6):
        y = top + plot_h * (1 - tick / 5)
        parts += [f'<line x1="{left}" y1="{y:.2f}" x2="{left+plot_w}" y2="{y:.2f}" stroke="#ddd"/>',
                  f'<text x="{left-10}" y="{y+5:.2f}" text-anchor="end" font-size="12" font-family="sans-serif">{max_y*tick/5:.3f}</text>']
    for index, label in enumerate(x_labels):
        x = left + plot_w * index / max(1, len(x_labels) - 1)
        parts.append(f'<text x="{x:.2f}" y="{height-40}" text-anchor="middle" font-size="12" font-family="sans-serif">{escape(label)}</text>')
    for legend, points, color in series:
        coords = [(left + plot_w * i / max(1, len(points)-1),
                   top + plot_h * (1 - value / max_y)) for i, value in enumerate(points)]
        joined = " ".join(f"{x:.2f},{y:.2f}" for x, y in coords)
        parts.append(f'<polyline points="{joined}" fill="none" stroke="{color}" stroke-width="3"/>')
        parts += [f'<circle cx="{x:.2f}" cy="{y:.2f}" r="4" fill="{color}"/>' for x, y in coords]
    for index, (legend, _, color) in enumerate(series):
        x = left + index * 155
        parts += [f'<rect x="{x}" y="{height-18}" width="14" height="8" fill="{color}"/>',
                  f'<text x="{x+20}" y="{height-10}" font-size="12" font-family="sans-serif">{escape(legend)}</text>']
    path.write_text("\n".join(parts + ["</svg>"]) + "\n")


def breakdown(path: Path, rows: list[dict]) -> None:
    groups = defaultdict(lambda: [0, 0, 0])
    for row in rows:
        group = groups[int(row["size_kib"])]
        for index, key in enumerate(("l1d_compulsory", "l1d_capacity", "l1d_conflict")):
            group[index] += row[key]
    sizes = sorted(groups)
    colors = ("#2864b4", "#df8b24", "#a43d56")
    labels = ("Compulsory", "Capacity", "Conflict")
    width, height = 760, 480
    left, top, plot_w, plot_h = 82, 65, 650, 340
    max_y = max(sum(groups[size]) for size in sizes) * 1.08
    parts = [f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}">',
             '<rect width="100%" height="100%" fill="white"/>',
             '<text x="380" y="30" text-anchor="middle" font-size="20" font-family="sans-serif">L1D miss breakdown across workloads and configs</text>']
    for index, size in enumerate(sizes):
        x = left + index * plot_w / len(sizes) + 15
        y = top + plot_h
        for count, color in zip(groups[size], colors):
            h = plot_h * count / max_y
            y -= h
            parts.append(f'<rect x="{x:.2f}" y="{y:.2f}" width="65" height="{h:.2f}" fill="{color}"/>')
        parts.append(f'<text x="{x+32}" y="{height-45}" text-anchor="middle" font-size="12" font-family="sans-serif">{size} KiB</text>')
    for index, (label, color) in enumerate(zip(labels, colors)):
        x = left + index * 180
        parts += [f'<rect x="{x}" y="{height-20}" width="14" height="9" fill="{color}"/>',
                  f'<text x="{x+20}" y="{height-11}" font-size="12" font-family="sans-serif">{label}</text>']
    path.write_text("\n".join(parts + ["</svg>"]) + "\n")


def weighted_rate(rows: list[dict], key: str, values: list[int]) -> list[float]:
    return [sum(row["l1d_misses"] for row in rows if int(row[key]) == value) /
            sum(row["l1d_accesses"] for row in rows if int(row[key]) == value)
            for value in values]


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--input", type=Path, default=ROOT / "results" / "sweep.csv")
    parser.add_argument("--output", type=Path, default=ROOT / "results")
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    rows = read_rows(args.input)
    sizes = sorted({int(row["size_kib"]) for row in rows})
    ways = sorted({int(row["ways"]) for row in rows})
    line_plot(args.output / "miss_vs_size.svg", "L1D miss rate vs size",
              [f"{size} KiB" for size in sizes],
              [("All policies", weighted_rate(rows, "size_kib", sizes), "#2864b4")],
              "Miss rate")
    line_plot(args.output / "miss_vs_associativity.svg", "L1D miss rate vs associativity",
              [f"{way}-way" for way in ways],
              [("All policies", weighted_rate(rows, "ways", ways), "#a43d56")],
              "Miss rate")
    breakdown(args.output / "miss_breakdown.svg", rows)
    amat = [sum(row["amat"] for row in rows if int(row["size_kib"]) == size) /
            sum(int(row["size_kib"]) == size for row in rows) for size in sizes]
    cpi = [sum(row["estimated_cpi"] for row in rows if int(row["size_kib"]) == size) /
           sum(int(row["size_kib"]) == size for row in rows) for size in sizes]
    line_plot(args.output / "amat_vs_size.svg", "AMAT vs L1D size",
              [f"{size} KiB" for size in sizes], [("AMAT", amat, "#2864b4")], "Cycles/access")
    line_plot(args.output / "cpi_vs_size.svg", "Estimated CPI vs L1D size",
              [f"{size} KiB" for size in sizes], [("CPI", cpi, "#df8b24")], "Cycles/instruction")
    print("Wrote 5 SVG plots")


if __name__ == "__main__":
    main()
