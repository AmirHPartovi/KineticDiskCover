#!/usr/bin/env python3
"""Create benchmark plots and an auto-generated report.

The benchmark JSON schema currently contains aggregate metrics only. The
cost_curves_comparison plot is generated when records also include per-time
cost samples (``cost_curve`` or ``cost_curves``); otherwise the report records
that this plot was skipped.
"""

from __future__ import annotations

import argparse
import json
import math
import re
import sys
from collections import Counter
from pathlib import Path
from typing import Any, Callable, Iterable

import matplotlib

matplotlib.use("Agg")

import matplotlib.pyplot as plt
import pandas as pd
import seaborn as sns


COLORS = {"minmax": "#1f77b4", "minsum": "#d62728"}
LABELS = {"minmax": "Min-Max", "minsum": "Min-Sum"}
OBJECTIVES = ("minmax", "minsum")


def _mode(value: Any) -> str | None:
    normalized = str(value).strip().lower().replace("_", "").replace("-", "")
    if normalized in {"minmax", "max"}:
        return "minmax"
    if normalized in {"minsum", "sum"}:
        return "minsum"
    return None


def _number(record: dict[str, Any], *keys: str) -> float | None:
    for key in keys:
        value = record.get(key)
        if isinstance(value, (int, float)) and math.isfinite(float(value)):
            return float(value)
    return None


def load_results(path: Path) -> pd.DataFrame:
    """Load an array of benchmark records into a normalized DataFrame."""
    with path.open("r", encoding="utf-8") as source:
        payload = json.load(source)
    if isinstance(payload, dict):
        payload = payload.get("results", payload.get("benchmark_results", []))
    if not isinstance(payload, list):
        raise ValueError("benchmark JSON must contain an array of result records")

    rows: list[dict[str, Any]] = []
    for item in payload:
        if not isinstance(item, dict):
            continue
        mode = _mode(item.get("objective", item.get("mode", "")))
        if mode is None:
            continue
        row = dict(item)
        row["mode"] = mode
        row["instance_name"] = str(item.get("instance_name", item.get("name", "")))
        row["n"] = _number(item, "n")
        row["m"] = _number(item, "m")
        row["runtime"] = _number(item, "wall_time_sec", "runtime_sec", "runtime")
        row["memory"] = _number(item, "peak_memory_mb", "memory_mb")
        row["gap_value"] = _number(item, "gap", "relative_gap")
        row["objective_value"] = _number(item, "objective_value", "objective")
        row["integral"] = _number(item, "integral", "total_integral")
        row["peak_cost"] = _number(item, "peak_cost", "peak")
        rows.append(row)
    return pd.DataFrame(rows)


def _style_axis(ax: plt.Axes, title: str, xlabel: str, ylabel: str) -> None:
    ax.set_title(title)
    ax.set_xlabel(xlabel)
    ax.set_ylabel(ylabel)
    ax.grid(True, linestyle="--", alpha=0.35)


def _save_figure(fig: plt.Figure, output_dir: Path, name: str) -> list[str]:
    output_dir.mkdir(parents=True, exist_ok=True)
    paths = [output_dir / f"{name}.png", output_dir / f"{name}.pdf"]
    for path in paths:
        fig.savefig(path, bbox_inches="tight", dpi=160)
    plt.close(fig)
    return [path.name for path in paths]


def _require_columns(frame: pd.DataFrame, columns: Iterable[str]) -> bool:
    return not frame.empty and all(column in frame.columns for column in columns)


def _fixed_value(frame: pd.DataFrame, column: str, requested: int | None) -> int | None:
    values = pd.to_numeric(frame[column], errors="coerce").dropna()
    if requested is not None:
        if (values == requested).any():
            return requested
        return None
    if values.empty:
        return None
    counts = Counter(int(value) for value in values)
    return counts.most_common(1)[0][0]


def _metric_plot(
    frame: pd.DataFrame,
    output_dir: Path,
    name: str,
    metric: str,
    fixed_column: str,
    fixed_value: int | None,
    title: str,
    ylabel: str,
) -> list[str]:
    if not _require_columns(frame, ["n", "m", "mode", metric]):
        return []
    chosen = _fixed_value(frame, fixed_column, fixed_value)
    if chosen is None:
        return []
    data = frame[frame[fixed_column] == chosen].dropna(subset=["n", metric])
    if data.empty:
        return []
    fig, ax = plt.subplots(figsize=(8, 5))
    for mode in OBJECTIVES:
        subset = data[data["mode"] == mode]
        if subset.empty:
            continue
        sns.scatterplot(
            data=subset, x="n", y=metric, color=COLORS[mode], ax=ax,
            label=LABELS[mode], alpha=0.7, s=45,
        )
        means = subset.groupby("n", as_index=False)[metric].mean()
        sns.lineplot(
            data=means, x="n", y=metric, color=COLORS[mode], ax=ax,
            marker="o", legend=False,
        )
    _style_axis(ax, f"{title} (fixed {fixed_column}={chosen})",
                "Number of points (n)", ylabel)
    if metric in {"runtime", "memory"}:
        ax.legend(title="Objective")
    else:
        ax.legend(title="Objective")
    return _save_figure(fig, output_dir, name)


def runtime_vs_n(frame: pd.DataFrame, output_dir: Path,
                 fixed_m: int | None = None) -> list[str]:
    return _metric_plot(frame, output_dir, "runtime_vs_n", "runtime", "m",
                        fixed_m, "Runtime vs. number of points",
                        "Wall time (seconds)")


def runtime_vs_m(frame: pd.DataFrame, output_dir: Path,
                 fixed_n: int | None = None) -> list[str]:
    if not _require_columns(frame, ["n", "m", "mode", "runtime"]):
        return []
    chosen = _fixed_value(frame, "n", fixed_n)
    if chosen is None:
        return []
    data = frame[frame["n"] == chosen].dropna(subset=["m", "runtime"])
    if data.empty:
        return []
    fig, ax = plt.subplots(figsize=(8, 5))
    for mode in OBJECTIVES:
        subset = data[data["mode"] == mode]
        if subset.empty:
            continue
        sns.scatterplot(data=subset, x="m", y="runtime", color=COLORS[mode],
                        ax=ax, label=LABELS[mode], alpha=0.7, s=45)
        means = subset.groupby("m", as_index=False)["runtime"].mean()
        sns.lineplot(data=means, x="m", y="runtime", color=COLORS[mode],
                     ax=ax, marker="o", legend=False)
    _style_axis(ax, f"Runtime vs. number of stations (fixed n={chosen})",
                "Number of stations (m)", "Wall time (seconds)")
    ax.legend(title="Objective")
    return _save_figure(fig, output_dir, "runtime_vs_m")


def memory_vs_n(frame: pd.DataFrame, output_dir: Path,
                fixed_m: int | None = None) -> list[str]:
    return _metric_plot(frame, output_dir, "memory_vs_n", "memory", "m",
                        fixed_m, "Peak memory vs. number of points",
                        "Peak memory (MB)")


def gap_vs_n(frame: pd.DataFrame, output_dir: Path,
             fixed_m: int | None = None) -> list[str]:
    return _metric_plot(frame, output_dir, "gap_vs_n", "gap_value", "m",
                        fixed_m, "Relative gap vs. number of points",
                        "Relative gap")


def objective_vs_n(frame: pd.DataFrame, output_dir: Path,
                   fixed_m: int | None = None) -> list[str]:
    if not _require_columns(frame, ["n", "m", "mode", "objective_value"]):
        return []
    chosen = _fixed_value(frame, "m", fixed_m)
    if chosen is None:
        return []
    data = frame[frame["m"] == chosen].dropna(subset=["n", "objective_value"])
    if data.empty:
        return []
    fig, axes = plt.subplots(1, 2, figsize=(12, 5), sharex=True)
    for axis, mode in zip(axes, OBJECTIVES):
        subset = data[data["mode"] == mode]
        if not subset.empty:
            sns.scatterplot(data=subset, x="n", y="objective_value",
                            color=COLORS[mode], ax=axis, label=LABELS[mode],
                            alpha=0.7, s=45)
            means = subset.groupby("n", as_index=False)["objective_value"].mean()
            sns.lineplot(data=means, x="n", y="objective_value",
                         color=COLORS[mode], ax=axis, marker="o",
                         legend=False)
        unit = "Peak cost" if mode == "minmax" else "Total integral"
        _style_axis(axis, f"{LABELS[mode]} objective (fixed m={chosen})",
                    "Number of points (n)", unit)
        if not subset.empty:
            axis.legend(title="Objective")
    return _save_figure(fig, output_dir, "objective_vs_n")


def _curve_from_record(record: dict[str, Any]) -> list[tuple[float, float]]:
    curve = record.get("cost_curve", record.get("cost_samples"))
    if curve is None and isinstance(record.get("cost_curves"), dict):
        curve = record["cost_curves"].get(_mode(record.get("objective", "")))
    if isinstance(curve, dict):
        curve = curve.get("samples", curve.get("points"))
    points: list[tuple[float, float]] = []
    if not isinstance(curve, list):
        return points
    for item in curve:
        if isinstance(item, dict):
            time = _number(item, "time", "t")
            cost = _number(item, "cost", "value")
        elif isinstance(item, (list, tuple)) and len(item) >= 2:
            time, cost = item[0], item[1]
            if not isinstance(time, (int, float)) or not isinstance(cost, (int, float)):
                continue
        else:
            continue
        if math.isfinite(float(time)) and math.isfinite(float(cost)):
            points.append((float(time), float(cost)))
    return sorted(points)


def cost_curves_comparison(
    records: list[dict[str, Any]], output_dir: Path,
    instance_name: str | None = None,
) -> list[str]:
    selected_name = instance_name
    if selected_name is None:
        selected_name = next(
            (str(record.get("instance_name", "")) for record in records
             if _curve_from_record(record)),
            None,
        )
    if not selected_name:
        return []
    chosen = {
        _mode(record.get("objective", record.get("mode", ""))): record
        for record in records
        if str(record.get("instance_name", record.get("name", ""))) ==
        selected_name and _mode(record.get("objective", record.get("mode", "")))
    }
    curves = {mode: _curve_from_record(record)
              for mode, record in chosen.items()}
    if not any(curves.values()):
        return []

    fig, ax = plt.subplots(figsize=(9, 5))
    for mode in OBJECTIVES:
        points = curves.get(mode, [])
        if not points:
            continue
        times, costs = zip(*points)
        ax.plot(times, costs, color=COLORS[mode], label=LABELS[mode])
        peak_index = max(range(len(costs)), key=costs.__getitem__)
        ax.scatter([times[peak_index]], [costs[peak_index]],
                   color=COLORS[mode], marker="^",
                   label=f"{LABELS[mode]} peak")
        average = _number(chosen[mode], "average_cost", "average",
                          "mean_cost")
        if average is None:
            integral = _number(chosen[mode], "integral", "total_integral")
            duration = times[-1] - times[0]
            if integral is not None and duration > 0:
                average = integral / duration
            elif duration > 0 and len(times) > 1:
                area = sum(
                    0.5 * (costs[index] + costs[index + 1]) *
                    (times[index + 1] - times[index])
                    for index in range(len(times) - 1)
                )
                average = area / duration
        if average is not None:
            ax.axhline(average, color=COLORS[mode], linestyle=":",
                       alpha=0.8, label=f"{LABELS[mode]} average")
    _style_axis(ax, f"Cost curves: {selected_name}", "Time (t)",
                "Cost")
    ax.legend()
    return _save_figure(fig, output_dir, "cost_curves_comparison")


def heatmap_integral_ratio(frame: pd.DataFrame,
                           output_dir: Path) -> list[str]:
    if not _require_columns(frame, ["n", "m", "mode"]):
        return []
    data = frame.copy()
    data["integral_value"] = data["integral"]
    data.loc[(data["mode"] == "minsum") &
             data["integral_value"].isna(), "integral_value"] = data.loc[
                 (data["mode"] == "minsum") &
                 data["integral_value"].isna(), "objective_value"
             ]
    data = data.dropna(subset=["n", "m", "integral_value"])
    ratios: dict[tuple[float, float], float] = {}
    for n in data["n"].unique():
        for m in data["m"].unique():
            max_values = data[(data["n"] == n) & (data["m"] == m) &
                              (data["mode"] == "minmax")]
            sum_values = data[(data["n"] == n) & (data["m"] == m) &
                              (data["mode"] == "minsum")]
            if max_values.empty or sum_values.empty:
                continue
            denominator = float(max_values["objective_value"].mean())
            numerator = float(sum_values["objective_value"].mean())
            if denominator > 0:
                ratios[(float(n), float(m))] = numerator / denominator
    if not ratios:
        return []
    ratio_frame = pd.DataFrame(
        [{"n": n, "m": m, "ratio": ratio}
         for (n, m), ratio in ratios.items()]
    )
    matrix = ratio_frame.pivot(index="n", columns="m", values="ratio")
    fig, ax = plt.subplots(figsize=(8, 6))
    sns.heatmap(matrix, annot=True, fmt=".3g", cmap="RdYlBu_r",
                ax=ax, cbar_kws={"label": "Min-Sum integral / Min-Max peak"})
    _style_axis(ax, "Integral ratio over (n, m)", "Number of stations (m)",
                "Number of points (n)")
    return _save_figure(fig, output_dir, "heatmap_integral_ratio")


def boxplot_gap_pub(frame: pd.DataFrame, output_dir: Path,
                    pub_pattern: str = "pub") -> list[str]:
    if not _require_columns(frame, ["instance_name", "mode", "gap_value"]):
        return []
    pattern = re.compile(pub_pattern, re.IGNORECASE)
    data = frame[frame["instance_name"].astype(str).map(
        lambda name: bool(pattern.search(name)))].dropna(subset=["gap_value"])
    if data.empty:
        return []
    fig, ax = plt.subplots(figsize=(7, 5))
    sns.boxplot(data=data, x="mode", y="gap_value", order=list(OBJECTIVES),
                hue="mode", hue_order=list(OBJECTIVES),
                palette=COLORS, dodge=False, legend=False, ax=ax)
    sns.stripplot(data=data, x="mode", y="gap_value",
                  order=list(OBJECTIVES), color="black", alpha=0.45,
                  size=4, ax=ax)
    ax.set_xticks(range(len(OBJECTIVES)))
    ax.set_xticklabels([LABELS[mode] for mode in OBJECTIVES])
    _style_axis(ax, "Gap distribution on publication (pub) instances",
                "Objective", "Relative gap")
    from matplotlib.patches import Patch

    ax.legend(handles=[
        Patch(facecolor=COLORS[mode], label=LABELS[mode])
        for mode in OBJECTIVES
    ], title="Objective")
    return _save_figure(fig, output_dir, "boxplot_gap_pub")


PLOT_FUNCTIONS: dict[str, Callable[..., list[str]]] = {
    "runtime_vs_n": runtime_vs_n,
    "runtime_vs_m": runtime_vs_m,
    "memory_vs_n": memory_vs_n,
    "gap_vs_n": gap_vs_n,
    "objective_vs_n": objective_vs_n,
    "heatmap_integral_ratio": heatmap_integral_ratio,
    "boxplot_gap_pub": boxplot_gap_pub,
}


def generate_report(
    output_dir: Path,
    generated: dict[str, list[str]],
    skipped: dict[str, str],
    input_path: Path,
    record_count: int,
) -> Path:
    report = output_dir / "REPORT.md"
    lines = [
        "# Benchmark plot report",
        "",
        f"- Input: `{input_path}`",
        f"- Benchmark records loaded: {record_count}",
        "",
        "## Generated plots",
        "",
    ]
    if generated:
        for name, files in generated.items():
            lines.append(f"- **{name}**: {', '.join(f'`{file}`' for file in files)}")
    else:
        lines.append("- No plots were generated; check the input fields and filters.")
    lines.extend(["", "## Skipped plots", ""])
    if skipped:
        for name, reason in skipped.items():
            lines.append(f"- **{name}**: {reason}")
    else:
        lines.append("- None.")
    lines.extend([
        "",
        "## Notes",
        "",
        "- Min-Max is blue (`#1f77b4`); Min-Sum is red (`#d62728`).",
        "- Scatter points show individual records; lines show means by size.",
        "- The heatmap uses Min-Sum total integral / Min-Max total integral "
        "where both modes provide an integral value for the same `(n, m)`.",
        "- The current benchmark summary schema stores aggregate values, not "
        "time-series curves; `cost_curves_comparison` needs `cost_curve` "
        "samples embedded in at least one record for each mode.",
        "",
    ])
    report.write_text("\n".join(lines), encoding="utf-8")
    return report


def create_plots(
    frame: pd.DataFrame,
    records: list[dict[str, Any]],
    output_dir: Path,
    fixed_m: int | None = None,
    fixed_n: int | None = None,
    instance_name: str | None = None,
    pub_pattern: str = "pub",
    input_path: Path = Path("results/json/benchmark.json"),
) -> Path:
    output_dir.mkdir(parents=True, exist_ok=True)
    generated: dict[str, list[str]] = {}
    skipped: dict[str, str] = {}

    for name, function in PLOT_FUNCTIONS.items():
        try:
            if name in {"runtime_vs_n", "memory_vs_n", "gap_vs_n",
                        "objective_vs_n"}:
                files = function(frame, output_dir, fixed_m)
            elif name == "runtime_vs_m":
                files = function(frame, output_dir, fixed_n)
            elif name == "boxplot_gap_pub":
                files = function(frame, output_dir, pub_pattern)
            else:
                files = function(frame, output_dir)
            if files:
                generated[name] = files
            else:
                if name == "heatmap_integral_ratio":
                    skipped[name] = (
                        "No matched (n, m) pairs have total-integral values "
                        "for both objectives. The current aggregate schema "
                        "stores Min-Max peak cost, not its total integral."
                    )
                else:
                    skipped[name] = (
                        "No usable records for the required dimensions and metric."
                    )
        except (KeyError, TypeError, ValueError) as error:
            skipped[name] = f"Could not plot available data: {error}"

    curve_files = cost_curves_comparison(records, output_dir, instance_name)
    if curve_files:
        generated["cost_curves_comparison"] = curve_files
    else:
        skipped["cost_curves_comparison"] = (
            "No per-time cost curve samples were found; aggregate benchmark "
            "records do not contain trajectory cost curves."
        )
    return generate_report(output_dir, generated, skipped, input_path,
                           len(frame))


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--input", type=Path, default=Path("results/json/benchmark.json"),
        help="benchmark JSON input (default: results/json/benchmark.json)",
    )
    parser.add_argument(
        "--output-dir", type=Path, default=Path("results/figures"),
        help="figure and report directory (default: results/figures)",
    )
    parser.add_argument("--fixed-m", type=int, default=None,
                        help="use this station count for plots versus n")
    parser.add_argument("--fixed-n", type=int, default=None,
                        help="use this point count for runtime versus m")
    parser.add_argument("--instance", default=None,
                        help="instance name for the cost-curve plot")
    parser.add_argument("--pub-pattern", default="pub",
                        help="case-insensitive instance-name pattern for the pub gap boxplot")
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)
    try:
        records = json.loads(args.input.read_text(encoding="utf-8"))
        if isinstance(records, dict):
            records = records.get("results",
                                 records.get("benchmark_results", []))
        if not isinstance(records, list):
            raise ValueError("benchmark JSON must contain an array")
        frame = load_results(args.input)
        report = create_plots(
            frame, records, args.output_dir, fixed_m=args.fixed_m,
            fixed_n=args.fixed_n, instance_name=args.instance,
            pub_pattern=args.pub_pattern, input_path=args.input,
        )
        print(f"Plot report written to {report}")
        return 0
    except (OSError, json.JSONDecodeError, ValueError) as error:
        print(f"plot_results.py: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
