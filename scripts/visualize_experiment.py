#!/usr/bin/env python3
"""Create experiment-scoped snapshots, comparison sheets, and animations."""

from __future__ import annotations

import argparse
import html
import json
import math
import os
from pathlib import Path
import sys
from typing import Any

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
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
    validate_run_directory,
    write_bytes_create_only,
    write_json_create_only,
)

TIME_FRACTIONS = (0.0, 0.5, 1.0)
OBJECTIVES = ("minmax", "minsum", "minmaxsum")
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
        peak = solver.get("peak_cost")
        integral = solver.get("integral_cost")
        if peak is None or integral is None:
            vector = solver.get("objective_vector")
            if isinstance(vector, dict):
                peak = vector.get("peak_cost", peak)
                integral = vector.get("integral_cost", integral)
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


def _draw_panel(
    axis: Any,
    instance: dict[str, Any],
    solution: dict[str, Any],
    record: dict[str, Any] | None,
    plan: dict[str, Any],
    tau: float,
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
    xs = [float(station["x"]) for station in stations]
    ys = [float(station["y"]) for station in stations]
    for trajectory in trajectories:
        for waypoint in trajectory["waypoints"]:
            xs.append(float(waypoint["x"]))
            ys.append(float(waypoint["y"]))
    radii = [
        radius_at(solution, station_index, time, instance)
        for station_index in range(len(stations))
    ]
    pad = max(
        max(xs, default=0.0) - min(xs, default=0.0),
        max(ys, default=0.0) - min(ys, default=0.0),
        max(radii, default=0.0),
        1.0,
    ) * 0.12
    xmin = min([x - r for x, r in zip(
        [float(station["x"]) for station in stations], radii
    )] + xs + [0.0]) - pad
    xmax = max([x + r for x, r in zip(
        [float(station["x"]) for station in stations], radii
    )] + xs + [1.0]) + pad
    ymin = min([y - r for y, r in zip(
        [float(station["y"]) for station in stations], radii
    )] + ys + [0.0]) - pad
    ymax = max([y + r for y, r in zip(
        [float(station["y"]) for station in stations], radii
    )] + ys + [1.0]) + pad
    axis.set_xlim(xmin, xmax)
    axis.set_ylim(ymin, ymax)
    axis.set_aspect("equal", adjustable="box")
    axis.grid(True, linestyle=":", alpha=0.35)
    axis.set_xlabel("x")
    axis.set_ylabel("y")
    axis.set_title(f"τ = {tau:g}  |  t = {time:.6g}")

    for point_index, trajectory in enumerate(trajectories):
        wx = [float(point["x"]) for point in trajectory["waypoints"]]
        wy = [float(point["y"]) for point in trajectory["waypoints"]]
        axis.plot(wx, wy, "--", color="#666666", alpha=0.55, linewidth=1,
                  label="Trajectory" if point_index == 0 else "_nolegend_")
        if len(wx) >= 2:
            axis.annotate(
                "", xy=(wx[-1], wy[-1]), xytext=(wx[-2], wy[-2]),
                arrowprops={"arrowstyle": "->", "color": "#666666",
                            "lw": 0.8, "alpha": 0.7},
            )
    for station_index, station in enumerate(stations):
        color = STATION_COLORS(station_index % 10)
        sx, sy = float(station["x"]), float(station["y"])
        axis.add_patch(Circle(
            (sx, sy), radii[station_index], facecolor=color,
            edgecolor=color, alpha=0.14, linewidth=1.2,
            label="Coverage disk" if station_index == 0 else "_nolegend_",
        ))
        axis.scatter(
            [sx], [sy], marker="^", s=65, color=[color], edgecolor="black",
            linewidth=0.4, zorder=4,
            label="Station" if station_index == 0 else "_nolegend_",
        )
    for point_index, (px, py) in enumerate(positions):
        owner = int(owners[point_index])
        color = STATION_COLORS(owner % 10)
        axis.scatter(
            [px], [py], marker="o", s=38, color=[color], edgecolor="black",
            linewidth=0.35, zorder=5,
            label="Moving point (color=assigned station)"
            if point_index == 0 else "_nolegend_",
        )
        axis.annotate(f"P{point_index}/S{owner}", (px, py), xytext=(4, 4),
                      textcoords="offset points", fontsize=6)
    for station_index, support in enumerate(supports):
        support = int(support)
        if support < 0:
            continue
        px, py = positions[support]
        station = stations[station_index]
        color = STATION_COLORS(station_index % 10)
        axis.plot(
            [float(station["x"]), px], [float(station["y"]), py],
            color=color, linewidth=1.0, alpha=0.8,
            label="Active support relationship" if station_index == 0
            else "_nolegend_",
        )
        axis.scatter([px], [py], marker="o", s=80, facecolors="none",
                     edgecolors=[color], linewidth=1.5, zorder=6)
    solver, verification = _result_metadata(record)
    instance_name = str(instance.get("name") or plan["instance_id"])
    run_id = str(record.get("run_id", "not-created")) if record else "not-created"
    status = _status_label(record)
    label = (
        f"{instance_name} | {plan['algorithm']} | {plan['objective']}\n"
        f"run={run_id} | {status}\n"
        f"T_end={t_end:g}, t={time:g} | {_objective_label(plan['objective'], solver)}"
    )
    axis.text(
        0.01, 0.99, label, transform=axis.transAxes, va="top", ha="left",
        fontsize=6.5, bbox={"facecolor": "white", "alpha": 0.84,
                           "edgecolor": "#999999"},
    )
    if verification.get("kind") in {None, "", "NONE"}:
        verified_label = "verification=NOT_AVAILABLE"
    else:
        verified_label = f"verification={verification.get('kind')}"
    axis.text(0.99, 0.01, verified_label, transform=axis.transAxes,
              va="bottom", ha="right", fontsize=6.5)
    handles, labels = axis.get_legend_handles_labels()
    if handles:
        unique = dict(zip(labels, handles))
        axis.legend(unique.values(), unique.keys(), loc="lower left",
                    fontsize=6, framealpha=0.85)


def _render_solution_images(
    experiment: Path,
    instance: dict[str, Any],
    solution: dict[str, Any],
    record: dict[str, Any] | None,
    plan: dict[str, Any],
    run_id: str,
    dpi: int,
) -> dict[str, Any]:
    instance_id = _safe_filename(str(plan["instance_id"]))
    algorithm = _safe_filename(str(plan["algorithm"]))
    objective = _safe_filename(str(plan["objective"]))
    directory = experiment / "snapshots" / instance_id / objective / algorithm / run_id
    t_end = float(instance["T_end"])
    render_parameters = {
        "dpi": dpi,
        "layout": "three-panel",
        "normalized_times": list(TIME_FRACTIONS),
        "actual_times": [tau * t_end for tau in TIME_FRACTIONS],
        "T_end": t_end,
        "axis_scaling": "equal",
        "geometry": "piecewise-linear trajectories; support-point distance radii",
    }
    panel_artifacts = []
    for index, tau in enumerate(TIME_FRACTIONS):
        figure, axis = plt.subplots(figsize=(6.0, 5.0))
        _draw_panel(axis, instance, solution, record, plan, tau)
        figure.tight_layout()
        from io import BytesIO

        buffer = BytesIO()
        figure.savefig(buffer, format="png", dpi=dpi, bbox_inches="tight")
        plt.close(figure)
        path = directory / f"tau-{tau:g}.png"
        _write_bytes(experiment, path, buffer.getvalue())
        panel_artifacts.append(_artifact(experiment, path, "snapshot-panel"))

    figure, axes = plt.subplots(1, 3, figsize=(18.0, 5.8), squeeze=False)
    for axis, tau in zip(axes[0], TIME_FRACTIONS):
        _draw_panel(axis, instance, solution, record, plan, tau)
    figure.suptitle(
        f"{instance.get('name', plan['instance_id'])} | {plan['algorithm']} | "
        f"{plan['objective']} | run {run_id} | {_status_label(record)}",
        fontsize=10,
    )
    figure.tight_layout(rect=(0, 0, 1, 0.90))
    from io import BytesIO

    buffer = BytesIO()
    figure.savefig(buffer, format="png", dpi=dpi, bbox_inches="tight")
    plt.close(figure)
    triptych = directory / "three-time.png"
    _write_bytes(experiment, triptych, buffer.getvalue())
    return {
        "triptych": _artifact(experiment, triptych, "three-panel-snapshot"),
        "panels": panel_artifacts,
        "rendering_parameters": render_parameters,
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
) -> dict[str, Any]:
    experiment = Path(experiment_dir).resolve()
    if not experiment.is_dir():
        raise ValueError(f"experiment directory does not exist: {experiment}")
    if fps <= 0 or frames < 2 or dpi <= 0:
        raise ValueError("fps/dpi must be positive and frames must be at least two")
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
                                    rendered_record, entry, run_id, dpi,
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
                path = _contained(experiment, experiment / item["path"])
                if (not path.is_file() or path.stat().st_size != item["size_bytes"]
                        or sha256_file(path) != item["sha256"]):
                    missing.append(item["path"])
        if slot.get("animation"):
            item = slot["animation"]
            path = _contained(experiment, experiment / item["path"])
            if (not path.is_file() or path.stat().st_size != item["size_bytes"]
                    or sha256_file(path) != item["sha256"]):
                missing.append(item["path"])
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


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--experiment", required=True)
    parser.add_argument("--fps", type=int, default=12)
    parser.add_argument("--frames", type=int, default=48)
    parser.add_argument("--dpi", type=int, default=90)
    parser.add_argument("--audit", action="store_true")
    args = parser.parse_args(argv)
    try:
        if args.audit:
            report = audit(args.experiment)
            print(json.dumps(report, sort_keys=True))
        else:
            report = generate(
                args.experiment, fps=args.fps, frames=args.frames, dpi=args.dpi
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
