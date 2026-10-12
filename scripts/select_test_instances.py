#!/usr/bin/env python3
"""Select and materialize a deterministic set of small valid instances."""

from __future__ import annotations

import argparse
import datetime as dt
import hashlib
import json
import math
import os
from pathlib import Path
import re
import shutil
import sys
from typing import Any
from collections import Counter, defaultdict

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from kdc_tools.schemas import dumps_json, loads_json, validate_document

SUPPORTED_SUFFIXES = {".mdc", ".json"}
SELECTOR_VERSION = "2.0"
POLICY_VERSION = "smoke-family-balanced-v1"
FAMILY_CLASSIFICATION_RULE = (
    "top-level directory name when files are grouped by directory; otherwise "
    "known public filename prefixes (us-night, euro-night, london, paris, "
    "stars, uniform, sbgdb date/type, many_holes), then normalized filename stem"
)
SELECTION_POLICY = (
    "eligible valid unique instances only; choose one smallest candidate per "
    "family first, then round-robin across represented families in deterministic "
    "size/path order; reject the selection if the requested count cannot be met"
)
SMOKE_PROFILE = "smoke20"


class SelectionShortageError(ValueError):
    """Requested selection cannot be met without violating configured limits."""


def _json_bytes(payload: dict[str, Any]) -> bytes:
    return (json.dumps(
        payload, indent=2, ensure_ascii=False, allow_nan=False
    ) + "\n").encode("utf-8")


def _number(value: Any, context: str) -> float:
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise ValueError(f"{context} must be a number")
    result = float(value)
    if not math.isfinite(result):
        raise ValueError(f"{context} must be finite")
    return result


def _canonical_payload(source: Path) -> dict[str, Any]:
    payload = loads_json(source.read_bytes())
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
        return str(source.resolve())


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


def family_classification(path: Path, directory_family: str | None) -> str:
    return directory_family or _flat_family(path)


def _logical_instance_id(payload: dict[str, Any]) -> str:
    identity = {
        "T_end": float(payload["T_end"]),
        "stations": [
            [float(station["x"]), float(station["y"])]
            for station in payload["stations"]
        ],
        "trajectories": [
            {
                "t_breaks": [float(value) for value in trajectory["t_breaks"]],
                "waypoints": [
                    [float(point["x"]), float(point["y"])]
                    for point in trajectory["waypoints"]
                ],
            }
            for trajectory in payload["trajectories"]
        ],
    }
    return hashlib.sha256(dumps_json(identity).encode("utf-8")).hexdigest()


def _canonical_relative_path(family: str, source: Path,
                             source_sha256: str) -> str:
    safe_family = re.sub(r"[^A-Za-z0-9._-]+", "_", family).strip("._-")
    safe_stem = re.sub(r"[^A-Za-z0-9._-]+", "_", source.stem).strip("._-")
    return (
        Path("instances") / (safe_family or "unknown")
        / f"{safe_stem or 'instance'}-{source_sha256[:12]}.json"
    ).as_posix()


def discover_candidates(source: Path) -> tuple[list[dict[str, Any]], list[dict[str, Any]]]:
    if not source.is_dir():
        raise ValueError(f"source dataset directory not found: {source}")
    candidates: list[dict[str, Any]] = []
    rejected: list[dict[str, Any]] = []
    seen_logical: dict[str, str] = {}
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
            family = family_classification(path, directory_family)
            relative_path = path.relative_to(source).as_posix()
            try:
                payload = _canonical_payload(path)
                n, m = validate_instance(payload)
            except (OSError, ValueError, json.JSONDecodeError) as error:
                rejected.append({
                    "source": _source_path(source, source_label, relative_path),
                    "relative_source": relative_path,
                    "reason_code": "invalid_input",
                    "reason": {"message": str(error)},
                })
                continue
            source_sha256 = hashlib.sha256(path.read_bytes()).hexdigest()
            logical_id = _logical_instance_id(payload)
            if logical_id in seen_logical:
                rejected.append({
                    "source": _source_path(source, source_label, relative_path),
                    "relative_source": relative_path,
                    "reason_code": "duplicate_logical_instance",
                    "reason": {"duplicate_of": seen_logical[logical_id]},
                    "instance_id": logical_id,
                })
                continue
            seen_logical[logical_id] = relative_path
            t_end = float(payload["T_end"])
            canonical_path = _canonical_relative_path(
                family, path, source_sha256
            )
            candidates.append({
                "instance_id": logical_id,
                "family": family,
                "family_classification_rule": (
                    "top_level_directory" if directory_family
                    else "filename_prefix_or_normalized_stem"
                ),
                "source": _source_path(source, source_label, relative_path),
                "relative_source": relative_path,
                "source_sha256": source_sha256,
                "n": n,
                "m": m,
                "T_end": t_end,
                "file_size_bytes": path.stat().st_size,
                "canonical_path": canonical_path,
                "canonical_sha256": hashlib.sha256(
                    _json_bytes(payload)
                ).hexdigest(),
                "_absolute_source": path,
                "_payload": payload,
            })
    candidates.sort(key=selection_key)
    return candidates, rejected


def selection_key(candidate: dict[str, Any]) -> tuple[Any, ...]:
    return (
        candidate["n"],
        candidate["m"],
        candidate["n"] * candidate["m"],
        candidate["file_size_bytes"],
        candidate["relative_source"],
    )


def _seeded_selection_key(candidate: dict[str, Any],
                          seed: int) -> tuple[Any, ...]:
    return (
        candidate["n"],
        candidate["m"],
        candidate["n"] * candidate["m"],
        candidate["file_size_bytes"],
        hashlib.sha256(
            f"{seed}:{candidate['relative_source']}".encode("utf-8")
        ).hexdigest(),
        candidate["relative_source"],
    )


def select_candidates(candidates: list[dict[str, Any]], count: int,
                      max_n: int = 50, max_m: int = 25,
                      seed: int = 0,
                      rejected: list[dict[str, Any]] | None = None,
                      ) -> list[dict[str, Any]]:
    if count <= 0 or max_n <= 0 or max_m <= 0:
        raise ValueError("count and smoke limits must be positive")
    eligible = [
        row for row in candidates
        if row["n"] <= max_n and row["m"] <= max_m
    ]
    oversized = [
        row for row in candidates
        if row["n"] > max_n or row["m"] > max_m
    ]
    for candidate in oversized:
        if rejected is not None:
            rejected.append({
                "source": candidate.get(
                    "source", candidate["relative_source"]
                ),
                "relative_source": candidate["relative_source"],
                "reason_code": "size_limit_exceeded",
                "reason": {
                    "n": candidate["n"], "m": candidate["m"],
                    "max_n": max_n, "max_m": max_m,
                },
            })
    if len(eligible) < count:
        shortage = count - len(eligible)
        counts = Counter(
            item["reason_code"] for item in (rejected or [])
        )
        details = ", ".join(
            f"{reason}={amount}" for reason, amount in sorted(counts.items())
        ) or "no rejected candidates"
        raise SelectionShortageError(
            f"requested {count} smoke instances with n<={max_n}, m<={max_m}; "
            f"only {len(eligible)} valid distinct in-limit instances are available "
            f"(shortage {shortage}; rejected: {details})"
        )
    by_family: dict[str, list[dict[str, Any]]] = defaultdict(list)
    for candidate in eligible:
        by_family[candidate["family"]].append(candidate)
    for family_candidates in by_family.values():
        family_candidates.sort(
            key=lambda candidate: _seeded_selection_key(candidate, seed)
        )

    selected: list[dict[str, Any]] = []
    families = sorted(
        by_family,
        key=lambda family: (
            _seeded_selection_key(by_family[family][0], seed), family
        ),
    )
    for family in families:
        if len(selected) == count:
            break
        candidate = by_family[family][0]
        candidate["selection_reason"] = (
            "first eligible candidate from family, selected for broad family coverage"
        )
        selected.append(candidate)

    rank_by_family = Counter(row["family"] for row in selected)
    while len(selected) < count:
        for family in families:
            if len(selected) == count:
                break
            candidates_for_family = by_family[family]
            if rank_by_family[family] >= len(candidates_for_family):
                continue
            candidate = candidates_for_family[rank_by_family[family]]
            candidate["selection_reason"] = (
                "deterministic round-robin fill to balance family representation"
            )
            selected.append(candidate)
            rank_by_family[family] += 1
    selected_sources = {row["relative_source"] for row in selected}
    for rank, candidate in enumerate(selected, 1):
        candidate["selection_rank"] = rank
        candidate["exceeds_size_limits"] = False
        candidate["selection_limits"] = {"max_n": max_n, "max_m": max_m}
    if rejected is not None:
        for candidate in eligible:
            if candidate["relative_source"] not in selected_sources:
                rejected.append({
                    "source": candidate["source"],
                    "relative_source": candidate["relative_source"],
                    "reason_code": "valid_candidate_not_selected",
                    "reason": {
                        "message": "selection count reached after deterministic family-balanced ordering",
                    },
                })
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
    manifest = loads_json(path.read_bytes())
    if not isinstance(manifest, dict) or not isinstance(
        manifest.get("instances"), list
    ):
        raise ValueError("selection manifest must contain an instances array")
    name = manifest.get("name", manifest.get("profile"))
    if name == "smoke10":
        return manifest
    if name == SMOKE_PROFILE:
        validate_document(manifest, "dataset_selection")
    return manifest


def _validate_selection_identity(manifest: dict[str, Any], count: int,
                                 max_n: int, max_m: int) -> None:
    name = manifest.get("name", manifest.get("profile"))
    if name == "smoke10":
        raise ValueError(
            "legacy smoke10 manifests are readable for compatibility but "
            "cannot be used to create a smoke20 experiment; regenerate selection evidence"
        )
    if name != SMOKE_PROFILE or manifest.get("selected_instance_count") != count:
        raise ValueError(
            f"selection manifest must identify {SMOKE_PROFILE} with exactly "
            f"{count} instances"
        )
    validate_document(manifest, "dataset_selection")
    limits = manifest.get("size_limits", {})
    if limits != {"max_n": max_n, "max_m": max_m}:
        raise ValueError("selection manifest size limits differ from requested profile")
    rows = manifest.get("instances")
    if not isinstance(rows, list) or len(rows) != count:
        raise ValueError("selection manifest has an invalid instance array")
    for key in ("instance_id", "source", "canonical_path"):
        values = [row.get(key) for row in rows if isinstance(row, dict)]
        if len(values) != count or any(not isinstance(value, str) for value in values):
            raise ValueError(f"selection manifest has missing {key} values")
        if len(set(values)) != count:
            raise ValueError(f"selection manifest has duplicate {key} values")


def resolve_instance_directory(path: Path) -> Path:
    if ((path / "manifest.json").is_file()
            and (path / "instances").is_dir()):
        manifest = read_manifest(path / "manifest.json")
        expected_name = manifest.get("name", manifest.get("profile"))
        if expected_name == "smoke10":
            raise ValueError(
                "legacy smoke10 materializations are not valid smoke20 inputs"
            )
        rows = manifest["instances"]
        expected = manifest.get("selected_instance_count", len(rows))
        if expected_name == SMOKE_PROFILE and expected != 20:
            raise ValueError(
                f"smoke20 materialization must contain exactly 20 instances, got {expected}"
            )
        if len(rows) != expected:
            raise ValueError(
                f"dataset manifest expects {expected} instances, has {len(rows)}"
            )
        seen_sources: set[str] = set()
        seen_instances: set[str] = set()
        instance_root = (path / "instances").resolve()
        for row in rows:
            if not isinstance(row, dict):
                raise ValueError("dataset manifest instances must be objects")
            source = str(row.get("source", ""))
            instance_id = str(row.get("instance_id", source))
            materialized = row.get("materialized_file", row.get("canonical_path"))
            if not source or source in seen_sources or instance_id in seen_instances:
                raise ValueError("dataset manifest has a missing or duplicate source")
            seen_sources.add(source)
            seen_instances.add(instance_id)
            if not isinstance(materialized, str):
                raise ValueError("dataset manifest lacks materialized_file")
            instance_path = (path / materialized).resolve()
            if not instance_path.is_relative_to(instance_root):
                raise ValueError(
                    f"manifest materialized path escapes instances directory: {materialized}"
                )
            if not instance_path.is_file():
                raise ValueError(f"materialized instance not found: {materialized}")
            expected_sha = row.get(
                "canonical_sha256", row.get("materialized_sha256")
            )
            if (expected_sha and hashlib.sha256(
                    instance_path.read_bytes()
            ).hexdigest() != expected_sha):
                raise ValueError(
                    f"materialized instance checksum mismatch: {materialized}"
                )
            payload = _canonical_payload(instance_path)
            n, m = validate_instance(payload)
            if _logical_instance_id(payload) != instance_id:
                raise ValueError(
                    f"materialized logical identity mismatch: {materialized}"
                )
            if (n != row.get("n") or m != row.get("m")
                    or not math.isclose(
                        float(payload["T_end"]), float(row.get("T_end", 1.0)),
                        rel_tol=1e-9, abs_tol=1e-9,
                    )):
                raise ValueError(
                    f"materialized instance metadata mismatch: {materialized}"
                )
            source_root = _manifest_source_root(manifest, path / "manifest.json")
            source_path = source_root / str(row.get("relative_source", ""))
            expected_source_sha = row.get("source_sha256", row.get("sha256"))
            if (expected_source_sha and not source_path.is_file()):
                raise ValueError(f"original source file not found: {source}")
            if (expected_source_sha and hashlib.sha256(
                    source_path.read_bytes()
            ).hexdigest() != expected_source_sha):
                raise ValueError(f"source checksum mismatch: {source}")
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
    smoke = args.dataset_profile == SMOKE_PROFILE
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
                  max_m: int = 25,
                  rejected: list[dict[str, Any]] | None = None,
                  seed: int = 0,
                  available_eligible_families: int | None = None,
                  ) -> dict[str, Any]:
    source_label = _root_label(source)
    rows = []
    for rank, candidate in enumerate(selected, 1):
        path = candidate["_absolute_source"]
        payload = candidate["_payload"]
        source_sha = hashlib.sha256(path.read_bytes()).hexdigest()
        canonical_bytes = _json_bytes(payload)
        canonical_path = candidate.get(
            "canonical_path",
            _canonical_relative_path(candidate["family"], path, source_sha),
        )
        rows.append({
            "instance_id": candidate.get(
                "instance_id", _logical_instance_id(payload)
            ),
            "family": candidate["family"],
            "family_classification_rule": candidate.get(
                "family_classification_rule",
                "top_level_directory_or_normalized_filename",
            ),
            "source": candidate["source"],
            "relative_source": candidate["relative_source"],
            "source_sha256": source_sha,
            "canonical_path": canonical_path,
            "canonical_sha256": hashlib.sha256(canonical_bytes).hexdigest(),
            "n": candidate["n"],
            "m": candidate["m"],
            "T_end": float(payload["T_end"]),
            "file_size_bytes": candidate["file_size_bytes"],
            "selection_rank": rank,
            "selection_reason": candidate.get(
                "selection_reason", "deterministic eligible family-balanced selection"
            ),
            "size_limit_exception": None,
        })
    family_counts = Counter(row["family"] for row in rows)
    manifest_name = SMOKE_PROFILE if count == 20 else f"selection{count}"
    manifest = {
        "schema_version": 1,
        "created_at": dt.datetime.now(dt.timezone.utc).isoformat(),
        "name": manifest_name,
        "profile": manifest_name,
        "policy_version": POLICY_VERSION,
        "selector_version": SELECTOR_VERSION,
        "selection_policy": SELECTION_POLICY,
        "policy_parameters": {
            "seed": seed,
            "ordering": "n,m,n*m,file_size_bytes,relative_source",
            "family_fill": "round_robin",
        },
        "size_limits": {"max_n": max_n, "max_m": max_m},
        "count": count,
        "source_root": source_label,
        "source_dataset_fingerprint": _dataset_fingerprint(source),
        "selected_instance_count": len(rows),
        "family_coverage": {
            "available_eligible_families": (
                available_eligible_families
                if available_eligible_families is not None
                else len({candidate["family"] for candidate in selected})
            ),
            "represented_families": sorted(family_counts),
            "selected_counts": dict(sorted(family_counts.items())),
        },
        "classification_rule": FAMILY_CLASSIFICATION_RULE,
        "instances": rows,
        "rejected_candidates": rejected or [],
    }
    if count == 20:
        validate_document(manifest, "dataset_selection")
    return manifest


def _write_selection_report(manifest: dict[str, Any], output: Path) -> Path:
    rows = manifest["instances"]
    report = [
        "# Deterministic smoke20 selection", "",
        f"- Policy: `{manifest['policy_version']}`",
        f"- Source fingerprint: `{manifest['source_dataset_fingerprint']}`",
        f"- Selected: {len(rows)} distinct logical instances",
        f"- Represented families: {len(manifest['family_coverage']['represented_families'])}",
        f"- Rejected candidates: {len(manifest['rejected_candidates'])}",
        "",
        "| Rank | Instance ID | Source | Family | n | m | T_end | Selection reason |",
        "|---:|---|---|---|---:|---:|---:|---|",
    ]
    for row in rows:
        report.append(
            f"| {row['selection_rank']} | `{row['instance_id']}` "
            f"| `{row['source']}` | {row['family']} | {row['n']} | {row['m']} | {row['T_end']:g} "
            f"| {row['selection_reason']} |"
        )
    path = output / "selection_report.md"
    path.write_text("\n".join(report) + "\n", encoding="utf-8")
    return path


def _print_selection(rows: list[dict[str, Any]], count: int) -> None:
    print(f"{SMOKE_PROFILE} dataset: {count} instances")
    print()
    print(f"{'Family':<31} {'Instance':<44} {'n':>5} {'m':>5}")
    print("-" * 89)
    for row in rows:
        print(f"{row['family']:<31} {row['source']:<44} "
              f"{row['n']:>5} {row['m']:>5}")


def materialize(source: Path, output: Path, count: int = 20,
                manifest_path: Path | None = None,
                max_n: int = 50, max_m: int = 25,
                seed: int = 0) -> dict[str, Any]:
    source = source.resolve()
    output = output.resolve()
    if output.exists() and any(output.iterdir()):
        raise ValueError(
            f"refusing to overwrite non-empty selection output directory: {output}"
        )
    if manifest_path is None:
        candidates, rejected = discover_candidates(source)
        selected = select_candidates(
            candidates, count, max_n, max_m, seed=seed, rejected=rejected
        )
        manifest = make_manifest(
            source, selected, count, max_n, max_m, rejected, seed,
            len({row["family"] for row in candidates
                 if row["n"] <= max_n and row["m"] <= max_m}),
        )
        payloads = [candidate["_payload"] for candidate in selected]
    else:
        manifest = read_manifest(manifest_path)
        _validate_selection_identity(manifest, count, max_n, max_m)
        if manifest["policy_parameters"]["seed"] != seed:
            raise ValueError("selection manifest seed differs from requested profile")
        if manifest["source_dataset_fingerprint"] != _dataset_fingerprint(source):
            raise ValueError("selection manifest source dataset fingerprint mismatch")
        rows = manifest["instances"]
        if len(rows) != count:
            raise ValueError(
                f"manifest selects {len(rows)} instances, expected {count}"
            )
        payloads = []
        selected = []
        seen_sources: set[str] = set()
        seen_instances: set[str] = set()
        for row in rows:
            if not isinstance(row, dict):
                raise ValueError("manifest instances must be objects")
            source_key = str(row.get("source", row.get("relative_source", "")))
            if not source_key or source_key in seen_sources:
                raise ValueError("manifest contains a missing or duplicate source")
            seen_sources.add(source_key)
            instance_id = row.get("instance_id")
            if not isinstance(instance_id, str) or instance_id in seen_instances:
                raise ValueError("manifest contains a missing or duplicate logical instance")
            seen_instances.add(instance_id)
            path = _resolve_manifest_entry(
                row, _manifest_source_root(manifest, manifest_path),
                manifest_path,
            )
            expected_sha = row.get("sha256")
            expected_sha = row.get("source_sha256", expected_sha)
            if expected_sha and hashlib.sha256(path.read_bytes()).hexdigest() != expected_sha:
                materialized_sha = row.get("materialized_sha256")
                if (not materialized_sha or hashlib.sha256(
                        path.read_bytes()
                ).hexdigest() != materialized_sha):
                    raise ValueError(f"source checksum mismatch for {source_key}")
            payload = _canonical_payload(path)
            n, m = validate_instance(payload)
            if _logical_instance_id(payload) != instance_id:
                raise ValueError(
                    f"materialized logical identity mismatch: {materialized}"
                )
            canonical_hash = hashlib.sha256(_json_bytes(payload)).hexdigest()
            if (n != row.get("n") or m != row.get("m")
                    or not math.isclose(
                        float(payload["T_end"]), float(row["T_end"]),
                        rel_tol=1e-9, abs_tol=1e-9,
                    )
                    or canonical_hash != row.get("canonical_sha256")):
                raise ValueError(f"manifest metadata mismatch for {source_key}")
            payloads.append(payload)
            selected.append({
                "family": row["family"], "source": source_key,
                "n": n, "m": m,
            })

    instance_dir = output / "instances"
    instance_dir.mkdir(parents=True, exist_ok=True)
    for row, payload in zip(manifest["instances"], payloads):
        destination = output / str(row["canonical_path"])
        if not destination.resolve().is_relative_to(instance_dir.resolve()):
            raise ValueError(f"canonical path escapes instances directory: {destination}")
        if destination.exists():
            raise ValueError(f"refusing to overwrite canonical input: {destination}")
        destination.parent.mkdir(parents=True, exist_ok=True)
        canonical_bytes = _json_bytes(payload)
        destination.write_bytes(canonical_bytes)
        canonical_payload = loads_json(destination.read_bytes())
        n, m = validate_instance(canonical_payload)
        if (n != row["n"] or m != row["m"]
                or not math.isclose(
                    float(canonical_payload["T_end"]), float(row["T_end"]),
                    rel_tol=1e-9, abs_tol=1e-9,
                )):
            raise ValueError(f"canonical metadata mismatch: {destination}")
        actual_hash = hashlib.sha256(destination.read_bytes()).hexdigest()
        if actual_hash != row["canonical_sha256"]:
            raise ValueError(f"canonical checksum mismatch: {destination}")
        row["materialized_file"] = destination.relative_to(output).as_posix()
        row["materialized_sha256"] = actual_hash
    manifest_path_out = output / "manifest.json"
    manifest_path_out.write_text(
        json.dumps(manifest, indent=2) + "\n", encoding="utf-8"
    )
    if count == 20:
        validate_document(manifest, "dataset_selection")
    if (len(manifest["instances"]) != count
            or len(list(instance_dir.rglob("*.json"))) != count
            or len({row["instance_id"] for row in manifest["instances"]}) != count):
        raise ValueError("materialized smoke20 manifest and canonical input counts differ")
    _write_selection_report(manifest, output)
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
    parser.add_argument("--count", type=int, default=20)
    parser.add_argument("--max-n", type=int, default=50)
    parser.add_argument("--max-m", type=int, default=25)
    parser.add_argument("--seed", type=int, default=0,
                        help="deterministic selector tie-break seed")
    parser.add_argument("--manifest",
                        help="reuse a checked-in selection manifest")
    parser.add_argument("--manifest-output",
                        help="also write the generated manifest to this path")
    parser.add_argument("--profile", choices=(SMOKE_PROFILE,),
                        default=SMOKE_PROFILE)
    args = parser.parse_args(argv)
    try:
        manifest = materialize(
            Path(args.source), Path(args.output), args.count,
            Path(args.manifest) if args.manifest else None,
            args.max_n, args.max_m, args.seed,
        )
        if args.manifest_output:
            destination = Path(args.manifest_output)
            destination.parent.mkdir(parents=True, exist_ok=True)
            destination.write_text(
                json.dumps(
                    {key: value for key, value in manifest.items()
                     if not key.startswith("_")},
                    indent=2,
                ) + "\n",
                encoding="utf-8",
            )
    except (OSError, ValueError, json.JSONDecodeError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
