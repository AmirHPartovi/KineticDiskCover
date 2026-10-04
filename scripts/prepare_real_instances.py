#!/usr/bin/env python3
"""Convert repository MDC instance JSON files to the solver's JSON schema."""

from __future__ import annotations

import argparse
import json
import math
from pathlib import Path
import sys
from typing import Any


def _coordinate(value: Any, context: str) -> tuple[float, float]:
    if not isinstance(value, list) or len(value) != 2:
        raise ValueError(f"{context} must be a two-number coordinate")
    if any(
        isinstance(component, bool)
        or not isinstance(component, (int, float))
        or not math.isfinite(component)
        for component in value
    ):
        raise ValueError(f"{context} must contain finite numbers")
    return float(value[0]), float(value[1])


def convert_instance(source: Path, instance_id: int) -> dict[str, Any]:
    with source.open(encoding="utf-8") as input_file:
        legacy = json.load(input_file)
    if not isinstance(legacy, dict):
        raise ValueError("instance root must be a JSON object")
    moving_points = legacy.get("moving_points")
    centers = legacy.get("centers")
    if not isinstance(moving_points, list) or not moving_points:
        raise ValueError("instance must contain non-empty moving_points")
    if not isinstance(centers, list) or not centers:
        raise ValueError("instance must contain non-empty centers")
    name = legacy.get("name") or source.stem
    if not isinstance(name, str):
        raise ValueError("instance name must be a string")

    stations = []
    for index, center in enumerate(centers):
        if not isinstance(center, dict):
            raise ValueError(f"center {index} must be an object")
        coordinates = center.get("coord", center.get("center"))
        x, y = _coordinate(coordinates, f"center {index}")
        stations.append({"id": index, "x": x, "y": y})

    trajectories = []
    for index, moving_point in enumerate(moving_points):
        if not isinstance(moving_point, dict):
            raise ValueError(f"moving point {index} must be an object")
        start = moving_point.get("start")
        end = moving_point.get("end")
        if not isinstance(start, dict) or not isinstance(end, dict):
            raise ValueError(f"moving point {index} needs start and end objects")
        start_x, start_y = _coordinate(start.get("coord"),
                                       f"moving point {index} start")
        end_x, end_y = _coordinate(end.get("coord"),
                                   f"moving point {index} end")
        trajectories.append({
            "t_breaks": [0.0, 1.0],
            "waypoints": [
                {"x": start_x, "y": start_y},
                {"x": end_x, "y": end_y},
            ],
        })

    return {
        "id": instance_id,
        "name": name,
        "T_end": 1.0,
        "stations": stations,
        "trajectories": trajectories,
    }


def convert_dataset(input_dir: Path, output_dir: Path) -> int:
    sources = sorted(input_dir.rglob("*.mdc"))
    if not sources:
        raise ValueError(f"no .mdc instances found in {input_dir}")
    output_dir.mkdir(parents=True, exist_ok=True)
    for instance_id, source in enumerate(sources):
        relative = source.relative_to(input_dir).with_suffix(".json")
        destination = output_dir / relative
        destination.parent.mkdir(parents=True, exist_ok=True)
        converted = convert_instance(source, instance_id)
        destination.write_text(
            json.dumps(converted, indent=2) + "\n", encoding="utf-8"
        )
    return len(sources)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", required=True, help="MDC dataset directory")
    parser.add_argument("--output", required=True,
                        help="directory for converted solver JSON files")
    args = parser.parse_args(argv)
    input_dir = Path(args.input)
    if not input_dir.is_dir():
        parser.error(f"instance directory not found: {input_dir}")
    try:
        count = convert_dataset(input_dir, Path(args.output))
    except (OSError, ValueError, json.JSONDecodeError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1
    print(f"Converted {count} MDC instances from {input_dir}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
