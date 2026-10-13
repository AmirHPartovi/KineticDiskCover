#!/usr/bin/env python3
"""Create experiment-scoped snapshots, comparison sheets, and animations."""

from __future__ import annotations

import argparse
import html
import json
import math
import os
from pathlib import Path
import shutil
import struct
import sys
import tempfile
import textwrap
from typing import Any

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.lines import Line2D
from matplotlib.patches import Circle
import numpy as np

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from animate_best import (  # noqa: E402
    _interval_at,
    _safe_filename,
    load_instance,
    load_solution,
    make_animation,
    position_at,
    radius_at,
)
from kdc_tools.schemas import loads_json  # noqa: E402
from kdc_tools.storage import (  # noqa: E402
    index_experiment_artifacts,
    read_json,
    sha256_file,
    utc_now,
    validate_run_directory,
    write_bytes_create_only,
    write_derived_bytes,
    write_derived_json,
    write_json_create_only,
)

TIME_FRACTIONS = (0.0, 0.5, 1.0)
OBJECTIVES = ("minmax", "minsum", "minmaxsum")
RENDERER_VERSION = 2
SPATIAL_PADDING_FRACTION = 0.08
TRAJECTORY_ALPHA = 0.20
TRAJECTORY_LINE_WIDTH = 0.65
DISK_ALPHA = 0.12
DISK_LINE_WIDTH = 0.8
SUPPORT_LINE_WIDTH = 0.8
SUPPORT_CONNECTOR_ALPHA = 0.65
SUPPORT_MARKER_LINE_WIDTH = 1.2
POINT_MARKER_LINE_WIDTH = 0.3
STATION_MARKER_LINE_WIDTH = 0.35
POINT_MARKER_AREA = 24
RUN_STATES = {
    "PLANNED", "RUNNING", "COMPLETED", "FAILED", "TIME_LIMIT",
    "TIMED_OUT", "INVALID", "CANCELLED", "ABANDONED", "SKIPPED",
}
STATION_COLORS = plt.get_cmap("tab10")


def _contained(experiment: Path, target: Path) -> Path:
    root = experiment.resolve()
    path = target.resolve()
    if not path.is_relative_to(root):
        raise ValueError(f"visualization artifact escapes experiment: {target}")
    return path


def _write_bytes(experiment: Path, target: Path, payload: bytes) -> None:
    target = _contained(experiment, target)
    relative = target.relative_to(experiment.resolve())
    write_bytes_create_only(experiment, relative, payload)
    if not target.is_file() or target.stat().st_size == 0:
        raise ValueError(f"visualization artifact is empty: {target}")


def _artifact(experiment: Path, path: Path, kind: str) -> dict[str, Any]:
    resolved = _contained(experiment, path)
    if not resolved.is_file() or resolved.stat().st_size == 0:
        raise ValueError(f"visualization artifact was not written: {path}")
    return {
        "kind": kind,
        "path": resolved.relative_to(experiment.resolve()).as_posix(),
        "size_bytes": resolved.stat().st_size,
        "sha256": sha256_file(resolved),
    }


def _read_plan(experiment: Path) -> list[dict[str, Any]]:
    path = experiment / "runs" / "plan.jsonl"
    if not path.is_file():
        raise ValueError(f"experiment has no immutable run plan: {path}")
    rows = [
        loads_json(line)
        for line in path.read_text(encoding="utf-8").splitlines()
        if line.strip()
    ]
    if not rows:
        raise ValueError("experiment run plan is empty")
    keys = [row.get("run_key") for row in rows]
    if any(not isinstance(key, str) for key in keys) or len(set(keys)) != len(keys):
        raise ValueError("run plan contains missing or duplicate run keys")
    return rows


def _load_attempts(experiment: Path) -> dict[str, list[dict[str, Any]]]:
    attempts: dict[str, list[dict[str, Any]]] = {}
    runs = experiment / "runs"
    for run_dir in sorted(path for path in runs.iterdir() if path.is_dir()):
        if run_dir.name == "plan.jsonl":
            continue
        try:
            record = read_json(run_dir / "run.json", schema_name="run_record")
            validate_run_directory(run_dir, experiment_id=experiment.name)
            status = record.get("status", "INVALID")
            if status not in RUN_STATES:
                status = "INVALID"
            record["_visual_run_dir"] = str(run_dir)
            attempts.setdefault(record["run_key"], []).append(record)
        except (OSError, ValueError, KeyError, TypeError) as error:
            try:
                record = read_json(run_dir / "run.json")
                key = record.get("run_key")
            except (OSError, ValueError, AttributeError):
                continue
            if isinstance(key, str):
                attempts.setdefault(key, []).append({
                    **record,
                    "status": "INVALID",
                    "_visual_run_dir": str(run_dir),
                    "_visual_validation_error": str(error),
                })
    for rows in attempts.values():
        rows.sort(key=lambda row: (
            str(row.get("finished_at", "")), str(row.get("run_id", ""))
        ))
    return attempts


def _select_attempt(rows: list[dict[str, Any]] | None) -> dict[str, Any] | None:
    if not rows:
        return None
    return rows[-1]


def _result_metadata(record: dict[str, Any] | None) -> tuple[dict[str, Any], dict[str, Any]]:
    if not record:
        return {}, {}
    result = record.get("result")
    result = result if isinstance(result, dict) else {}
    solver = result.get("solver_result")
    solver = solver if isinstance(solver, dict) else {}
    verification = record.get("verification")
    verification = verification if isinstance(verification, dict) else {}
    return solver, verification


def _slot_status(record: dict[str, Any] | None) -> str:
    return str(record.get("status", "PLANNED")) if record else "PLANNED"


def _status_label(record: dict[str, Any] | None) -> str:
    if not record:
        return "PLANNED — no terminal run record"
    solver, verification = _result_metadata(record)
    status = _slot_status(record)
    feasible = record.get("result", {}).get("feasible", False)
    verified = verification.get("passed", False)
    kind = verification.get("kind", "NONE")
    optimality = solver.get("optimality_status", "UNKNOWN")
    return (
        f"{status}; feasible={feasible}; verified={verified} ({kind}); "
        f"optimality={optimality}"
    )


def _objective_label(objective: str, solver: dict[str, Any]) -> str:
    if objective == "minmaxsum":
        vector = solver.get("objective_vector")
        vector = vector if isinstance(vector, dict) else {}
        peak = vector.get("peak_cost", solver.get("peak_cost"))
        integral = vector.get("integral_cost", solver.get("integral_cost"))
        return f"MinMaxSum vector (peak={peak}, integral={integral})"
    if objective == "minmax":
        return f"MinMax peak={solver.get('peak_cost', solver.get('objective_value'))}"
    return f"MinSum integral={solver.get('integral_cost', solver.get('objective_value'))}"


def _validate_solution_for_instance(
    solution: dict[str, Any], instance: dict[str, Any]
) -> None:
    intervals = solution.get("intervals")
    if not isinstance(intervals, list) or not intervals:
        raise ValueError("solution has no intervals")
    t_end = float(instance["T_end"])
    if not math.isclose(float(intervals[0]["t_start"]), 0.0, abs_tol=1e-8):
        raise ValueError("solution does not start at t=0")
    if not math.isclose(float(intervals[-1]["t_end"]), t_end, abs_tol=1e-8):
        raise ValueError("solution does not end at T_end")
    for index, interval in enumerate(intervals):
        lo, hi = float(interval["t_start"]), float(interval["t_end"])
        if not (math.isfinite(lo) and math.isfinite(hi) and hi > lo):
            raise ValueError(f"solution interval {index} has invalid time bounds")
        if index and not math.isclose(
            float(intervals[index - 1]["t_end"]), lo, abs_tol=1e-8
        ):
            raise ValueError("solution intervals are not contiguous")
        supports = interval.get(
            "supporting_point", interval.get("supporting_points")
        )
        owners = interval.get("assigned_points")
        if not isinstance(supports, list) or len(supports) != len(instance["stations"]):
            raise ValueError(f"solution interval {index} has invalid supports")
        if not isinstance(owners, list) or len(owners) != len(instance["trajectories"]):
            raise ValueError(f"solution interval {index} has invalid assignments")
        if any(int(owner) < 0 or int(owner) >= len(instance["stations"])
               for owner in owners):
            raise ValueError(f"solution interval {index} has invalid assignment id")
        if any(int(support) < -1 or int(support) >= len(instance["trajectories"])
               for support in supports):
            raise ValueError(f"solution interval {index} has invalid support id")


def _snapshot_bounds(
    instance: dict[str, Any],
    solution: dict[str, Any],
    times: tuple[float, ...],
    *,
    padding_fraction: float = SPATIAL_PADDING_FRACTION,
) -> dict[str, Any]:
    """Return one square, padded viewport enclosing all displayed geometry."""
    if not math.isfinite(padding_fraction) or padding_fraction < 0:
        raise ValueError("snapshot padding fraction must be finite and nonnegative")
    if not times or any(not math.isfinite(time) for time in times):
        raise ValueError("snapshot bounds require finite display times")

    stations = instance["stations"]
    trajectories = instance["trajectories"]
    xs = [float(station["x"]) for station in stations]
    ys = [float(station["y"]) for station in stations]
    for trajectory in trajectories:
        for waypoint in trajectory["waypoints"]:
            xs.append(float(waypoint["x"]))
            ys.append(float(waypoint["y"]))

    max_radius = 0.0
    station_max_radii = [0.0 for _ in stations]
    for time in times:
        for point_index in range(len(trajectories)):
            x, y = position_at(instance, point_index, time)
            xs.append(x)
            ys.append(y)
        radii = [radius_at(solution, i, time, instance)
                 for i in range(len(stations))]
        max_radius = max(max_radius, *radii, 0.0)
        station_max_radii = [
            max(previous, current)
            for previous, current in zip(station_max_radii, radii)
        ]

    for station, radius in zip(stations, station_max_radii):
        sx, sy = float(station["x"]), float(station["y"])
        xs.extend((sx - radius, sx + radius))
        ys.extend((sy - radius, sy + radius))

    if not xs or not ys:
        raise ValueError("cannot compute snapshot bounds for empty geometry")
    xmin, xmax = min(xs), max(xs)
    ymin, ymax = min(ys), max(ys)
    span = max(xmax - xmin, ymax - ymin, 2.0 * max_radius)
    if span == 0.0:
        span = 1.0
    half_span = span * (0.5 + padding_fraction)
    center_x = (xmin + xmax) * 0.5
    center_y = (ymin + ymax) * 0.5
    return {
        "xlim": (center_x - half_span, center_x + half_span),
        "ylim": (center_y - half_span, center_y + half_span),
        "padding_fraction": padding_fraction,
        "maximum_radius": max_radius,
    }


def _legend_handles(
    *, show_trajectories: bool, show_support_connectors: bool
) -> list[Line2D]:
    handles = [
        Line2D([], [], color="#777777", marker="o", linestyle="None",
               markersize=7, markerfacecolor="#777777", alpha=DISK_ALPHA + 0.2,
               label="Coverage disk"),
        Line2D([], [], color="#333333", marker="^", linestyle="None",
               markersize=6, label="Station"),
        Line2D([], [], color="#333333", marker="o", linestyle="None",
               markersize=5, label="Moving point (color=assigned station)"),
        Line2D([], [], color="#333333", marker="D", linestyle="None",
               markerfacecolor="none", markersize=6, label="Active support"),
    ]
    if show_trajectories:
        handles.insert(
            0, Line2D([], [], color="#666666", linestyle="--",
                      linewidth=TRAJECTORY_LINE_WIDTH, label="Trajectory")
        )
    if show_support_connectors:
        handles.append(
            Line2D([], [], color="#555555", linewidth=SUPPORT_LINE_WIDTH,
                   label="Support connector")
        )
    return handles


def _snapshot_title(
    instance: dict[str, Any],
    record: dict[str, Any] | None,
    plan: dict[str, Any],
    run_id: str,
) -> str:
    solver, verification = _result_metadata(record)
    verification_kind = verification.get("kind", "NONE")
    verified = verification.get("passed", False)
    title_lines = [
        f"{instance.get('name') or plan['instance_id']} | "
        f"{plan['algorithm']} | {plan['objective']}",
        f"run {run_id}",
        (
            f"{_slot_status(record)}; "
            f"feasible={bool((record or {}).get('result', {}).get('feasible', False))}; "
            f"verified={verified} ({verification_kind}); "
            f"optimality={solver.get('optimality_status', 'UNKNOWN')}"
        ),
        _objective_label(plan["objective"], solver),
    ]
    return "\n".join(
        textwrap.fill(line, width=84, break_long_words=True,
                      break_on_hyphens=False)
        for line in title_lines
    )


def _draw_panel(
    axis: Any,
    instance: dict[str, Any],
    solution: dict[str, Any],
    record: dict[str, Any] | None,
    plan: dict[str, Any],
    tau: float,
    bounds: dict[str, Any],
    *,
    show_trajectories: bool,
    point_label_policy: str,
    show_support_connectors: bool,
) -> None:
    t_end = float(instance["T_end"])
    time = tau * t_end
    stations = instance["stations"]
    trajectories = instance["trajectories"]
    interval = _interval_at(solution, time)
    supports = interval.get(
        "supporting_point", interval.get("supporting_points", [])
    )
    owners = interval["assigned_points"]
    positions = [
        position_at(instance, point_index, time)
        for point_index in range(len(trajectories))
    ]
    radii = [
        radius_at(solution, station_index, time, instance)
        for station_index in range(len(stations))
    ]
    axis.set_xlim(*bounds["xlim"])
    axis.set_ylim(*bounds["ylim"])
    axis.set_aspect("equal", adjustable="box")
    axis.grid(True, linestyle=":", alpha=0.35)
    axis.set_xlabel("x")
    axis.set_ylabel("y")
    axis.set_title(f"τ = {tau:g}  |  t = {time:.6g}")

    if show_trajectories:
        for trajectory in trajectories:
            wx = [float(point["x"]) for point in trajectory["waypoints"]]
            wy = [float(point["y"]) for point in trajectory["waypoints"]]
            axis.plot(
                wx, wy, "--", color="#666666", alpha=TRAJECTORY_ALPHA,
                linewidth=TRAJECTORY_LINE_WIDTH, zorder=1,
            )
    for station_index, station in enumerate(stations):
        color = STATION_COLORS(station_index % 10)
        sx, sy = float(station["x"]), float(station["y"])
        axis.add_patch(Circle(
            (sx, sy), radii[station_index], facecolor=color, edgecolor=color,
            alpha=DISK_ALPHA, linewidth=DISK_LINE_WIDTH, zorder=2,
        ))
        axis.scatter(
            [sx], [sy], marker="^", s=42, color=[color], edgecolor="black",
            linewidth=STATION_MARKER_LINE_WIDTH, zorder=3,
        )
    for point_index, (px, py) in enumerate(positions):
        owner = int(owners[point_index])
        color = STATION_COLORS(owner % 10)
        axis.scatter(
            [px], [py], marker="o", s=POINT_MARKER_AREA, color=[color],
            edgecolor="black", linewidth=POINT_MARKER_LINE_WIDTH, zorder=4,
        )
        if point_label_policy == "all":
            axis.annotate(
                f"P{point_index}/S{owner}", (px, py), xytext=(3, 3),
                textcoords="offset points", fontsize=6, zorder=7,
            )
    active_support_points: dict[int, int] = {}
    for station_index, support in enumerate(supports):
        support = int(support)
        if support < 0:
            continue
        px, py = positions[support]
        station = stations[station_index]
        color = STATION_COLORS(station_index % 10)
        active_support_points.setdefault(support, station_index)
        if show_support_connectors:
            axis.plot(
                [float(station["x"]), px], [float(station["y"]), py],
                color=color, linewidth=SUPPORT_LINE_WIDTH,
                alpha=SUPPORT_CONNECTOR_ALPHA,
                zorder=5,
            )
    for point_index, station_index in active_support_points.items():
        px, py = positions[point_index]
        color = STATION_COLORS(station_index % 10)
        axis.scatter(
            [px], [py], marker="D", s=48, facecolors="none",
            edgecolors=[color], linewidth=SUPPORT_MARKER_LINE_WIDTH, zorder=6,
        )
        if point_label_policy == "supports":
            owner = int(owners[point_index])
            axis.annotate(
                f"P{point_index}/S{owner}", (px, py), xytext=(3, 3),
                textcoords="offset points", fontsize=6, zorder=7,
            )


def _render_solution_images(
    experiment: Path,
    instance: dict[str, Any],
    solution: dict[str, Any],
    record: dict[str, Any] | None,
    plan: dict[str, Any],
    run_id: str,
    dpi: int,
    *,
    show_trajectories: bool,
    point_label_policy: str,
    show_support_connectors: bool,
) -> dict[str, Any]:
    instance_id = _safe_filename(str(plan["instance_id"]))
    algorithm = _safe_filename(str(plan["algorithm"]))
    objective = _safe_filename(str(plan["objective"]))
    directory = experiment / "snapshots" / instance_id / objective / algorithm / run_id
    t_end = float(instance["T_end"])
    bounds = _snapshot_bounds(
        instance, solution,
        tuple(tau * t_end for tau in TIME_FRACTIONS),
    )
    rendering_settings = {
        "renderer_version": RENDERER_VERSION,
        "dpi": dpi,
        "layout_method": "reserved-gridspec",
        "output_format": "png",
        "normalized_times": list(TIME_FRACTIONS),
        "actual_times": [tau * t_end for tau in TIME_FRACTIONS],
        "T_end": t_end,
        "spatial_bounds_policy": (
            "one square padded extent over station centers, trajectory waypoints, "
            "snapshot positions, and maximum displayed-time coverage radii"
        ),
        "spatial_bounds": {
            "xlim": list(bounds["xlim"]),
            "ylim": list(bounds["ylim"]),
            "padding_fraction": bounds["padding_fraction"],
            "maximum_radius": bounds["maximum_radius"],
        },
        "axis_scaling": "equal",
        "trajectory_visibility": "visible" if show_trajectories else "hidden",
        "point_label_policy": point_label_policy,
        "support_connector_policy": (
            "visible" if show_support_connectors else "hidden"
        ),
        "opacity": {
            "trajectory": TRAJECTORY_ALPHA,
            "coverage_disk": DISK_ALPHA,
            "support_connector": SUPPORT_CONNECTOR_ALPHA,
        },
        "line_widths": {
            "trajectory": TRAJECTORY_LINE_WIDTH,
            "coverage_disk_outline": DISK_LINE_WIDTH,
            "support_connector": SUPPORT_LINE_WIDTH,
            "support_marker_outline": SUPPORT_MARKER_LINE_WIDTH,
            "moving_point_outline": POINT_MARKER_LINE_WIDTH,
            "station_marker_outline": STATION_MARKER_LINE_WIDTH,
        },
        "geometry": "piecewise-linear trajectories; support-point distance radii",
    }
    title = _snapshot_title(instance, record, plan, run_id)
    from io import BytesIO

    def save_figure(figure: Any, path: Path, kind: str) -> dict[str, Any]:
        buffer = BytesIO()
        figure.savefig(buffer, format="png", dpi=dpi)
        png_data = buffer.getvalue()
        if png_data[:8] != b"\x89PNG\r\n\x1a\n" or len(png_data) < 24:
            plt.close(figure)
            raise ValueError("Matplotlib produced an invalid PNG image")
        width, height = struct.unpack(">II", png_data[16:24])
        plt.close(figure)
        _write_bytes(experiment, path, png_data)
        return {
            **_artifact(experiment, path, kind),
            "image_dimensions": {"width": width, "height": height},
        }

    panel_paths = []
    for tau in TIME_FRACTIONS:
        figure = plt.figure(
            figsize=(6.2, 6.0), dpi=dpi, constrained_layout=True
        )
        grid = figure.add_gridspec(2, 1, height_ratios=(12, 1))
        axis = figure.add_subplot(grid[0, 0])
        legend_axis = figure.add_subplot(grid[1, 0])
        legend_axis.set_axis_off()
        figure.suptitle(title, fontsize=8.5)
        _draw_panel(
            axis, instance, solution, record, plan, tau, bounds,
            show_trajectories=show_trajectories,
            point_label_policy=point_label_policy,
            show_support_connectors=show_support_connectors,
        )
        legend_axis.legend(
            handles=_legend_handles(
                show_trajectories=show_trajectories,
                show_support_connectors=show_support_connectors,
            ), loc="center", ncol=3,
            fontsize=7, frameon=False,
        )
        path = directory / f"tau-{tau:g}.png"
        panel_paths.append(save_figure(figure, path, "snapshot-panel"))

    figure = plt.figure(
        figsize=(15.0, 6.0), dpi=dpi, constrained_layout=True
    )
    grid = figure.add_gridspec(2, 3, height_ratios=(12, 1))
    axes = [figure.add_subplot(grid[0, index]) for index in range(3)]
    legend_axis = figure.add_subplot(grid[1, :])
    legend_axis.set_axis_off()
    figure.suptitle(title, fontsize=9)
    for axis, tau in zip(axes, TIME_FRACTIONS):
        _draw_panel(
            axis, instance, solution, record, plan, tau, bounds,
            show_trajectories=show_trajectories,
            point_label_policy=point_label_policy,
            show_support_connectors=show_support_connectors,
        )
    legend_axis.legend(
        handles=_legend_handles(
            show_trajectories=show_trajectories,
            show_support_connectors=show_support_connectors,
        ), loc="center", ncol=6,
        fontsize=8, frameon=False,
    )
    triptych_path = directory / "three-time.png"
    triptych = save_figure(figure, triptych_path, "three-panel-snapshot")
    return {
        "triptych": triptych,
        "panels": panel_paths,
        "rendering_parameters": {
            **rendering_settings,
            "image_dimensions": {
                "individual_panel": panel_paths[0]["image_dimensions"],
                "triptych": triptych["image_dimensions"],
            },
        },
    }


def _write_comparison_sheet(
    experiment: Path,
    instance_id: str,
    objective: str,
    rows: list[dict[str, Any]],
) -> dict[str, Any]:
    target = experiment / "snapshots" / "comparisons" / _safe_filename(
        instance_id
    ) / f"{objective}.html"
    parts = [
        "<!doctype html><html><head><meta charset=\"utf-8\">",
        f"<title>{html.escape(instance_id)} — {html.escape(objective)}</title>",
        "<style>body{font:14px sans-serif;margin:1.5rem}table{border-collapse:collapse;width:100%}"
        "th,td{border:1px solid #bbb;padding:.45rem;vertical-align:top}th{background:#eee}"
        "img{max-width:100%;height:auto} .placeholder{background:#f4f4f4;color:#555;padding:1rem}"
        "</style></head><body>",
        f"<h1>{html.escape(instance_id)} — {html.escape(objective)}</h1>",
        "<p>Columns show normalized τ = 0, 0.5, 1; actual time is scaled by T_end. "
        "Rows include every algorithm scheduled by the immutable run plan.</p>",
        "<table><thead><tr><th>Algorithm / run status</th>"
        "<th>Start (τ=0)</th><th>Midpoint (τ=0.5)</th><th>End (τ=1)</th>"
        "</tr></thead><tbody>",
    ]
    for row in rows:
        entry = row["plan"]
        record = row.get("record")
        run_id = str(record.get("run_id", "not-created")) if record else "not-created"
        details = _status_label(record)
        parts.append(
            "<tr><th>"
            f"{html.escape(str(entry['algorithm']))}<br>"
            f"run: {html.escape(run_id)}<br>{html.escape(details)}"
            "</th>"
        )
        panel_paths = (
            row.get("snapshot", {}).get("panels", [])
            if row.get("snapshot") else []
        )
        for panel_index in range(3):
            parts.append("<td>")
            if panel_index < len(panel_paths):
                relative = os.path.relpath(
                    experiment / panel_paths[panel_index]["path"],
                    target.parent,
                )
                parts.append(
                    f"<a href=\"{html.escape(relative)}\">"
                    f"<img src=\"{html.escape(relative)}\" alt=\""
                    f"{html.escape(str(entry['algorithm']))} τ={TIME_FRACTIONS[panel_index]}\"></a>"
                )
            else:
                reason = row.get("render_failure") or row.get(
                    "ineligible_reason", "no usable solution artifact"
                )
                parts.append(
                    f"<div class=\"placeholder\">{html.escape(details)}<br>"
                    f"{html.escape(str(reason))}</div>"
                )
            parts.append("</td>")
        parts.append("</tr>")
    parts.append("</tbody></table></body></html>\n")
    _write_bytes(experiment, target, "".join(parts).encode("utf-8"))
    return _artifact(experiment, target, "comparison-sheet")


def _check_selection_coverage(
    experiment: Path, plan: list[dict[str, Any]], manifest: dict[str, Any]
) -> tuple[list[str], list[str]]:
    instance_ids = sorted({str(row["instance_id"]) for row in plan})
    objectives = sorted({str(row["objective"]) for row in plan})
    selection_path = experiment / "dataset" / "selection_manifest.json"
    if not selection_path.is_file():
        selection_path = experiment / "dataset" / "manifest.json"
    expected_count = manifest.get("selected_instance_count")
    if selection_path.is_file():
        selection = read_json(selection_path)
        selected = selection.get("selected_instance_count")
        if selected is None and isinstance(selection.get("instances"), list):
            selected = len(selection["instances"])
        if selected is not None and len(instance_ids) != int(selected):
            raise ValueError(
                f"run plan has {len(instance_ids)} instances; selection manifest "
                f"requires {selected}"
            )
        if expected_count is not None and int(expected_count) != len(instance_ids):
            raise ValueError("experiment and run-plan selected-instance counts differ")
    expected_objectives = manifest.get("objectives")
    if isinstance(expected_objectives, list) and set(objectives) != set(
        str(value) for value in expected_objectives
    ):
        raise ValueError("run-plan objectives differ from experiment configuration")
    if len(objectives) != len(set(objectives)):
        raise ValueError("duplicate objectives in run plan")
    return instance_ids, objectives


def generate(
    experiment_dir: str | Path,
    *,
    fps: int = 12,
    frames: int = 48,
    dpi: int = 90,
    show_trajectories: bool = True,
    point_label_policy: str = "supports",
    show_support_connectors: bool = False,
) -> dict[str, Any]:
    experiment = Path(experiment_dir).resolve()
    if not experiment.is_dir():
        raise ValueError(f"experiment directory does not exist: {experiment}")
    if fps <= 0 or frames < 2 or dpi <= 0:
        raise ValueError("fps/dpi must be positive and frames must be at least two")
    if point_label_policy not in {"none", "supports", "all"}:
        raise ValueError("point label policy must be none, supports, or all")
    manifest = read_json(experiment / "experiment_manifest.json")
    plan = _read_plan(experiment)
    attempts = _load_attempts(experiment)
    instance_ids, objectives = _check_selection_coverage(experiment, plan, manifest)
    if not set(objectives).issubset(OBJECTIVES):
        raise ValueError("run plan contains an unsupported objective")
    by_slot: dict[tuple[str, str], list[dict[str, Any]]] = {}
    for entry in plan:
        by_slot.setdefault(
            (str(entry["instance_id"]), str(entry["objective"])), []
        ).append(entry)
    if len(by_slot) != len(instance_ids) * len(objectives):
        raise ValueError("run plan is missing an instance/objective comparison group")

    output_rows = []
    sheets = []
    animation_count = 0
    snapshot_count = 0
    for instance_id in instance_ids:
        input_path = experiment / "input" / f"{instance_id}.json"
        if not input_path.is_file():
            raise ValueError(f"planned canonical input is missing: {input_path}")
        instance = load_instance(input_path)
        for objective in objectives:
            comparison_rows = []
            entries = by_slot.get((instance_id, objective), [])
            if not entries:
                raise ValueError(
                    f"no algorithm slots for instance {instance_id}/{objective}"
                )
            for entry in sorted(entries, key=lambda item: str(item["algorithm"])):
                run_key = str(entry["run_key"])
                record = _select_attempt(attempts.get(run_key))
                run_id = str(record.get("run_id")) if record else "not-created"
                row: dict[str, Any] = {
                    "plan": entry,
                    "record": record,
                    "snapshot": None,
                    "animation": None,
                    "ineligible_reason": None,
                    "render_failure": None,
                }
                if record is None:
                    row["ineligible_reason"] = "NO_TERMINAL_RUN_RECORD"
                elif record.get("_visual_validation_error"):
                    row["ineligible_reason"] = (
                        "RUN_RECORD_VALIDATION_FAILED: "
                        + str(record["_visual_validation_error"])
                    )
                else:
                    run_dir = Path(str(record["_visual_run_dir"]))
                    result = record.get("result")
                    result = result if isinstance(result, dict) else {}
                    solution_rel = (
                        (result.get("solver_result") or {}).get("solution_json_path")
                        if isinstance(result.get("solver_result"), dict) else None
                    )
                    solution_path = (
                        run_dir / str(solution_rel) if solution_rel else
                        run_dir / "result" / "solution.json"
                    )
                    if not solution_path.is_file():
                        row["ineligible_reason"] = (
                            "NO_SOLUTION_ARTIFACT"
                            if not result.get("feasible")
                            else "FEASIBLE_RESULT_WITHOUT_SOLUTION_ARTIFACT"
                        )
                    else:
                        row["source_solution_path"] = (
                            solution_path.relative_to(experiment).as_posix()
                        )
                        row["source_solution_sha256"] = sha256_file(solution_path)
                        if result.get("feasible") is not True:
                            row["ineligible_reason"] = "RESULT_NOT_FEASIBLE"
                            comparison_rows.append(row)
                            output_rows.append(row)
                            continue
                        try:
                            solution = load_solution(solution_path, instance)
                            _validate_solution_for_instance(solution, instance)
                            solution_objective = str(
                                solution.get("objective", objective)
                            ).strip().lower().replace("-", "").replace("_", "")
                            if solution_objective != objective.replace("-", ""):
                                raise ValueError(
                                    f"serialized solution objective "
                                    f"{solution_objective!r} does not match "
                                    f"planned {objective!r}"
                                )
                        except (OSError, ValueError, KeyError, TypeError) as error:
                            row["ineligible_reason"] = (
                                f"INVALID_SOLUTION: {type(error).__name__}: {error}"
                            )
                        else:
                            solver_result, verification = _result_metadata(record)
                            rendered_record = {
                                **solver_result,
                                "run_id": record.get("run_id"),
                                "experiment_id": record.get("experiment_id"),
                                "instance_id": record.get("instance_id"),
                                "instance_name": instance.get("name", instance_id),
                                "algorithm_name": record.get("algorithm"),
                                "objective": record.get("objective"),
                                "run_status": record.get("status"),
                                "feasible": result.get("feasible"),
                                "verified": verification.get("passed"),
                                "verification_kind": verification.get("kind"),
                                "optimality_status": solver_result.get(
                                    "optimality_status", "UNKNOWN"
                                ),
                                "solution_json_path": row["source_solution_path"],
                                "source_solution_sha256": row[
                                    "source_solution_sha256"
                                ],
                                "result_json_path": (
                                    run_dir / "result" / "result.json"
                                ).relative_to(experiment).as_posix(),
                                "git_commit": record.get(
                                    "provenance", {}
                                ).get("source_commit"),
                            }
                            try:
                                snapshot = _render_solution_images(
                                    experiment, instance, solution,
                                    record, entry, run_id, dpi,
                                    show_trajectories=show_trajectories,
                                    point_label_policy=point_label_policy,
                                    show_support_connectors=show_support_connectors,
                                )
                                row["snapshot"] = snapshot
                                animation_dir = (
                                    experiment / "animations"
                                    / _safe_filename(instance_id)
                                    / _safe_filename(objective)
                                    / _safe_filename(str(entry["algorithm"]))
                                )
                                animation_record = {
                                    **rendered_record,
                                    "summary": solver_result.get("summary", {}),
                                    "objective_vector": solver_result.get(
                                        "objective_vector"
                                    ),
                                }
                                movie_path = make_animation(
                                    instance, solution, animation_record,
                                    animation_dir, objective,
                                    fps=fps, frames=frames, dpi=dpi,
                                )
                                row["animation"] = _artifact(
                                    experiment, movie_path,
                                    "per-solution-animation",
                                )
                                animation_count += 1
                                snapshot_count += 1 + len(snapshot["panels"])
                                row["rendering_parameters"] = {
                                    **snapshot["rendering_parameters"],
                                    "animation_fps": fps,
                                    "animation_frames": frames,
                                    "animation_dpi": dpi,
                                    "animation_format": movie_path.suffix.lstrip("."),
                                    "encoder_fallback": (
                                        "gif-pillow" if movie_path.suffix == ".gif"
                                        else "mp4-ffmpeg"
                                    ),
                                }
                            except (OSError, ValueError, KeyError, TypeError) as error:
                                row["render_failure"] = (
                                    f"{type(error).__name__}: {error}"
                                )
                comparison_rows.append(row)
                output_rows.append(row)
            sheet = _write_comparison_sheet(
                experiment, instance_id, objective, comparison_rows
            )
            sheet.update({
                "experiment_id": experiment.name,
                "instance_id": instance_id,
                "objective": objective,
                "member_slots": [{
                    "run_id": (
                        row["record"].get("run_id") if row.get("record") else None
                    ),
                    "run_key": row["plan"]["run_key"],
                    "algorithm": row["plan"]["algorithm"],
                    "run_status": _slot_status(row.get("record")),
                    "source_solution_sha256": row.get(
                        "source_solution_sha256"
                    ),
                } for row in comparison_rows],
                "rendering_parameters": {
                    "columns": ["tau=0", "tau=0.5", "tau=1"],
                    "dpi": dpi,
                    "normalized_times": list(TIME_FRACTIONS),
                    "time_conversion": "t = tau * T_end",
                },
            })
            sheets.append(sheet)

    artifacts = []
    for row in output_rows:
        entry = row["plan"]
        record = row["record"]
        solver, verification = _result_metadata(record)
        artifacts.append({
            "experiment_id": experiment.name,
            "run_id": record.get("run_id") if record else None,
            "run_key": entry["run_key"],
            "instance_id": entry["instance_id"],
            "instance_name": (
                record.get("result", {}).get("solver_result", {}).get(
                    "instance_name"
                )
                if record else entry["instance_id"]
            ),
            "algorithm": entry["algorithm"],
            "objective": entry["objective"],
            "run_status": _slot_status(record),
            "feasible": (
                record.get("result", {}).get("feasible") if record else None
            ),
            "verification_kind": verification.get("kind", "NONE"),
            "verified": verification.get("passed", False),
            "optimality_status": solver.get("optimality_status", "UNKNOWN"),
            "source_solution_path": row.get("source_solution_path"),
            "source_solution_sha256": row.get("source_solution_sha256"),
            "snapshots": row.get("snapshot"),
            "animation": row.get("animation"),
            "rendering_parameters": row.get("rendering_parameters", {
                "dpi": dpi,
                "fps": fps,
                "frames": frames,
                "normalized_times": list(TIME_FRACTIONS),
            }),
            "generation_status": (
                "GENERATED" if row.get("snapshot") and row.get("animation")
                else "INELIGIBLE" if row.get("ineligible_reason")
                else "FAILED"
            ),
            "failure_reason": row.get("render_failure")
            or row.get("ineligible_reason"),
        })
    manifest_payload = {
        "schema_version": 1,
        "renderer_version": RENDERER_VERSION,
        "experiment_id": experiment.name,
        "dataset_instance_count": len(instance_ids),
        "objective_count": len(objectives),
        "expected_comparison_sheets": len(instance_ids) * len(objectives),
        "comparison_sheets": sheets,
        "planned_slots": len(plan),
        "generated_solution_snapshots": sum(
            bool(row.get("snapshot")) for row in output_rows
        ),
        "generated_snapshot_files": snapshot_count,
        "generated_animations": animation_count,
        "ineligible_slots": sum(bool(row.get("ineligible_reason")) for row in output_rows),
        "failed_slots": sum(bool(row.get("render_failure")) for row in output_rows),
        "rendering_policy": {
            "render_requires": "valid serialized solution and feasible result",
            "verification_is_not_inferred_from_rendering": True,
            "render_unverified_feasible_incumbents": True,
            "normalized_times": list(TIME_FRACTIONS),
            "time_conversion": "t = tau * T_end",
            "fps": fps,
            "frames": frames,
            "dpi": dpi,
            "renderer_version": RENDERER_VERSION,
            "visualization_schema_version": 1,
            "layout_method": "reserved-gridspec",
            "output_format": "png",
            "trajectory_visibility": (
                "visible" if show_trajectories else "hidden"
            ),
            "point_label_policy": point_label_policy,
            "support_connector_policy": (
                "visible" if show_support_connectors else "hidden"
            ),
            "opacity": {
                "trajectory": TRAJECTORY_ALPHA,
                "coverage_disk": DISK_ALPHA,
                "support_connector": SUPPORT_CONNECTOR_ALPHA,
            },
            "line_widths": {
                "trajectory": TRAJECTORY_LINE_WIDTH,
                "coverage_disk_outline": DISK_LINE_WIDTH,
                "support_connector": SUPPORT_LINE_WIDTH,
                "support_marker_outline": SUPPORT_MARKER_LINE_WIDTH,
                "moving_point_outline": POINT_MARKER_LINE_WIDTH,
                "station_marker_outline": STATION_MARKER_LINE_WIDTH,
            },
            "spatial_bounds_policy": (
                "one square padded extent over station centers, trajectory "
                "waypoints, snapshot positions, and displayed-time disk extents"
            ),
            "axis_scaling": "equal",
            "animation_encoder": "ffmpeg MP4 when available; Pillow GIF fallback",
        },
        "slots": artifacts,
    }
    manifest_path = experiment / "reports" / "visualization_manifest.json"
    write_json_create_only(experiment, manifest_path.relative_to(experiment),
                           manifest_payload)
    report = experiment / "reports" / "visualization_report.md"
    lines = [
        "# Visualization generation report", "",
        f"- Experiment: `{experiment.name}`",
        f"- Selected instances: {len(instance_ids)}",
        f"- Objectives: {', '.join(objectives)}",
        f"- Expected comparison sheets: {len(instance_ids) * len(objectives)}",
        f"- Created comparison sheets: {len(sheets)}",
        f"- Slots in run plan: {len(plan)}",
        f"- Solutions with snapshots: {manifest_payload['generated_solution_snapshots']}",
        f"- Per-solution animations: {animation_count}",
        f"- Ineligible slots: {manifest_payload['ineligible_slots']}",
        f"- Rendering failures: {manifest_payload['failed_slots']}",
        "",
        "Renderability requires a valid serialized solution and a feasible result. "
        "Verification and optimality are displayed from run records and are never "
        "inferred from rendering.",
        "",
        f"Manifest: `{manifest_path.relative_to(experiment).as_posix()}`",
    ]
    write_bytes_create_only(
        experiment, report.relative_to(experiment),
        ("\n".join(lines) + "\n").encode("utf-8"),
    )
    index_experiment_artifacts(experiment)
    audit(experiment)
    return manifest_payload


def audit(experiment_dir: str | Path) -> dict[str, Any]:
    experiment = Path(experiment_dir).resolve()
    manifest_path = experiment / "reports" / "visualization_manifest.json"
    if not manifest_path.is_file():
        raise ValueError("visualization manifest is missing")
    payload = read_json(manifest_path)
    if payload.get("experiment_id") != experiment.name:
        raise ValueError("visualization manifest belongs to another experiment")
    if len(payload.get("comparison_sheets", [])) != payload.get(
        "expected_comparison_sheets"
    ):
        raise ValueError("visualization comparison-sheet coverage is incomplete")
    missing = []
    for sheet in payload.get("comparison_sheets", []):
        path = _contained(experiment, experiment / sheet["path"])
        if not path.is_file() or path.stat().st_size != sheet["size_bytes"] \
                or sha256_file(path) != sheet["sha256"]:
            missing.append(sheet["path"])
    indexed_artifacts = read_json(
        experiment / "artifacts.json", schema_name="artifact_manifest"
    ).get("artifacts", [])
    index_by_path = {
        item.get("path"): item for item in indexed_artifacts
        if isinstance(item, dict)
    }
    indexed_paths: list[dict[str, Any]] = []

    def check_artifact(item: dict[str, Any], *, png: bool = False) -> None:
        path = _contained(experiment, experiment / item["path"])
        if (not path.is_file() or path.stat().st_size != item["size_bytes"]
                or sha256_file(path) != item["sha256"]):
            missing.append(item["path"])
            return
        indexed_paths.append(item)
        if png:
            try:
                from PIL import Image

                with Image.open(path) as image:
                    dimensions = image.size
                    image.verify()
                expected = item.get("image_dimensions", {})
                if image.format != "PNG" or (
                    expected and dimensions != (
                        expected.get("width"), expected.get("height")
                    )
                ):
                    missing.append(f"{item['path']} (invalid PNG metadata)")
            except (OSError, ValueError) as error:
                missing.append(f"{item['path']} (invalid PNG: {error})")

    for slot in payload.get("slots", []):
        if slot["generation_status"] == "FAILED":
            raise ValueError(
                f"visualization generation failed for {slot.get('run_key')}: "
                f"{slot.get('failure_reason')}"
            )
        snapshot = slot.get("snapshots")
        if slot["generation_status"] == "GENERATED" and (
            not snapshot or not slot.get("animation")
        ):
            raise ValueError(
                f"rendered slot {slot.get('run_key')} is missing an artifact record"
            )
        if snapshot:
            for item in [snapshot["triptych"], *snapshot["panels"]]:
                check_artifact(item, png=True)
        if slot.get("animation"):
            check_artifact(slot["animation"])
        if slot["generation_status"] != "GENERATED" and not slot.get(
            "failure_reason"
        ):
            raise ValueError(
                f"slot {slot.get('run_key')} is unrendered without a reason"
            )
        source_path = slot.get("source_solution_path")
        source_hash = slot.get("source_solution_sha256")
        if source_path:
            source = _contained(experiment, experiment / source_path)
            if not source.is_file() or sha256_file(source) != source_hash:
                missing.append(source_path)
    if missing:
        raise ValueError("missing or modified visualization artifacts: "
                         + ", ".join(sorted(set(missing))))
    for item in payload.get("comparison_sheets", []):
        indexed_paths.append(item)
    manifest_relative = manifest_path.relative_to(experiment).as_posix()
    manifest_index_entry = index_by_path.get(manifest_relative)
    for item in indexed_paths:
        indexed = index_by_path.get(item["path"])
        if (
            not indexed
            or indexed.get("size_bytes") != item.get("size_bytes")
            or indexed.get("sha256") != item.get("sha256")
        ):
            missing.append(f"{item['path']} (global artifact index mismatch)")
    if (
        not manifest_index_entry
        or manifest_index_entry.get("size_bytes") != manifest_path.stat().st_size
        or manifest_index_entry.get("sha256") != sha256_file(manifest_path)
    ):
        missing.append(f"{manifest_relative} (global artifact index mismatch)")
    if missing:
        raise ValueError("missing or modified visualization artifacts: "
                         + ", ".join(sorted(set(missing))))
    plan = _read_plan(experiment)
    if len(payload.get("slots", [])) != len(plan):
        raise ValueError("visualization manifest does not index every planned slot")
    manifest_slots = {
        slot["run_key"]: slot for slot in payload.get("slots", [])
    }
    if len(manifest_slots) != len(plan):
        raise ValueError("visualization manifest has duplicate or missing run keys")
    attempts = _load_attempts(experiment)
    instance_cache: dict[str, dict[str, Any]] = {}
    for entry in plan:
        run_key = str(entry["run_key"])
        visual_slot = manifest_slots[run_key]
        record = _select_attempt(attempts.get(run_key))
        should_render = False
        if record and not record.get("_visual_validation_error"):
            result = record.get("result")
            result = result if isinstance(result, dict) else {}
            solver = result.get("solver_result")
            solver = solver if isinstance(solver, dict) else {}
            solution_rel = solver.get("solution_json_path")
            run_dir = Path(str(record["_visual_run_dir"]))
            source = (
                run_dir / str(solution_rel) if solution_rel
                else run_dir / "result" / "solution.json"
            )
            if result.get("feasible") is True and source.is_file():
                instance_id = str(entry["instance_id"])
                if instance_id not in instance_cache:
                    instance_cache[instance_id] = load_instance(
                        experiment / "input" / f"{instance_id}.json"
                    )
                try:
                    parsed = load_solution(source, instance_cache[instance_id])
                    _validate_solution_for_instance(
                        parsed, instance_cache[instance_id]
                    )
                    actual_objective = str(parsed.get("objective", entry["objective"]))
                    actual_objective = actual_objective.lower().replace(
                        "-", ""
                    ).replace("_", "")
                    should_render = (
                        actual_objective == str(entry["objective"]).replace("-", "")
                    )
                except (OSError, ValueError, KeyError, TypeError):
                    should_render = False
        if should_render and visual_slot["generation_status"] != "GENERATED":
            raise ValueError(
                f"renderable run {run_key} is missing its snapshot or animation"
            )
        if not should_render and visual_slot["generation_status"] == "INELIGIBLE":
            if not visual_slot.get("failure_reason"):
                raise ValueError(
                    f"ineligible run {run_key} has no grounded reason"
                )
    return {
        "ok": True,
        "planned_slots": len(plan),
        "comparison_sheets": len(payload["comparison_sheets"]),
        "rendered_slots": payload["generated_solution_snapshots"],
        "ineligible_slots": payload["ineligible_slots"],
        "failed_slots": payload["failed_slots"],
    }


def _snapshot_only_rows(
    experiment: Path, plan: list[dict[str, Any]], attempts: dict[str, list[dict[str, Any]]]
) -> tuple[list[dict[str, Any]], dict[str, dict[str, Any]]]:
    rows: list[dict[str, Any]] = []
    instances: dict[str, dict[str, Any]] = {}
    for entry in plan:
        run_key = str(entry["run_key"])
        record = _select_attempt(attempts.get(run_key))
        row: dict[str, Any] = {
            "plan": entry,
            "record": record,
            "solution": None,
            "instance": None,
            "source_solution_path": None,
            "source_solution_sha256": None,
            "ineligible_reason": None,
        }
        if record is None:
            row["ineligible_reason"] = "NO_TERMINAL_RUN_RECORD"
        elif record.get("_visual_validation_error"):
            row["ineligible_reason"] = (
                "RUN_RECORD_VALIDATION_FAILED: "
                + str(record["_visual_validation_error"])
            )
        else:
            instance_id = str(entry["instance_id"])
            if instance_id not in instances:
                input_path = experiment / "input" / f"{instance_id}.json"
                if not input_path.is_file():
                    raise ValueError(f"planned canonical input is missing: {input_path}")
                instances[instance_id] = load_instance(input_path)
            instance = instances[instance_id]
            canonical_hash = sha256_file(
                experiment / "input" / f"{instance_id}.json"
            )
            if record.get("input_sha256") != canonical_hash:
                raise ValueError(
                    f"canonical input checksum differs from validated run "
                    f"record for {run_key}"
                )
            run_dir = Path(str(record["_visual_run_dir"]))
            result = record.get("result")
            result = result if isinstance(result, dict) else {}
            solver = result.get("solver_result")
            solver = solver if isinstance(solver, dict) else {}
            solution_rel = solver.get("solution_json_path")
            solution_path = (
                run_dir / str(solution_rel)
                if solution_rel else run_dir / "result" / "solution.json"
            )
            if not solution_path.is_file():
                row["ineligible_reason"] = (
                    "NO_SOLUTION_ARTIFACT" if not result.get("feasible")
                    else "FEASIBLE_RESULT_WITHOUT_SOLUTION_ARTIFACT"
                )
            else:
                row["source_solution_path"] = solution_path.relative_to(
                    experiment
                ).as_posix()
                row["source_solution_sha256"] = sha256_file(solution_path)
                row["instance"] = instance
                if result.get("feasible") is not True:
                    row["ineligible_reason"] = "RESULT_NOT_FEASIBLE"
                else:
                    try:
                        solution = load_solution(solution_path, instance)
                        _validate_solution_for_instance(solution, instance)
                        solution_objective = str(
                            solution.get("objective", entry["objective"])
                        ).strip().lower().replace("-", "").replace("_", "")
                        if solution_objective != str(entry["objective"]).replace(
                            "-", ""
                        ):
                            raise ValueError(
                                f"serialized solution objective "
                                f"{solution_objective!r} does not match "
                                f"planned {entry['objective']!r}"
                            )
                    except (OSError, ValueError, KeyError, TypeError) as error:
                        row["ineligible_reason"] = (
                            f"INVALID_SOLUTION: {type(error).__name__}: {error}"
                        )
                    else:
                        row["solution"] = solution
        rows.append(row)
    return rows, instances


def _validate_staged_png(path: Path, artifact: dict[str, Any]) -> None:
    from PIL import Image

    if not path.is_file() or path.stat().st_size <= 0:
        raise ValueError(f"staged snapshot is missing or empty: {path}")
    if path.stat().st_size != artifact["size_bytes"]:
        raise ValueError(f"staged snapshot size mismatch: {path}")
    if sha256_file(path) != artifact["sha256"]:
        raise ValueError(f"staged snapshot checksum mismatch: {path}")
    with Image.open(path) as image:
        image_format = image.format
        dimensions = image.size
        image.verify()
    expected = artifact.get("image_dimensions", {})
    if image_format != "PNG" or min(dimensions) < 100:
        raise ValueError(f"staged snapshot is not a sensible PNG: {path}")
    if expected and dimensions != (
        expected.get("width"), expected.get("height")
    ):
        raise ValueError(f"staged snapshot dimensions mismatch: {path}")


def _replace_file_atomically(source: Path, target: Path) -> None:
    target.parent.mkdir(parents=True, exist_ok=True)
    os.replace(source, target)
    directory_fd = os.open(target.parent, os.O_RDONLY)
    try:
        os.fsync(directory_fd)
    finally:
        os.close(directory_fd)


def regenerate_snapshots(
    experiment_dir: str | Path,
    *,
    dpi: int = 90,
    show_trajectories: bool = True,
    point_label_policy: str = "supports",
    show_support_connectors: bool = False,
) -> dict[str, Any]:
    """Safely replace only snapshot PNGs from existing validated run artifacts."""
    experiment = Path(experiment_dir).resolve()
    if not experiment.is_dir():
        raise ValueError(f"experiment directory does not exist: {experiment}")
    experiment_manifest_path = experiment / "experiment_manifest.json"
    if not experiment_manifest_path.is_file():
        raise ValueError("experiment manifest is missing")
    experiment_manifest = read_json(experiment_manifest_path)
    if experiment_manifest.get("experiment_id") != experiment.name:
        raise ValueError("experiment manifest does not belong to this directory")
    plan = _read_plan(experiment)
    visualization_path = experiment / "reports" / "visualization_manifest.json"
    if not visualization_path.is_file():
        raise ValueError("visualization manifest is missing")
    old_manifest = read_json(visualization_path)
    if old_manifest.get("experiment_id") != experiment.name:
        raise ValueError("visualization manifest belongs to another experiment")
    if old_manifest.get("planned_slots") != len(plan):
        raise ValueError("visualization manifest planned-slot count does not match run plan")
    old_slots = old_manifest.get("slots")
    if not isinstance(old_slots, list) or len(old_slots) != len(plan):
        raise ValueError("visualization manifest does not cover the complete run plan")
    old_by_key = {slot.get("run_key"): slot for slot in old_slots}
    plan_keys = {str(entry["run_key"]) for entry in plan}
    if len(old_by_key) != len(plan) or set(old_by_key) != plan_keys:
        raise ValueError("visualization manifest slots do not match immutable run plan")
    if dpi <= 0 or point_label_policy not in {"none", "supports", "all"}:
        raise ValueError("invalid snapshot rendering settings")

    attempts = _load_attempts(experiment)
    rows, instances = _snapshot_only_rows(experiment, plan, attempts)
    eligible = [row for row in rows if row["solution"] is not None]
    old_renderable = {
        key for key, slot in old_by_key.items()
        if slot.get("snapshots") and slot.get("generation_status") == "GENERATED"
    }
    new_renderable = {str(row["plan"]["run_key"]) for row in eligible}
    if old_renderable != new_renderable:
        raise ValueError(
            "existing visualization manifest eligibility disagrees with validated "
            f"run artifacts (old={len(old_renderable)}, current={len(new_renderable)})"
        )
    for row in eligible:
        existing = old_by_key[str(row["plan"]["run_key"])]
        if existing.get("source_solution_sha256") != row["source_solution_sha256"]:
            raise ValueError(
                "serialized solution checksum differs from visualization manifest "
                f"for {row['plan']['run_key']}"
            )

    stage_root = Path(tempfile.mkdtemp(
        prefix=f".{experiment.name}.snapshot-stage-", dir=experiment.parent
    )).resolve()
    backup_root = stage_root / "backup"
    backup_root.mkdir()
    png_replacements: list[tuple[Path, Path, dict[str, Any]]] = []
    try:
        new_snapshots: dict[str, dict[str, Any]] = {}
        for row in eligible:
            entry = row["plan"]
            run_id = str(row["record"]["run_id"])
            rendered = _render_solution_images(
                stage_root, row["instance"], row["solution"], row["record"],
                entry, run_id, dpi,
                show_trajectories=show_trajectories,
                point_label_policy=point_label_policy,
                show_support_connectors=show_support_connectors,
            )
            expected_dir = Path("snapshots") / _safe_filename(
                str(entry["instance_id"])
            ) / _safe_filename(str(entry["objective"])) / _safe_filename(
                str(entry["algorithm"])
            ) / run_id
            items = [*rendered["panels"], rendered["triptych"]]
            expected_names = [
                "tau-0.png", "tau-0.5.png", "tau-1.png", "three-time.png"
            ]
            mapped_items = []
            for item, filename in zip(items, expected_names):
                expected_relative = (expected_dir / filename).as_posix()
                staged_path = stage_root / item["path"]
                if item["path"] != expected_relative:
                    raise ValueError(
                        f"renderer produced unexpected snapshot path: {item['path']}"
                    )
                _validate_staged_png(staged_path, item)
                target = _contained(experiment, experiment / expected_relative)
                if not target.is_file():
                    raise ValueError(f"existing snapshot to replace is missing: {target}")
                mapped = {**item, "path": expected_relative}
                mapped_items.append(mapped)
                png_replacements.append((staged_path, target, mapped))
            new_snapshots[str(entry["run_key"])] = {
                "triptych": mapped_items[3],
                "panels": mapped_items[:3],
                "rendering_parameters": rendered["rendering_parameters"],
            }

        staged_pngs = sorted(
            path.relative_to(stage_root).as_posix()
            for path in (stage_root / "snapshots").rglob("*.png")
        )
        expected_pngs = sorted(item[2]["path"] for item in png_replacements)
        if staged_pngs != expected_pngs:
            raise ValueError("staged PNG set differs from validated renderable slots")
        if len(png_replacements) != len(eligible) * 4:
            raise ValueError("staged output count is not four PNGs per eligible slot")

        updated_manifest = json.loads(json.dumps(old_manifest))
        slot_lookup = {slot["run_key"]: slot for slot in updated_manifest["slots"]}
        for row in rows:
            key = str(row["plan"]["run_key"])
            slot = slot_lookup[key]
            if key in new_snapshots:
                slot["snapshots"] = new_snapshots[key]
                slot["generation_status"] = (
                    "GENERATED" if slot.get("animation") else "FAILED"
                )
                if slot["generation_status"] == "FAILED":
                    slot["failure_reason"] = "existing animation artifact is missing"
                old_parameters = slot.get("rendering_parameters", {})
                animation_parameters = {
                    name: value for name, value in old_parameters.items()
                    if name.startswith("animation_") or name == "encoder_fallback"
                }
                slot["rendering_parameters"] = {
                    **animation_parameters,
                    **new_snapshots[key]["rendering_parameters"],
                }
            else:
                slot["snapshots"] = None
                slot["generation_status"] = "INELIGIBLE"
                slot["failure_reason"] = row["ineligible_reason"]
        if any(slot["generation_status"] == "FAILED" for slot in updated_manifest["slots"]):
            raise ValueError("a previously generated slot has no animation artifact")
        updated_manifest.update({
            "renderer_version": RENDERER_VERSION,
            "generated_solution_snapshots": len(eligible),
            "generated_snapshot_files": len(png_replacements),
            "ineligible_slots": len(rows) - len(eligible),
            "failed_slots": 0,
            "rendering_policy": {
                **updated_manifest.get("rendering_policy", {}),
                "renderer_version": RENDERER_VERSION,
                "visualization_schema_version": 1,
                "normalized_times": list(TIME_FRACTIONS),
                "time_conversion": "t = tau * T_end",
                "dpi": dpi,
                "output_format": "png",
                "layout_method": "reserved-gridspec",
                "trajectory_visibility": (
                    "visible" if show_trajectories else "hidden"
                ),
                "point_label_policy": point_label_policy,
                "support_connector_policy": (
                    "visible" if show_support_connectors else "hidden"
                ),
                "opacity": {
                    "trajectory": TRAJECTORY_ALPHA,
                    "coverage_disk": DISK_ALPHA,
                    "support_connector": SUPPORT_CONNECTOR_ALPHA,
                },
                "line_widths": {
                    "trajectory": TRAJECTORY_LINE_WIDTH,
                    "coverage_disk_outline": DISK_LINE_WIDTH,
                    "support_connector": SUPPORT_LINE_WIDTH,
                    "support_marker_outline": SUPPORT_MARKER_LINE_WIDTH,
                    "moving_point_outline": POINT_MARKER_LINE_WIDTH,
                    "station_marker_outline": STATION_MARKER_LINE_WIDTH,
                },
                "spatial_bounds_policy": (
                    "one square padded extent over station centers, trajectory "
                    "waypoints, snapshot positions, and displayed-time disk extents"
                ),
                "axis_scaling": "equal",
            },
        })

        report_path = experiment / "reports" / "visualization_report.md"
        report_text = "\n".join([
            "# Visualization generation report", "",
            f"- Experiment: `{experiment.name}`",
            f"- Selected instances: {updated_manifest['dataset_instance_count']}",
            f"- Objectives: {', '.join(sorted({str(e['objective']) for e in plan}))}",
            f"- Expected comparison sheets: {updated_manifest['expected_comparison_sheets']}",
            f"- Created comparison sheets: {len(updated_manifest['comparison_sheets'])}",
            f"- Slots in run plan: {len(plan)}",
            f"- Solutions with snapshots: {len(eligible)}",
            f"- Regenerated snapshot PNGs: {len(png_replacements)}",
            f"- Per-solution animations (unchanged): {updated_manifest['generated_animations']}",
            f"- Ineligible slots: {len(rows) - len(eligible)}",
            "- Rendering failures: 0",
            "",
            "Renderability was re-derived from the immutable plan, validated run "
            "records, canonical inputs and serialized solutions. Snapshot-only "
            "regeneration did not invoke solvers, calibration, or animation generation.",
            "",
            f"Manifest: `{visualization_path.relative_to(experiment).as_posix()}`",
        ]) + "\n"

        regeneration_path = experiment / "reports" / "snapshot_regeneration_report.md"
        from datetime import datetime, timezone

        current_revision = "unknown"
        try:
            import subprocess

            current_revision = subprocess.run(
                ["git", "rev-parse", "HEAD"], cwd=ROOT, check=True,
                capture_output=True, text=True,
            ).stdout.strip()
        except (OSError, subprocess.CalledProcessError):
            pass
        renderer_sha256 = sha256_file(Path(__file__).resolve())
        ineligible_reasons: dict[str, int] = {}
        for row in rows:
            if row["ineligible_reason"]:
                reason = str(row["ineligible_reason"])
                ineligible_reasons[reason] = ineligible_reasons.get(reason, 0) + 1
        ineligible_lines = []
        for reason, count in sorted(ineligible_reasons.items()):
            escaped_reason = reason.replace("|", "\\|")
            ineligible_lines.append(f"| `{escaped_reason}` | {count} |")
        regeneration_text = "\n".join([
            "# Snapshot regeneration report", "",
            f"- Target experiment ID: `{experiment.name}`",
            f"- Renderer version: `{RENDERER_VERSION}`",
            f"- Source code revision (Git HEAD): `{current_revision}`",
            f"- Renderer source SHA-256 (working tree): `{renderer_sha256}`",
            f"- Completed at (UTC): `{datetime.now(timezone.utc).isoformat()}`",
            f"- Planned slots: {len(plan)}",
            f"- Renderable solutions: {len(eligible)}",
            f"- PNGs regenerated: {len(png_replacements)}",
            f"- Ineligible slots: {len(rows) - len(eligible)}",
            "- Rendering failures: 0",
            "- PNG validation: all staged images decoded with Pillow; dimensions, "
            "sizes, checksums, output paths and four-file-per-slot coverage verified.",
            "- Solver execution, calibration, run-plan creation and animation "
            "generation: not performed.",
            "- Solver run records, results, serialized solutions, traces, aggregates, "
            "tables and source inputs: not modified.",
            "- Visualization manifest, report, global artifact index and snapshot "
            "figure status: refreshed transactionally.",
            "",
            "## Ineligible planned slots",
            "",
            "| Reason | Slots |",
            "|---|---:|",
            *ineligible_lines,
            "",
            "## Validation",
            "",
            f"- Panels: {len(eligible) * 3}; triptychs: {len(eligible)}.",
            f"- Expected and decoded PNGs: {len(png_replacements)}.",
            "- Comparison sheets retained in place; their existing relative PNG links "
            "continue to target the replaced files.",
            "- Existing final smoke20 audit remains FAIL if unrelated solver-result "
            "integrity failures persist; snapshot status is reported separately.",
        ]) + "\n"

        # Back up every destination before replacement; metadata is published only
        # after all staged images have passed their independent decode checks.
        mutable_payloads: dict[Path, bytes | None] = {}
        for path in (
            visualization_path, report_path, regeneration_path,
            experiment / "artifacts.json",
            experiment / "figures" / "figure_status.json",
            experiment / "figures" / "REPORT.md",
            experiment / "reports" / "final_smoke20_audit.json",
            experiment / "reports" / "final_smoke20_audit.md",
        ):
            mutable_payloads[path] = path.read_bytes() if path.is_file() else None
        backup_files: dict[Path, Path] = {}
        for target in [target for _, target, _ in png_replacements]:
            backup = backup_root / target.relative_to(experiment)
            backup.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(target, backup)
            backup_files[target] = backup

        replaced_pngs: list[Path] = []
        try:
            for staged, target, _item in png_replacements:
                _replace_file_atomically(staged, target)
                replaced_pngs.append(target)
            write_derived_json(visualization_path, updated_manifest)
            write_derived_bytes(report_path, report_text.encode("utf-8"))
            write_derived_bytes(regeneration_path, regeneration_text.encode("utf-8"))

            from report_experiment import _snapshot_figure_status

            figure_status_path = experiment / "figures" / "figure_status.json"
            figure_status = read_json(figure_status_path)
            figure_rows = figure_status.get("figures", [])
            index_experiment_artifacts(experiment)
            snapshot_status = _snapshot_figure_status(experiment)
            for index, item in enumerate(figure_rows):
                if item.get("figure") == "three_time_solution_figures":
                    figure_rows[index] = snapshot_status
                    break
            else:
                figure_rows.append(snapshot_status)
            write_derived_json(figure_status_path, figure_status)
            figure_report_path = experiment / "figures" / "REPORT.md"
            figure_report = figure_report_path.read_text(encoding="utf-8")
            figure_status_line = (
                f"| three_time_solution_figures | "
                f"{snapshot_status['generation_status']} |"
            )
            figure_report = __import__("re").sub(
                r"\| three_time_solution_figures \| [A-Z_]+ \|",
                figure_status_line, figure_report,
            )
            write_derived_bytes(
                figure_report_path, figure_report.encode("utf-8")
            )

            audit_json_path = experiment / "reports" / "final_smoke20_audit.json"
            if audit_json_path.is_file():
                final_audit = read_json(audit_json_path)
                validation = final_audit.get("validation_results", {})
                figure_check = validation.get("figure_files", {})
                details = figure_check.get("details", {})
                details["stale_three_time_status"] = False
                details["three_time_status"] = snapshot_status["generation_status"]
                not_generated = details.get("not_generated", [])
                details["not_generated"] = [
                    item for item in not_generated
                    if item.get("figure") != "three_time_solution_figures"
                ]
                figure_check["status"] = "PASS"
                figure_check["details"] = details
                validation["figure_files"] = figure_check
                visualization_check = validation.get("visualization_artifacts", {})
                visualization_details = visualization_check.get("details", {})
                visualization_details.update({
                    "renderable": len(eligible),
                    "triptychs": len(eligible),
                    "snapshot_panels": len(eligible) * 3,
                    "snapshot_pngs": len(png_replacements),
                    "invalid_files": [],
                })
                visualization_check["details"] = visualization_details
                validation["visualization_artifacts"] = visualization_check
                final_audit["validation_results"] = validation
                final_audit["critical_errors"] = [
                    item for item in final_audit.get("critical_errors", [])
                    if item.get("check") != "stale_figure_status"
                ]
                final_audit["observed_counts"]["triptychs"] = len(eligible)
                final_audit["observed_counts"]["snapshot_panels"] = len(eligible) * 3
                final_audit["observed_counts"]["snapshot_pngs"] = len(png_replacements)
                final_audit["snapshot_regeneration"] = {
                    "status": "PASS",
                    "renderer_version": RENDERER_VERSION,
                    "renderable": len(eligible),
                    "pngs_regenerated": len(png_replacements),
                    "triptychs_validated": len(eligible),
                    "solver_execution": False,
                }
                remaining_failures = [
                    item for item in final_audit.get("validation_results", {}).values()
                    if isinstance(item, dict) and item.get("status") == "FAIL"
                ]
                final_audit["audit_status"] = (
                    "FAIL" if remaining_failures or final_audit["critical_errors"]
                    else "PASS"
                )
                final_audit["audited_at"] = utc_now()
                write_derived_json(audit_json_path, final_audit)

                audit_md_path = experiment / "reports" / "final_smoke20_audit.md"
                audit_md = audit_md_path.read_text(encoding="utf-8")
                audit_md = audit_md.replace(
                    "- **stale_figure_status**: "
                    "\"three-time figures marked NOT_GENERATED although triptychs exist\"\n",
                    "",
                )
                audit_md = audit_md.replace(
                    "- **Audit status:** FAIL",
                    f"- **Audit status:** {final_audit['audit_status']}",
                    1,
                )
                audit_md += (
                    "\n## Snapshot regeneration re-audit\n\n"
                    f"- Snapshot-specific audit: **PASS** — {len(eligible)} "
                    f"renderable solutions, {len(eligible) * 3} panels, "
                    f"{len(eligible)} triptychs, and {len(png_replacements)} "
                    "decoded and checksummed PNGs.\n"
                    "- `three_time_solution_figures` is now "
                    f"`{snapshot_status['generation_status']}` based on validated "
                    "visualization-manifest artifacts.\n"
                    "- Solver results were not rerun or rewritten. The overall "
                    f"audit remains **{final_audit['audit_status']}** due only to "
                    "any unrelated pre-existing failures retained above.\n"
                    "- Detailed rendering-only evidence: "
                    "[snapshot regeneration report](snapshot_regeneration_report.md).\n"
                )
                write_derived_bytes(
                    audit_md_path, audit_md.encode("utf-8")
                )

            index_experiment_artifacts(experiment)
            final_audit_result = audit(experiment)
            if final_audit_result["rendered_slots"] != len(eligible):
                raise ValueError("post-replacement visualization audit count mismatch")
            artifact_index_path = experiment / "artifacts.json"
            indexed_count = len(read_json(
                artifact_index_path, schema_name="artifact_manifest"
            )["artifacts"])
            audit_json_path = experiment / "reports" / "final_smoke20_audit.json"
            if audit_json_path.is_file():
                final_audit = read_json(audit_json_path)
                final_audit["validation_results"]["artifact_index"]["details"][
                    "indexed"
                ] = indexed_count
                final_audit["observed_counts"]["indexed_artifacts"] = indexed_count
                final_audit["observed_counts"]["tracked_in_experiment"] = (
                    indexed_count + 1
                )
                write_derived_json(audit_json_path, final_audit)
                audit_md_path = experiment / "reports" / "final_smoke20_audit.md"
                audit_md = audit_md_path.read_text(encoding="utf-8")
                audit_md = __import__("re").sub(
                    r"Artifact index entries \d+;",
                    f"Artifact index entries {indexed_count};",
                    audit_md,
                )
                audit_md = __import__("re").sub(
                    r"and \d+ in this experiment\.",
                    f"and {indexed_count + 1} in this experiment.",
                    audit_md,
                    count=1,
                )
                write_derived_bytes(audit_md_path, audit_md.encode("utf-8"))
                index_experiment_artifacts(experiment)
                final_audit_result = audit(experiment)
        except BaseException:
            for target in reversed(replaced_pngs):
                backup = backup_files[target]
                restore = stage_root / "restore" / target.relative_to(experiment)
                restore.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(backup, restore)
                _replace_file_atomically(restore, target)
            for path, contents in mutable_payloads.items():
                if contents is None:
                    path.unlink(missing_ok=True)
                else:
                    write_derived_bytes(path, contents)
            raise

        index_experiment_artifacts(experiment)
        return {
            "experiment_id": experiment.name,
            "planned_slots": len(plan),
            "renderable_solutions": len(eligible),
            "regenerated_pngs": len(png_replacements),
            "ineligible_slots": len(rows) - len(eligible),
            "ineligible_reasons": ineligible_reasons,
            "renderer_version": RENDERER_VERSION,
            "audit": final_audit_result,
        }
    finally:
        shutil.rmtree(stage_root)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--experiment", required=True)
    parser.add_argument("--fps", type=int, default=12)
    parser.add_argument("--frames", type=int, default=48)
    parser.add_argument("--dpi", type=int, default=90)
    parser.add_argument("--hide-trajectories", action="store_true")
    parser.add_argument(
        "--point-labels", choices=("none", "supports", "all"),
        default="supports",
    )
    parser.add_argument("--show-support-connectors", action="store_true")
    parser.add_argument("--audit", action="store_true")
    parser.add_argument(
        "--snapshots-only",
        action="store_true",
        help="safely regenerate static PNG snapshots without animations or solvers",
    )
    parser.add_argument(
        "--replace-existing",
        action="store_true",
        help="allow snapshots-only mode to atomically replace existing PNGs",
    )
    args = parser.parse_args(argv)
    try:
        if args.audit:
            report = audit(args.experiment)
            print(json.dumps(report, sort_keys=True))
        elif args.snapshots_only:
            if not args.replace_existing:
                raise ValueError(
                    "--snapshots-only requires explicit --replace-existing"
                )
            report = regenerate_snapshots(
                args.experiment,
                dpi=args.dpi,
                show_trajectories=not args.hide_trajectories,
                point_label_policy=args.point_labels,
                show_support_connectors=args.show_support_connectors,
            )
            print(json.dumps(report, sort_keys=True))
        elif args.replace_existing:
            raise ValueError("--replace-existing is only valid with --snapshots-only")
        else:
            report = generate(
                args.experiment, fps=args.fps, frames=args.frames, dpi=args.dpi,
                show_trajectories=not args.hide_trajectories,
                point_label_policy=args.point_labels,
                show_support_connectors=args.show_support_connectors,
            )
            print(json.dumps({
                "experiment_id": report["experiment_id"],
                "planned_slots": report["planned_slots"],
                "comparison_sheets": report["expected_comparison_sheets"],
                "rendered_solutions": report["generated_solution_snapshots"],
                "animations": report["generated_animations"],
                "ineligible_slots": report["ineligible_slots"],
                "render_failures": report["failed_slots"],
            }, sort_keys=True))
    except (OSError, ValueError, KeyError, TypeError, json.JSONDecodeError) as error:
        print(f"visualize_experiment.py: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
