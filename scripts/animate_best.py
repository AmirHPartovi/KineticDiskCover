#!/usr/bin/env python3
"""Animate the best verified kinetic solution for each instance."""

from __future__ import annotations

import argparse
import json
import logging
import math
from pathlib import Path
import re
import sys
from typing import Any

import matplotlib

matplotlib.use("Agg")
import matplotlib.animation as animation
import matplotlib.pyplot as plt
from matplotlib.patches import Circle
import numpy as np
import pandas as pd

np.random.seed(0)

LOGGER = logging.getLogger("animate_best")
STATION_COLORS = plt.get_cmap("tab10")


def select_best(frame: pd.DataFrame, instance: str, mode: str) -> pd.Series | None:
    """Choose a verified feasible record using the specified deterministic tiebreaks."""
    rows = frame[
        (frame["instance_name"].astype(str) == str(instance))
        & (frame["objective"].astype(str).str.lower() == mode.lower())
    ].copy()
    if "verified" in rows:
        rows = rows[rows["verified"].map(_as_bool)]
    if "feasible" in rows:
        rows = rows[rows["feasible"].map(_as_bool)]
    if rows.empty:
        return None
    rows["objective_value"] = pd.to_numeric(rows["objective_value"], errors="coerce")
    rows["gap"] = pd.to_numeric(rows.get("gap", 0.0), errors="coerce")
    rows["wall_time_sec"] = pd.to_numeric(
        rows.get("wall_time_sec", 0.0), errors="coerce"
    )
    rows = rows.dropna(subset=["objective_value", "gap", "wall_time_sec"])
    if rows.empty:
        return None
    return rows.sort_values(
        ["objective_value", "gap", "wall_time_sec", "algorithm_name"],
        kind="stable",
    ).iloc[0]


def _as_bool(value: object) -> bool:
    if isinstance(value, (bool, np.bool_)):
        return bool(value)
    return str(value).strip().lower() in {"true", "1", "yes"}


def _number(value: object, name: str) -> float:
    try:
        result = float(value)
    except (TypeError, ValueError) as error:
        raise ValueError(f"{name} must be numeric") from error
    if not math.isfinite(result):
        raise ValueError(f"{name} must be finite")
    return result


def load_instance(path: str | Path) -> dict[str, Any]:
    with Path(path).open(encoding="utf-8") as source:
        instance = json.load(source)
    if not isinstance(instance, dict):
        raise ValueError("instance JSON root must be an object")
    stations = instance.get("stations")
    trajectories = instance.get("trajectories")
    if not isinstance(stations, list) or not isinstance(trajectories, list):
        raise ValueError("instance JSON must contain stations and trajectories")
    t_end = _number(instance.get("T_end"), "T_end")
    if t_end <= 0:
        raise ValueError("T_end must be positive")
    for trajectory in trajectories:
        breaks = trajectory.get("t_breaks", [])
        waypoints = trajectory.get("waypoints", [])
        if len(breaks) < 2 or len(breaks) != len(waypoints):
            raise ValueError("trajectory has mismatched time breaks and waypoints")
    return instance


def load_solution(path: str | Path, instance: dict[str, Any]) -> dict[str, Any]:
    with Path(path).open(encoding="utf-8") as source:
        solution = json.load(source)
    if not isinstance(solution, dict) or not isinstance(
        solution.get("intervals"), list
    ):
        raise ValueError("solution JSON must contain an intervals array")
    metadata = solution.get("instance", {})
    if metadata and (
        metadata.get("n") != len(instance["trajectories"])
        or metadata.get("m") != len(instance["stations"])
        or abs(float(metadata.get("T_end", -1)) -
               float(instance["T_end"])) >= 1e-9
    ):
        raise ValueError("solution does not match instance dimensions or time")
    for interval in solution["intervals"]:
        supports = interval.get(
            "supporting_point", interval.get("supporting_points")
        )
        if not isinstance(supports, list) or len(supports) != len(instance["stations"]):
            raise ValueError("solution interval has invalid supporting points")
    solution["_instance"] = instance
    return solution


def position_at(instance: dict[str, Any], trajectory_index: int,
                time: float) -> tuple[float, float]:
    trajectory = instance["trajectories"][trajectory_index]
    times = np.asarray(trajectory["t_breaks"], dtype=float)
    waypoints = trajectory["waypoints"]
    x = np.asarray([point["x"] for point in waypoints], dtype=float)
    y = np.asarray([point["y"] for point in waypoints], dtype=float)
    return float(np.interp(time, times, x)), float(np.interp(time, times, y))


def _interval_at(solution: dict[str, Any], time: float) -> dict[str, Any]:
    intervals = solution.get("intervals", [])
    if not intervals:
        raise ValueError("solution has no intervals")
    for index, interval in enumerate(intervals):
        if time < float(interval["t_end"]) or index + 1 == len(intervals):
            if time >= float(interval["t_start"]) - 1e-12:
                return interval
    return intervals[-1]


def radius_at(solution: dict[str, Any], station_idx: int, t: float,
              instance: dict[str, Any] | None = None) -> float:
    geometry = instance or solution.get("_instance")
    if geometry is None:
        geometry = solution
    supports = _interval_at(solution, t).get(
        "supporting_point",
        _interval_at(solution, t).get("supporting_points", []),
    )
    if station_idx < 0 or station_idx >= len(supports):
        raise IndexError("station index is outside the supporting-point list")
    support = int(supports[station_idx])
    if support < 0:
        return 0.0
    stations = geometry.get("stations", geometry.get("station_positions", []))
    trajectories = geometry.get("trajectories", [])
    if support >= len(trajectories) or station_idx >= len(stations):
        raise ValueError("supporting point or station geometry is unavailable")
    station = stations[station_idx]
    if isinstance(station, dict):
        station_x, station_y = float(station["x"]), float(station["y"])
    else:
        station_x, station_y = float(station[0]), float(station[1])
    point_x, point_y = position_at(geometry, support, t)
    return math.hypot(point_x - station_x, point_y - station_y)


def cost_at_time(solution: dict[str, Any], t: float,
                 instance: dict[str, Any] | None = None) -> float:
    geometry = instance or solution.get("_instance") or solution
    stations = geometry.get("stations", geometry.get("station_positions", []))
    return math.pi * sum(
        radius_at(solution, index, t, geometry) ** 2
        for index in range(len(stations))
    )


def integrate_cost(times: Iterable[float], costs: Iterable[float]) -> np.ndarray:
    """Return cumulative trapezoidal integrals, starting at zero."""
    time_values = np.asarray(list(times), dtype=float)
    cost_values = np.asarray(list(costs), dtype=float)
    if time_values.ndim != 1 or cost_values.ndim != 1 or len(time_values) != len(cost_values):
        raise ValueError("times and costs must be equal-length one-dimensional arrays")
    if len(time_values) == 0:
        return np.asarray([], dtype=float)
    if np.any(np.diff(time_values) < 0):
        raise ValueError("time grid must be nondecreasing")
    increments = (cost_values[:-1] + cost_values[1:]) * 0.5 * np.diff(time_values)
    return np.concatenate(([0.0], np.cumsum(increments)))


def integral_at(time: float, times: np.ndarray, costs: np.ndarray,
                cumulative: np.ndarray) -> float:
    if time <= times[0]:
        return 0.0
    if time >= times[-1]:
        return float(cumulative[-1])
    index = int(np.searchsorted(times, time, side="right") - 1)
    partial_cost = float(np.interp(time, times[index:index + 2],
                                   costs[index:index + 2]))
    partial = (costs[index] + partial_cost) * 0.5 * (time - times[index])
    return float(cumulative[index] + partial)


def _find_instance_file(directory: Path, name: str) -> Path | None:
    for path in sorted(directory.rglob("*.json")):
        try:
            with path.open(encoding="utf-8") as source:
                payload = json.load(source)
            if payload.get("name") == name or path.stem == name:
                return path
        except (OSError, json.JSONDecodeError, AttributeError):
            continue
    return None


def _solution_path(batch_dir: Path, record: pd.Series) -> Path:
    candidate = batch_dir / "runs" / str(record.instance_name) / \
        str(record.algorithm_name) / str(record.objective) / "solution.json"
    if candidate.is_file():
        return candidate
    declared = str(record.get("solution_json_path", ""))
    if declared:
        path = Path(declared)
        if not path.is_absolute():
            path = Path.cwd() / path
        if path.is_file():
            return path
    return candidate


def _frame_values(instance: dict[str, Any], solution: dict[str, Any],
                  frames: int) -> tuple[np.ndarray, np.ndarray, np.ndarray]:
    times = np.linspace(0.0, float(instance["T_end"]), frames)
    costs = np.asarray([cost_at_time(solution, time, instance) for time in times])
    integrals = integrate_cost(times, costs)
    return times, costs, integrals


def make_animation(instance: dict[str, Any], solution: dict[str, Any],
                   record: pd.Series, output: Path, mode: str,
                   fps: int = 30, frames: int = 200, dpi: int = 150) -> Path:
    if frames < 2 or fps <= 0 or dpi <= 0:
        raise ValueError("fps and dpi must be positive and frames must be at least 2")
    instance_name = str(record["instance_name"])
    algorithm = str(record["algorithm_name"])
    stations = instance["stations"]
    trajectories = instance["trajectories"]
    for interval in solution.get("intervals", []):
        owners = interval.get("assigned_points")
        if not isinstance(owners, list) or len(owners) != len(trajectories):
            raise ValueError("solution assigned_points must match point count")
        if any(int(owner) < 0 or int(owner) >= len(stations) for owner in owners):
            raise ValueError("solution contains an invalid assigned station")
    times, costs, integrals = _frame_values(instance, solution, frames)
    colors = [STATION_COLORS(index % 10) for index in range(len(stations))]

    figure = plt.figure(figsize=(10, 8))
    grid = figure.add_gridspec(2, 1, height_ratios=[8, 2], hspace=0.28,
                               top=0.82, bottom=0.09)
    scene = figure.add_subplot(grid[0])
    curve = figure.add_subplot(grid[1])
    all_x = [float(station["x"]) for station in stations]
    all_y = [float(station["y"]) for station in stations]
    for trajectory in trajectories:
        all_x.extend(float(point["x"]) for point in trajectory["waypoints"])
        all_y.extend(float(point["y"]) for point in trajectory["waypoints"])
        scene.plot([point["x"] for point in trajectory["waypoints"]],
                   [point["y"] for point in trajectory["waypoints"]],
                   "--", color="gray", alpha=0.5, linewidth=1,
                   label="Trajectory" if trajectory is trajectories[0] else None)

    point_scatter = scene.scatter([], [], s=35, edgecolors="black",
                                  linewidths=0.3, zorder=4, label="Moving points")
    station_legend_handles = []
    for index, station in enumerate(stations):
        station_legend_handles.append(
            scene.scatter([station["x"]], [station["y"]],
                          marker="^", s=110, color=colors[index],
                          edgecolors="black", linewidths=0.5, zorder=5,
                          label=f"Station {index}")
        )
    disks = []
    for index, station in enumerate(stations):
        patch = Circle((station["x"], station["y"]), 0.0,
                       facecolor=colors[index], edgecolor=colors[index],
                       alpha=0.15, linewidth=1.5)
        scene.add_patch(patch)
        disks.append(patch)
    support_links = [
        scene.plot([], [], color=colors[index], linewidth=1.2, alpha=0.75,
                   label="Active support links" if index == 0 else None)[0]
        for index in range(len(stations))
    ]
    support_markers = [
        scene.scatter([], [], marker="o", s=75, facecolors="none",
                      edgecolors=colors[index], linewidths=1.5, zorder=6)
        for index in range(len(stations))
    ]
    support_labels = [
        scene.text(0.0, 0.0, "", color=colors[index], fontsize=8,
                   ha="left", va="bottom", zorder=7,
                   bbox={"facecolor": "white", "alpha": 0.75,
                         "edgecolor": "none", "pad": 1})
        for index in range(len(stations))
    ]

    span_x = max(all_x) - min(all_x) if all_x else 1.0
    span_y = max(all_y) - min(all_y) if all_y else 1.0
    pad_x = max(span_x * 0.1, 0.5)
    pad_y = max(span_y * 0.1, 0.5)
    scene.set_xlim(min(all_x, default=0.0) - pad_x,
                   max(all_x, default=1.0) + pad_x)
    scene.set_ylim(min(all_y, default=0.0) - pad_y,
                   max(all_y, default=1.0) + pad_y)
    scene.set_aspect("equal", adjustable="box")

    curve.plot(times, costs, color="#1f77b4", label="cost(t)")
    previous_supports: list[int] | None = None
    support_change_legend_added = False
    for interval in solution.get("intervals", []):
        supports = interval.get(
            "supporting_point", interval.get("supporting_points", [])
        )
        normalized = [int(value) for value in supports]
        if previous_supports is not None and normalized != previous_supports:
            curve.axvline(
                float(interval["t_start"]), color="#9467bd",
                linestyle=":", linewidth=1, alpha=0.8,
                label="Support changes" if not support_change_legend_added
                else "_nolegend_",
            )
            support_change_legend_added = True
        previous_supports = normalized
    time_indicator = curve.axvline(times[0], color="#d62728",
                                   linestyle="-", label="Current time")
    summary = solution.get("summary", {})
    peak_value = max(costs) if costs.size else 0.0
    final_integral = float(integrals[-1]) if integrals.size else 0.0
    reference = peak_value if mode == "minmax" else (
        final_integral / float(instance["T_end"])
    )
    curve.axhline(reference, color="#d62728", linestyle="--",
                  label="Peak" if mode == "minmax" else "Mean cost")
    lower_bound = float(record.get("lower_bound", 0.0) or 0.0)
    curve.axhline(lower_bound, color="#2ca02c", linestyle=":",
                  label="Lower bound")
    curve.set_xlim(0.0, float(instance["T_end"]))
    curve.set_xlabel("Time")
    curve.set_ylabel("Cost")
    curve.set_title("Cost over time")
    curve.legend(loc="best", fontsize=8)
    curve.grid(True, alpha=0.25)

    left_text = scene.text(
        0.02, 0.97, "", transform=scene.transAxes, va="top", ha="left",
        bbox={"facecolor": "white", "alpha": 0.78, "edgecolor": "none"},
    )
    gap_pct = float(record.get("gap", 0.0) or 0.0) * 100.0
    runtime = float(record.get("wall_time_sec", 0.0) or 0.0)
    figure.text(
        0.98, 0.91,
        f"peak = {float(summary.get('peak_cost', peak_value)):.3f}\n"
        f"final_integral = {float(summary.get('total_integral', final_integral)):.3f}\n"
        f"gap = {gap_pct:.2f}%\nruntime = {runtime:.3f} s",
        transform=figure.transFigure, va="top", ha="right", fontsize=8,
        bbox={"facecolor": "white", "alpha": 0.78, "edgecolor": "none"},
    )
    figure.suptitle(
        f"Instance {instance_name} | n={len(trajectories)}, m={len(stations)}\n"
        f"Algorithm: {algorithm} | Objective: {mode}",
        fontsize=13,
    )
    figure.legend(handles=station_legend_handles, loc="center",
                  bbox_to_anchor=(0.5, 0.85),
                  ncol=max(1, len(station_legend_handles)), fontsize=8)

    def update(frame_index: int):
        time = float(times[frame_index])
        positions = np.asarray([
            position_at(instance, index, time)
            for index in range(len(trajectories))
        ], dtype=float).reshape((-1, 2))
        if positions.size:
            point_scatter.set_offsets(positions)
            interval = _interval_at(solution, time)
            point_colors = [
                colors[int(owner)] for owner in interval["assigned_points"]
            ]
            point_scatter.set_color(point_colors)
            for station_index, patch in enumerate(disks):
                radius = radius_at(solution, station_index, time, instance)
                patch.set_radius(radius)
                supports = interval.get(
                    "supporting_point", interval.get("supporting_points", [])
                )
                support = int(supports[station_index])
                if 0 <= support < len(positions):
                    station = stations[station_index]
                    support_position = positions[support]
                    support_links[station_index].set_data(
                        [station["x"], support_position[0]],
                        [station["y"], support_position[1]],
                    )
                    support_markers[station_index].set_offsets(
                        support_position.reshape(1, 2)
                    )
                else:
                    support_links[station_index].set_data([], [])
                    support_markers[station_index].set_offsets(
                        np.empty((0, 2))
                    )
                if 0 <= support < len(positions):
                    support_labels[station_index].set_position(
                        tuple(positions[support])
                    )
                    support_labels[station_index].set_text(
                        f"S{station_index}:P{support}"
                    )
                else:
                    support_labels[station_index].set_text("")
        time_indicator.set_xdata([time, time])
        left_text.set_text(
            f"t = {time:.3f}\n"
            f"cost = {costs[frame_index]:.3f}\n"
            f"integral up to now = "
            f"{integral_at(time, times, costs, integrals):.3f}"
        )
        return [
            point_scatter, time_indicator, left_text, *disks,
            *support_links, *support_markers, *support_labels,
        ]

    movie = animation.FuncAnimation(
        figure, update, frames=frames, interval=1000.0 / fps,
        blit=False, repeat=False,
    )
    output.mkdir(parents=True, exist_ok=True)
    if animation.writers.is_available("ffmpeg"):
        destination = output / f"{_safe_filename(instance_name)}_{mode}.mp4"
        writer = animation.FFMpegWriter(fps=fps, metadata={"artist": "kdc-solver"})
        movie.save(destination, writer=writer, dpi=dpi)
    else:
        LOGGER.warning(
            "ffmpeg is not available; saving GIF instead. Install ffmpeg "
            "(for example, `brew install ffmpeg` or `apt install ffmpeg`) "
            "to produce MP4 files."
        )
        destination = output / f"{_safe_filename(instance_name)}_{mode}.gif"
        movie.save(destination, writer=animation.PillowWriter(fps=fps), dpi=dpi)
    plt.close(figure)
    return destination


def _safe_filename(value: str) -> str:
    return re.sub(r"[^A-Za-z0-9._-]+", "_", value).strip("._") or "unnamed"


def run(batch_dir: str | Path, instances_dir: str | Path,
        output_dir: str | Path, mode: str = "minmax", fps: int = 30,
        frames: int = 200, dpi: int = 150, top_n: int = 0,
        all_algorithms: bool = False,
        algorithm: str | None = None) -> list[Path]:
    batch_path = Path(batch_dir)
    with (batch_path / "master_results.json").open(encoding="utf-8") as source:
        records = json.load(source)
    if not isinstance(records, list):
        raise ValueError("master_results.json root must be an array")
    dataframe = pd.DataFrame(records)
    if dataframe.empty:
        LOGGER.warning("master_results.json contains no records")
        return []
    if algorithm is not None:
        dataframe = dataframe[
            dataframe["algorithm_name"].astype(str).str.casefold()
            == algorithm.casefold()
        ]
        if dataframe.empty:
            raise ValueError(f"No results found for algorithm {algorithm!r}")
    modes = ("minmax", "minsum") if mode == "both" else (mode,)
    instances = sorted(dataframe["instance_name"].astype(str).unique())
    if top_n > 0:
        instances = instances[:top_n]
    output = Path(output_dir)
    created = []
    for instance_name in instances:
        instance_path = _find_instance_file(Path(instances_dir), instance_name)
        if instance_path is None:
            LOGGER.warning("No instance JSON found for %s; skipping", instance_name)
            continue
        instance = load_instance(instance_path)
        for objective in modes:
            if all_algorithms:
                eligible = dataframe[
                    (dataframe["instance_name"].astype(str) == instance_name)
                    & (dataframe["objective"].astype(str).str.lower()
                       == objective.lower())
                ]
                algorithms = sorted(
                    eligible["algorithm_name"].astype(str).unique()
                )
                selected_records = [
                    select_best(eligible, instance_name, objective)
                    if len(algorithms) == 1 else
                    select_best(
                        eligible[
                            eligible["algorithm_name"].astype(str) == algorithm
                        ],
                        instance_name,
                        objective,
                    )
                    for algorithm in algorithms
                ]
                selected_records = [
                    record for record in selected_records if record is not None
                ]
            else:
                record = select_best(dataframe, instance_name, objective)
                selected_records = [] if record is None else [record]
            if not selected_records:
                LOGGER.warning(
                    "No feasible verified %s result for %s; skipping",
                    objective, instance_name,
                )
                continue
            for record in selected_records:
                algorithm_output = (
                    output / _safe_filename(str(record["algorithm_name"]))
                    if all_algorithms else output
                )
                solution_path = _solution_path(batch_path, record)
                if not solution_path.is_file():
                    LOGGER.warning(
                        "Solution JSON is missing for %s / %s / %s: %s",
                        instance_name, record.algorithm_name, objective,
                        solution_path,
                    )
                    continue
                solution = load_solution(solution_path, instance)
                created.append(
                    make_animation(instance, solution, record, algorithm_output,
                                   objective, fps, frames, dpi)
                )
    return created


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--batch", default="results/batch")
    parser.add_argument("--instances", default="data/instances")
    parser.add_argument("--output", default="results/animations")
    parser.add_argument("--mode", choices=("minmax", "minsum", "both"),
                        default="minmax")
    parser.add_argument("--fps", type=int, default=30)
    parser.add_argument("--frames", type=int, default=200)
    parser.add_argument("--dpi", type=int, default=150)
    parser.add_argument("--top-n", type=int, default=0)
    parser.add_argument(
        "--all-algorithms", action="store_true",
        help="animate the best verified result for each algorithm instead of "
        "only the overall best result",
    )
    parser.add_argument(
        "--algorithm",
        help="restrict animation generation to this algorithm",
    )
    args = parser.parse_args(argv)
    if args.fps <= 0 or args.frames < 2 or args.dpi <= 0 or args.top_n < 0:
        parser.error("--fps/--dpi must be positive, --frames >= 2, --top-n >= 0")
    logging.basicConfig(level=logging.INFO,
                        format="%(levelname)s: %(message)s")
    try:
        files = run(args.batch, args.instances, args.output, args.mode,
                    args.fps, args.frames, args.dpi, args.top_n,
                        args.all_algorithms, args.algorithm)
    except (OSError, ValueError, json.JSONDecodeError) as error:
        LOGGER.error("%s", error)
        return 1
    print(f"Created {len(files)} animation(s) in {args.output}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
