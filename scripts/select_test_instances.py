#!/usr/bin/env python3
"""Select and materialize a deterministic set of small valid instances."""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import re
import shutil
import sys
from typing import Any

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
SUPPORTED_SUFFIXES = {".mdc", ".json"}
SELECTION_POLICY = (
    "select eligible small candidates first, maximizing family coverage, "
    "then fill by size; use oversized family representatives only when fewer "
    "than the requested count of valid in-limit instances exist"
)


def _number(value: Any, context: str) -> float:
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise ValueError(f"{context} must be a number")
    result = float(value)
    if not math.isfinite(result):
        raise ValueError(f"{context} must be finite")
    return result


def _canonical_payload(source: Path) -> dict[str, Any]:
    with source.open(encoding="utf-8") as stream:
        payload = json.load(stream)
    if not isinstance(payload, dict):
        raise ValueError("instance root must be a JSON object")
    if "stations" in payload and "trajectories" in payload:
        return payload

    from prepare_real_instances import convert_instance

    return convert_instance(source, 0)


def validate_instance(payload: Any) -> tuple[int, int]:
    if not isinstance(payload, dict):
        raise ValueError("instance root must be an object")
    stations = payload.get("stations")
    trajectories = payload.get("trajectories")
    if not isinstance(stations, list) or not stations:
        raise ValueError("stations must be a non-empty array")
    if not isinstance(trajectories, list) or not trajectories:
        raise ValueError("trajectories must be a non-empty array")
    n, m = len(trajectories), len(stations)
    if n <= 0 or m <= 0:
        raise ValueError("n and m must be positive")

    t_end = _number(payload.get("T_end"), "T_end")
    if t_end <= 0:
        raise ValueError("T_end must be positive")

    for index, station in enumerate(stations):
        if not isinstance(station, dict):
            raise ValueError(f"station {index} must be an object")
        _number(station.get("x"), f"station {index}.x")
        _number(station.get("y"), f"station {index}.y")

    for index, trajectory in enumerate(trajectories):
        if not isinstance(trajectory, dict):
            raise ValueError(f"trajectory {index} must be an object")
        breaks = trajectory.get("t_breaks")
        waypoints = trajectory.get("waypoints")
        if (not isinstance(breaks, list) or len(breaks) < 2
                or not isinstance(waypoints, list)
                or len(breaks) != len(waypoints)):
            raise ValueError(f"trajectory {index} has malformed breaks/waypoints")
        times = [_number(value, f"trajectory {index} time break")
                 for value in breaks]
        if any(right <= left for left, right in zip(times, times[1:])):
            raise ValueError(f"trajectory {index} time breaks must increase")
        if not math.isclose(times[0], 0.0, abs_tol=1e-9):
            raise ValueError(f"trajectory {index} must start at time zero")
        if not math.isclose(times[-1], t_end, rel_tol=1e-9, abs_tol=1e-9):
            raise ValueError(f"trajectory {index} must end at T_end")
        for waypoint_index, waypoint in enumerate(waypoints):
            if not isinstance(waypoint, dict):
                raise ValueError(
                    f"trajectory {index} waypoint {waypoint_index} must be an object"
                )
            _number(waypoint.get("x"),
                    f"trajectory {index} waypoint {waypoint_index}.x")
            _number(waypoint.get("y"),
                    f"trajectory {index} waypoint {waypoint_index}.y")
    return n, m


def _dataset_fingerprint(source: Path) -> str:
    digest = hashlib.sha256()
    files = sorted(
        path for path in source.rglob("*")
        if path.is_file() and path.suffix.lower() in SUPPORTED_SUFFIXES
    )
    for path in files:
        digest.update(path.relative_to(source).as_posix().encode("utf-8"))
        digest.update(b"\0")
        with path.open("rb") as stream:
            for chunk in iter(lambda: stream.read(1024 * 1024), b""):
                digest.update(chunk)
        digest.update(b"\0")
    return digest.hexdigest()


def _root_label(source: Path) -> str:
    try:
        return source.resolve().relative_to(ROOT).as_posix()
    except ValueError:
        return "."


def _source_path(source_root: Path, source_label: str, relative: str) -> str:
    if source_label == ".":
        return relative
    return f"{source_label.rstrip('/')}/{relative}"


def _flat_family(path: Path) -> str:
    stem = path.name.lower()
    if stem.endswith(".instance.mdc"):
        stem = stem[:-len(".instance.mdc")]
    else:
        stem = path.stem.lower()
    patterns = (
        r"^(euro-night|london|paris|stars|uniform|us-night)(?:[-_].*)?$",
        r"^(sbgdb-\d{8}-(?:fpg-poly|pntset))(?:[-_].*)?$",
        r"^(many_holes)(?:[_-].*)?$",
    )
    for pattern in patterns:
        match = re.match(pattern, stem, re.IGNORECASE)
        if match:
            return match.group(1).lower()
    return re.sub(r"[-_]\d+(?:[-_].*)?$", "", stem).lower() or stem


def discover_candidates(source: Path) -> tuple[list[dict[str, Any]], list[str]]:
    if not source.is_dir():
        raise ValueError(f"source dataset directory not found: {source}")
    candidates: list[dict[str, Any]] = []
    errors: list[str] = []
    source_label = _root_label(source)
    family_dirs = sorted(path for path in source.iterdir() if path.is_dir())
    scopes: list[tuple[str | None, list[Path]]] = []
    for family_dir in family_dirs:
        scopes.append((
            family_dir.name,
            sorted(
                path for path in family_dir.rglob("*")
                if path.is_file() and path.suffix.lower() in SUPPORTED_SUFFIXES
            ),
        ))
    root_files = sorted(
        path for path in source.iterdir()
        if path.is_file() and path.suffix.lower() in SUPPORTED_SUFFIXES
    )
    if root_files:
        scopes.append((None, root_files))
    if not scopes:
        nested_files = sorted(
            path for path in source.rglob("*")
            if path.is_file() and path.suffix.lower() in SUPPORTED_SUFFIXES
        )
        if nested_files:
            scopes.append((None, nested_files))
    for directory_family, files in scopes:
        files = sorted(
            files
        )
        for path in files:
            family = directory_family or _flat_family(path)
            relative_path = path.relative_to(source).as_posix()
            try:
                payload = _canonical_payload(path)
                n, m = validate_instance(payload)
            except (OSError, ValueError, json.JSONDecodeError) as error:
                errors.append(f"{relative_path}: {error}")
                continue
            candidates.append({
                "family": family,
                "source": _source_path(source, source_label, relative_path),
                "relative_source": relative_path,
                "n": n,
                "m": m,
                "file_size_bytes": path.stat().st_size,
                "_absolute_source": path,
                "_payload": payload,
            })
    candidates.sort(key=selection_key)
    return candidates, errors


def selection_key(candidate: dict[str, Any]) -> tuple[Any, ...]:
    return (
        candidate["n"],
        candidate["m"],
        candidate["n"] * candidate["m"],
        candidate["file_size_bytes"],
        candidate["relative_source"],
    )


def select_candidates(candidates: list[dict[str, Any]], count: int,
                      max_n: int = 50, max_m: int = 25
                      ) -> list[dict[str, Any]]:
    if count <= 0 or max_n <= 0 or max_m <= 0:
        raise ValueError("count and smoke limits must be positive")
    if len(candidates) < count:
        raise ValueError(
            f"requested {count} instances, but only {len(candidates)} valid "
            "instances were found"
        )
    eligible = [
        row for row in candidates
        if row["n"] <= max_n and row["m"] <= max_m
    ]
    by_family: dict[str, list[dict[str, Any]]] = {}
    for candidate in eligible:
        by_family.setdefault(candidate["family"], []).append(candidate)

    selected: list[dict[str, Any]] = []
    selected_sources: set[str] = set()
    family_representatives = sorted(
        (rows[0] for rows in by_family.values()), key=selection_key
    )
    for candidate in family_representatives[:count]:
        selected.append(candidate)
        selected_sources.add(candidate["relative_source"])
        candidate["selection_reason"] = (
            "smallest in-limit valid candidate selected for family coverage"
        )
    for candidate in eligible:
        if len(selected) >= count:
            break
        if candidate["relative_source"] not in selected_sources:
            candidate["selection_reason"] = (
                "small valid candidate used to fill the smoke set"
            )
            selected.append(candidate)
            selected_sources.add(candidate["relative_source"])

    if len(selected) < count:
        selected_families = {row["family"] for row in selected}
        oversized_representatives: list[dict[str, Any]] = []
        by_oversized_family: dict[str, list[dict[str, Any]]] = {}
        for candidate in candidates:
            if candidate["family"] not in selected_families:
                by_oversized_family.setdefault(
                    candidate["family"], []
                ).append(candidate)
        oversized_representatives = sorted(
            (rows[0] for rows in by_oversized_family.values()),
            key=selection_key,
        )
        for candidate in oversized_representatives:
            if len(selected) >= count:
                break
            candidate["selection_reason"] = (
                "oversized family fallback: no valid in-limit candidate; "
                "included only to preserve family coverage"
            )
            selected.append(candidate)
            selected_sources.add(candidate["relative_source"])
            selected_families.add(candidate["family"])
        for candidate in candidates:
            if len(selected) >= count:
                break
            if candidate["relative_source"] not in selected_sources:
                candidate["selection_reason"] = (
                    "smallest remaining valid candidate; smoke limits exceeded "
                    "because fewer in-limit candidates exist than requested"
                )
                selected.append(candidate)
                selected_sources.add(candidate["relative_source"])

    for candidate in selected:
        candidate["exceeds_smoke_limits"] = (
            candidate["n"] > max_n or candidate["m"] > max_m
        )
        candidate["smoke_limits"] = {"max_n": max_n, "max_m": max_m}
    return selected


def _manifest_source_root(manifest: dict[str, Any],
                          manifest_path: Path) -> Path:
    source_root = str(manifest.get("source_root", "."))
    if source_root == ".":
        return manifest_path.resolve().parent
    path = Path(source_root)
    return path if path.is_absolute() else ROOT / path


def _resolve_manifest_entry(entry: dict[str, Any], source_root: Path,
                            manifest_path: Path) -> Path:
    materialized = entry.get("materialized_file")
    if isinstance(materialized, str):
        local_path = manifest_path.resolve().parent / materialized
        if local_path.is_file():
            return local_path
    relative = entry.get("relative_source")
    if not isinstance(relative, str):
        source = entry.get("source")
        if not isinstance(source, str):
            raise ValueError("manifest instance needs source or relative_source")
        source_path = Path(source)
        try:
            relative = source_path.resolve().relative_to(source_root.resolve()).as_posix()
        except ValueError:
            relative = source
    candidate = source_root / relative
    if not candidate.is_file():
        # A portable local materialization stores selected files alongside its manifest.
        candidate = manifest_path.resolve().parent / "instances" / relative
    if not candidate.is_file():
        raise ValueError(f"manifest source file not found: {entry.get('source', relative)}")
    return candidate


def read_manifest(path: Path) -> dict[str, Any]:
    with path.open(encoding="utf-8") as stream:
        manifest = json.load(stream)
    if not isinstance(manifest, dict) or not isinstance(
        manifest.get("instances"), list
    ):
        raise ValueError("selection manifest must contain an instances array")
    return manifest


def resolve_instance_directory(path: Path) -> Path:
    if ((path / "manifest.json").is_file()
            and (path / "instances").is_dir()):
        manifest = read_manifest(path / "manifest.json")
        rows = manifest["instances"]
        expected = manifest.get("selected_instance_count", len(rows))
        if len(rows) != expected:
            raise ValueError(
                f"dataset manifest expects {expected} instances, has {len(rows)}"
            )
        seen_sources: set[str] = set()
        instance_root = (path / "instances").resolve()
        for row in rows:
            if not isinstance(row, dict):
                raise ValueError("dataset manifest instances must be objects")
            source = str(row.get("source", ""))
            materialized = row.get("materialized_file")
            if not source or source in seen_sources:
                raise ValueError("dataset manifest has a missing or duplicate source")
            seen_sources.add(source)
            if not isinstance(materialized, str):
                raise ValueError("dataset manifest lacks materialized_file")
            instance_path = (path / materialized).resolve()
            if not instance_path.is_relative_to(instance_root):
                raise ValueError(
                    f"manifest materialized path escapes instances directory: {materialized}"
                )
            if not instance_path.is_file():
                raise ValueError(f"materialized instance not found: {materialized}")
            expected_sha = row.get("materialized_sha256")
            if (expected_sha and hashlib.sha256(
                    instance_path.read_bytes()
            ).hexdigest() != expected_sha):
                raise ValueError(
                    f"materialized instance checksum mismatch: {materialized}"
                )
            n, m = validate_instance(_canonical_payload(instance_path))
            if n != row.get("n") or m != row.get("m"):
                raise ValueError(
                    f"materialized instance metadata mismatch: {materialized}"
                )
        actual = list(instance_root.rglob("*.json"))
        if len(actual) != expected:
            raise ValueError(
                f"materialized dataset contains {len(actual)} JSON files, "
                f"expected {expected}"
            )
        return path / "instances"
    return path


def apply_execution_defaults(args: Any) -> None:
    """Set conservative smoke defaults while preserving full-run defaults."""
    smoke = args.dataset_profile == "smoke10"
    defaults = {
        "threads": 2 if smoke else max(1, os.cpu_count() or 1),
        "time_limit": 2.0 if smoke else 60.0,
        "fast_time_limit": 5.0 if smoke else 30.0,
        "exact_time_limit": 10.0 if smoke else 600.0,
    }
    for name, default in defaults.items():
        if getattr(args, name) is None:
            setattr(args, name, default)


def make_manifest(source: Path, selected: list[dict[str, Any]],
                  count: int, max_n: int = 50,
                  max_m: int = 25) -> dict[str, Any]:
    source_label = _root_label(source)
    rows = []
    for rank, candidate in enumerate(selected, 1):
        path = candidate["_absolute_source"]
        rows.append({
            "family": candidate["family"],
            "source": candidate["source"],
            "relative_source": candidate["relative_source"],
            "n": candidate["n"],
            "m": candidate["m"],
            "file_size_bytes": candidate["file_size_bytes"],
            "sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
            "selection_rank": rank,
            "selection_reason": candidate.get(
                "selection_reason", "provided selection manifest"
            ),
            "exceeds_smoke_limits": bool(candidate.get(
                "exceeds_smoke_limits",
                candidate["n"] > max_n or candidate["m"] > max_m,
            )),
            "smoke_limits": {"max_n": max_n, "max_m": max_m},
        })
    return {
        "name": f"smoke{count}",
        "selection_policy": SELECTION_POLICY,
        "smoke_limits": {"max_n": max_n, "max_m": max_m},
        "source_root": source_label,
        "source_dataset_fingerprint": _dataset_fingerprint(source),
        "selected_instance_count": len(rows),
        "instances": rows,
    }


def _print_selection(rows: list[dict[str, Any]], count: int) -> None:
    print(f"Smoke dataset: {count} instances")
    print()
    print(f"{'Family':<31} {'Instance':<44} {'n':>5} {'m':>5}")
    print("-" * 89)
    for row in rows:
        print(f"{row['family']:<31} {row['source']:<44} "
              f"{row['n']:>5} {row['m']:>5}")


def materialize(source: Path, output: Path, count: int,
                manifest_path: Path | None = None,
                max_n: int = 50, max_m: int = 25) -> dict[str, Any]:
    source = source.resolve()
    output = output.resolve()
    previous_manifest = output / "manifest.json"
    if previous_manifest.is_file():
        try:
            previous = read_manifest(previous_manifest)
        except (OSError, ValueError, json.JSONDecodeError):
            previous = {}
        for row in previous.get("instances", []):
            relative = row.get("materialized_file") if isinstance(row, dict) else None
            if isinstance(relative, str):
                prior_file = (output / relative).resolve()
                if prior_file.is_relative_to((output / "instances").resolve()):
                    prior_file.unlink(missing_ok=True)
    if manifest_path is None:
        candidates, _errors = discover_candidates(source)
        selected = select_candidates(candidates, count, max_n, max_m)
        manifest = make_manifest(source, selected, count, max_n, max_m)
        payloads = [candidate["_payload"] for candidate in selected]
    else:
        manifest = read_manifest(manifest_path)
        rows = manifest["instances"]
        if len(rows) != count:
            raise ValueError(
                f"manifest selects {len(rows)} instances, expected {count}"
            )
        payloads = []
        selected = []
        seen_sources: set[str] = set()
        for row in rows:
            if not isinstance(row, dict):
                raise ValueError("manifest instances must be objects")
            source_key = str(row.get("source", row.get("relative_source", "")))
            if not source_key or source_key in seen_sources:
                raise ValueError("manifest contains a missing or duplicate source")
            seen_sources.add(source_key)
            path = _resolve_manifest_entry(
                row, _manifest_source_root(manifest, manifest_path),
                manifest_path,
            )
            expected_sha = row.get("sha256")
            if expected_sha and hashlib.sha256(path.read_bytes()).hexdigest() != expected_sha:
                materialized_sha = row.get("materialized_sha256")
                if (not materialized_sha or hashlib.sha256(
                        path.read_bytes()
                ).hexdigest() != materialized_sha):
                    raise ValueError(f"source checksum mismatch for {source_key}")
            payload = _canonical_payload(path)
            n, m = validate_instance(payload)
            if n != row.get("n") or m != row.get("m"):
                raise ValueError(f"manifest metadata mismatch for {source_key}")
            payloads.append(payload)
            selected.append({
                "family": row["family"], "source": source_key,
                "n": n, "m": m,
            })

    instance_dir = output / "instances"
    instance_dir.mkdir(parents=True, exist_ok=True)
    for row, payload in zip(manifest["instances"], payloads):
        family = str(row["family"])
        source_name = Path(str(row.get("relative_source")
                               or row["source"])).stem
        safe_family = re.sub(r"[^A-Za-z0-9._-]+", "_", family)
        destination_dir = instance_dir / safe_family
        destination_dir.mkdir(parents=True, exist_ok=True)
        destination = destination_dir / f"{source_name}.json"
        if destination.exists():
            rank = int(row.get("selection_rank", 0))
            destination = destination_dir / f"{source_name}_{rank}.json"
        destination.write_text(
            json.dumps(payload, indent=2) + "\n", encoding="utf-8"
        )
        row["materialized_file"] = destination.relative_to(output).as_posix()
        row["materialized_sha256"] = hashlib.sha256(
            destination.read_bytes()
        ).hexdigest()
    manifest_path_out = output / "manifest.json"
    manifest_path_out.write_text(
        json.dumps(manifest, indent=2) + "\n", encoding="utf-8"
    )
    if manifest_path is None:
        manifest["_selected"] = selected
    _print_selection(manifest["instances"], count)
    return manifest


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", required=True,
                        help="dataset root with one subdirectory per family")
    parser.add_argument("--output", required=True,
                        help="output directory for selected canonical JSON")
    parser.add_argument("--count", type=int, default=10)
    parser.add_argument("--max-n", type=int, default=50)
    parser.add_argument("--max-m", type=int, default=25)
    parser.add_argument("--manifest",
                        help="reuse a checked-in selection manifest")
    parser.add_argument("--manifest-output",
                        help="also write the generated manifest to this path")
    args = parser.parse_args(argv)
    try:
        manifest = materialize(
            Path(args.source), Path(args.output), args.count,
            Path(args.manifest) if args.manifest else None,
            args.max_n, args.max_m,
        )
        if args.manifest_output:
            destination = Path(args.manifest_output)
            destination.parent.mkdir(parents=True, exist_ok=True)
            manifest.pop("_selected", None)
            for row in manifest["instances"]:
                row.pop("materialized_file", None)
                row.pop("materialized_sha256", None)
            destination.write_text(
                json.dumps(manifest, indent=2) + "\n", encoding="utf-8"
            )
    except (OSError, ValueError, json.JSONDecodeError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
