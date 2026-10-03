#!/usr/bin/env python3
"""Animate a kdc-solver instance and its kinetic solution.

Solution files are expected to contain ``intervals`` with ``t_start``,
``t_end``, ``supporting_point`` (or ``supporting_points``), and quadratic cost
coefficients ``a``, ``b``, ``c``, and one ``assigned_points`` station id per
trajectory. The file may contain one solution or a
``solutions``/``minmax``/``minsum`` mapping for both modes.
"""

from __future__ import annotations

import argparse
import json
import math
import shutil
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Any

import matplotlib

matplotlib.use("Agg")

import matplotlib.animation as animation
import matplotlib.pyplot as plt
from matplotlib.patches import Circle
import numpy as np


POINT_COLORS = (
    "#1f77b4", "#ff7f0e", "#d62728", "#9467bd", "#8c564b",
    "#e377c2", "#7f7f7f", "#bcbd22", "#17becf",
)
MODE_COLORS = {"minmax": "#d62728", "minsum": "#d62728"}
MODE_LABELS = {"minmax": "Min-Max", "minsum": "Min-Sum"}


@dataclass
class Trajectory:
    times: np.ndarray
    x: np.ndarray
    y: np.ndarray

    def position(self, time: float) -> tuple[float, float]:
        return (
            float(np.interp(time, self.times, self.x)),
            float(np.interp(time, self.times, self.y)),
        )


@dataclass
class Interval:
    start: float
    end: float
    supports: list[int]
    assigned_points: list[int]
    a: float
    b: float
    c: float

    def cost(self, time: float) -> float:
        return (self.a * time + self.b) * time + self.c


@dataclass
class SolutionData:
    mode: str
    intervals: list[Interval]
    lower_bound: float | list[tuple[float, float]] | None

    @property
    def start(self) -> float:
        return self.intervals[0].start

    @property
    def end(self) -> float:
        return self.intervals[-1].end

    def interval_at(self, time: float) -> Interval:
        for index, interval in enumerate(self.intervals):
            if time < interval.end or index + 1 == len(self.intervals):
                return interval
        return self.intervals[-1]

    def cost(self, time: float) -> float:
        return self.interval_at(time).cost(time)

    def integral(self, end_time: float) -> float:
        total = 0.0
        for interval in self.intervals:
            lo = max(interval.start, self.start)
            hi = min(interval.end, end_time)
            if hi <= lo:
                continue
            total += (
                interval.a / 3.0 * (hi**3 - lo**3)
                + interval.b / 2.0 * (hi**2 - lo**2)
                + interval.c * (hi - lo)
            )
        return total

    def lower_bound_at(self, time: float) -> float | None:
        if isinstance(self.lower_bound, (int, float)):
            return float(self.lower_bound)
        if not self.lower_bound:
            return None
        times, values = zip(*self.lower_bound)
        return float(np.interp(time, times, values))


def _finite_number(value: Any, field: str) -> float:
    try:
        result = float(value)
    except (TypeError, ValueError) as error:
        raise ValueError(f"{field} must be a number") from error
    if not math.isfinite(result):
        raise ValueError(f"{field} must be finite")
    return result


def load_instance(path: Path) -> tuple[list[tuple[float, float]], list[Trajectory], float]:
    with path.open("r", encoding="utf-8") as source:
        payload = json.load(source)
    if not isinstance(payload, dict):
        raise ValueError("instance JSON root must be an object")
    stations_payload = payload.get("stations")
    trajectories_payload = payload.get("trajectories")
    if not isinstance(stations_payload, list) or not isinstance(
        trajectories_payload, list
    ):
        raise ValueError("instance JSON must contain stations and trajectories arrays")

    stations = [
        (_finite_number(station.get("x"), "station.x"),
         _finite_number(station.get("y"), "station.y"))
        for station in stations_payload
    ]
    trajectories: list[Trajectory] = []
    for index, item in enumerate(trajectories_payload):
        times = np.asarray(item.get("t_breaks", []), dtype=float)
        waypoints = item.get("waypoints", [])
        if times.ndim != 1 or len(times) < 2 or len(waypoints) != len(times):
            raise ValueError(f"trajectory {index} has invalid waypoints or t_breaks")
        if not np.all(np.isfinite(times)) or np.any(np.diff(times) <= 0):
            raise ValueError(f"trajectory {index} t_breaks must be finite and increasing")
        x = np.asarray([_finite_number(point.get("x"), "waypoint.x")
                        for point in waypoints], dtype=float)
        y = np.asarray([_finite_number(point.get("y"), "waypoint.y")
                        for point in waypoints], dtype=float)
        trajectories.append(Trajectory(times, x, y))

    end_time = _finite_number(payload.get("T_end"), "T_end")
    if end_time <= 0:
        raise ValueError("T_end must be positive")
    return stations, trajectories, end_time


def _mode_name(value: Any) -> str | None:
    normalized = str(value).strip().lower().replace("-", "").replace("_", "")
    if normalized in {"minmax", "max"}:
        return "minmax"
    if normalized in {"minsum", "sum"}:
        return "minsum"
    return None


def _lower_bound(data: dict[str, Any], mode: str) -> float | list[tuple[float, float]] | None:
    candidates = data.get("lower_bound_samples", data.get("lb_samples"))
    if isinstance(candidates, list):
        samples: list[tuple[float, float]] = []
        for sample in candidates:
            if isinstance(sample, dict):
                time = sample.get("time", sample.get("t"))
                value = sample.get("lower_bound", sample.get("value"))
            elif isinstance(sample, (list, tuple)) and len(sample) >= 2:
                time, value = sample[0], sample[1]
            else:
                continue
            samples.append((_finite_number(time, "lower-bound time"),
                            _finite_number(value, "lower bound")))
        if samples:
            return sorted(samples)
    if isinstance(data.get("lower_bounds"), dict):
        candidates = data["lower_bounds"].get(mode)
        if isinstance(candidates, (int, float)):
            return _finite_number(candidates, "lower bound")
    for key in ("lower_bound",):
        value = data.get(key)
        if isinstance(value, (int, float)):
            return _finite_number(value, key)
    return None


def _solution_object(payload: dict[str, Any], mode: str) -> dict[str, Any]:
    for container_name in ("solutions", "results", "by_mode"):
        container = payload.get(container_name)
        if isinstance(container, dict):
            candidate = container.get(mode, container.get(MODE_LABELS[mode]))
            if isinstance(candidate, dict):
                return candidate
        if isinstance(container, list):
            for candidate in container:
                if isinstance(candidate, dict) and _mode_name(
                    candidate.get("mode", candidate.get("objective"))
                ) == mode:
                    return candidate
    candidate = payload.get(mode)
    if isinstance(candidate, dict):
        return candidate
    candidate = payload.get("solution")
    if isinstance(candidate, dict):
        merged = dict(candidate)
        for key in ("lower_bound", "lower_bounds", "lower_bound_samples",
                    "lb_samples"):
            if key in payload and key not in merged:
                merged[key] = payload[key]
        return merged
    record_mode = _mode_name(payload.get("mode", payload.get("objective")))
    if record_mode is not None and record_mode != mode:
        raise ValueError(
            f"solution file contains {record_mode}, not requested {mode}"
        )
    return payload


def load_solution(path: Path, mode: str, end_time: float) -> SolutionData:
    with path.open("r", encoding="utf-8") as source:
        payload = json.load(source)
    if not isinstance(payload, dict):
        raise ValueError("solution JSON root must be an object")
    data = _solution_object(payload, mode)
    intervals_payload = data.get("intervals")
    if not isinstance(intervals_payload, list) or not intervals_payload:
        raise ValueError("solution must contain a non-empty intervals array")
    intervals: list[Interval] = []
    for index, item in enumerate(intervals_payload):
        start = _finite_number(item.get("t_start"), f"interval {index} t_start")
        end = _finite_number(item.get("t_end"), f"interval {index} t_end")
        if end <= start:
            raise ValueError(f"interval {index} must have positive duration")
        supports = item.get("supporting_point", item.get("supporting_points"))
        if not isinstance(supports, list):
            raise ValueError(f"interval {index} is missing supporting_point")
        assigned = item.get("assigned_points", [])
        if not isinstance(assigned, list):
            raise ValueError(f"interval {index} assigned_points must be an array")
        intervals.append(
            Interval(
                start, end, [int(value) for value in supports],
                [int(value) for value in assigned],
                _finite_number(item.get("a", 0.0), f"interval {index} a"),
                _finite_number(item.get("b", 0.0), f"interval {index} b"),
                _finite_number(item.get("c", 0.0), f"interval {index} c"),
            )
        )
    intervals.sort(key=lambda interval: interval.start)
    for index, interval in enumerate(intervals):
        if index and not math.isclose(
            intervals[index - 1].end, interval.start, rel_tol=0.0, abs_tol=1e-8
        ):
            raise ValueError("solution intervals must be contiguous")
        if len(interval.supports) == 0:
            raise ValueError(f"interval {index} has no station support entries")
    if not math.isclose(intervals[0].start, 0.0, abs_tol=1e-8) or not math.isclose(
        intervals[-1].end, end_time, abs_tol=1e-8
    ):
        raise ValueError("solution intervals must cover [0, T_end]")
    return SolutionData(mode, intervals, _lower_bound(data, mode))


def animate_mode(
    instance_path: Path,
    solution_path: Path,
    output_dir: Path,
    mode: str,
    frames: int = 200,
    fps: int = 20,
) -> Path:
    stations, trajectories, end_time = load_instance(instance_path)
    solution = load_solution(solution_path, mode, end_time)
    if len(solution.intervals[0].supports) != len(stations):
        raise ValueError("solution station count does not match instance")
    for interval in solution.intervals:
        if len(interval.assigned_points) != len(trajectories):
            raise ValueError(
                "solution assigned_points must match the point count"
            )
        if any(owner < 0 or owner >= len(stations)
               for owner in interval.assigned_points):
            raise ValueError("solution contains an invalid assigned station")
    for interval in solution.intervals:
        if len(interval.supports) != len(stations):
            raise ValueError("supporting_point length differs between intervals")

    times = np.linspace(0.0, end_time, frames)
    all_x = [station[0] for station in stations]
    all_y = [station[1] for station in stations]
    for trajectory in trajectories:
        all_x.extend(trajectory.x.tolist())
        all_y.extend(trajectory.y.tolist())
    x_span = max(all_x) - min(all_x) if all_x else 1.0
    y_span = max(all_y) - min(all_y) if all_y else 1.0
    padding = max(x_span, y_span, 1.0) * 0.12

    figure, (scene, cost_axis) = plt.subplots(
        2, 1, figsize=(9, 9), gridspec_kw={"height_ratios": [2, 1]}
    )
    scene.set_aspect("equal", adjustable="box")
    scene.set_xlim(min(all_x, default=0.0) - padding,
                   max(all_x, default=1.0) + padding)
    scene.set_ylim(min(all_y, default=0.0) - padding,
                   max(all_y, default=1.0) + padding)
    scene.set_title(f"{MODE_LABELS[mode]} kinetic coverage")
    scene.set_xlabel("x")
    scene.set_ylabel("y")
    scene.grid(True, linestyle=":", alpha=0.35)

    for trajectory in trajectories:
        scene.plot(trajectory.x, trajectory.y, linestyle="--",
                   color="gray", alpha=0.55, linewidth=1.0)
    station_colors = [
        POINT_COLORS[index % len(POINT_COLORS)]
        for index in range(len(stations))
    ]
    for station_index, station in enumerate(stations):
        scene.scatter([station[0]], [station[1]], marker="^",
                      color=station_colors[station_index], s=90,
                      label=f"Station {station_index}", zorder=4)
    if stations:
        scene.legend(loc="best", fontsize=8)

    disks = [
        Circle(station, 0.0, facecolor=station_colors[index],
               edgecolor=station_colors[index], alpha=0.3, linewidth=1.0)
        for index, station in enumerate(stations)
    ]
    for disk in disks:
        scene.add_patch(disk)
    point_artists = [
        scene.plot([], [], marker="o", linestyle="", markersize=5,
                   color=station_colors[0] if station_colors else "#1f77b4",
                   markeredgecolor="black", markeredgewidth=0.25,
                   zorder=5)[0]
        for index in range(len(trajectories))
    ]
    time_text = scene.text(0.02, 0.97, "", transform=scene.transAxes,
                           va="top", ha="left",
                           bbox={"facecolor": "white", "alpha": 0.8,
                                 "edgecolor": "none"})

    cost_axis.set_title("Cost over time")
    cost_axis.set_xlabel("Time (t)")
    cost_axis.set_ylabel("Cost")
    cost_axis.grid(True, linestyle="--", alpha=0.35)
    dense_times = np.linspace(0.0, end_time, max(1000, frames * 4))
    dense_costs = np.asarray([solution.cost(time) for time in dense_times])
    cost_axis.plot(dense_times, dense_costs, color=MODE_COLORS[mode],
                   label=MODE_LABELS[mode])
    lower_bound = solution.lower_bound_at(0.0)
    if lower_bound is not None:
        if isinstance(solution.lower_bound, list):
            lower_times, lower_values = zip(*solution.lower_bound)
            cost_axis.plot(lower_times, lower_values, "--", color="black",
                           label="Lower bound")
        else:
            cost_axis.axhline(lower_bound, linestyle="--", color="black",
                              label="Lower bound")
    current_time_line = cost_axis.axvline(0.0, color="#444444",
                                          linewidth=1.2, label="Current time")
    cost_axis.legend(loc="best")
    annotation = figure.text(0.5, 0.015, "", ha="center", va="bottom")
    figure.tight_layout(rect=(0, 0.04, 1, 1))

    def update(frame_index: int) -> list[Any]:
        time = float(times[frame_index])
        interval = solution.interval_at(time)
        for station_index, support in enumerate(interval.supports):
            if support == -1:
                radius = 0.0
            elif support < 0 or support >= len(trajectories):
                raise ValueError(f"invalid point support index {support}")
            else:
                radius = math.dist(
                    stations[station_index], trajectories[support].position(time)
                )
            disks[station_index].set_radius(radius)

        point_positions: list[tuple[float, float]] = []
        for point_index, trajectory in enumerate(trajectories):
            point = trajectory.position(time)
            point_positions.append(point)
            owner = interval.assigned_points[point_index]
            color = station_colors[owner]
            point_artists[point_index].set_data([point[0]], [point[1]])
            point_artists[point_index].set_color(color)

        value = solution.cost(time)
        integral = solution.integral(time)
        support_ids = " ".join(
            f"S{station}=P{point}"
            for station, point in enumerate(interval.supports)
        )
        time_text.set_text(
            f"t = {time:.4f}    cost = {value:.6g}\n"
            f"supports: {support_ids}"
        )
        annotation.set_text(
            f"Mode: {MODE_LABELS[mode]}    Integral to t: {integral:.6g}"
        )
        current_time_line.set_xdata([time, time])
        return [*disks, *point_artists, time_text, annotation,
                current_time_line]

    movie = animation.FuncAnimation(
        figure, update, frames=frames, interval=1000.0 / fps, blit=False
    )
    output_dir.mkdir(parents=True, exist_ok=True)
    stem = f"{instance_path.stem}_{mode}"
    output_path = output_dir / f"{stem}.mp4"
    if animation.writers.is_available("ffmpeg") and shutil.which("ffmpeg"):
        writer = animation.FFMpegWriter(fps=fps, metadata={"artist": "kdc-solver"})
    else:
        output_path = output_dir / f"{stem}.gif"
        writer = animation.PillowWriter(fps=fps)
    movie.save(output_path, writer=writer)
    plt.close(figure)
    return output_path


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--instance", required=True,
        help="instance name or path to its JSON file",
    )
    parser.add_argument(
        "--solution", type=Path, default=None,
        help="solution JSON (default: results/json/<instance>_solution.json)",
    )
    parser.add_argument(
        "--mode", choices=("minmax", "minsum", "both"), default="minmax",
    )
    parser.add_argument("--input-dir", type=Path, default=Path("results/json"))
    parser.add_argument("--output-dir", type=Path, default=Path("results/figures"))
    parser.add_argument("--frames", type=int, default=200)
    parser.add_argument("--fps", type=int, default=20)
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)
    if args.frames <= 0 or args.fps <= 0:
        print("animate_solution.py: --frames and --fps must be positive",
              file=sys.stderr)
        return 2
    instance_path = Path(args.instance)
    if not instance_path.is_file():
        instance_path = args.input_dir / f"{args.instance}.json"
    if not instance_path.is_file():
        print(f"animate_solution.py: instance file not found: {instance_path}",
              file=sys.stderr)
        return 1
    solution_path = args.solution or (
        args.input_dir / f"{instance_path.stem}_solution.json"
    )
    if not solution_path.is_file():
        print(f"animate_solution.py: solution file not found: {solution_path}",
              file=sys.stderr)
        return 1

    modes = ("minmax", "minsum") if args.mode == "both" else (args.mode,)
    try:
        if args.mode == "both":
            with solution_path.open("r", encoding="utf-8") as source:
                payload = json.load(source)
            if not isinstance(payload, dict):
                raise ValueError("solution JSON root must be an object")
            has_mode_solutions = any(
                isinstance(payload.get(key), dict)
                for key in ("solutions", "results", "by_mode", "minmax", "minsum")
            )
            if not has_mode_solutions:
                raise ValueError(
                    "--mode both requires minmax and minsum solution entries"
                )
        paths = [
            animate_mode(instance_path, solution_path, args.output_dir, mode,
                         frames=args.frames, fps=args.fps)
            for mode in modes
        ]
    except (OSError, json.JSONDecodeError, ValueError, KeyError) as error:
        print(f"animate_solution.py: {error}", file=sys.stderr)
        return 1
    for path in paths:
        print(f"Animation written to {path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
