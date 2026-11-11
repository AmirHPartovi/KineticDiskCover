#!/usr/bin/env python3
"""Create experiment-scoped snapshots, comparison sheets, and animations."""

from __future__ import annotations

import argparse
import html
import json
import math
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import textwrap
from typing import Any
import zlib

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
PUBLICATION_RENDERER_VERSION = 2
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
PUBLICATION_DPI = 600
PUBLICATION_PANEL_SIZE = (3.4, 3.4)
PUBLICATION_TRIPTYCH_SIZE = (7.1, 3.15)
PUBLICATION_TIMES = (
    ("tau-0", 0.0, "t = 0"),
    ("tau-0.5", 0.5, "t = T/2"),
    ("tau-1", 1.0, "t = T"),
)
PUBLICATION_TRAJECTORY_COLOR = "#737b83"
PUBLICATION_TRAJECTORY_ALPHA = 0.38
PUBLICATION_TRAJECTORY_LINE_WIDTH = 0.55
PUBLICATION_DISK_ALPHA = 0.14
PUBLICATION_DISK_LINE_WIDTH = 0.65
PUBLICATION_MARKER_EDGE_COLOR = "#20252a"
PUBLICATION_MARKER_EDGE_WIDTH = 0.45


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


def _pdf_streams_and_metadata(
    contents: bytes, *, path: Path | None = None
) -> tuple[list[bytes], bytes]:
    location = f": {path}" if path is not None else ""
    streams: list[bytes] = []
    metadata = bytearray(contents)
    cursor = 0
    while True:
        match = re.search(rb"\bstream\r?\n", contents[cursor:])
        if match is None:
            break
        token_start = cursor + match.start()
        stream_start = cursor + match.end()
        dictionary_start = contents.rfind(b"<<", 0, token_start)
        dictionary_end = contents.rfind(b">>", dictionary_start, token_start)
        if dictionary_start < 0 or dictionary_end < dictionary_start:
            raise ValueError(f"publication PDF has an invalid stream dictionary{location}")
        dictionary = contents[dictionary_start:dictionary_end + 2]
        length_match = re.search(rb"/Length\b\s+(\d+)\b", dictionary)
        if length_match is None:
            raise ValueError(
                f"publication PDF stream has no byte length{location}"
            )
        length = int(length_match.group(1))
        reference_match = re.match(
            rb"/Length\b\s+\d+\s+\d+\s+R\b",
            dictionary[length_match.start():],
        )
        if reference_match is not None:
            object_match = re.search(
                rb"(?m)(?:^|\n)"
                + re.escape(length_match.group(1))
                + rb"\s+\d+\s+obj\s*\r?\n\s*(\d+)\s*\r?\nendobj\b",
                contents,
            )
            if object_match is None:
                raise ValueError(
                    f"publication PDF stream length reference is invalid{location}"
                )
            length = int(object_match.group(1))
        stream_end = stream_start + length
        terminator = re.match(rb"\r?\nendstream\b", contents[stream_end:])
        if terminator is None:
            raise ValueError(f"publication PDF stream length is invalid{location}")
        stream = contents[stream_start:stream_end]
        if b"/FlateDecode" in dictionary:
            try:
                stream = zlib.decompress(stream)
            except zlib.error as error:
                raise ValueError(
                    f"publication PDF has an invalid compressed stream{location}"
                ) from error
        streams.append(stream)
        metadata[stream_start:stream_end] = b" " * length
        cursor = stream_end + terminator.end()
    return streams, bytes(metadata)


def _validate_pdf_bytes(contents: bytes, *, path: Path | None = None) -> None:
    location = f": {path}" if path is not None else ""
    if (
        len(contents) < 500
        or not contents.startswith(b"%PDF-")
        or b"%%EOF" not in contents[-2048:]
    ):
        raise ValueError(f"Matplotlib produced a malformed PDF{location}")
    streams, metadata = _pdf_streams_and_metadata(contents, path=path)
    if re.search(rb"/Subtype\s*/Image\b", metadata):
        raise ValueError(
            f"publication PDF contains raster image content{location}"
        )
    if b"/FontFile2" not in metadata and b"/FontFile3" not in metadata:
        raise ValueError(f"publication PDF has no embedded font{location}")
    pages = re.search(
        rb"/Type\s*/Pages\b[^>]*?/Count\s+(\d+)", metadata, re.DOTALL
    )
    page_objects = re.findall(rb"/Type\s*/Page\b", metadata)
    if pages is None or pages.group(1) != b"1" or len(page_objects) != 1:
        raise ValueError(f"publication PDF must contain exactly one page{location}")
    media_boxes = re.findall(
        rb"/MediaBox\s*\[([^\]]+)\]", metadata
    )
    if len(media_boxes) != 1:
        raise ValueError(f"publication PDF has invalid page dimensions{location}")
    try:
        media_box = [float(value) for value in media_boxes[0].split()]
    except ValueError as error:
        raise ValueError(
            f"publication PDF has invalid page dimensions{location}"
        ) from error
    if len(media_box) != 4 or not all(math.isfinite(value) for value in media_box):
        raise ValueError(f"publication PDF has invalid page dimensions{location}")
    vector_operators = {
        operator: sum(stream.count(operator) for stream in streams)
        for operator in (b" m\n", b" l\n", b" c\n")
    }
    if any(count < 5 for count in vector_operators.values()):
        raise ValueError(
            f"publication PDF has insufficient vector geometry{location}"
        )


def _validate_publication_figure_text(figure: Any) -> None:
    from matplotlib.text import Text

    forbidden = (
        "run_", "experiment_", "feasible=", "verified=", "optimality=",
        "runtime=", "algorithm=", "backend=", "lower_bound",
        "upper_bound", "gap=",
    )
    for artist in figure.findobj(match=Text):
        text = artist.get_text()
        normalized = text.lower()
        if any(value in normalized for value in forbidden) or re.search(
            r"\bP\d+\s*/\s*S\d+\b", text, re.IGNORECASE
        ):
            raise ValueError(
                f"publication figure contains prohibited text: {text!r}"
            )


def _validate_pdf_pages_with_ghostscript(
    paths: list[Path], script_path: Path
) -> None:
    executable = shutil.which("gs")
    if executable is None:
        raise ValueError(
            "Ghostscript is required to validate staged publication PDFs"
        )
    commands = ["%!PS-Adobe-3.0"]
    for path in paths:
        literal = str(path.resolve())
        if any(character in literal for character in ("\n", "\r")):
            raise ValueError(f"PDF path cannot be safely inspected: {path}")
        literal = literal.replace("\\", "\\\\").replace("(", "\\(").replace(")", "\\)")
        commands.append(
            f"({literal}) (r) file runpdfbegin pdfpagecount = runpdfend"
        )
    commands.append("quit")
    script_path.write_text("\n".join(commands) + "\n", encoding="ascii")
    try:
        result = subprocess.run(
            [
                executable, "-q", "-dNOSAFER", "-dNODISPLAY",
                "-f", str(script_path),
            ],
            check=True,
            capture_output=True,
            text=True,
            timeout=3600,
        )
    except (OSError, subprocess.CalledProcessError, subprocess.TimeoutExpired) as error:
        raise ValueError(f"Ghostscript could not validate staged PDFs: {error}") from error
    page_counts = result.stdout.split()
    if len(page_counts) != len(paths) or any(
        page_count != "1" for page_count in page_counts
    ):
        raise ValueError(
            "Ghostscript found a staged PDF with other than exactly one page"
        )


def _station_identity(station: dict[str, Any]) -> str:
    """Return a stable color key independent of station-list iteration order."""
    identity = [
        station.get("id"),
        float(station["x"]),
        float(station["y"]),
    ]
    return json.dumps(identity, sort_keys=True, separators=(",", ":"), default=str)


def _publication_station_colors(
    stations: list[dict[str, Any]],
) -> dict[str, tuple[float, float, float, float]]:
    identities = sorted({_station_identity(station) for station in stations})
    palette = plt.get_cmap("turbo", max(1, len(identities)))
    colors = {
        identity: tuple(float(channel) for channel in palette(index))
        for index, identity in enumerate(identities)
    }
    return {
        _station_identity(station): colors[_station_identity(station)]
        for station in stations
    }


def _publication_legend_handles() -> list[Line2D]:
    return [
        Line2D(
            [], [], color=PUBLICATION_TRAJECTORY_COLOR, linestyle="--",
            linewidth=PUBLICATION_TRAJECTORY_LINE_WIDTH, label="Trajectory",
        ),
        Line2D(
            [], [], marker="o", linestyle="None", markersize=6,
            markerfacecolor="#b5bbc0", markeredgecolor=PUBLICATION_MARKER_EDGE_COLOR,
            label="Coverage disk",
        ),
        Line2D(
            [], [], marker="^", linestyle="None", markersize=5.5,
            markerfacecolor="#727b83", markeredgecolor=PUBLICATION_MARKER_EDGE_COLOR,
            label="Station",
        ),
        Line2D(
            [], [], marker="o", linestyle="None", markersize=4.5,
            markerfacecolor="#727b83", markeredgecolor=PUBLICATION_MARKER_EDGE_COLOR,
            label="Point (owner color)",
        ),
        Line2D(
            [], [], marker="D", linestyle="None", markersize=5,
            markerfacecolor="none", markeredgecolor="#343a40",
            label="Support point",
        ),
    ]


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


def _publication_state(
    instance: dict[str, Any],
    solution: dict[str, Any],
    time: float,
) -> dict[str, Any]:
    interval = _interval_at(solution, time)
    positions = [
        position_at(instance, point_index, time)
        for point_index in range(len(instance["trajectories"]))
    ]
    if any(not all(math.isfinite(value) for value in position)
           for position in positions):
        raise ValueError(f"trajectory position is non-finite at t={time:g}")
    supports = interval.get(
        "supporting_point", interval.get("supporting_points")
    )
    owners = interval.get("assigned_points")
    if not isinstance(supports, list) or len(supports) != len(instance["stations"]):
        raise ValueError(f"solution has invalid supports at t={time:g}")
    if not isinstance(owners, list) or len(owners) != len(instance["trajectories"]):
        raise ValueError(f"solution has invalid assignments at t={time:g}")
    normalized_supports = [int(support) for support in supports]
    normalized_owners = [int(owner) for owner in owners]
    if any(
        support < -1 or support >= len(positions)
        for support in normalized_supports
    ):
        raise ValueError(f"solution has an invalid support index at t={time:g}")
    if any(
        owner < 0 or owner >= len(instance["stations"])
        for owner in normalized_owners
    ):
        raise ValueError(f"solution has an invalid station assignment at t={time:g}")
    radii = [
        float(radius_at(solution, station_index, time, instance))
        for station_index in range(len(instance["stations"]))
    ]
    if any(not math.isfinite(radius) or radius < 0.0 for radius in radii):
        raise ValueError(f"solution has an invalid coverage radius at t={time:g}")
    return {
        "positions": positions,
        "supports": normalized_supports,
        "owners": normalized_owners,
        "radii": radii,
    }


def _draw_publication_panel(
    axis: Any,
    instance: dict[str, Any],
    state: dict[str, Any],
    bounds: dict[str, Any],
    station_colors: dict[str, tuple[float, float, float, float]],
    *,
    panel_label: str,
    show_trajectories: bool,
    show_support_connectors: bool,
) -> None:
    stations = instance["stations"]
    trajectories = instance["trajectories"]
    positions = state["positions"]
    supports = state["supports"]
    owners = state["owners"]
    axis.set_facecolor("white")
    axis.set_xlim(*bounds["xlim"])
    axis.set_ylim(*bounds["ylim"])
    axis.set_aspect("equal", adjustable="box")
    axis.set_xlabel("x", fontsize=8, labelpad=2)
    axis.set_ylabel("y", fontsize=8, labelpad=2)
    axis.set_title(panel_label, fontsize=8.5, pad=4)
    axis.tick_params(axis="both", labelsize=7, length=2.5, width=0.55, pad=2)
    axis.grid(True, linestyle=":", linewidth=0.35, alpha=0.25, color="#77818a")
    axis.set_axisbelow(True)
    for spine in axis.spines.values():
        spine.set_linewidth(0.65)
        spine.set_color("#424a50")

    if show_trajectories:
        for trajectory in trajectories:
            waypoints = trajectory["waypoints"]
            xs = [float(point["x"]) for point in waypoints]
            ys = [float(point["y"]) for point in waypoints]
            if not all(math.isfinite(value) for value in (*xs, *ys)):
                raise ValueError("trajectory contains a non-finite waypoint")
            axis.plot(
                xs, ys, linestyle=(0, (3, 2)), color=PUBLICATION_TRAJECTORY_COLOR,
                alpha=PUBLICATION_TRAJECTORY_ALPHA,
                linewidth=PUBLICATION_TRAJECTORY_LINE_WIDTH, zorder=1,
            )

    for station_index, station in enumerate(stations):
        color = station_colors[_station_identity(station)]
        center = (float(station["x"]), float(station["y"]))
        radius = state["radii"][station_index]
        axis.add_patch(Circle(
            center, radius, facecolor=color, edgecolor=color,
            alpha=PUBLICATION_DISK_ALPHA,
            linewidth=PUBLICATION_DISK_LINE_WIDTH, zorder=2,
        ))
        axis.scatter(
            [center[0]], [center[1]], marker="^", s=34, color=[color],
            edgecolors=PUBLICATION_MARKER_EDGE_COLOR,
            linewidths=PUBLICATION_MARKER_EDGE_WIDTH, zorder=4,
        )

    for point_index, (x, y) in enumerate(positions):
        owner_station = stations[owners[point_index]]
        color = station_colors[_station_identity(owner_station)]
        axis.scatter(
            [x], [y], marker="o", s=19, color=[color],
            edgecolors=PUBLICATION_MARKER_EDGE_COLOR,
            linewidths=PUBLICATION_MARKER_EDGE_WIDTH, zorder=5,
        )

    station_order = sorted(
        range(len(stations)),
        key=lambda index: _station_identity(stations[index]),
    )
    supports_by_point: dict[int, list[int]] = {}
    for station_index in station_order:
        support = supports[station_index]
        if support >= 0:
            supports_by_point.setdefault(support, []).append(station_index)
    for support, station_indices in supports_by_point.items():
        for rank, station_index in enumerate(station_indices):
            point_x, point_y = positions[support]
            station = stations[station_index]
            station_x, station_y = float(station["x"]), float(station["y"])
            color = station_colors[_station_identity(station)]
            if show_support_connectors:
                axis.plot(
                    [station_x, point_x], [station_y, point_y],
                    color=color, linewidth=0.65, alpha=0.65, zorder=3,
                )
            # Nested outlines preserve every station-to-support association when
            # several stations share one supporting moving point.
            axis.scatter(
                [point_x], [point_y], marker="D", s=48 + 4 * rank,
                facecolors="none", edgecolors=[color], linewidths=1.0,
                zorder=6 + rank * 0.001,
            )


def _build_publication_panel(
    instance: dict[str, Any],
    state: dict[str, Any],
    bounds: dict[str, Any],
    station_colors: dict[str, tuple[float, float, float, float]],
    *,
    panel_label: str,
    show_trajectories: bool,
    show_support_connectors: bool,
    dpi: int,
) -> Any:
    figure = plt.figure(
        figsize=PUBLICATION_PANEL_SIZE, dpi=dpi, facecolor="white"
    )
    grid = figure.add_gridspec(2, 1, height_ratios=(16, 3), hspace=0.5)
    axis = figure.add_subplot(grid[0])
    legend_axis = figure.add_subplot(grid[1])
    legend_axis.set_axis_off()
    _draw_publication_panel(
        axis, instance, state, bounds, station_colors,
        panel_label=panel_label,
        show_trajectories=show_trajectories,
        show_support_connectors=show_support_connectors,
    )
    legend_axis.legend(
        handles=_publication_legend_handles(), loc="center",
        ncol=2, fontsize=6.2, frameon=False, handlelength=1.25,
        columnspacing=0.7, handletextpad=0.3, labelspacing=0.3,
    )
    figure.subplots_adjust(left=0.19, right=0.98, top=0.90, bottom=0.045)
    return figure


def _build_publication_triptych(
    instance: dict[str, Any],
    states: list[dict[str, Any]],
    bounds: dict[str, Any],
    station_colors: dict[str, tuple[float, float, float, float]],
    *,
    show_trajectories: bool,
    show_support_connectors: bool,
    dpi: int,
) -> Any:
    figure = plt.figure(
        figsize=PUBLICATION_TRIPTYCH_SIZE, dpi=dpi, facecolor="white"
    )
    grid = figure.add_gridspec(
        2, 3, height_ratios=(18, 1.4), hspace=0.02, wspace=0.27
    )
    axes = [figure.add_subplot(grid[0, index]) for index in range(3)]
    legend_axis = figure.add_subplot(grid[1, :])
    legend_axis.set_axis_off()
    panel_labels = ("(a)  t = 0", "(b)  t = T/2", "(c)  t = T")
    for axis, state, panel_label in zip(axes, states, panel_labels):
        _draw_publication_panel(
            axis, instance, state, bounds, station_colors,
            panel_label=panel_label,
            show_trajectories=show_trajectories,
            show_support_connectors=show_support_connectors,
        )
    legend_axis.legend(
        handles=_publication_legend_handles(), loc="center", ncol=5,
        fontsize=6.5, frameon=False, handlelength=1.55,
        columnspacing=1.1, handletextpad=0.4,
    )
    figure.subplots_adjust(left=0.075, right=0.99, top=0.94, bottom=0.13)
    return figure


def _save_publication_figure(
    experiment: Path,
    figure: Any,
    png_path: Path,
    pdf_path: Path,
    *,
    dpi: int,
    formats: tuple[str, ...],
    figure_type: str,
    temporal_state: str,
    source_solution_sha256: str,
    rendering_configuration: dict[str, Any],
) -> dict[str, Any]:
    physical_size = tuple(float(value) for value in figure.get_size_inches())
    png_record: dict[str, Any] | None = None
    pdf_record: dict[str, Any] | None = None
    try:
        _validate_publication_figure_text(figure)
        if "png" in formats:
            from io import BytesIO

            buffer = BytesIO()
            with matplotlib.rc_context({"savefig.bbox": None}):
                figure.savefig(
                    buffer, format="png", dpi=dpi, facecolor="white",
                    transparent=False,
                )
            payload = buffer.getvalue()
            if len(payload) < 24 or payload[:8] != b"\x89PNG\r\n\x1a\n":
                raise ValueError(f"Matplotlib produced an invalid PNG: {png_path}")
            width, height = struct.unpack(">II", payload[16:24])
            if width <= 0 or height <= 0:
                raise ValueError(f"Matplotlib produced an empty PNG: {png_path}")
            _write_bytes(experiment, png_path, payload)
            png_record = {
                **_artifact(experiment, png_path, "publication-snapshot-png"),
                "path": png_path.relative_to(experiment).as_posix(),
                "figure_type": figure_type,
                "temporal_state": temporal_state,
                "image_dimensions": {"width": width, "height": height},
                "dpi": dpi,
                "physical_dimensions_inches": {
                    "width": physical_size[0], "height": physical_size[1],
                },
                "renderer_version": PUBLICATION_RENDERER_VERSION,
                "rendering_configuration": rendering_configuration,
                "source_solution_sha256": source_solution_sha256,
            }
        if "pdf" in formats:
            from io import BytesIO

            buffer = BytesIO()
            with matplotlib.rc_context({
                "font.family": "DejaVu Sans",
                "pdf.fonttype": 42,
                "pdf.use14corefonts": False,
                "ps.fonttype": 42,
                "text.usetex": False,
                "savefig.bbox": None,
            }):
                figure.savefig(
                    buffer, format="pdf", dpi=dpi, facecolor="white",
                    metadata={"Creator": "KineticDiskCover publication renderer"},
                )
            payload = buffer.getvalue()
            _validate_pdf_bytes(payload, path=pdf_path)
            _write_bytes(experiment, pdf_path, payload)
            pdf_record = {
                **_artifact(experiment, pdf_path, "publication-snapshot-pdf"),
                "path": pdf_path.relative_to(experiment).as_posix(),
                "figure_type": figure_type,
                "temporal_state": temporal_state,
                "physical_dimensions_inches": {
                    "width": physical_size[0], "height": physical_size[1],
                },
                "renderer_version": PUBLICATION_RENDERER_VERSION,
                "rendering_configuration": rendering_configuration,
                "source_solution_sha256": source_solution_sha256,
                "vector_geometry": True,
                "embedded_font_family": "DejaVu Sans",
            }
    finally:
        plt.close(figure)
    if png_record is None or pdf_record is None:
        raise ValueError("publication snapshots require both PNG and PDF outputs")
    return {
        **png_record,
        "png_path": png_record["path"],
        "pdf_path": pdf_record["path"],
        "pdf": pdf_record,
    }


def _render_publication_solution_images(
    experiment: Path,
    instance: dict[str, Any],
    solution: dict[str, Any],
    plan: dict[str, Any],
    run_id: str,
    dpi: int,
    source_solution_sha256: str,
    *,
    show_trajectories: bool = True,
    show_support_connectors: bool = False,
) -> dict[str, Any]:
    if dpi <= 0:
        raise ValueError("publication DPI must be positive")
    if not isinstance(source_solution_sha256, str) or len(source_solution_sha256) != 64:
        raise ValueError("publication rendering requires a SHA-256 solution checksum")
    _validate_solution_for_instance(solution, instance)
    t_end = float(instance["T_end"])
    if not math.isfinite(t_end) or t_end <= 0:
        raise ValueError("instance horizon must be finite and positive")
    stations = instance["stations"]
    if not stations:
        raise ValueError("publication snapshots require at least one station")
    for station in stations:
        if not math.isfinite(float(station["x"])) or not math.isfinite(
            float(station["y"])
        ):
            raise ValueError("station coordinates must be finite")
    for trajectory in instance["trajectories"]:
        for waypoint in trajectory["waypoints"]:
            if not math.isfinite(float(waypoint["x"])) or not math.isfinite(
                float(waypoint["y"])
            ):
                raise ValueError("trajectory contains a non-finite waypoint")
    station_colors = _publication_station_colors(stations)
    actual_times = tuple(fraction * t_end for fraction in TIME_FRACTIONS)
    states = [
        _publication_state(instance, solution, time) for time in actual_times
    ]
    bounds = _snapshot_bounds(instance, solution, actual_times)
    normalized_colors = {
        identity: [round(channel, 8) for channel in color]
        for identity, color in station_colors.items()
    }
    configuration = {
        "renderer": "publication",
        "renderer_version": PUBLICATION_RENDERER_VERSION,
        "dpi": dpi,
        "formats": ["png", "pdf"],
        "individual_panel_size_inches": list(PUBLICATION_PANEL_SIZE),
        "triptych_size_inches": list(PUBLICATION_TRIPTYCH_SIZE),
        "normalized_times": list(TIME_FRACTIONS),
        "actual_times": list(actual_times),
        "spatial_bounds": {
            "xlim": list(bounds["xlim"]),
            "ylim": list(bounds["ylim"]),
            "padding_fraction": bounds["padding_fraction"],
        },
        "axis_aspect": "equal",
        "trajectory_visibility": "visible" if show_trajectories else "hidden",
        "support_connectors": (
            "visible" if show_support_connectors else "hidden"
        ),
        "point_labels": "none",
        "station_color_policy": (
            "discrete turbo palette keyed by stable station id and coordinates"
        ),
        "station_colors": normalized_colors,
        "coverage_disk_alpha": PUBLICATION_DISK_ALPHA,
        "trajectory_alpha": PUBLICATION_TRAJECTORY_ALPHA,
        "pdf_font_family": "DejaVu Sans",
        "pdf_fonttype": 42,
    }
    instance_id = _safe_filename(str(plan["instance_id"]))
    algorithm = _safe_filename(str(plan["algorithm"]))
    objective = _safe_filename(str(plan["objective"]))
    directory = (
        experiment / "snapshots" / instance_id / objective / algorithm / run_id
    )
    panels = []
    for index, (stem, _fraction, panel_label) in enumerate(PUBLICATION_TIMES):
        figure = _build_publication_panel(
            instance, states[index], bounds, station_colors,
            panel_label=panel_label,
            show_trajectories=show_trajectories,
            show_support_connectors=show_support_connectors,
            dpi=dpi,
        )
        panels.append(_save_publication_figure(
            experiment, figure, directory / f"{stem}.png",
            directory / f"{stem}.pdf", dpi=dpi, formats=("png", "pdf"),
            figure_type="individual-panel",
            temporal_state=panel_label,
            source_solution_sha256=source_solution_sha256,
            rendering_configuration=configuration,
        ))
    triptych_figure = _build_publication_triptych(
        instance, states, bounds, station_colors,
        show_trajectories=show_trajectories,
        show_support_connectors=show_support_connectors,
        dpi=dpi,
    )
    triptych = _save_publication_figure(
        experiment, triptych_figure, directory / "three-time.png",
        directory / "three-time.pdf", dpi=dpi, formats=("png", "pdf"),
        figure_type="three-panel-composite",
        temporal_state="t = 0, T/2, T",
        source_solution_sha256=source_solution_sha256,
        rendering_configuration=configuration,
    )
    return {
        "triptych": triptych,
        "panels": panels,
        "rendering_parameters": {
            **configuration,
            "image_dimensions": {
                "individual_panel": panels[0]["image_dimensions"],
                "triptych": triptych["image_dimensions"],
            },
            "physical_dimensions_inches": {
                "individual_panel": list(PUBLICATION_PANEL_SIZE),
                "triptych": list(PUBLICATION_TRIPTYCH_SIZE),
            },
        },
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

    def check_artifact(
        item: dict[str, Any], *, png: bool = False, pdf: bool = False
    ) -> None:
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
                    image_format = image.format
                    image_dpi = image.info.get("dpi")
                    image.verify()
                expected = item.get("image_dimensions", {})
                if image_format != "PNG" or (
                    expected and dimensions != (
                        expected.get("width"), expected.get("height")
                    )
                ):
                    missing.append(f"{item['path']} (invalid PNG metadata)")
                expected_dpi = item.get("dpi")
                if expected_dpi is not None and (
                    image_dpi is None
                    or len(image_dpi) != 2
                    or any(
                        not math.isclose(
                            float(axis_dpi), float(expected_dpi),
                            rel_tol=0.005,
                        )
                        for axis_dpi in image_dpi
                    )
                ):
                    missing.append(f"{item['path']} (invalid PNG DPI metadata)")
            except (OSError, ValueError) as error:
                missing.append(f"{item['path']} (invalid PNG: {error})")
        if pdf:
            try:
                _validate_pdf_bytes(path.read_bytes(), path=path)
            except (OSError, ValueError) as error:
                missing.append(f"{item['path']} (invalid PDF: {error})")

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
                if item.get("pdf"):
                    check_artifact(item["pdf"], pdf=True)
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
        image_dpi = image.info.get("dpi")
        image.verify()
    expected = artifact.get("image_dimensions", {})
    if image_format != "PNG" or min(dimensions) < 100:
        raise ValueError(f"staged snapshot is not a sensible PNG: {path}")
    if expected and dimensions != (
        expected.get("width"), expected.get("height")
    ):
        raise ValueError(f"staged snapshot dimensions mismatch: {path}")
    expected_dpi = artifact.get("dpi")
    if expected_dpi is not None and (
        image_dpi is None
        or len(image_dpi) != 2
        or any(
            not math.isclose(
                float(axis_dpi), float(expected_dpi), rel_tol=0.005
            )
            for axis_dpi in image_dpi
        )
    ):
        raise ValueError(f"staged snapshot DPI metadata mismatch: {path}")


def _validate_staged_pdf(path: Path, artifact: dict[str, Any]) -> None:
    if not path.is_file() or path.stat().st_size != artifact["size_bytes"]:
        raise ValueError(f"staged PDF is missing or has the wrong size: {path}")
    if sha256_file(path) != artifact["sha256"]:
        raise ValueError(f"staged PDF checksum mismatch: {path}")
    contents = path.read_bytes()
    _validate_pdf_bytes(contents, path=path)
    media_box = re.search(rb"/MediaBox\s*\[([^\]]+)\]", contents)
    physical_size = artifact.get("physical_dimensions_inches", {})
    if media_box is None or not physical_size:
        raise ValueError(f"staged PDF has no expected physical size: {path}")
    points = [float(value) for value in media_box.group(1).split()]
    expected_width = float(physical_size["width"]) * 72.0
    expected_height = float(physical_size["height"]) * 72.0
    if (
        not math.isclose(points[2] - points[0], expected_width, abs_tol=0.02)
        or not math.isclose(points[3] - points[1], expected_height, abs_tol=0.02)
    ):
        raise ValueError(f"staged PDF physical dimensions mismatch: {path}")


def _validate_external_backup(
    experiment: Path, backup_dir: str | Path
) -> dict[str, Any]:
    backup = Path(backup_dir).resolve()
    repository = ROOT.resolve()
    if backup.is_relative_to(repository) or backup.is_relative_to(experiment):
        raise ValueError("snapshot backup must be outside the repository")
    if not backup.is_dir() or backup.stat().st_dev != experiment.stat().st_dev:
        raise ValueError(
            "snapshot backup must exist on the experiment's filesystem"
        )
    inventory_path = backup / "backup_sha256.json"
    if not inventory_path.is_file():
        raise ValueError(f"snapshot backup checksum inventory is missing: {inventory_path}")
    inventory = read_json(inventory_path)
    if inventory.get("experiment_id") != experiment.name:
        raise ValueError("snapshot backup belongs to a different experiment")
    files = inventory.get("files")
    if not isinstance(files, list):
        raise ValueError("snapshot backup checksum inventory is invalid")
    by_path: dict[str, dict[str, Any]] = {}
    for item in files:
        if not isinstance(item, dict) or not isinstance(item.get("path"), str):
            raise ValueError("snapshot backup checksum inventory has an invalid entry")
        relative = Path(item["path"])
        if relative.is_absolute() or ".." in relative.parts:
            raise ValueError("snapshot backup inventory contains an unsafe path")
        key = relative.as_posix()
        if key in by_path:
            raise ValueError(f"snapshot backup has a duplicate path: {key}")
        source = backup / "payload" / relative
        if (
            not source.is_file()
            or source.stat().st_size != item.get("size_bytes")
            or sha256_file(source) != item.get("sha256")
        ):
            raise ValueError(f"snapshot backup checksum mismatch: {key}")
        by_path[key] = item

    required_paths = {
        path.relative_to(experiment).as_posix()
        for path in (experiment / "snapshots").rglob("*")
        if path.is_file() and path.suffix.lower() == ".png"
    }
    for relative in (
        "reports/visualization_manifest.json",
        "reports/visualization_report.md",
        "reports/snapshot_regeneration_report.md",
        "artifacts.json",
    ):
        if (experiment / relative).is_file():
            required_paths.add(relative)
    missing = required_paths - by_path.keys()
    if missing:
        raise ValueError(
            "snapshot backup is incomplete: " + ", ".join(sorted(missing)[:10])
        )
    return {
        "path": str(backup),
        "inventory_path": str(inventory_path),
        "inventory_sha256": sha256_file(inventory_path),
        "file_count": len(files),
        "snapshot_pngs": sum(
            name.startswith("snapshots/") and name.lower().endswith(".png")
            for name in by_path
        ),
        "snapshot_pdfs": sum(
            name.startswith("snapshots/") and name.lower().endswith(".pdf")
            for name in by_path
        ),
        "paths": set(by_path),
    }


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
    dpi: int = PUBLICATION_DPI,
    formats: tuple[str, ...] = ("png", "pdf"),
    show_trajectories: bool = True,
    point_label_policy: str = "none",
    show_support_connectors: bool = False,
    staging_parent: str | Path | None = None,
    backup_dir: str | Path | None = None,
) -> dict[str, Any]:
    """Safely replace static publication snapshots from validated run artifacts."""
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
    normalized_formats = tuple(
        sorted({str(value).strip().lower() for value in formats})
    )
    if dpi <= 0 or point_label_policy != "none":
        raise ValueError("invalid snapshot rendering settings")
    if normalized_formats != ("pdf", "png"):
        raise ValueError(
            "publication snapshots require both PNG and PDF formats"
        )
    if backup_dir is None:
        raise ValueError(
            "snapshot regeneration requires a verified external backup directory"
        )
    backup_info = _validate_external_backup(experiment, backup_dir)
    staging_parent_path = Path(
        staging_parent if staging_parent is not None else tempfile.gettempdir()
    ).resolve()
    if (
        not staging_parent_path.is_dir()
        or staging_parent_path.is_relative_to(ROOT.resolve())
        or staging_parent_path.stat().st_dev != experiment.stat().st_dev
    ):
        raise ValueError(
            "snapshot staging directory must be outside the repository and "
            "on the experiment's filesystem"
        )

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

    manifest_png_paths = {
        item["path"]
        for slot in old_slots
        if isinstance(slot.get("snapshots"), dict)
        for item in [
            slot["snapshots"]["triptych"],
            *slot["snapshots"]["panels"],
        ]
    }
    existing_png_paths = {
        path.relative_to(experiment).as_posix()
        for path in (experiment / "snapshots").rglob("*.png")
        if path.is_file()
    }
    if manifest_png_paths != existing_png_paths:
        raise ValueError(
            "existing PNG paths do not exactly match visualization manifest"
        )
    expected_pdf_paths: set[str] = set()
    for row in eligible:
        entry = row["plan"]
        run_id = str(row["record"]["run_id"])
        snapshot_dir = (
            Path("snapshots") / _safe_filename(str(entry["instance_id"]))
            / _safe_filename(str(entry["objective"]))
            / _safe_filename(str(entry["algorithm"])) / run_id
        )
        expected_pdf_paths.update(
            (snapshot_dir / Path(name).with_suffix(".pdf")).as_posix()
            for name in (
                "tau-0.png", "tau-0.5.png", "tau-1.png", "three-time.png"
            )
        )
    existing_pdf_paths = {
        path.relative_to(experiment).as_posix()
        for path in (experiment / "snapshots").rglob("*.pdf")
        if path.is_file()
    }
    if not existing_pdf_paths.issubset(expected_pdf_paths):
        raise ValueError(
            "existing PDF paths include artifacts outside the planned snapshot set"
        )
    unbacked_existing_pdfs = existing_pdf_paths - backup_info["paths"]
    if unbacked_existing_pdfs:
        raise ValueError(
            "existing PDF snapshots are missing from the external backup: "
            + ", ".join(sorted(unbacked_existing_pdfs)[:10])
        )

    stage_root = Path(tempfile.mkdtemp(
        prefix=f".{experiment.name}.snapshot-stage-",
        dir=staging_parent_path,
    )).resolve()
    backup_root = stage_root / "backup"
    backup_root.mkdir()
    png_replacements: list[tuple[Path, Path, dict[str, Any]]] = []
    pdf_replacements: list[tuple[Path, Path, dict[str, Any]]] = []
    artifact_replacements: list[tuple[Path, Path, dict[str, Any]]] = []
    completed = False
    try:
        new_snapshots: dict[str, dict[str, Any]] = {}
        for row in eligible:
            entry = row["plan"]
            run_id = str(row["record"]["run_id"])
            rendered = _render_publication_solution_images(
                stage_root, row["instance"], row["solution"],
                entry, run_id, dpi, row["source_solution_sha256"],
                show_trajectories=show_trajectories,
                show_support_connectors=show_support_connectors,
            )
            expected_dir = Path("snapshots") / _safe_filename(
                str(entry["instance_id"])
            ) / _safe_filename(str(entry["objective"])) / _safe_filename(
                str(entry["algorithm"])
            ) / run_id
            items = [*rendered["panels"], rendered["triptych"]]
            expected_names = ["tau-0.png", "tau-0.5.png", "tau-1.png", "three-time.png"]
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
                pdf_item = item["pdf"]
                expected_pdf = (expected_dir / Path(filename).with_suffix(".pdf")).as_posix()
                staged_pdf = stage_root / pdf_item["path"]
                if pdf_item["path"] != expected_pdf:
                    raise ValueError(
                        f"renderer produced unexpected PDF path: {pdf_item['path']}"
                    )
                _validate_staged_pdf(staged_pdf, pdf_item)
                pdf_target = _contained(experiment, experiment / expected_pdf)
                mapped_pdf = {**pdf_item, "path": expected_pdf}
                mapped = {
                    **item,
                    "path": expected_relative,
                    "png_path": expected_relative,
                    "pdf_path": expected_pdf,
                    "pdf": mapped_pdf,
                }
                mapped_items.append(mapped)
                png_replacements.append((staged_path, target, mapped))
                pdf_replacements.append((staged_pdf, pdf_target, mapped_pdf))
                artifact_replacements.extend([
                    (staged_path, target, mapped),
                    (staged_pdf, pdf_target, mapped_pdf),
                ])
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
        staged_pdfs = sorted(
            path.relative_to(stage_root).as_posix()
            for path in (stage_root / "snapshots").rglob("*.pdf")
        )
        expected_pdfs = sorted(item[2]["path"] for item in pdf_replacements)
        if staged_pdfs != expected_pdfs:
            raise ValueError("staged PDF set differs from validated renderable slots")
        if len(pdf_replacements) != len(eligible) * 4:
            raise ValueError("staged output count is not four PDFs per eligible slot")
        png_targets = [target for _, target, _ in png_replacements]
        pdf_targets = [target for _, target, _ in pdf_replacements]
        if (
            len(set(png_targets)) != len(png_targets)
            or len(set(pdf_targets)) != len(pdf_targets)
            or set(png_targets) & set(pdf_targets)
        ):
            raise ValueError("snapshot destinations contain duplicate paths")
        if stage_root.is_relative_to(experiment / "snapshots"):
            raise ValueError("temporary staging files are inside snapshots")
        _validate_pdf_pages_with_ghostscript(
            [staged for staged, _, _ in pdf_replacements],
            stage_root / "pdf-validation.ps",
        )

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
            "publication_renderer_version": PUBLICATION_RENDERER_VERSION,
            "generated_solution_snapshots": len(eligible),
            "generated_snapshot_files": len(png_replacements),
            "generated_snapshot_pdfs": len(pdf_replacements),
            "ineligible_slots": len(rows) - len(eligible),
            "failed_slots": 0,
            "rendering_policy": {
                **updated_manifest.get("rendering_policy", {}),
                "renderer": "publication",
                "renderer_version": RENDERER_VERSION,
                "publication_renderer_version": PUBLICATION_RENDERER_VERSION,
                "visualization_schema_version": 1,
                "normalized_times": list(TIME_FRACTIONS),
                "time_conversion": "t = tau * T_end",
                "dpi": dpi,
                "formats": ["png", "pdf"],
                "output_format": "PNG and vector PDF",
                "layout_method": "publication gridspec with shared triptych legend",
                "trajectory_visibility": (
                    "visible" if show_trajectories else "hidden"
                ),
                "point_label_policy": "none",
                "support_connector_policy": (
                    "visible" if show_support_connectors else "hidden"
                ),
                "station_color_policy": (
                    "discrete turbo palette keyed by stable station id and coordinates"
                ),
                "pdf_font_family": "DejaVu Sans",
                "pdf_fonttype": 42,
                "individual_panel_size_inches": list(PUBLICATION_PANEL_SIZE),
                "triptych_size_inches": list(PUBLICATION_TRIPTYCH_SIZE),
                "opacity": {
                    "trajectory": PUBLICATION_TRAJECTORY_ALPHA,
                    "coverage_disk": PUBLICATION_DISK_ALPHA,
                    "support_connector": 0.65,
                },
                "line_widths": {
                    "trajectory": PUBLICATION_TRAJECTORY_LINE_WIDTH,
                    "coverage_disk_outline": PUBLICATION_DISK_LINE_WIDTH,
                    "support_connector": 0.65,
                    "support_marker_outline": 1.0,
                    "moving_point_outline": PUBLICATION_MARKER_EDGE_WIDTH,
                    "station_marker_outline": PUBLICATION_MARKER_EDGE_WIDTH,
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
            f"- Regenerated publication PDFs: {len(pdf_replacements)}",
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
            f"- Repository: `{ROOT}`",
            f"- Target experiment ID: `{experiment.name}`",
            f"- Renderer version: `{RENDERER_VERSION}`",
            f"- Source code revision (Git HEAD): `{current_revision}`",
            f"- Renderer source SHA-256 (working tree): `{renderer_sha256}`",
            f"- Completed at (UTC): `{datetime.now(timezone.utc).isoformat()}`",
            f"- Original snapshot PNGs: {backup_info['snapshot_pngs']}",
            f"- Original snapshot PDFs: {backup_info['snapshot_pdfs']}",
            f"- Planned slots: {len(plan)}",
            f"- Renderable solutions: {len(eligible)}",
            f"- PNGs regenerated: {len(png_replacements)}",
            f"- PDFs regenerated: {len(pdf_replacements)}",
            f"- Ineligible slots: {len(rows) - len(eligible)}",
            f"- External backup: `{backup_info['path']}`",
            f"- Backup files checksum-verified: {backup_info['file_count']}",
            f"- Backup inventory SHA-256: `{backup_info['inventory_sha256']}`",
            "- Rendering failures: 0",
            "- PNG validation: staged images decoded with Pillow; dimensions, DPI, "
            "sizes, checksums, output paths and four-file-per-slot coverage verified.",
            "- PDF validation: one page per file; MediaBox matched configured "
            "dimensions; vector path operators, embedded DejaVu Sans fonts, and "
            "absence of raster image objects were checked; Ghostscript opened every PDF.",
            "- Solver execution, calibration, run-plan creation and animation "
            "generation: not performed.",
            "- Solver run records, results, serialized solutions, traces, aggregates, "
            "tables and source inputs: not modified.",
            "- Only the visualization manifest/report, snapshot regeneration report, "
            "artifact index, and snapshot PNG/PDF files were updated.",
            "- Snapshot-specific figure-status metadata and all other reports were "
            "left unchanged.",
            "- Other experiments, comparison HTML pages, and animation files were "
            "left unchanged.",
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
            f"- Expected and validated vector PDFs: {len(pdf_replacements)}.",
            "- Comparison sheets retained in place; their existing relative PNG links "
            "continue to target the replaced files.",
            "- No solver, benchmark, calibration, experiment pipeline, or animation "
            "was executed.",
        ]) + "\n"

        # Back up every destination before replacement; metadata is published only
        # after all staged images have passed their independent decode checks.
        mutable_payloads: dict[Path, bytes | None] = {}
        for path in (
            visualization_path, report_path, regeneration_path,
            experiment / "artifacts.json",
        ):
            mutable_payloads[path] = path.read_bytes() if path.is_file() else None
        backup_files: dict[Path, Path] = {}
        for target in [target for _, target, _ in artifact_replacements]:
            if not target.is_file():
                continue
            backup = backup_root / target.relative_to(experiment)
            backup.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(target, backup)
            backup_files[target] = backup

        replaced_artifacts: list[tuple[Path, Path]] = []
        try:
            for staged, target, _item in artifact_replacements:
                replaced_artifacts.append((staged, target))
                _replace_file_atomically(staged, target)
            write_derived_json(visualization_path, updated_manifest)
            write_derived_bytes(report_path, report_text.encode("utf-8"))
            write_derived_bytes(regeneration_path, regeneration_text.encode("utf-8"))

            index_experiment_artifacts(experiment)
            final_audit_result = audit(experiment)
            if final_audit_result["rendered_slots"] != len(eligible):
                raise ValueError("post-replacement visualization audit count mismatch")
        except BaseException:
            for staged, target in reversed(replaced_artifacts):
                if target.is_file() and not staged.exists():
                    staged.parent.mkdir(parents=True, exist_ok=True)
                    shutil.copy2(target, staged)
                backup = backup_files.get(target)
                if backup is None:
                    target.unlink(missing_ok=True)
                else:
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

        completed = True
        return {
            "experiment_id": experiment.name,
            "planned_slots": len(plan),
            "renderable_solutions": len(eligible),
            "regenerated_pngs": len(png_replacements),
            "regenerated_pdfs": len(pdf_replacements),
            "ineligible_slots": len(rows) - len(eligible),
            "ineligible_reasons": ineligible_reasons,
            "renderer_version": PUBLICATION_RENDERER_VERSION,
            "audit": final_audit_result,
        }
    finally:
        if completed:
            shutil.rmtree(stage_root)


def _parse_publication_formats(value: str) -> tuple[str, ...]:
    formats = [part.strip().lower() for part in value.split(",") if part.strip()]
    if len(formats) != len(set(formats)) or set(formats) != {"png", "pdf"}:
        raise ValueError(
            "--formats must specify both png and pdf exactly once"
        )
    return tuple(sorted(formats))


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--experiment", required=True)
    parser.add_argument("--fps", type=int, default=12)
    parser.add_argument("--frames", type=int, default=48)
    parser.add_argument("--dpi", type=int)
    parser.add_argument(
        "--formats",
        default="png,pdf",
        help="publication snapshot formats; both png and pdf are required",
    )
    parser.add_argument("--hide-trajectories", action="store_true")
    parser.add_argument(
        "--point-labels", choices=("none", "supports", "all"),
        default=None,
    )
    parser.add_argument("--show-support-connectors", action="store_true")
    parser.add_argument(
        "--staging-parent",
        help="existing directory outside the repository on the experiment filesystem",
    )
    parser.add_argument(
        "--backup-dir",
        help="verified external backup directory required for snapshots-only replacement",
    )
    parser.add_argument("--audit", action="store_true")
    parser.add_argument(
        "--snapshots-only",
        action="store_true",
        help=(
            "safely regenerate publication PNG/PDF snapshots without animations "
            "or solvers"
        ),
    )
    parser.add_argument(
        "--replace-existing",
        action="store_true",
        help="allow snapshots-only mode to atomically replace existing PNGs",
    )
    args = parser.parse_args(argv)
    try:
        if args.audit:
            if args.snapshots_only or args.replace_existing:
                raise ValueError("--audit cannot be combined with snapshot replacement")
            report = audit(args.experiment)
            print(json.dumps(report, sort_keys=True))
        elif args.snapshots_only:
            if not args.replace_existing:
                raise ValueError(
                    "--snapshots-only requires explicit --replace-existing"
                )
            if not args.backup_dir:
                raise ValueError(
                    "--snapshots-only requires a verified external --backup-dir"
                )
            point_label_policy = args.point_labels or "none"
            if point_label_policy != "none":
                raise ValueError(
                    "publication snapshots do not support point ownership labels"
                )
            report = regenerate_snapshots(
                args.experiment,
                dpi=args.dpi if args.dpi is not None else PUBLICATION_DPI,
                formats=_parse_publication_formats(args.formats),
                show_trajectories=not args.hide_trajectories,
                point_label_policy=point_label_policy,
                show_support_connectors=args.show_support_connectors,
                staging_parent=args.staging_parent,
                backup_dir=args.backup_dir,
            )
            print(json.dumps(report, sort_keys=True))
        elif args.replace_existing or args.backup_dir or args.staging_parent:
            raise ValueError(
                "--replace-existing, --backup-dir, and --staging-parent "
                "are only valid with --snapshots-only"
            )
        elif args.formats != "png,pdf":
            raise ValueError("--formats is only valid with --snapshots-only")
        else:
            report = generate(
                args.experiment, fps=args.fps, frames=args.frames,
                dpi=args.dpi if args.dpi is not None else 90,
                show_trajectories=not args.hide_trajectories,
                point_label_policy=args.point_labels or "supports",
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
