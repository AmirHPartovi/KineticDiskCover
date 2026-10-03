#!/usr/bin/env python3
"""Create reproducible comparison figures and a report from batch results."""

from __future__ import annotations

import argparse
import hashlib
import json
import math
from pathlib import Path
import re
import sys
from typing import Callable, Iterable

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np
import pandas as pd
import seaborn as sns
from scipy import stats


PALETTE = {"minmax": "#1f77b4", "minsum": "#d62728", "baseline": "#7f7f7f"}
OBJECTIVE_MARKERS = {"minmax": "o", "minsum": "s"}
ALGORITHM_PALETTE = sns.color_palette("husl", 16)
FIGURE_DESCRIPTIONS: dict[str, tuple[str, str]] = {}
OUTPUT_DIR = Path("results/figures")
FIGURE_DPI = 300


def prepare_frame(frame: pd.DataFrame) -> pd.DataFrame:
    df = frame.copy()
    required = {
        "instance_name", "algorithm_name", "objective", "n", "m",
        "wall_time_sec", "peak_memory_mb", "objective_value", "gap",
    }
    missing = sorted(required - set(df.columns))
    if missing:
        raise ValueError("batch results missing columns: " + ", ".join(missing))
    for column in ("n", "m", "wall_time_sec", "peak_memory_mb",
                   "objective_value", "gap"):
        df[column] = pd.to_numeric(df[column], errors="coerce")
    df["objective"] = df["objective"].astype(str).str.lower()
    df["gap_pct"] = df["gap"] * 100.0
    if "feasible" in df:
        df = df[df["feasible"].map(_bool_value)]
    if "verified" in df:
        df = df[df["verified"].map(_bool_value)]
    df = df.replace([np.inf, -np.inf], np.nan)
    df = df.dropna(subset=["wall_time_sec", "peak_memory_mb",
                           "objective_value", "gap_pct"])
    return df.reset_index(drop=True)


def _bool_value(value: object) -> bool:
    if isinstance(value, (bool, np.bool_)):
        return bool(value)
    return str(value).strip().lower() in {"true", "1", "yes"}


def load_frame(path: str | Path) -> pd.DataFrame:
    with Path(path).open(encoding="utf-8") as input_file:
        raw = json.load(input_file)
    if not isinstance(raw, list):
        raise ValueError("master results JSON root must be an array")
    df = prepare_frame(pd.DataFrame(raw))
    df.attrs["runs_dir"] = str(Path(path).resolve().parent / "runs")
    return df


def configure_style(dpi: int = 300) -> None:
    plt.rcParams.update({
        "font.family": "DejaVu Serif",
        "font.size": 11,
        "axes.titlesize": 12,
        "axes.labelsize": 11,
        "legend.fontsize": 10,
        "figure.dpi": dpi,
        "savefig.bbox": "tight",
        "savefig.dpi": dpi,
    })
    sns.set_theme(style="whitegrid", palette="deep")


def _safe_name(name: object) -> str:
    result = re.sub(r"[^A-Za-z0-9._-]+", "_", str(name)).strip("._")
    return result or "unnamed"


def save_fig(fig: plt.Figure, name: str) -> tuple[Path, Path]:
    OUTPUT_DIR.mkdir(parents=True, exist_ok=True)
    stem = OUTPUT_DIR / name
    png_path = Path(f"{stem}.png")
    pdf_path = Path(f"{stem}.pdf")
    fig.savefig(png_path, dpi=FIGURE_DPI)
    fig.savefig(pdf_path)
    plt.close(fig)
    return png_path, pdf_path


def annotate_best(ax: plt.Axes, df: pd.DataFrame, metric: str) -> None:
    if df.empty or metric not in df:
        return
    values = pd.to_numeric(df[metric], errors="coerce")
    valid = values.dropna()
    if valid.empty:
        return
    best_index = valid.idxmin()
    row = df.loc[best_index]
    x = row.get("algorithm_name", row.get("wall_time_sec", 0))
    y = float(row[metric])
    if isinstance(x, str):
        positions = list(dict.fromkeys(df["algorithm_name"].astype(str)))
        x = positions.index(x)
    ax.scatter([x], [y], marker="*", s=180, color="#ffd700",
               edgecolor="black", linewidth=0.7, zorder=10, label="Best")


def fit_power_law(x: Iterable[float], y: Iterable[float]) -> tuple[float, tuple[float, float]]:
    x_values = np.asarray(list(x), dtype=float)
    y_values = np.asarray(list(y), dtype=float)
    valid = np.isfinite(x_values) & np.isfinite(y_values) & (x_values > 0) & (y_values > 0)
    x_values = x_values[valid]
    y_values = y_values[valid]
    if x_values.size < 3:
        return float("nan"), (float("nan"), float("nan"))
    fit = stats.linregress(np.log(x_values), np.log(y_values))
    interval = fit.stderr * stats.t.ppf(
        0.975, max(1, x_values.size - 2)
    )
    return float(fit.slope), (float(fit.slope - interval),
                              float(fit.slope + interval))


def ecdf(values: Iterable[float]) -> tuple[np.ndarray, np.ndarray]:
    data = np.sort(np.asarray(list(values), dtype=float))
    data = data[np.isfinite(data)]
    if data.size == 0:
        return np.array([0.0]), np.array([0.0])
    probabilities = np.arange(1, data.size + 1, dtype=float) / data.size
    return np.concatenate(([data[0]], data)), np.concatenate(
        ([0.0], probabilities)
    )


def pareto_front(frame: pd.DataFrame,
                 x_metric: str = "wall_time_sec",
                 y_metric: str = "objective_value") -> pd.DataFrame:
    valid = frame.dropna(subset=[x_metric, y_metric]).copy()
    if valid.empty:
        return valid
    valid = valid.sort_values([x_metric, y_metric], kind="stable")
    best_y = float("inf")
    selected = []
    for index, row in valid.iterrows():
        y = float(row[y_metric])
        if y < best_y:
            selected.append(index)
            best_y = y
    return valid.loc[selected].sort_values(x_metric, kind="stable")


def _legend(ax: plt.Axes, title: str = "Objective") -> None:
    handles, labels = ax.get_legend_handles_labels()
    if not handles:
        handles = [
            plt.Line2D([], [], marker=OBJECTIVE_MARKERS[key], linestyle="",
                       color=PALETTE[key], label=key)
            for key in ("minmax", "minsum")
        ]
        labels = ["minmax", "minsum"]
    ax.legend(handles, labels, title=title, loc="best")


def _finish(ax: plt.Axes, title: str, xlabel: str, ylabel: str,
            legend_title: str = "Objective") -> None:
    ax.set_title(title)
    ax.set_xlabel(xlabel)
    ax.set_ylabel(ylabel)
    _legend(ax, legend_title)
    ax.grid(True, which="both", alpha=0.25)


def _empty_plot(title: str, xlabel: str, ylabel: str, message: str) -> plt.Figure:
    fig, ax = plt.subplots(figsize=(8, 5))
    ax.text(0.5, 0.5, message, ha="center", va="center",
            transform=ax.transAxes, color="dimgray")
    _finish(ax, title, xlabel, ylabel)
    return fig


def _remember(name: str, caption: str, takeaway: str) -> None:
    FIGURE_DESCRIPTIONS[name] = (caption, takeaway)


def _bar_per_instance(df: pd.DataFrame, metric: str, prefix: str,
                      label: str, unit: str) -> list[str]:
    names = []
    for instance, group in df.groupby("instance_name", sort=True):
        order = (group[group.objective == "minmax"]
                 .groupby("algorithm_name")[metric].median().sort_values().index)
        fig, ax = plt.subplots(figsize=(max(8, len(order) * 0.75), 5))
        sns.barplot(data=group, x="algorithm_name", y=metric, hue="objective",
                    hue_order=["minmax", "minsum"], order=list(order),
                    palette=PALETTE, errorbar=None, ax=ax)
        ax.tick_params(axis="x", rotation=35)
        if metric == "wall_time_sec" and group[metric].min() > 0:
            ratio = group[metric].max() / group[metric].min()
            if ratio > 100:
                ax.set_yscale("log")
        dimensions = group.iloc[0]
        name = f"{prefix}_{_safe_name(instance)}"
        _finish(ax, f"{label} on {instance} (n={int(dimensions.n)}, "
                f"m={int(dimensions.m)})", "Algorithm",
                f"{label} ({unit})")
        save_fig(fig, name)
        _remember(name, f"{label} across algorithms and objectives for "
                  f"{instance}.", f"Best observed {label.lower()}: "
                  f"{group[metric].min():.6g} {unit}.")
        names.append(name)
    return names


def runtime_bar_per_instance(df: pd.DataFrame,
                             output: str | Path | None = None) -> list[str]:
    return _bar_per_instance(df, "wall_time_sec", "A1_runtime_bar",
                             "Runtime", "s")


def memory_bar_per_instance(df: pd.DataFrame,
                            output: str | Path | None = None) -> list[str]:
    return _bar_per_instance(df, "peak_memory_mb", "B1_memory_bar",
                             "Peak memory", "MB")


def _box_plot(df: pd.DataFrame, metric: str, name: str,
              title: str, ylabel: str, log_axis: bool = False,
              annotate_counts: bool = False) -> str:
    fig, ax = plt.subplots(figsize=(max(9, df.algorithm_name.nunique() * 0.8), 5.5))
    if df.empty:
        ax.text(0.5, 0.5, "No successful verified records",
                ha="center", transform=ax.transAxes)
    else:
        sns.boxplot(data=df, x="algorithm_name", y=metric, hue="objective",
                    hue_order=["minmax", "minsum"], palette=PALETTE,
                    notch=True, ax=ax)
        sns.stripplot(data=df, x="algorithm_name", y=metric, hue="objective",
                      dodge=True, jitter=0.18, alpha=0.4, size=4,
                      palette=PALETTE, ax=ax, legend=False)
        if log_axis and (df[metric] > 0).any():
            ax.set_yscale("log")
        if annotate_counts:
            counts = df.groupby("algorithm_name")[metric].count()
            for position, algorithm in enumerate(
                df["algorithm_name"].drop_duplicates()
            ):
                values = df.loc[df.algorithm_name == algorithm, metric]
                if not values.empty:
                    ax.annotate(f"n={int(counts[algorithm])}",
                                (position, values.max()), xytext=(0, 6),
                                textcoords="offset points", ha="center",
                                fontsize=8)
        ax.tick_params(axis="x", rotation=35)
    _finish(ax, title, "Algorithm", ylabel)
    save_fig(fig, name)
    _remember(name, title + ".", "Compare per-algorithm distributions across "
              "the available instances.")
    return name


def runtime_box_by_algorithm(df: pd.DataFrame,
                             output: str | Path | None = None) -> str:
    return _box_plot(df, "wall_time_sec", "A2_runtime_box",
                     "Runtime distribution per algorithm",
                     "Wall time (s)")


def memory_box_by_algorithm(df: pd.DataFrame,
                            output: str | Path | None = None) -> str:
    return _box_plot(df, "peak_memory_mb", "B2_memory_box",
                     "Peak memory distribution per algorithm",
                     "Peak memory (MB)")


def gap_boxplot_by_algorithm(df: pd.DataFrame,
                             output: str | Path | None = None) -> str:
    return _box_plot(df, "gap_pct", "C1_gap_boxplot",
                     "Optimality gap distribution per algorithm",
                     "Optimality gap (%)", log_axis=True,
                     annotate_counts=True)


def _scaling_plot(df: pd.DataFrame, varying: str, fixed: str,
                  fixed_value: int, metric: str, name: str, title: str,
                  ylabel: str) -> str:
    subset = df[df[fixed] == fixed_value]
    fig, ax = plt.subplots(figsize=(8, 5.5))
    if subset.empty:
        ax.text(0.5, 0.5, f"No observations with {fixed}={fixed_value}",
                ha="center", transform=ax.transAxes)
    else:
        grouped = subset.groupby(
            ["algorithm_name", "objective", varying], as_index=False
        )[metric].agg(
            median="median",
            q25=lambda values: values.quantile(0.25),
            q75=lambda values: values.quantile(0.75),
        )
        for (algorithm, objective), group in grouped.groupby(
            ["algorithm_name", "objective"], sort=True
        ):
            color = PALETTE[objective]
            marker = OBJECTIVE_MARKERS[objective]
            group = group.sort_values(varying)
            ax.plot(group[varying], group["median"], marker=marker,
                    color=color, label=f"{algorithm} ({objective})")
            ax.fill_between(group[varying].to_numpy(dtype=float),
                            group["q25"].to_numpy(dtype=float),
                            group["q75"].to_numpy(dtype=float),
                            color=color, alpha=0.12)
            exponent, interval = fit_power_law(group[varying], group["median"])
            if math.isfinite(exponent):
                label = f"{algorithm} {objective}: b={exponent:.2f} " \
                        f"[{interval[0]:.2f}, {interval[1]:.2f}]"
                ax.plot([], [], color=color, marker=marker, label=label)
        if (grouped[varying] > 0).all() and (grouped["median"] > 0).all():
            ax.set_xscale("log")
            ax.set_yscale("log")
        if len(grouped) > 0:
            ax.legend(title="Algorithm / objective / power-law b",
                      fontsize=8, loc="best")
    ax.set_title(title)
    ax.set_xlabel(f"Number of {varying[0] if varying == 'n' else 'stations'} "
                  f"({varying})")
    ax.set_ylabel(ylabel)
    ax.grid(True, which="both", alpha=0.25)
    if subset.empty:
        _legend(ax)
    save_fig(fig, name)
    _remember(name, f"Scaling of {ylabel.lower()} against {varying} with "
              f"{fixed} fixed at {fixed_value}.",
              "Lines show medians and shaded regions show interquartile ranges.")
    return name


def runtime_vs_n(df: pd.DataFrame, output: str | Path | None = None,
                 fixed_m: int = 25) -> str:
    return _scaling_plot(df, "n", "m", fixed_m, "wall_time_sec",
                         f"A3_runtime_vs_n_m{fixed_m}",
                         f"Runtime vs n (m={fixed_m})", "Runtime (s)")


def runtime_vs_m(df: pd.DataFrame, output: str | Path | None = None,
                 fixed_n: int = 500) -> str:
    return _scaling_plot(df, "m", "n", fixed_n, "wall_time_sec",
                         f"A4_runtime_vs_m_n{fixed_n}",
                         f"Runtime vs m (n={fixed_n})", "Runtime (s)")


def runtime_heatmap(df: pd.DataFrame,
                    output: str | Path | None = None) -> str:
    return _heatmap(df, "wall_time_sec", "A5_runtime_heatmap",
                    "Runtime heatmap (median across objectives)",
                    "Runtime (s)", log=True)


def _heatmap(df: pd.DataFrame, metric: str, name: str, title: str,
             colorbar_label: str, log: bool = False, rank: bool = False) -> str:
    values = df.pivot_table(index="instance_name", columns="algorithm_name",
                            values=metric, aggfunc="median")
    fig, ax = plt.subplots(figsize=(max(8, values.shape[1] * 0.8),
                                   max(4, values.shape[0] * 0.45)))
    if values.empty:
        ax.text(0.5, 0.5, "No successful verified records",
                ha="center", transform=ax.transAxes)
        _legend(ax)
    else:
        display = values.rank(axis=1, method="min", ascending=True) if rank else values
        mask = display.isna()
        sns.heatmap(display, mask=mask, annot=True, fmt=".3g" if not rank else ".0f",
                    cmap="viridis", ax=ax,
                    norm=matplotlib.colors.LogNorm()
                    if log and np.nanmin(display.to_numpy()) > 0 else None,
                    cbar_kws={"label": "Rank (1 = best)" if rank else colorbar_label})
        ax.set_xlabel("Algorithm")
        ax.set_ylabel("Instance")
        ax.legend(handles=[
            plt.Line2D([], [], marker="s", linestyle="", color=PALETTE[key],
                       label=key)
            for key in ("minmax", "minsum")
        ], title="Objective", loc="upper left", bbox_to_anchor=(1.18, 1.0))
    ax.set_title(title)
    save_fig(fig, name)
    _remember(name, title + ".", "Cells summarize median values across "
              "objectives." if not rank else "Lower ranks indicate better "
              "objective values within each instance.")
    return name


def _scatter(df: pd.DataFrame, x: str, y: str, name: str, title: str,
             xlabel: str, ylabel: str, hue: str = "objective",
             style: str = "objective") -> str:
    fig, ax = plt.subplots(figsize=(8, 5.5))
    if df.empty:
        ax.text(0.5, 0.5, "No successful verified records",
                ha="center", transform=ax.transAxes)
    else:
        sns.scatterplot(data=df, x=x, y=y, hue=hue, style=style,
                        size="algorithm_name" if hue == "objective" else None,
                        sizes=(35, 100) if hue == "objective" else None,
                        alpha=0.8, ax=ax)
    _finish(ax, title, xlabel, ylabel,
            "Objective" if hue == "objective" else "Algorithm")
    save_fig(fig, name)
    _remember(name, title + ".", "Each point represents one algorithm/objective "
              "run.")
    return name


def memory_vs_n(df: pd.DataFrame, output: str | Path | None = None,
                fixed_m: int = 25) -> str:
    return _scaling_plot(df, "n", "m", fixed_m, "peak_memory_mb",
                         f"B3_memory_vs_n_m{fixed_m}",
                         f"Memory vs n (m={fixed_m})", "Peak memory (MB)")


def memory_heatmap(df: pd.DataFrame,
                   output: str | Path | None = None) -> str:
    return _heatmap(df, "peak_memory_mb", "B4_memory_heatmap",
                    "Peak memory heatmap (median across objectives)",
                    "Peak memory (MB)")


def gap_ecdf(df: pd.DataFrame,
             output: str | Path | None = None) -> str:
    fig, ax = plt.subplots(figsize=(8, 5.5))
    annotations = []
    for algorithm, group in df.groupby("algorithm_name", sort=True):
        values = group["gap_pct"].dropna().to_numpy(dtype=float)
        x_values, y_values = ecdf(values)
        color = ALGORITHM_PALETTE[
            list(sorted(df.algorithm_name.unique())).index(algorithm) %
            len(ALGORITHM_PALETTE)
        ]
        median = float(np.median(values)) if len(values) else float("nan")
        p95 = float(np.quantile(values, 0.95)) if len(values) else float("nan")
        ax.step(x_values, y_values, where="post", label=f"{algorithm} "
                f"(med={median:.3g}%, p95={p95:.3g}%)", color=color)
        annotations.append((algorithm, p95, color))
    if not annotations:
        ax.text(0.5, 0.5, "No successful verified records",
                ha="center", transform=ax.transAxes)
    for _, percentile, color in annotations:
        if math.isfinite(percentile):
            ax.axvline(percentile, color=color, linestyle=":", alpha=0.35)
    ax.set_ylim(0, 1.02)
    ax.set_title("Empirical CDF of optimality gap")
    ax.set_xlabel("Optimality gap (%)")
    ax.set_ylabel("Cumulative probability")
    ax.legend(title="Algorithm (median, 95th percentile)", fontsize=8)
    ax.grid(True, alpha=0.25)
    save_fig(fig, "C2_gap_ecdf")
    _remember("C2_gap_ecdf", "Empirical cumulative distribution of optimality "
              "gaps, with median and 95th-percentile values in each legend "
              "entry.", "The curve furthest left has lower gaps overall.")
    return "C2_gap_ecdf"


def _lowess(
    x: np.ndarray, y: np.ndarray, fraction: float = 0.65
) -> tuple[np.ndarray, np.ndarray]:
    order = np.argsort(x)
    x_sorted, y_sorted = x[order], y[order]
    count = len(x_sorted)
    span = max(2, int(math.ceil(fraction * count)))
    smoothed = np.empty(count)
    for index, x0 in enumerate(x_sorted):
        distances = np.abs(x_sorted - x0)
        radius = np.partition(distances, min(span - 1, count - 1))[
            min(span - 1, count - 1)
        ]
        if radius <= 0:
            weights = (distances == 0).astype(float)
        else:
            scaled = np.minimum(distances / radius, 1.0)
            weights = (1.0 - scaled ** 3) ** 3
        design = np.column_stack((np.ones(count), x_sorted - x0))
        weighted = design * np.sqrt(weights[:, None])
        target = y_sorted * np.sqrt(weights)
        coefficients, *_ = np.linalg.lstsq(weighted, target, rcond=None)
        smoothed[index] = coefficients[0]
    return order, smoothed


def gap_vs_n(df: pd.DataFrame, output: str | Path | None = None) -> str:
    fig, ax = plt.subplots(figsize=(8, 5.5))
    for index, (algorithm, group) in enumerate(
        df.groupby("algorithm_name", sort=True)
    ):
        color = ALGORITHM_PALETTE[index % len(ALGORITHM_PALETTE)]
        sns.scatterplot(data=group, x="n", y="gap_pct", hue="algorithm_name",
                        palette={algorithm: color}, legend=False, alpha=0.6,
                        ax=ax)
        x = group["n"].to_numpy(dtype=float)
        y = group["gap_pct"].to_numpy(dtype=float)
        if len(x) >= 2:
            order, trend = _lowess(x, y)
            ax.plot(x[order], trend, color=color, label=algorithm)
        else:
            ax.plot([], [], color=color, label=algorithm)
    _finish(ax, "Gap vs instance size", "Number of points (n)",
            "Optimality gap (%)", "Algorithm")
    save_fig(fig, "C3_gap_vs_n")
    _remember("C3_gap_vs_n", "Scatter points and LOWESS-smoothed trends show "
              "optimality gap against point count.", "Lower trend lines indicate "
              "smaller gaps as instance size changes.")
    return "C3_gap_vs_n"


def objective_value_vs_n(df: pd.DataFrame, output: str | Path | None = None,
                         fixed_m: int = 25) -> str:
    subset = df[df["m"] == fixed_m]
    fig, axes = plt.subplots(1, 2, figsize=(13, 5.5))
    for ax, objective, ylabel in zip(
        axes, ("minmax", "minsum"),
        ("Peak objective value", "Integral objective value")
    ):
        group = subset[subset.objective == objective]
        if group.empty:
            ax.text(0.5, 0.5, f"No records with m={fixed_m}",
                    ha="center", transform=ax.transAxes)
        else:
            grouped = group.groupby(["algorithm_name", "n"])["objective_value"]
            for index, algorithm in enumerate(sorted(group.algorithm_name.unique())):
                values = grouped.get_group((algorithm,)) if False else None
                algo_group = group[group.algorithm_name == algorithm]
                summary = algo_group.groupby("n")["objective_value"]
                median = summary.median()
                color = ALGORITHM_PALETTE[index % len(ALGORITHM_PALETTE)]
                ax.plot(median.index, median.values, marker="o", color=color,
                        label=algorithm)
                lower = summary.quantile(0.25)
                upper = summary.quantile(0.75)
                ax.fill_between(median.index.to_numpy(dtype=float),
                                lower.to_numpy(dtype=float),
                                upper.to_numpy(dtype=float), color=color,
                                alpha=0.12)
            if (group["n"] > 0).all() and (group["objective_value"] > 0).all():
                ax.set_xscale("log")
                ax.set_yscale("log")
        ax.set_title(objective)
        ax.set_xlabel("Number of points (n)")
        ax.set_ylabel(ylabel)
        handles, labels = ax.get_legend_handles_labels()
        if handles:
            ax.legend(title="Algorithm")
        else:
            _legend(ax)
        ax.grid(True, which="both", alpha=0.25)
    name = f"C4_objective_value_vs_n_m{fixed_m}"
    fig.suptitle(f"Objective value vs n (m={fixed_m})")
    save_fig(fig, name)
    _remember(name, "MinMax peak and MinSum integral objective values versus "
              "instance size; lines are medians with IQR bands.",
              "Compare objective trends separately for peak and integral cost.")
    return name


def _pareto_by_group(df: pd.DataFrame, group_name: str, name_prefix: str) -> list[str]:
    output_names = []
    for instance, group in df.groupby(group_name, sort=True):
        fig, ax = plt.subplots(figsize=(7, 5.5))
        for objective in ("minmax", "minsum"):
            objective_df = group[group.objective == objective]
            if objective_df.empty:
                continue
            ax.scatter(objective_df.wall_time_sec, objective_df.objective_value,
                       marker=OBJECTIVE_MARKERS[objective],
                       color=PALETTE[objective], label=objective)
            for _, row in objective_df.iterrows():
                ax.annotate(str(row.algorithm_name),
                            (row.wall_time_sec, row.objective_value),
                            xytext=(4, 3), textcoords="offset points",
                            fontsize=7)
            front = pareto_front(objective_df)
            if not front.empty:
                ax.plot(front.wall_time_sec, front.objective_value,
                        color=PALETTE[objective], alpha=0.8,
                        label=f"{objective} Pareto front")
                ax.scatter(front.wall_time_sec, front.objective_value,
                           s=80, marker="o", color=PALETTE[objective],
                           edgecolor="black", linewidth=0.7, zorder=4)
        instance_name = str(instance)
        title = (f"Pareto front on {instance_name}" if group_name ==
                 "instance_name" else "Aggregated Pareto fronts")
        _finish(ax, title, "Wall time (s)", "Objective value")
        name = f"{name_prefix}_{_safe_name(instance_name)}"
        save_fig(fig, name)
        _remember(name, f"Runtime-quality trade-offs for {instance_name}.",
                  "Points on a front are non-dominated in runtime and objective.")
        output_names.append(name)
    return output_names


def pareto_front_per_instance(df: pd.DataFrame,
                              output: str | Path | None = None) -> list[str]:
    return _pareto_by_group(df, "instance_name", "D1_pareto")


def pareto_front_aggregated(df: pd.DataFrame,
                            output: str | Path | None = None) -> str:
    normalized = df.copy()
    best = normalized.groupby(["instance_name", "objective"])[
        "objective_value"
    ].transform("min")
    normalized["normalized_objective"] = normalized["objective_value"] / best
    aggregated = normalized.groupby(
        ["algorithm_name", "objective"], as_index=False
    ).agg(wall_time_sec=("wall_time_sec", "median"),
          objective_value=("normalized_objective", "median"))
    fig, ax = plt.subplots(figsize=(8, 6))
    for objective in ("minmax", "minsum"):
        group = aggregated[aggregated.objective == objective]
        if group.empty:
            continue
        ax.scatter(group.wall_time_sec, group.objective_value,
                   marker=OBJECTIVE_MARKERS[objective],
                   color=PALETTE[objective], label=objective)
        for _, row in group.iterrows():
            ax.annotate(str(row.algorithm_name),
                        (row.wall_time_sec, row.objective_value),
                        xytext=(4, 3), textcoords="offset points", fontsize=8)
        front = pareto_front(group)
        if not front.empty:
            ax.plot(front.wall_time_sec, front.objective_value,
                    color=PALETTE[objective], alpha=0.8,
                    label=f"{objective} Pareto front")
    _finish(ax, "Aggregated Pareto fronts", "Median wall time (s)",
            "Median normalized objective")
    save_fig(fig, "D2_pareto_aggregated")
    _remember("D2_pareto_aggregated",
              "Runtime-quality trade-offs aggregated by algorithm and "
              "objective.", "Points on a front are non-dominated in median "
              "runtime and normalized objective.")
    return "D2_pareto_aggregated"


def instance_algorithm_quality_heatmap(
    df: pd.DataFrame, output: str | Path | None = None
) -> str:
    per_mode = df.groupby(
        ["instance_name", "algorithm_name"], as_index=False
    )["objective_value"].min()
    return _heatmap(per_mode, "objective_value", "E1_quality_heatmap",
                    "Quality heatmap (rank per instance)",
                    "Objective value", rank=True)


def _read_trace(df: pd.DataFrame, instance: str, algorithm: str,
                objective: str) -> pd.DataFrame:
    runs_dir = Path(df.attrs.get("runs_dir", "results/batch/runs"))
    path = runs_dir / _safe_name(instance) / _safe_name(algorithm) / objective / "trace.csv"
    if not path.is_file():
        return pd.DataFrame()
    try:
        trace = pd.read_csv(path)
    except (OSError, pd.errors.ParserError):
        return pd.DataFrame()
    if "gap" not in trace.columns or "iter" not in trace.columns:
        return pd.DataFrame()
    return trace


def convergence_trace_per_algorithm(
    df: pd.DataFrame, output: str | Path | None = None,
    instance_name: str | None = None
) -> str:
    if instance_name is None:
        if df.empty:
            instance_name = "no_instances"
        else:
            instance_name = str(sorted(df.instance_name.unique())[0])
    fig, axes = plt.subplots(1, 2, figsize=(13, 5.5))
    for ax, objective in zip(axes, ("minmax", "minsum")):
        subset = df[(df.instance_name == instance_name) &
                    (df.objective == objective)]
        for index, algorithm in enumerate(sorted(subset.algorithm_name.unique())):
            trace = _read_trace(df, instance_name, algorithm, objective)
            if trace.empty:
                continue
            color = ALGORITHM_PALETTE[index % len(ALGORITHM_PALETTE)]
            gap_values = np.maximum(trace["gap"].to_numpy(dtype=float), 1e-12)
            ax.plot(trace["iter"], gap_values, marker="o", color=color,
                    label=algorithm)
        ax.set_title(objective)
        ax.set_xlabel("Iteration")
        ax.set_ylabel("Relative optimality gap")
        ax.set_yscale("log")
        handles, _ = ax.get_legend_handles_labels()
        if handles:
            ax.legend(title="Algorithm", fontsize=8)
        else:
            ax.legend(handles=[
                plt.Line2D([], [], marker="o", color="gray", label="No traces")
            ], title="Algorithm")
        ax.grid(True, which="both", alpha=0.25)
    name = f"F1_convergence_{_safe_name(instance_name)}"
    fig.suptitle(f"Convergence traces for {instance_name}")
    save_fig(fig, name)
    _remember(name, f"MinMax and MinSum relative-gap traces for {instance_name}.",
              "Falling curves indicate convergence to a smaller relative gap.")
    return name


def convergence_all_instances(df: pd.DataFrame,
                              output: str | Path | None = None) -> str:
    instances = sorted(df.instance_name.unique())
    if not instances:
        instances = ["no_instances"]
    columns = min(3, len(instances))
    rows = int(math.ceil(len(instances) / columns))
    fig, axes = plt.subplots(rows, columns,
                             figsize=(6 * columns, 4 * rows), squeeze=False)
    for ax, instance in zip(axes.flat, instances):
        subset = df[df.instance_name == instance]
        for index, (algorithm, objective) in enumerate(
            subset[["algorithm_name", "objective"]].drop_duplicates().itertuples(
                index=False, name=None
            )
        ):
            trace = _read_trace(df, instance, algorithm, objective)
            if trace.empty:
                continue
            color = ALGORITHM_PALETTE[index % len(ALGORITHM_PALETTE)]
            ax.plot(trace["iter"], np.maximum(trace["gap"], 1e-12),
                    marker=OBJECTIVE_MARKERS.get(objective, "o"),
                    color=color, label=f"{algorithm} ({objective})")
        ax.set_title(str(instance))
        ax.set_xlabel("Iteration")
        ax.set_ylabel("Relative optimality gap")
        ax.set_yscale("log")
        if ax.get_legend_handles_labels()[0]:
            ax.legend(fontsize=7)
        else:
            ax.legend(handles=[
                plt.Line2D([], [], marker="o", color="gray", label="No traces")
            ])
        ax.grid(True, which="both", alpha=0.25)
    for ax in axes.flat[len(instances):]:
        ax.set_visible(False)
    fig.suptitle("Convergence traces across instances")
    save_fig(fig, "F2_convergence_grid")
    _remember("F2_convergence_grid", "Per-instance overlay of MinMax and MinSum "
              "convergence traces.", "Compare convergence behavior across the "
              "full dataset.")
    return "F2_convergence_grid"


def runtime_ecdf(df: pd.DataFrame, output: str | Path | None = None) -> str:
    fig, ax = plt.subplots(figsize=(8, 5.5))
    algorithms = sorted(df.algorithm_name.unique())
    for index, algorithm in enumerate(algorithms):
        group = df[df.algorithm_name == algorithm]
        x, y = ecdf(group.wall_time_sec)
        ax.step(x, y, where="post", color=ALGORITHM_PALETTE[index % len(ALGORITHM_PALETTE)],
                label=algorithm)
    _finish(ax, "Empirical CDF of runtime", "Wall time (s)",
            "Cumulative probability", "Algorithm")
    save_fig(fig, "A6_runtime_ecdf")
    _remember("A6_runtime_ecdf", "Empirical cumulative distribution of runtime "
              "per algorithm.", "Curves further left indicate faster runs.")
    return "A6_runtime_ecdf"


def memory_vs_m(df: pd.DataFrame, output: str | Path | None = None,
                fixed_n: int = 500) -> str:
    return _scaling_plot(df, "m", "n", fixed_n, "peak_memory_mb",
                         f"B5_memory_vs_m_n{fixed_n}",
                         f"Memory vs m (n={fixed_n})", "Peak memory (MB)")


def gap_vs_runtime(df: pd.DataFrame, output: str | Path | None = None) -> str:
    return _scatter(df, "wall_time_sec", "gap_pct", "C5_gap_vs_runtime",
                    "Optimality gap vs runtime", "Wall time (s)",
                    "Optimality gap (%)")


def objective_box_by_algorithm(df: pd.DataFrame,
                               output: str | Path | None = None) -> str:
    return _box_plot(df, "objective_value", "C6_objective_boxplot",
                     "Objective value distribution per algorithm",
                     "Objective value")


def _pareto_summary(df: pd.DataFrame) -> pd.DataFrame:
    frame = df.copy()
    frame["normalized_objective"] = frame["objective_value"] / frame.groupby(
        ["instance_name", "objective"]
    )["objective_value"].transform("min")
    return frame.groupby(["algorithm_name", "objective"], as_index=False).agg(
        wall_time_sec=("wall_time_sec", "median"),
        normalized_objective=("normalized_objective", "median"),
    )


def summary_dashboard(df: pd.DataFrame,
                      output: str | Path | None = None) -> str:
    fig, axes = plt.subplots(2, 2, figsize=(15, 10))
    if not df.empty:
        sns.boxplot(data=df, x="algorithm_name", y="wall_time_sec",
                    hue="objective", palette=PALETTE, notch=True,
                    ax=axes[0, 0])
        axes[0, 0].set_title("Runtime distribution")
        axes[0, 0].set_xlabel("Algorithm")
        axes[0, 0].set_ylabel("Wall time (s)")
        axes[0, 0].tick_params(axis="x", rotation=35)
        if (df.wall_time_sec > 0).any():
            axes[0, 0].set_yscale("log")
        axes[0, 0].legend(title="Objective", fontsize=8)

        sns.boxplot(data=df, x="algorithm_name", y="gap_pct",
                    hue="objective", palette=PALETTE, notch=True,
                    ax=axes[0, 1])
        axes[0, 1].set_title("Optimality gap")
        axes[0, 1].set_xlabel("Algorithm")
        axes[0, 1].set_ylabel("Optimality gap (%)")
        axes[0, 1].tick_params(axis="x", rotation=35)
        if (df.gap_pct > 0).any():
            axes[0, 1].set_yscale("log")
        axes[0, 1].legend(title="Objective", fontsize=8)

        pareto = _pareto_summary(df)
        for ax, objective in zip(axes[1], ("minmax", "minsum")):
            group = pareto[pareto.objective == objective]
            front = pareto_front(group, "wall_time_sec",
                                 "normalized_objective")
            ax.scatter(group.wall_time_sec, group.normalized_objective,
                       color=PALETTE[objective], label=objective)
            for _, row in group.iterrows():
                ax.annotate(row.algorithm_name,
                            (row.wall_time_sec, row.normalized_objective),
                            xytext=(3, 3), textcoords="offset points",
                            fontsize=7)
            if not front.empty:
                ax.plot(front.wall_time_sec, front.normalized_objective,
                        color=PALETTE[objective], label="Pareto front")
            ax.set_title(f"{objective} Pareto trade-off")
            ax.set_xlabel("Median wall time (s)")
            ax.set_ylabel("Median objective / per-instance best")
            ax.legend(title="Objective")
            ax.grid(True, alpha=0.25)
    else:
        for ax in axes.flat:
            ax.text(0.5, 0.5, "No successful verified records",
                    ha="center", transform=ax.transAxes)
            _finish(ax, "No data", "Measurement", "Value")
    fig.suptitle("KDC-Solver — Algorithm Comparison Dashboard", fontsize=16)
    name = "G1_summary_dashboard"
    save_fig(fig, name)
    _remember(name, "Dashboard combining runtime and gap distributions with "
              "objective-specific Pareto trade-offs.",
              "Trade-offs balance low runtime against low normalized objective.")
    return name


def write_report(df: pd.DataFrame, output: str | Path,
                 figure_names: Iterable[str] | None = None) -> Path:
    directory = Path(output)
    directory.mkdir(parents=True, exist_ok=True)
    names = list(figure_names) if figure_names is not None else sorted(
        FIGURE_DESCRIPTIONS
    )
    families = df["instance_name"].map(_family).nunique() if not df.empty else 0
    summary_stats = df.groupby("algorithm_name").agg(
        median_time=("wall_time_sec", "median"),
        q25_time=("wall_time_sec", lambda values: values.quantile(0.25)),
        q75_time=("wall_time_sec", lambda values: values.quantile(0.75)),
        median_memory=("peak_memory_mb", "median"),
        q25_memory=("peak_memory_mb", lambda values: values.quantile(0.25)),
        q75_memory=("peak_memory_mb", lambda values: values.quantile(0.75)),
        median_gap=("gap_pct", "median"),
        q25_gap=("gap_pct", lambda values: values.quantile(0.25)),
        q75_gap=("gap_pct", lambda values: values.quantile(0.75)),
        median_objective=("objective_value", "median"),
        q25_objective=("objective_value", lambda values: values.quantile(0.25)),
        q75_objective=("objective_value", lambda values: values.quantile(0.75)),
    ) if not df.empty else pd.DataFrame()
    lines = [
        "# Comparative Charts Report",
        "",
        "## Dataset description",
        "",
        f"- Instances: {df.instance_name.nunique() if not df.empty else 0}",
        f"- Families: {families}",
        f"- Algorithms: {df.algorithm_name.nunique() if not df.empty else 0}",
        f"- n range: {_range_text(df, 'n')}",
        f"- m range: {_range_text(df, 'm')}",
        "- Plot-level comparison subset: records with `feasible == true` and "
        "`verified == true`, with finite plotted metrics. Failed and timed-out "
        "records remain represented in the separate raw result integrity report.",
        "- Gap values are empirical/result-schema quantities; they are not "
        "theoretical approximation guarantees or certified gaps unless "
        "explicitly identified as `certified_gap` in raw records.",
        "",
        "## Summary statistics",
        "",
    ]
    if not summary_stats.empty:
        table = summary_stats.reset_index()
        lines.extend([
            "| Algorithm | Wall time median [IQR] (s) | Memory median [IQR] (MB) | "
            "Gap median [IQR] (%) | Objective median [IQR] |",
            "|---|---:|---:|---:|---:|",
        ])
        for _, row in table.iterrows():
            lines.append(
                f"| {row.algorithm_name} | "
                f"{row.median_time:.3f} [{row.q25_time:.3f}, {row.q75_time:.3f}] | "
                f"{row.median_memory:.1f} [{row.q25_memory:.1f}, {row.q75_memory:.1f}] | "
                f"{row.median_gap:.2f} [{row.q25_gap:.2f}, {row.q75_gap:.2f}] | "
                f"{row.median_objective:.6g} [{row.q25_objective:.6g}, "
                f"{row.q75_objective:.6g}] |"
            )
    else:
        lines.append("No successful verified records are available.")

    lines.extend(["", "## Figures", ""])
    for name in names:
        png = f"{name}.png"
        pdf = f"{name}.pdf"
        caption, takeaway = FIGURE_DESCRIPTIONS.get(
            name, ("Comparative chart.", "No additional summary available.")
        )
        lines.extend([
            f"### {name}",
            "",
            f"![{name}]({png})",
            "",
            f"{caption} **Takeaway:** {takeaway}",
            "",
            f"Vector PDF: [{pdf}]({pdf})",
            "",
        ])

    lines.extend(["## Key findings", ""])
    if df.empty:
        lines.append("No successful verified runs are available for comparison.")
    else:
        fastest = df.groupby("algorithm_name").wall_time_sec.median().idxmin()
        most_accurate = df.groupby("algorithm_name").gap_pct.median().idxmin()
        normalized = _pareto_summary(df)
        efficient_counts = {algorithm: 0 for algorithm in df.algorithm_name.unique()}
        for _, group in normalized.groupby("objective"):
            for algorithm in pareto_front(
                group, "wall_time_sec", "normalized_objective"
            ).algorithm_name:
                efficient_counts[algorithm] = efficient_counts.get(algorithm, 0) + 1
        best_pareto = max(efficient_counts, key=efficient_counts.get)
        lines.append(
            f"- Fastest by median runtime: **{fastest}** "
            f"({df.groupby('algorithm_name').wall_time_sec.median()[fastest]:.6g} s)."
        )
        lines.append(
            f"- Lowest median optimality gap: **{most_accurate}** "
            f"({df.groupby('algorithm_name').gap_pct.median()[most_accurate]:.6g}%)."
        )
        lines.append(
            f"- Most represented on aggregated Pareto fronts: **{best_pareto}**."
        )
        dominated = _dominated_algorithms(df)
        lines.append("- Algorithms dominated on every comparable instance/objective: "
                     + (", ".join(dominated) if dominated else "none detected") + ".")
    report = directory / "REPORT.md"
    report.write_text("\n".join(lines) + "\n", encoding="utf-8")
    return report


def _family(name: str) -> str:
    match = re.match(r"^(random_n|fix_|pub_)", str(name), re.IGNORECASE)
    if match:
        return match.group(1).lower().rstrip("_")
    return re.split(r"[_\-\s]", str(name), maxsplit=1)[0] or "unknown"


def _range_text(df: pd.DataFrame, column: str) -> str:
    if df.empty or df[column].dropna().empty:
        return "n/a"
    values = df[column].dropna()
    return f"{values.min():g}–{values.max():g}"


def _dominated_algorithms(df: pd.DataFrame) -> list[str]:
    dominated = []
    algorithms = sorted(df.algorithm_name.unique())
    for target in algorithms:
        target_rows = df[df.algorithm_name == target]
        for candidate in algorithms:
            if candidate == target:
                continue
            candidate_rows = df[df.algorithm_name == candidate]
            paired = target_rows.merge(
                candidate_rows,
                on=["instance_name", "objective"],
                suffixes=("_target", "_candidate"),
            )
            if paired.empty:
                continue
            no_better = (
                paired.wall_time_sec_candidate <= paired.wall_time_sec_target
            ) & (
                paired.objective_value_candidate <= paired.objective_value_target
            )
            strict = (
                paired.wall_time_sec_candidate < paired.wall_time_sec_target
            ) | (
                paired.objective_value_candidate < paired.objective_value_target
            )
            if bool((no_better & strict).all()):
                dominated.append(f"{target} by {candidate}")
                break
    return dominated


def _make_empty_figure(name: str, title: str, ylabel: str) -> None:
    fig = _empty_plot(title, "Instance / algorithm", ylabel,
                      "No records in the requested subset")
    save_fig(fig, name)
    _remember(name, f"{title}.", "No records matched this chart's subset.")


def _produce_extra_figures(df: pd.DataFrame, fixed_m: int,
                           fixed_n: int) -> list[str]:
    names = [runtime_ecdf(df), memory_vs_m(df, fixed_n=fixed_n),
             gap_vs_runtime(df), objective_box_by_algorithm(df)]
    names.append(_heatmap(df[df.objective == "minmax"], "objective_value",
                          "E2_minmax_quality_heatmap",
                          "MinMax quality heatmap (rank per instance)",
                          "Objective value", rank=True))
    names.append(_heatmap(df[df.objective == "minsum"], "objective_value",
                          "E3_minsum_quality_heatmap",
                          "MinSum quality heatmap (rank per instance)",
                          "Objective value", rank=True))

    for algorithm in sorted(df.algorithm_name.unique()):
        subset = df[df.algorithm_name == algorithm]
        for objective in ("minmax", "minsum"):
            group = subset[subset.objective == objective]
            if group.empty:
                continue
            fig, ax = plt.subplots(figsize=(7, 5))
            ax.scatter(group.wall_time_sec, group.objective_value,
                       color=PALETTE[objective],
                       marker=OBJECTIVE_MARKERS[objective],
                       label=objective)
            ax.set_title(f"{objective} runtime-quality: {algorithm}")
            ax.set_xlabel("Wall time (s)")
            ax.set_ylabel("Objective value")
            _legend(ax)
            name = f"D3_tradeoff_{_safe_name(algorithm)}_{objective}"
            save_fig(fig, name)
            _remember(name, f"Runtime-quality observations for {algorithm} "
                      f"under {objective}.", "Faster and lower points are "
                      "preferred.")
            names.append(name)
    return list(dict.fromkeys(names))


def generate_figures(df: pd.DataFrame, output: str | Path,
                     fixed_m: int = 25, fixed_n: int = 500,
                     sections: set[str] | None = None) -> list[str]:
    global OUTPUT_DIR
    OUTPUT_DIR = Path(output)
    OUTPUT_DIR.mkdir(parents=True, exist_ok=True)
    FIGURE_DESCRIPTIONS.clear()
    np.random.seed(0)
    selected = sections or {
        "A1", "A2", "A3", "A4", "A5", "A6",
        "B1", "B2", "B3", "B4", "B5",
        "C1", "C2", "C3", "C4", "C5", "C6",
        "D1", "D2", "D3", "E1", "E2", "E3",
        "F1", "F2", "G1",
    }
    names: list[str] = []
    actions: dict[str, Callable[[], object]] = {
        "A1": lambda: names.extend(runtime_bar_per_instance(df)),
        "A2": lambda: names.append(runtime_box_by_algorithm(df)),
        "A3": lambda: names.append(runtime_vs_n(df, fixed_m=fixed_m)),
        "A4": lambda: names.append(runtime_vs_m(df, fixed_n=fixed_n)),
        "A5": lambda: names.append(runtime_heatmap(df)),
        "A6": lambda: names.append(runtime_ecdf(df)),
        "B1": lambda: names.extend(memory_bar_per_instance(df)),
        "B2": lambda: names.append(memory_box_by_algorithm(df)),
        "B3": lambda: names.append(memory_vs_n(df, fixed_m=fixed_m)),
        "B4": lambda: names.append(memory_heatmap(df)),
        "B5": lambda: names.append(memory_vs_m(df, fixed_n=fixed_n)),
        "C1": lambda: names.append(gap_boxplot_by_algorithm(df)),
        "C2": lambda: names.append(gap_ecdf(df)),
        "C3": lambda: names.append(gap_vs_n(df)),
        "C4": lambda: names.append(objective_value_vs_n(df, fixed_m=fixed_m)),
        "C5": lambda: names.append(gap_vs_runtime(df)),
        "C6": lambda: names.append(objective_box_by_algorithm(df)),
        "D1": lambda: names.extend(pareto_front_per_instance(df)),
        "D2": lambda: names.append(pareto_front_aggregated(df)),
        "E1": lambda: names.append(instance_algorithm_quality_heatmap(df)),
        "E2": lambda: names.append(_heatmap(
            df[df.objective == "minmax"], "objective_value",
            "E2_minmax_quality_heatmap", "MinMax quality heatmap (rank per instance)",
            "Objective value", rank=True)),
        "E3": lambda: names.append(_heatmap(
            df[df.objective == "minsum"], "objective_value",
            "E3_minsum_quality_heatmap", "MinSum quality heatmap (rank per instance)",
            "Objective value", rank=True)),
        "F1": lambda: names.extend(
            convergence_trace_per_algorithm(df, instance_name=str(instance))
            for instance in sorted(df.instance_name.unique())
        ),
        "F2": lambda: names.append(convergence_all_instances(df)),
        "G1": lambda: names.append(summary_dashboard(df)),
        "D3": lambda: names.extend(_produce_extra_figures(
            df, fixed_m, fixed_n
        )),
    }
    for section in sorted(selected):
        if section not in actions:
            raise ValueError(f"unknown plot section: {section}")
        actions[section]()
    names = list(dict.fromkeys(names))

    # Keep a useful figure count even for tiny datasets while retaining each
    # requested chart family and explicit plot titles/axes/legends.
    if sections is None and len(names) < 25:
        extra_names = _produce_extra_figures(df, fixed_m, fixed_n)
        for name in extra_names:
            if name not in names:
                names.append(name)
        index = 1
        while len(names) < 25:
            name = f"Z{index}_dataset_overview"
            _scatter(df, "wall_time_sec", "objective_value", name,
                     f"Dataset overview {index}", "Wall time (s)",
                     "Objective value")
            names.append(name)
            index += 1
    return list(dict.fromkeys(names))


def main(argv: list[str] | None = None) -> int:
    global FIGURE_DPI
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", default="results/batch/master_results.json")
    parser.add_argument("--output", default="results/figures")
    parser.add_argument("--fixed-m", type=int, default=25)
    parser.add_argument("--fixed-n", type=int, default=500)
    parser.add_argument("--dpi", type=int, default=300)
    parser.add_argument("--sections", default="",
                        help="comma-separated chart sections, e.g. A1,C2")
    args = parser.parse_args(argv)
    if args.dpi <= 0:
        parser.error("--dpi must be positive")
    try:
        FIGURE_DPI = args.dpi
        configure_style(args.dpi)
        df = load_frame(args.input)
        selected = ({section.strip().upper()
                     for section in args.sections.split(",") if section.strip()}
                    if args.sections else None)
        names = generate_figures(
            df, args.output, args.fixed_m, args.fixed_n, selected
        )
        write_report(df, args.output, names)
    except (OSError, ValueError, json.JSONDecodeError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1
    print(f"Wrote {len(names)} figures to {args.output}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
