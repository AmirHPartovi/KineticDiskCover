#!/usr/bin/env python3
"""State, integrity reports, summaries, and exact-animation gating for experiments."""

from __future__ import annotations

import argparse
import datetime as dt
import hashlib
import json
import math
import os
from pathlib import Path
import platform
import shutil
import statistics
import subprocess
import sys
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
EXACT_STATUSES = {"OPTIMAL"}


def sha256_files(directory: Path, suffixes: set[str]) -> str:
    digest = hashlib.sha256()
    files = sorted(
        path for path in directory.rglob("*")
        if path.is_file() and path.suffix.lower() in suffixes
    )
    for path in files:
        digest.update(path.relative_to(directory).as_posix().encode())
        digest.update(b"\0")
        with path.open("rb") as stream:
            for chunk in iter(lambda: stream.read(1024 * 1024), b""):
                digest.update(chunk)
        digest.update(b"\0")
    return digest.hexdigest()


def json_files(directory: Path) -> list[Path]:
    return sorted(directory.rglob("*.json"))


def load_json(path: Path) -> Any:
    with path.open(encoding="utf-8") as source:
        return json.load(source)


def write_json(path: Path, payload: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")
    temporary.replace(path)


def relpath(path: Path) -> str:
    try:
        return path.resolve().relative_to(ROOT).as_posix()
    except ValueError:
        return path.resolve().as_posix()


def command_init(args: argparse.Namespace) -> None:
    experiment = Path(args.experiment).resolve()
    manifest_path = experiment / "experiment_manifest.json"
    selection_manifest = None
    selection_manifest_path = (
        Path(args.selection_manifest).resolve()
        if args.selection_manifest else None
    )
    if selection_manifest_path is not None:
        selection_manifest = load_json(selection_manifest_path)
        if not isinstance(selection_manifest, dict) or not isinstance(
            selection_manifest.get("instances"), list
        ):
            raise ValueError("selection manifest must contain an instances array")
    selected_files = [
        {
            "family": row.get("family"),
            "source": row.get("source"),
            "n": row.get("n"),
            "m": row.get("m"),
            "selection_rank": row.get("selection_rank"),
        }
        for row in (selection_manifest or {}).get("instances", [])
    ]
    if args.resume:
        if not manifest_path.is_file():
            raise ValueError(f"cannot resume: missing {manifest_path}")
        manifest = load_json(manifest_path)
        if manifest.get("dataset_source_fingerprint") != args.dataset_fingerprint:
            raise ValueError("resume refused: source dataset fingerprint changed")
        expected = {
            "algorithm_list": args.algorithms,
            "objectives": ["minmax", "minsum"] if args.modes == "both"
            else [args.modes],
            "seed": args.seed,
            "repeats": args.repeats,
            "thread_count": args.threads,
            "fast_time_limit_sec": args.fast_limit,
            "exact_time_limit_sec": args.exact_limit,
            "per_static_time_limit_sec": args.static_limit,
            "requested_exact_backend": args.exact_reference,
            "animation_top_n": args.animation_top_n,
            "animation_mode": args.animation_mode,
            "animation_instances_requested": args.animation_instances,
            "dataset_profile": args.dataset_profile,
            "selection_manifest_fingerprint": args.selection_fingerprint,
        }
        changed = [key for key, value in expected.items()
                   if manifest.get(key) != value]
        if changed:
            raise ValueError(
                "resume refused: experiment configuration differs for "
                + ", ".join(changed)
            )
        return

    manifest = {
        "experiment_id": experiment.name,
        "timestamp": dt.datetime.now(dt.timezone.utc).isoformat(),
        "repository": "AmirHPartovi/KineticDiskCover",
        "git_commit": subprocess.run(
            ["git", "rev-parse", "HEAD"], cwd=ROOT, check=True,
            text=True, capture_output=True,
        ).stdout.strip(),
        "git_dirty": bool(subprocess.run(
            ["git", "status", "--porcelain"], cwd=ROOT, check=True,
            text=True, capture_output=True,
        ).stdout.strip()),
        "dataset_source": args.dataset_label,
        "dataset_source_fingerprint": args.dataset_fingerprint,
        "source_dataset_fingerprint": args.source_dataset_fingerprint,
        "dataset_profile": args.dataset_profile,
        "selection_manifest": args.selection_manifest or None,
        "selected_instance_count": len(selected_files) or None,
        "selection_policy": (selection_manifest or {}).get(
            "selection_policy"
        ),
        "selection_manifest_fingerprint": args.selection_fingerprint,
        "selected_files": selected_files,
        "dataset_canonical_path": "dataset",
        "instance_count": None,
        "algorithm_list": args.algorithms,
        "algorithm_set": [],
        "objectives": ["minmax", "minsum"] if args.modes == "both"
        else [args.modes],
        "profile": "fast",
        "seed": args.seed,
        "repeats": args.repeats,
        "thread_count": args.threads,
        "fast_time_limit_sec": args.fast_limit,
        "exact_time_limit_sec": args.exact_limit,
        "per_static_time_limit_sec": args.static_limit,
        "requested_exact_backend": args.exact_reference,
        "animation_top_n": args.animation_top_n,
        "animation_mode": args.animation_mode,
        "animation_instances_requested": args.animation_instances,
        "selected_exact_backend": None,
        "actual_exact_backend": None,
        "exact_reference_calibration": None,
        "animation_instance_selection": [],
        "python_version": platform.python_version(),
        "compiler": None,
        "build_type": None,
        "host": {
            "platform": platform.platform(),
            "machine": platform.machine(),
            "logical_cpu_count": os.cpu_count(),
        },
        "commands": [],
        "stages": {},
        "status": "RUNNING",
        "reproduction_command": args.reproduction_command,
    }
    write_json(manifest_path, manifest)


def command_stage(args: argparse.Namespace) -> None:
    path = Path(args.experiment).resolve() / "experiment_manifest.json"
    manifest = load_json(path)
    stage = manifest.setdefault("stages", {}).setdefault(args.name, {})
    stage.update({
        "status": args.status.upper(),
        "updated_at": dt.datetime.now(dt.timezone.utc).isoformat(),
    })
    if args.command:
        stage["command"] = args.command
        manifest.setdefault("commands", []).append({
            "stage": args.name,
            "command": args.command,
            "recorded_at": dt.datetime.now(dt.timezone.utc).isoformat(),
        })
    if args.exit_code is not None:
        stage["exit_code"] = args.exit_code
    if args.message:
        stage["message"] = args.message
    write_json(path, manifest)


def command_dataset(args: argparse.Namespace) -> None:
    exp = Path(args.experiment).resolve()
    source = Path(args.source).resolve()
    canonical = exp / "dataset"
    converted_legacy = False
    if args.resume and canonical.is_dir():
        if sha256_files(canonical, {".json"}) == args.canonical_fingerprint:
            return
        shutil.rmtree(canonical)
    canonical.mkdir(parents=True, exist_ok=True)
    if json_files(canonical):
        pass
    if source.is_file():
        if source.suffix.lower() != ".json":
            raise ValueError("a dataset file must be canonical .json")
        payload = load_json(source)
        if isinstance(payload, dict) and isinstance(
            payload.get("instances"), list
        ):
            converted_legacy = any(
                Path(str(row.get("relative_source", row.get("source", ""))))
                .suffix.lower() == ".mdc"
                for row in payload["instances"] if isinstance(row, dict)
            )
            _materialize_selection_manifest(source, payload, canonical)
        else:
            shutil.copy2(source, canonical / source.name)
    else:
        source_manifest_path = source / "manifest.json"
        if source_manifest_path.is_file():
            selection = load_json(source_manifest_path)
            if not isinstance(selection, dict) or not isinstance(
                selection.get("instances"), list
            ):
                raise ValueError("dataset manifest must contain an instances array")
            _copy_materialized_selection(source, selection, canonical)
            source = source / "instances"
        mdc = sorted(source.rglob("*.mdc"))
        if json_files(canonical):
            pass
        elif mdc:
            if json_files(source):
                raise ValueError(
                    "dataset contains both .mdc and .json; select one canonical format"
                )
            command = [
                sys.executable, str(ROOT / "scripts" / "prepare_real_instances.py"),
                "--input", str(source), "--output", str(canonical),
            ]
            subprocess.run(command, cwd=ROOT, check=True)
            converted_legacy = True
        else:
            files = json_files(source)
            if not files:
                raise ValueError(f"dataset has no .mdc or .json instances: {source}")
            for original in files:
                target = canonical / original.relative_to(source)
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(original, target)
    files = json_files(canonical)
    if not files:
        raise ValueError("canonical dataset contains no JSON instances")
    fingerprint = sha256_files(canonical, {".json"})
    manifest_path = exp / "experiment_manifest.json"
    manifest = load_json(manifest_path)
    manifest.update({
        "dataset_canonical_path": "dataset",
        "dataset_fingerprint": fingerprint,
        "instance_count": len(files),
        "dataset_conversion": (
            "mdc-to-canonical-json" if converted_legacy else "none"
        ),
        "canonical_instance_files": [
            path.relative_to(canonical).as_posix() for path in files
        ],
        "selected_instance_count": len(files),
    })
    write_json(manifest_path, manifest)


def _selection_module():
    scripts_dir = str(ROOT / "scripts")
    if scripts_dir not in sys.path:
        sys.path.insert(0, scripts_dir)
    import select_test_instances

    return select_test_instances


def _write_canonical_payload(payload: dict[str, Any], destination: Path) -> None:
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_text(
        json.dumps(payload, indent=2) + "\n", encoding="utf-8"
    )


def _materialize_selection_manifest(manifest_path: Path,
                                    selection: dict[str, Any],
                                    canonical: Path) -> None:
    selector = _selection_module()
    source_root = selector._manifest_source_root(selection, manifest_path)
    rows = selection["instances"]
    expected = selection.get("selected_instance_count", len(rows))
    if len(rows) != expected:
        raise ValueError(
            f"selection manifest expects {expected} instances, has {len(rows)}"
        )
    seen_sources: set[str] = set()
    for row in rows:
        if not isinstance(row, dict):
            raise ValueError("selection manifest instances must be objects")
        source_name = str(row.get("source", row.get("relative_source", "")))
        if not source_name or source_name in seen_sources:
            raise ValueError("selection manifest has a missing or duplicate source")
        seen_sources.add(source_name)
        source = selector._resolve_manifest_entry(
            row, source_root, manifest_path
        )
        expected_sha = row.get("sha256")
        actual_sha = hashlib.sha256(source.read_bytes()).hexdigest()
        materialized_sha = row.get("materialized_sha256")
        if (expected_sha and actual_sha != expected_sha
                and actual_sha != materialized_sha):
            raise ValueError(f"selection source checksum mismatch: {row.get('source')}")
        payload = selector._canonical_payload(source)
        n, m = selector.validate_instance(payload)
        if n != row.get("n") or m != row.get("m"):
            raise ValueError(f"selection manifest metadata mismatch: {row.get('source')}")
        relative = Path(str(row.get("relative_source") or source.name))
        family = str(row.get("family", "instances"))
        destination = canonical / family / f"{relative.stem}.json"
        _write_canonical_payload(payload, destination)


def _copy_materialized_selection(source: Path,
                                 selection: dict[str, Any],
                                 canonical: Path) -> None:
    selector = _selection_module()
    rows = selection["instances"]
    expected = selection.get("selected_instance_count", len(rows))
    if len(rows) != expected:
        raise ValueError(
            f"materialized manifest expects {expected} instances, has {len(rows)}"
        )
    seen_sources: set[str] = set()
    for row in rows:
        if not isinstance(row, dict):
            raise ValueError("selection manifest instances must be objects")
        source_name = str(row.get("source", ""))
        if not source_name or source_name in seen_sources:
            raise ValueError("materialized manifest has a missing or duplicate source")
        seen_sources.add(source_name)
        materialized = row.get("materialized_file")
        if not isinstance(materialized, str):
            raise ValueError("materialized smoke manifest lacks materialized_file")
        path = source / materialized
        if not path.is_file():
            raise ValueError(f"materialized instance not found: {materialized}")
        payload = load_json(path)
        expected_sha = row.get("materialized_sha256")
        if expected_sha and hashlib.sha256(path.read_bytes()).hexdigest() != expected_sha:
            raise ValueError(f"materialized instance checksum mismatch: {materialized}")
        n, m = selector.validate_instance(payload)
        if n != row.get("n") or m != row.get("m"):
            raise ValueError(f"materialized instance metadata mismatch: {materialized}")
        relative = Path(materialized).relative_to("instances")
        destination = canonical / relative
        _write_canonical_payload(payload, destination)


def command_preflight_snapshot(args: argparse.Namespace) -> None:
    destination = Path(args.experiment).resolve() / "preflight"
    destination.mkdir(parents=True, exist_ok=True)
    source_dir = ROOT / "results" / "preflight"
    report = source_dir / "preflight_report.md"
    if not report.is_file():
        raise ValueError("preflight did not produce results/preflight/preflight_report.md")
    shutil.copy2(report, destination / report.name)
    summary = source_dir / "00_preflight_summary.md"
    if summary.is_file():
        shutil.copy2(summary, destination / summary.name)


def command_fingerprint(args: argparse.Namespace) -> None:
    path = Path(args.path).resolve()
    suffixes = {".json", ".mdc"}
    if path.is_file():
        digest = hashlib.sha256()
        digest.update(path.name.encode())
        digest.update(b"\0")
        with path.open("rb") as stream:
            for chunk in iter(lambda: stream.read(1024 * 1024), b""):
                digest.update(chunk)
        print(digest.hexdigest())
    elif path.is_dir():
        print(sha256_files(path, suffixes))
    else:
        raise ValueError(f"dataset path does not exist: {path}")


def bool_value(value: Any) -> bool:
    if isinstance(value, bool):
        return value
    return str(value).strip().lower() in {"true", "1", "yes"}


def status_value(record: dict[str, Any]) -> str:
    value = str(record.get("optimality_status", "")).upper()
    if value:
        return value
    if bool_value(record.get("timeout", False)):
        return "TIME_LIMIT"
    if bool_value(record.get("failed", False)):
        return "FAILED"
    return "FEASIBLE" if bool_value(record.get("feasible", False)) else "FAILED"


def command_validate(args: argparse.Namespace) -> None:
    exp = Path(args.experiment).resolve()
    source = Path(args.results)
    records = load_json(source)
    if not isinstance(records, list):
        raise ValueError("master_results.json must contain an array")
    records = [row for row in records if isinstance(row, dict)]
    manifest = load_json(exp / "batch" / "experiment_manifest.json")
    instances = int(manifest.get("dataset", {}).get("instance_count", 0) or 0)
    if not instances:
        instances = int(load_json(exp / "experiment_manifest.json").get("instance_count", 0))
    algorithms = manifest.get("algorithm_set", [])
    objectives = load_json(exp / "experiment_manifest.json").get("objectives", [])
    repeats = int(manifest.get("seed_policy", {}).get("repeats", 1))
    exact_algorithms = [str(row.get("algorithm_name", "")) for row in records
                        if row.get("algorithm_category") == "exact_reference"]
    nonexact_count = len([name for name in algorithms
                          if name not in {"ip-kont", "branch-and-bound"}])
    expected = instances * len(objectives) * (nonexact_count * repeats + 1)
    counts = {
        "total": len(records),
        "successful": sum(not bool_value(r.get("failed")) for r in records),
        "feasible": sum(bool_value(r.get("feasible")) for r in records),
        "verified": sum(bool_value(r.get("verified")) for r in records),
        "failed": sum(bool_value(r.get("failed")) for r in records),
        "timeouts": sum(bool_value(r.get("timeout")) for r in records),
    }
    status_counts = {
        key: sum(status_value(row) == key for row in records)
        for key in ("OPTIMAL", "FEASIBLE", "TIME_LIMIT", "INFEASIBLE", "FAILED")
    }
    by_objective: dict[str, dict[str, Any]] = {}
    for objective in ("minmax", "minsum"):
        subset = [row for row in records
                  if str(row.get("objective", "")).lower() == objective]
        by_objective[objective] = {
            "total": len(subset),
            "successful": sum(not bool_value(r.get("failed")) for r in subset),
            "feasible": sum(bool_value(r.get("feasible")) for r in subset),
            "verified": sum(bool_value(r.get("verified")) for r in subset),
            "failed": sum(bool_value(r.get("failed")) for r in subset),
            "timeouts": sum(bool_value(r.get("timeout")) for r in subset),
            **{status: sum(status_value(r) == status for r in subset)
               for status in ("OPTIMAL", "FEASIBLE", "TIME_LIMIT", "INFEASIBLE", "FAILED")},
        }
    keys: list[tuple[str, str, str, int]] = []
    for row in records:
        keys.append((
            str(row.get("instance_name", "")),
            str(row.get("algorithm_name", "")),
            str(row.get("objective", "")).lower(),
            int(row.get("repeat", 1) or 1),
        ))
    duplicates = len(keys) - len(set(keys))
    missing_solutions: list[str] = []
    missing_traces: list[str] = []
    declared_paths = 0
    for row in records:
        for field in ("solution_json_path", "trace_csv_path"):
            value = str(row.get(field, "") or "")
            if value:
                declared_paths += 1
                path = Path(value)
                if not path.is_absolute():
                    path = ROOT / path
                if not path.is_file():
                    detail = (
                        f"{row.get('instance_name')}/{row.get('algorithm_name')}/"
                        f"{row.get('objective')}: {field}={value}"
                    )
                    (missing_solutions if field == "solution_json_path"
                     else missing_traces).append(detail)
    provenance = sorted({
        f"requested={row.get('requested_backend') or 'N/A'}; actual={row.get('actual_backend') or 'N/A'}"
        for row in records if row.get("algorithm_category") == "exact_reference"
    })
    report = [
        "# Raw result integrity report", "",
        f"- Runs: {counts['total']} (expected {expected})",
        f"- Instances: {instances}",
        f"- Algorithms: {len(set(str(r.get('algorithm_name', '')) for r in records))}",
        f"- Successful (not marked failed): {counts['successful']}",
        f"- Feasible: {counts['feasible']}",
        f"- Verified: {counts['verified']}",
        f"- Failed: {counts['failed']}",
        f"- Timeouts: {counts['timeouts']}",
        f"- OPTIMAL / FEASIBLE / TIME_LIMIT / INFEASIBLE / FAILED: "
        f"{status_counts['OPTIMAL']} / {status_counts['FEASIBLE']} / "
        f"{status_counts['TIME_LIMIT']} / {status_counts['INFEASIBLE']} / "
        f"{status_counts['FAILED']}",
        f"- Duplicate run keys: {duplicates}",
        f"- Missing solution files among declared paths: {len(missing_solutions)}",
        f"- Missing trace files among declared paths: {len(missing_traces)}",
        f"- Declared solution/trace paths checked: {declared_paths}",
        f"- Missing records against expected count: {max(0, expected - counts['total'])}",
        "",
        "## By objective", "",
        "| Objective | Runs | Successful | Feasible | Verified | Failed | Timeouts | OPTIMAL | FEASIBLE | TIME_LIMIT | INFEASIBLE | FAILED |",
        "|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|",
    ]
    for objective, data in by_objective.items():
        report.append(
            f"| {objective} | {data['total']} | {data['successful']} | "
            f"{data['feasible']} | {data['verified']} | {data['failed']} | "
            f"{data['timeouts']} | {data['OPTIMAL']} | {data['FEASIBLE']} | "
            f"{data['TIME_LIMIT']} | {data['INFEASIBLE']} | {data['FAILED']} |"
        )
    report.extend(["", "## Exact backend provenance", ""])
    report.extend([f"- {line}" for line in provenance] or ["- No exact-reference records"])
    report.extend(["", "## Missing declared paths", "", "### Solutions", ""])
    report.extend([f"- {line}" for line in missing_solutions] or ["- None"])
    report.extend(["", "### Traces", ""])
    report.extend([f"- {line}" for line in missing_traces] or ["- None"])
    (exp / "reports" / "result_integrity_report.md").write_text(
        "\n".join(report) + "\n", encoding="utf-8"
    )
    state_path = exp / "reports" / "result_integrity.json"
    write_json(state_path, {
        "counts": counts,
        "statuses": status_counts,
        "by_objective": by_objective,
        "expected_runs": expected,
        "actual_runs": len(records),
        "missing_run_count": max(0, expected - len(records)),
        "duplicate_run_count": duplicates,
        "missing_solution_paths": missing_solutions,
        "missing_trace_paths": missing_traces,
        "exact_backend_provenance": provenance,
        "algorithm_count": len(set(str(r.get("algorithm_name", "")) for r in records)),
        "instance_count": instances,
    })


def q(values: list[float], fraction: float) -> float:
    return float(statistics.quantiles(values, n=4, method="inclusive")[int(fraction * 4) - 1]) if len(values) > 1 and fraction in (0.25, 0.5, 0.75) else (values[0] if values else math.nan)


def command_summary(args: argparse.Namespace) -> None:
    exp = Path(args.experiment).resolve()
    records = load_json(exp / "batch" / "master_results.json")
    dataset = exp / "dataset"
    instances = []
    for path in json_files(dataset):
        payload = load_json(path)
        instances.append({
            "name": str(payload.get("name", path.stem)),
            "n": len(payload.get("trajectories", [])),
            "m": len(payload.get("stations", [])),
            "T_end": payload.get("T_end"),
            "family": str(payload.get("name", path.stem)).split("-")[0].split("_")[0],
        })
    profile = str(load_json(exp / "experiment_manifest.json").get(
        "dataset_profile", "full"
    ))
    heading = ("# Smoke / development validation summary" if profile == "smoke10"
               else "# Benchmark research summary")
    report = [heading, ""]
    if profile == "smoke10":
        report.extend([
            "This is a smoke / development validation dataset, not the full "
            "scientific benchmark.", "",
        ])
    report.extend([
        "## Dataset summary", "",
        f"- Profile: {profile}",
        f"- Instances: {len(instances)}",
    ])
    for field, label in (("n", "n"), ("m", "m"), ("T_end", "T_end")):
        values = [float(item[field]) for item in instances if item[field] is not None]
        report.append(f"- {label} range: {min(values):g} to {max(values):g}" if values
                      else f"- {label} range: unavailable")
    report.append("- Families: " + (", ".join(sorted({i["family"] for i in instances})) or "unavailable"))
    report.extend(["", "## Runtime and quality statistics", "",
                   "Runtime uses `solve_time_sec` when present, otherwise `wall_time_sec`; "
                   "quality metrics are not filtered to successful rows.", "",
                   "| Algorithm | Objective | Runs | Runtime median | Q1 | Q3 | Min | Max | P95 | Objective median | Lower-bound median | Upper-bound median | Bound status | Certified-gap median | Heuristic-gap median | Empirical ratio median | Feasibility | Verification | Timeout | Failure | Iterations median | Static solves median |",
                   "|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|"])
    groups = sorted({
        (str(row.get("algorithm_name", "")), str(row.get("objective", "")).lower())
        for row in records if isinstance(row, dict)
    })
    for algorithm, objective in groups:
        rows = [row for row in records if str(row.get("algorithm_name", "")) == algorithm
                and str(row.get("objective", "")).lower() == objective]
        runtime = [float(row.get("solve_time_sec", row.get("wall_time_sec", 0)) or 0)
                   for row in rows if row.get("solve_time_sec", row.get("wall_time_sec")) is not None]
        runtime.sort()
        quantile = lambda p: float(__import__("numpy").quantile(runtime, p)) if runtime else math.nan
        objectives = [float(r["objective_value"]) for r in rows
                      if r.get("objective_value") is not None]
        lower_bounds = [float(r["lower_bound"]) for r in rows
                        if r.get("lower_bound") is not None]
        upper_bounds = [float(r["upper_bound"]) for r in rows
                        if r.get("upper_bound") is not None]
        bounds = sorted({str(r.get("bound_status", "NONE")) for r in rows})
        certified = [float(r["certified_gap"]) for r in rows if r.get("certified_gap") is not None]
        heuristic = [float(r["heuristic_gap"]) for r in rows if r.get("heuristic_gap") is not None]
        ratios = [float(r["empirical_ratio_to_exact"]) for r in rows
                  if r.get("empirical_ratio_to_exact") is not None]
        median = lambda xs: statistics.median(xs) if xs else math.nan
        feasible = sum(bool_value(r.get("feasible")) for r in rows) / max(1, len(rows))
        verified = sum(bool_value(r.get("verified")) for r in rows) / max(1, len(rows))
        timeouts = sum(bool_value(r.get("timeout")) for r in rows) / max(1, len(rows))
        failed = sum(bool_value(r.get("failed")) for r in rows) / max(1, len(rows))
        iterations = [float(r.get("num_iterations", 0) or 0) for r in rows]
        static = [float(r.get("num_static_solves", r.get("num_ip_solves", 0)) or 0)
                  for r in rows]
        cells = [algorithm, objective, str(len(rows))]
        cells.extend(f"{quantile(p):.6g}" if math.isfinite(quantile(p)) else "N/A"
                     for p in (0.5, .25, .75))
        cells.extend([
            f"{min(runtime):.6g}" if runtime else "N/A",
            f"{max(runtime):.6g}" if runtime else "N/A",
            f"{quantile(.95):.6g}" if runtime else "N/A",
            f"{median(objectives):.6g}" if objectives else "N/A",
            f"{median(lower_bounds):.6g}" if lower_bounds else "N/A",
            f"{median(upper_bounds):.6g}" if upper_bounds else "N/A",
            ", ".join(bounds),
            f"{median(certified):.6g}" if certified else "N/A",
            f"{median(heuristic):.6g}" if heuristic else "N/A",
            f"{median(ratios):.6g}" if ratios else "N/A",
            f"{feasible:.1%}", f"{verified:.1%}", f"{timeouts:.1%}", f"{failed:.1%}",
            f"{median(iterations):.6g}" if iterations else "N/A",
            f"{median(static):.6g}" if static else "N/A",
        ])
        report.append("| " + " | ".join(cells) + " |")
    report.extend([
        "", "## Interpretation", "",
        "`certified_gap` is reported separately from heuristic gap/progress. "
        "Empirical ratios are not theoretical approximation ratios. "
        "Reliability rates include every record, including timeouts and failures.",
    ])
    (exp / "reports" / "benchmark_summary.md").write_text(
        "\n".join(report) + "\n", encoding="utf-8"
    )


def instance_index(dataset: Path) -> dict[str, Path]:
    result: dict[str, Path] = {}
    for path in json_files(dataset):
        payload = load_json(path)
        result[str(payload.get("name", path.stem))] = path
        result[path.stem] = path
    return result


def command_select(args: argparse.Namespace) -> None:
    exp = Path(args.experiment).resolve()
    dataset = exp / "dataset"
    indexed = instance_index(dataset)
    if args.names:
        requested = [part.strip() for part in args.names.split(",") if part.strip()]
        missing = [name for name in requested if name not in indexed]
        if missing:
            raise ValueError("unknown animation instance(s): " + ", ".join(missing))
        chosen: list[Path] = []
        seen: set[Path] = set()
        for name in requested:
            path = indexed[name]
            if path not in seen:
                seen.add(path)
                chosen.append(path)
    else:
        candidates = []
        for path in json_files(dataset):
            payload = load_json(path)
            name = str(payload.get("name", path.stem))
            n, m = len(payload.get("trajectories", [])), len(payload.get("stations", []))
            family = name.split("-")[0].split("_")[0]
            candidates.append((n * max(1, m), n, m, family, name, path))
        candidates.sort()
        chosen = []
        families: set[str] = set()
        for item in candidates:
            if item[3] not in families:
                chosen.append(item[5])
                families.add(item[3])
            if len(chosen) >= args.top_n:
                break
        for item in candidates:
            if len(chosen) >= args.top_n:
                break
            if item[5] not in chosen:
                chosen.append(item[5])
    subset = exp / "exact" / "animation_dataset"
    subset.mkdir(parents=True, exist_ok=True)
    names: list[str] = []
    for path in chosen:
        payload = load_json(path)
        names.append(str(payload.get("name", path.stem)))
        target = subset / path.relative_to(dataset)
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(path, target)
    (exp / "exact" / "animation_instances.txt").write_text(
        "".join(name + "\n" for name in names), encoding="utf-8"
    )
    manifest_path = exp / "experiment_manifest.json"
    manifest = load_json(manifest_path)
    manifest["animation_instance_selection"] = names
    manifest["animation_selection_policy"] = (
        "explicit user order" if args.names else
        f"deterministic smallest n*m with family-first coverage, cap={args.top_n}"
    )
    write_json(manifest_path, manifest)


def exact_proven(row: dict[str, Any]) -> bool:
    return (
        row.get("algorithm_category") == "exact_reference"
        and bool_value(row.get("feasible"))
        and bool_value(row.get("verified"))
        and str(row.get("verification_kind", "")).upper() == "CERTIFIED_CONTINUOUS"
        and status_value(row) == "OPTIMAL"
    )


def command_animations(args: argparse.Namespace) -> None:
    exp = Path(args.experiment).resolve()
    selected = [line.strip() for line in
                (exp / "exact" / "animation_instances.txt").read_text(encoding="utf-8").splitlines()
                if line.strip()]
    exact_dir = exp / "exact" / "solves"
    decision_path = exp / "batch" / "experiment_manifest.json"
    decision_manifest = load_json(decision_path) if decision_path.is_file() else {}
    decision_actual_backend = str(decision_manifest.get("actual_backend", "") or "")
    records: list[dict[str, Any]] = []
    if (exact_dir / "master_results.json").is_file():
        loaded = load_json(exact_dir / "master_results.json")
        if isinstance(loaded, list):
            records = [r for r in loaded if isinstance(r, dict)]
    index = instance_index(exp / "dataset")
    report = ["# Exact animation report", ""]
    made = 0
    for instance_name in selected:
        for mode in args.modes.split(","):
            mode = mode.strip().lower()
            matching = [row for row in records
                        if str(row.get("instance_name", "")) == instance_name
                        and str(row.get("objective", "")).lower() == mode]
            if not matching:
                report.extend([
                    f"## {instance_name} / {mode}", "",
                    "- Exact backend: unavailable",
                    "- Solution status: no exact run record",
                    "- Verification status: unavailable",
                    "- Optimality status: FAILED",
                    "- Animation generated: no",
                    "- Reason: exact solve did not produce a record", "",
                ])
                continue
            row = matching[0]
            solution_value = str(row.get("solution_json_path", "") or "")
            solution_path = Path(solution_value)
            if not solution_path.is_absolute():
                solution_path = ROOT / solution_path
            proof = exact_proven(row)
            generated = False
            animation_path = ""
            reason = ""
            if not proof:
                reason = ("result does not satisfy exact-reference proof gate "
                          "(category, feasibility, continuous verification, and OPTIMAL required)")
            elif not solution_path.is_file():
                reason = "proven result has no retained solution JSON"
            else:
                try:
                    import pandas as pd
                    from animate_best import load_instance as load_animation_instance
                    from animate_best import make_animation
                except ImportError:
                    try:
                        from scripts.animate_best import load_instance as load_animation_instance
                        from scripts.animate_best import make_animation
                        import pandas as pd
                    except ImportError as error:
                        raise RuntimeError(f"animation dependencies unavailable: {error}") from error
                source_instance = load_json(index[instance_name])
                instance = load_animation_instance(index[instance_name])
                solution_payload = load_json(solution_path)
                solution_payload["_instance"] = instance
                record = pd.Series(dict(row))
                record["algorithm_name"] = (
                    f"proven exact reference | backend="
                    f"{decision_actual_backend or row.get('actual_backend')} | "
                    f"status={status_value(row)}"
                )
                output = exp / "animations"
                result_path = make_animation(
                    instance, solution_payload, record, output, mode, fps=15,
                    frames=60, dpi=100,
                )
                animation_path = result_path.relative_to(exp).as_posix()
                generated = True
                made += 1
            report.extend([
                f"## {instance_name} / {mode}", "",
                f"- Exact backend: {decision_actual_backend or row.get('actual_backend') or 'unreported'}",
                f"- Solution status: feasible={bool_value(row.get('feasible'))}",
                f"- Verification status: {row.get('verification_kind') or 'unreported'}; verified={bool_value(row.get('verified'))}",
                f"- Optimality status: {status_value(row)}",
                f"- Animation generated: {'yes' if generated else 'no'}",
                f"- Animation path: {animation_path or 'N/A'}",
                f"- Reason if skipped: {reason or 'N/A'}", "",
            ])
    (exp / "reports" / "exact_animation_report.md").write_text(
        "\n".join(report), encoding="utf-8"
    )
    print(f"Generated {made} proven exact animation(s)")


def command_finalize(args: argparse.Namespace) -> None:
    exp = Path(args.experiment).resolve()
    main_manifest_path = exp / "experiment_manifest.json"
    manifest = load_json(main_manifest_path)
    batch_manifest_path = exp / "batch" / "experiment_manifest.json"
    if batch_manifest_path.is_file():
        batch_manifest = load_json(batch_manifest_path)
        manifest["solver_manifest"] = batch_manifest
        manifest["requested_exact_backend"] = batch_manifest.get(
            "requested_backend", manifest.get("requested_exact_backend")
        )
        manifest["selected_exact_backend"] = batch_manifest.get(
            "selected_backend", batch_manifest.get("selected_exact_reference")
        )
        manifest["actual_exact_backend"] = batch_manifest.get(
            "actual_backend", manifest.get("actual_exact_backend")
        )
        manifest["exact_reference_calibration"] = {
            key: batch_manifest.get(key)
            for key in (
                "calibration_instances", "successful_runs", "rejected_runs",
                "runtime_statistics", "selection_rule", "selected_backend",
                "actual_backend", "kont_runtime_available",
            ) if key in batch_manifest
        }
        manifest["compiler"] = batch_manifest.get("compiler")
        manifest["build_type"] = batch_manifest.get("build_type")
    exact_manifest_path = exp / "exact" / "solves" / "experiment_manifest.json"
    if exact_manifest_path.is_file():
        exact_manifest = load_json(exact_manifest_path)
        manifest["animation_exact_run_manifest"] = exact_manifest
    integrity_path = exp / "reports" / "result_integrity.json"
    if integrity_path.is_file():
        manifest["result_integrity"] = load_json(integrity_path)
    state = manifest.get("stages", {})
    failures = [name for name, item in state.items()
                if item.get("status") in {"FAILED", "SKIPPED"}]
    manifest["status"] = "PARTIAL" if failures else "COMPLETE"
    manifest["failed_or_skipped_stages"] = failures
    write_json(main_manifest_path, manifest)
    reports = exp / "reports" / "final_experiment_report.md"
    integrity = (exp / "reports" / "result_integrity_report.md").read_text(
        encoding="utf-8"
    ) if (exp / "reports" / "result_integrity_report.md").is_file() else "Not available.\n"
    summary = (exp / "reports" / "benchmark_summary.md").read_text(
        encoding="utf-8"
    ) if (exp / "reports" / "benchmark_summary.md").is_file() else "Not available.\n"
    animation_report = (exp / "reports" / "exact_animation_report.md").read_text(
        encoding="utf-8"
    ) if (exp / "reports" / "exact_animation_report.md").is_file() else "Not generated.\n"
    tables = sorted(p.relative_to(exp).as_posix() for p in (exp / "tables").rglob("*")
                    if p.is_file())
    figures = sorted(p.relative_to(exp).as_posix() for p in (exp / "figures").rglob("*")
                     if p.is_file())
    animations = sorted(p.relative_to(exp).as_posix() for p in (exp / "animations").rglob("*")
                        if p.is_file())
    is_smoke = manifest.get("dataset_profile") == "smoke10"
    sections = [
        "# Smoke / development validation report" if is_smoke
        else "# Final experiment report", "",
        *(
            ["This is a smoke / development validation run and must not be "
             "interpreted as the full scientific benchmark.", ""]
            if is_smoke else []
        ),
        "## 1. Experiment configuration", "",
        f"- Status: **{manifest['status']}**",
        f"- Experiment ID: `{manifest['experiment_id']}`",
        f"- Reproduction command: `{manifest.get('reproduction_command', 'bash scripts/run_experiment.sh')}`",
        f"- Algorithms/profile: "
        f"`{', '.join(manifest.get('algorithm_set', [])) or manifest.get('algorithm_list')}` / FAST",
        f"- Objectives: {', '.join(manifest.get('objectives', []))}",
        f"- Seed/repeats/threads: {manifest.get('seed')} / {manifest.get('repeats')} / {manifest.get('thread_count')}",
        "", "## 2. Dataset description", "",
        f"- Profile/source: `{manifest.get('dataset_profile', 'full')}` / "
        f"`{manifest.get('dataset_source')}`",
        f"- Canonical instances: {manifest.get('instance_count')}",
        f"- Fingerprint: `{manifest.get('dataset_fingerprint')}`",
        *(
            [
                f"- Selection policy: {manifest.get('selection_policy')}",
                "- Selected files:",
                *[
                    f"  - `{row.get('source')}` ({row.get('family')}, "
                    f"n={row.get('n')}, m={row.get('m')})"
                    for row in manifest.get("selected_files", [])
                ],
                f"- Source dataset fingerprint: "
                f"`{manifest.get('source_dataset_fingerprint')}`",
            ] if is_smoke else []
        ),
        "", "## 3. Environment", "",
        f"- Commit: `{manifest.get('git_commit')}` (dirty={manifest.get('git_dirty')})",
        f"- Compiler/build: {manifest.get('compiler') or 'unreported'} / {manifest.get('build_type') or 'unreported'}",
        f"- Python: {manifest.get('python_version')}; host: {manifest.get('host', {}).get('platform')}",
        "", "## 4. Exact-reference selection", "",
        f"- Requested: {manifest.get('requested_exact_backend')}",
        f"- Selected: {manifest.get('selected_exact_backend')}",
        f"- Actual: {manifest.get('actual_exact_backend')}",
        "", "## 5-11. FAST benchmark, per-objective results, runtime, quality, and reliability", "",
        summary, "", "### Raw integrity", "", integrity,
        "", "## 12. Bound semantics", "",
        "`certified_gap` is only meaningful for certified lower bounds and feasible upper bounds. "
        "Empirical ratios are not theoretical approximation guarantees.",
        "", "## 13. Exact-reference evidence", "",
        json.dumps(manifest.get("exact_reference_calibration"), indent=2),
        "", "## 14. Exact-animation results", "", animation_report,
        "", "## 15. Generated tables", "",
        "\n".join(f"- `{p}`" for p in tables) or "None.",
        "", "## 16. Generated figures", "",
        "\n".join(f"- `{p}`" for p in figures) or "None.",
        "", "## 17. Generated animations", "",
        "\n".join(f"- `{p}`" for p in animations) or "None.",
        "", "## 18. Reproduction command", "",
        f"`{manifest.get('reproduction_command', 'bash scripts/run_experiment.sh')}`",
        "", "## 19. Failed/skipped stages", "",
        ", ".join(failures) if failures else "None recorded.",
        "", "## 20. Remaining limitations", "",
        f"This report covers {manifest.get('instance_count')} dataset instance(s) only; "
        "it does not imply that other dataset inputs were executed.",
        f"Native KONT/COPT availability: "
        f"{'available' if manifest.get('solver_manifest', {}).get('kont_runtime_available') else 'unavailable or not used'}.",
        "A visualization is not a feasibility or optimality proof. Exact animation is emitted only "
        "for results marked exact-reference, feasible, continuously verified, and OPTIMAL. "
        "Review `result_integrity_report.md` and per-run metadata for failed and timed-out cases.",
        "",
    ]
    reports.parent.mkdir(parents=True, exist_ok=True)
    reports.write_text("\n".join(sections), encoding="utf-8")


def command_check_figures(args: argparse.Namespace) -> None:
    directory = Path(args.experiment).resolve() / "figures"
    report_path = directory / "REPORT.md"
    if not report_path.is_file():
        raise ValueError("missing figures/REPORT.md")
    import re
    names = re.findall(r"!\[[^\]]*\]\(([^)]+\.png)\)", report_path.read_text(encoding="utf-8"))
    if not names:
        raise ValueError("figure report contains no PNG references")
    missing = []
    for relative in names:
        png = directory / relative
        pdf = directory / (Path(relative).stem + ".pdf")
        if not png.is_file() or not pdf.is_file():
            missing.append(f"{relative} and/or its PDF")
    if missing:
        raise ValueError("missing figure artifact(s): " + ", ".join(missing))


def command_check_tables(args: argparse.Namespace) -> None:
    directory = Path(args.experiment).resolve() / "tables"
    index_path = directory / "00_index.md"
    if not index_path.is_file():
        raise ValueError("missing tables/00_index.md")
    import re
    links = re.findall(r"\]\(([^)]+)\)", index_path.read_text(encoding="utf-8"))
    missing = [link for link in links
               if not (directory / link).is_file()]
    if not links or missing:
        raise ValueError("table index has missing artifacts: " +
                         (", ".join(missing) if missing else "no table links"))


def command_check_animations(args: argparse.Namespace) -> None:
    exp = Path(args.experiment).resolve()
    report = exp / "reports" / "exact_animation_report.md"
    if not report.is_file():
        raise ValueError("missing exact animation report")
    import re
    content = report.read_text(encoding="utf-8")
    paths = re.findall(r"^- Animation path: (.+)$", content, flags=re.MULTILINE)
    missing = []
    for value in paths:
        if value == "N/A":
            continue
        if not (exp / value).is_file():
            missing.append(value)
    if missing:
        raise ValueError("missing animation artifact(s): " + ", ".join(missing))


def command_resume(args: argparse.Namespace) -> None:
    root = Path(args.root)
    source = Path(args.source)
    target = None
    for candidate in sorted(root.glob("*/experiment_manifest.json"),
                            key=lambda p: p.stat().st_mtime, reverse=True):
        try:
            manifest = load_json(candidate)
        except (OSError, json.JSONDecodeError):
            continue
        if manifest.get("status") == "COMPLETE":
            continue
        if manifest.get("dataset_source_fingerprint") == args.source_fingerprint:
            target = candidate.parent
            break
    print(str(target) if target else "")


def main() -> int:
    parser = argparse.ArgumentParser()
    subparsers = parser.add_subparsers(dest="command", required=True)
    init = subparsers.add_parser("init")
    init.add_argument("--experiment", required=True)
    init.add_argument("--dataset", required=True)
    init.add_argument("--dataset-label", required=True)
    init.add_argument("--dataset-fingerprint", required=True)
    init.add_argument("--dataset-profile", choices=("full", "smoke10"),
                      default="full")
    init.add_argument("--source-dataset-fingerprint", default="")
    init.add_argument("--selection-fingerprint", default="")
    init.add_argument("--selection-manifest", default="")
    init.add_argument("--algorithms", required=True)
    init.add_argument("--modes", required=True)
    init.add_argument("--seed", type=int, required=True)
    init.add_argument("--repeats", type=int, required=True)
    init.add_argument("--threads", type=int, required=True)
    init.add_argument("--fast-limit", type=float, required=True)
    init.add_argument("--exact-limit", type=float, required=True)
    init.add_argument("--static-limit", type=float, required=True)
    init.add_argument("--exact-reference", required=True)
    init.add_argument("--animation-top-n", type=int, required=True)
    init.add_argument("--animation-mode", required=True)
    init.add_argument("--animation-instances", default="")
    init.add_argument("--resume", action="store_true")
    init.add_argument("--reproduction-command", required=True)
    init.set_defaults(func=command_init)
    stage = subparsers.add_parser("stage")
    stage.add_argument("--experiment", required=True)
    stage.add_argument("--name", required=True)
    stage.add_argument("--status", required=True)
    stage.add_argument("--command")
    stage.add_argument("--exit-code", type=int)
    stage.add_argument("--message")
    stage.set_defaults(func=command_stage)
    dataset_parser = subparsers.add_parser("dataset")
    dataset_parser.add_argument("--experiment", required=True)
    dataset_parser.add_argument("--source", required=True)
    dataset_parser.add_argument("--resume", action="store_true")
    dataset_parser.add_argument("--canonical-fingerprint")
    dataset_parser.set_defaults(func=command_dataset)
    preflight_snapshot = subparsers.add_parser("preflight-snapshot")
    preflight_snapshot.add_argument("--experiment", required=True)
    preflight_snapshot.set_defaults(func=command_preflight_snapshot)
    validate = subparsers.add_parser("validate")
    validate.add_argument("--experiment", required=True)
    validate.add_argument("--results", required=True)
    validate.set_defaults(func=command_validate)
    summary = subparsers.add_parser("summary")
    summary.add_argument("--experiment", required=True)
    summary.set_defaults(func=command_summary)
    select = subparsers.add_parser("select")
    select.add_argument("--experiment", required=True)
    select.add_argument("--names", default="")
    select.add_argument("--top-n", type=int, default=1)
    select.set_defaults(func=command_select)
    animations = subparsers.add_parser("animations")
    animations.add_argument("--experiment", required=True)
    animations.add_argument("--modes", required=True)
    animations.set_defaults(func=command_animations)
    finalize = subparsers.add_parser("finalize")
    finalize.add_argument("--experiment", required=True)
    finalize.set_defaults(func=command_finalize)
    resume = subparsers.add_parser("resume")
    resume.add_argument("--root", required=True)
    resume.add_argument("--source", required=True)
    resume.add_argument("--source-fingerprint", required=True)
    resume.set_defaults(func=command_resume)
    fingerprint = subparsers.add_parser("fingerprint")
    fingerprint.add_argument("--path", required=True)
    fingerprint.set_defaults(func=command_fingerprint)
    check_figures = subparsers.add_parser("check-figures")
    check_figures.add_argument("--experiment", required=True)
    check_figures.set_defaults(func=command_check_figures)
    check_tables = subparsers.add_parser("check-tables")
    check_tables.add_argument("--experiment", required=True)
    check_tables.set_defaults(func=command_check_tables)
    check_animations = subparsers.add_parser("check-animations")
    check_animations.add_argument("--experiment", required=True)
    check_animations.set_defaults(func=command_check_animations)
    args = parser.parse_args()
    try:
        args.func(args)
    except (OSError, ValueError, RuntimeError, json.JSONDecodeError,
            subprocess.CalledProcessError) as error:
        print(f"experiment_pipeline.py: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
