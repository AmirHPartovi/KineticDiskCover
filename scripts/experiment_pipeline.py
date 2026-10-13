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
from types import SimpleNamespace
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from kdc_tools.storage import (  # noqa: E402
    index_experiment_artifacts,
    make_experiment_id,
    validate_joint_result,
)

EXACT_STATUSES = {"OPTIMAL"}
PIPELINE_NAMES = {"smoke", "reference", "full"}
REQUIRED_PIPELINE_STAGES = [
    "init", "dataset", "build", "tests", "preflight", "backend_selection",
    "benchmark", "result_validation", "tables", "figures", "animations",
    "final_report", "summary",
]
SMOKE_REQUIRED_PIPELINE_STAGES = [
    "init", "dataset", "build", "tests", "certification", "preflight",
    "backend_selection", "benchmark", "result_validation", "tables",
    "figures", "animations", "final_report", "summary",
]


def load_pipeline_config(name: str) -> dict[str, Any]:
    if name not in PIPELINE_NAMES:
        raise ValueError(
            f"unknown pipeline {name!r}; expected smoke, reference, or full"
        )
    path = ROOT / "configs" / "pipeline" / f"{name}.json"
    config = load_json(path)
    if not isinstance(config, dict) or config.get("pipeline") != name:
        raise ValueError(f"invalid pipeline config: {path}")
    required = {
        "label", "dataset_profile", "dataset", "algorithms", "exact_backend",
        "objectives", "repeats", "seed", "threads", "fast_time_limit_sec",
        "exact_time_limit_sec", "static_time_limit_sec", "benchmark_profile",
        "verify_after", "verify_each_iteration", "save_traces",
        "save_solutions", "tables", "figures", "animation",
        "artifact_retention_days", "required_stages",
    }
    missing = sorted(required - config.keys())
    if missing:
        raise ValueError(f"pipeline {name} config missing: {', '.join(missing)}")
    expected_stages = (
        SMOKE_REQUIRED_PIPELINE_STAGES if name == "smoke"
        else REQUIRED_PIPELINE_STAGES
    )
    if config["required_stages"] != expected_stages:
        raise ValueError(f"pipeline {name} has an invalid stage sequence")
    if name == "smoke":
        certification = config.get("certification", {})
        if (config["dataset_profile"] != "smoke20"
                or config.get("algorithms") != "all-registered"
                or config.get("smoke_count") != 20
                or config.get("smoke_seed") != 0
                or config.get("smoke_max_n") != 50
                or config.get("smoke_max_m") != 25
                or config.get("brute_force_max_assignments") != 100000
                or config.get("brute_force_time_limit_sec") != 0.5
                or certification.get("seed_count") != 200
                or certification.get("repetitions") != 1
                or config.get("animation", {}).get("policy") != "all-solutions"
                or config.get("snapshots") != {
                    "enabled": True,
                    "layout": "three-panel",
                    "normalized_times": [0, 0.5, 1],
                    "comparison_sheet": "per-instance-objective",
                }):
            raise ValueError("smoke config must define the smoke20 selection policy")
    elif config["dataset_profile"] != "full":
        raise ValueError(f"pipeline {name} must use the full dataset profile")
    if (name == "reference" and config["algorithms"] != "greedy") or (
        name == "full" and config["algorithms"] != "all"
    ):
        raise ValueError(f"pipeline {name} algorithm policy is invalid")
    if config["objectives"] not in {"both", "all"} or config["exact_backend"] != "auto":
        raise ValueError(f"pipeline {name} must use a supported objective set and AUTO")
    return config


def command_config(args: argparse.Namespace) -> None:
    config = load_pipeline_config(args.pipeline)
    if args.format == "json":
        print(json.dumps(config, sort_keys=True))
        return
    values = {
        "PIPELINE_PROFILE": config["pipeline"],
        "PIPELINE_LABEL": config["label"],
        "DATASET_PROFILE": config["dataset_profile"],
        "DATASET": config["dataset"],
        "ALGORITHMS": config["algorithms"],
        "EXACT_REFERENCE": config["exact_backend"],
        "MODES": config["objectives"],
        "SEED": config["seed"],
        "REPEATS": config["repeats"],
        "THREADS": config["threads"],
        "FAST_LIMIT": config["fast_time_limit_sec"],
        "EXACT_LIMIT": config["exact_time_limit_sec"],
        "STATIC_LIMIT": config["static_time_limit_sec"],
        "BENCHMARK_PROFILE": config["benchmark_profile"],
        "VERIFY_AFTER": int(config["verify_after"]),
        "VERIFY_EACH_ITERATION": int(config["verify_each_iteration"]),
        "SAVE_TRACES": int(config["save_traces"]),
        "SAVE_SOLUTIONS": int(config["save_solutions"]),
        "TABLES_ENABLED": int(config["tables"]),
        "FIGURES_ENABLED": int(config["figures"]),
        "ANIMATION_ENABLED": int(config["animation"]["enabled"]),
        "ANIMATION_MODE_DEFAULT": config["animation"].get("mode", "both"),
        "ANIMATION_POLICY": config["animation"]["policy"],
        "ANIMATION_TOP_N": config["animation"]["top_n"],
        "ANIMATION_FPS": config["animation"]["fps"],
        "ANIMATION_FRAMES": config["animation"]["frames"],
        "ANIMATION_DPI": config["animation"]["dpi"],
        "ARTIFACT_RETENTION_DAYS": config["artifact_retention_days"],
        "SMOKE_COUNT": config.get("smoke_count", 20),
        "SMOKE_SEED": config.get("smoke_seed", 0),
        "SMOKE_MAX_N": config.get("smoke_max_n", 50),
        "SMOKE_MAX_M": config.get("smoke_max_m", 25),
        "BRUTE_FORCE_MAX_ASSIGNMENTS": config.get(
            "brute_force_max_assignments", 100_000
        ),
        "BRUTE_FORCE_TIME_LIMIT": config.get(
            "brute_force_time_limit_sec", 0.5
        ),
        "CERTIFICATION_SEEDS": config.get("certification", {}).get(
            "seed_count", 0
        ),
        "CERTIFICATION_REPETITIONS": config.get("certification", {}).get(
            "repetitions", 0
        ),
    }
    for key, value in values.items():
        print(f"{key}\t{value}")


def command_new_id(args: argparse.Namespace) -> None:
    print(make_experiment_id(args.profile))


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
            "instance_id": row.get("instance_id"),
            "family": row.get("family"),
            "family_classification_rule": row.get(
                "family_classification_rule"
            ),
            "source": row.get("source"),
            "relative_source": row.get("relative_source"),
            "source_sha256": row.get(
                "source_sha256", row.get("sha256")
            ),
            "canonical_path": row.get("canonical_path"),
            "canonical_sha256": row.get("canonical_sha256"),
            "n": row.get("n"),
            "m": row.get("m"),
            "T_end": row.get("T_end"),
            "file_size_bytes": row.get("file_size_bytes"),
            "selection_reason": row.get("selection_reason"),
            "selection_rank": row.get("selection_rank"),
            "size_limit_exception": row.get("size_limit_exception"),
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
            "pipeline": args.pipeline,
            "algorithm_list": args.algorithms,
            "objectives": (
                ["minmax", "minsum"] if args.modes == "both"
                else ["minmax", "minsum", "minmaxsum"] if args.modes == "all"
                else [args.modes]
            ),
            "seed": args.seed,
            "repeats": args.repeats,
            "thread_count": args.threads,
            "fast_time_limit_sec": args.fast_limit,
            "exact_time_limit_sec": args.exact_limit,
            "per_static_time_limit_sec": args.static_limit,
            "requested_exact_backend": args.exact_reference,
            "animation_top_n": args.animation_top_n,
            "animation_mode": args.animation_mode,
            "animation_policy": args.animation_policy,
            "animation_instances_requested": args.animation_instances,
            "dataset_profile": args.dataset_profile,
            "benchmark_profile": args.benchmark_profile,
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

    pipeline_config = load_pipeline_config(args.pipeline)
    if args.dataset_profile == "smoke20":
        pipeline_config.update({
            "smoke_count": args.selection_count,
            "smoke_seed": args.selection_seed,
            "smoke_max_n": args.selection_max_n,
            "smoke_max_m": args.selection_max_m,
        })
    config_payload = json.dumps(
        pipeline_config, sort_keys=True, separators=(",", ":")
    ).encode("utf-8")
    manifest = {
        "experiment_id": experiment.name,
        "pipeline": args.pipeline,
        "experiment_label": pipeline_config["label"],
        "pipeline_config": pipeline_config,
        "pipeline_config_sha256": hashlib.sha256(config_payload).hexdigest(),
        "dataset_profile": args.dataset_profile,
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
        "dataset_fingerprint": None,
        "instance_count": None,
        "algorithm_list": args.algorithms,
        "algorithm_set": [],
        "objectives": (
            ["minmax", "minsum"] if args.modes == "both"
            else ["minmax", "minsum", "minmaxsum"] if args.modes == "all"
            else [args.modes]
        ),
        "profile": args.benchmark_profile,
        "benchmark_profile": args.benchmark_profile,
        "seed": args.seed,
        "repeats": args.repeats,
        "thread_count": args.threads,
        "fast_time_limit_sec": args.fast_limit,
        "exact_time_limit_sec": args.exact_limit,
        "per_static_time_limit_sec": args.static_limit,
        "requested_exact_backend": args.exact_reference,
        "animation_top_n": args.animation_top_n,
        "animation_mode": args.animation_mode,
        "animation_policy": args.animation_policy,
        "animation_instances_requested": args.animation_instances,
        "verify_after": bool(args.verify_after),
        "verify_each_iteration": bool(args.verify_each_iteration),
        "save_traces": bool(args.save_traces),
        "save_solutions": bool(args.save_solutions),
        "artifact_retention_days": pipeline_config["artifact_retention_days"],
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
        "required_stages": pipeline_config["required_stages"],
        "status": "RUNNING",
        "reproduction_command": args.reproduction_command,
    }
    if selection_manifest is not None:
        selection_path = experiment / "dataset" / "selection_manifest"
        write_json(selection_path, selection_manifest)
        manifest["selection_manifest"] = "dataset/selection_manifest"
        manifest["selection_manifest_fingerprint"] = args.selection_fingerprint
        manifest["selected_files"] = selected_files
    write_json(manifest_path, manifest)
    command_stage(SimpleNamespace(
        experiment=str(experiment), name="init", status="COMPLETED",
        command=args.reproduction_command, exit_code=0, message=None,
        duration_sec=0.0,
    ))


def command_stage(args: argparse.Namespace) -> None:
    path = Path(args.experiment).resolve() / "experiment_manifest.json"
    manifest = load_json(path)
    stage = manifest.setdefault("stages", {}).setdefault(args.name, {})
    now = dt.datetime.now(dt.timezone.utc).isoformat()
    status = args.status.upper()
    stage["status"] = status
    stage["updated_at"] = now
    if status == "RUNNING":
        stage["started_at"] = now
    else:
        stage["completed_at"] = now
    if args.command:
        stage["command"] = args.command
        manifest.setdefault("commands", []).append({
            "stage": args.name,
            "command": args.command,
            "recorded_at": dt.datetime.now(dt.timezone.utc).isoformat(),
        })
    if args.exit_code is not None:
        stage["exit_code"] = args.exit_code
    if args.duration_sec is not None:
        stage["duration_sec"] = args.duration_sec
    if args.message:
        stage["message"] = args.message
    write_json(path, manifest)


def command_emit_summary(args: argparse.Namespace) -> None:
    manifest = load_json(Path(args.experiment).resolve()
                         / "experiment_manifest.json")
    for name in manifest.get("required_stages", []):
        item = manifest.get("stages", {}).get(name, {})
        print(f"{name}: {item.get('status', 'MISSING')}")


def command_dataset(args: argparse.Namespace) -> None:
    exp = Path(args.experiment).resolve()
    source = Path(args.source).resolve()
    canonical = exp / "dataset"
    experiment_manifest = load_json(exp / "experiment_manifest.json")
    smoke20 = experiment_manifest.get("dataset_profile") == "smoke20"
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
            if payload.get("name", payload.get("profile")) == "smoke10":
                raise ValueError(
                    "legacy smoke10 manifests cannot be materialized as new data; "
                    "regenerate smoke20 selection evidence"
                )
            if smoke20:
                selector = _selection_module()
                selection_config = experiment_manifest["pipeline_config"]
                selector._validate_selection_identity(
                    payload, selection_config["smoke_count"],
                    selection_config["smoke_max_n"],
                    selection_config["smoke_max_m"],
                )
                expected_seed = int(
                    selection_config["smoke_seed"]
                )
                if payload.get("policy_parameters", {}).get("seed") != expected_seed:
                    raise ValueError(
                        "smoke20 selection manifest seed differs from experiment configuration"
                    )
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
            if selection.get("name", selection.get("profile")) == "smoke10":
                raise ValueError(
                    "legacy smoke10 manifests cannot be materialized as new data; "
                    "regenerate smoke20 selection evidence"
                )
            if smoke20:
                selector = _selection_module()
                selection_config = experiment_manifest["pipeline_config"]
                selector._validate_selection_identity(
                    selection, selection_config["smoke_count"],
                    selection_config["smoke_max_n"],
                    selection_config["smoke_max_m"],
                )
                expected_seed = int(
                    selection_config["smoke_seed"]
                )
                if selection.get("policy_parameters", {}).get("seed") != expected_seed:
                    raise ValueError(
                        "smoke20 selection manifest seed differs from experiment configuration"
                    )
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
    if smoke20 and len(files) != 20:
        raise ValueError(
            f"smoke20 canonical dataset must contain exactly 20 JSON inputs; "
            f"found {len(files)}"
        )
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
    manifest["dataset_profile"] = manifest.get(
        "dataset_profile", "full"
    )
    manifest["experiment_label"] = manifest.get(
        "experiment_label", "full scientific benchmark"
    )
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
    selector.read_manifest(manifest_path)
    source_root = selector._manifest_source_root(selection, manifest_path)
    if selector._dataset_fingerprint(source_root) != selection.get(
            "source_dataset_fingerprint"):
        raise ValueError("selection source dataset fingerprint mismatch")
    rows = selection["instances"]
    expected = selection.get("selected_instance_count", len(rows))
    if len(rows) != expected:
        raise ValueError(
            f"selection manifest expects {expected} instances, has {len(rows)}"
        )
    seen_sources: set[str] = set()
    seen_instances: set[str] = set()
    for row in rows:
        if not isinstance(row, dict):
            raise ValueError("selection manifest instances must be objects")
        source_name = str(row.get("source", row.get("relative_source", "")))
        if not source_name or source_name in seen_sources:
            raise ValueError("selection manifest has a missing or duplicate source")
        seen_sources.add(source_name)
        instance_id = row.get("instance_id")
        if not isinstance(instance_id, str) or instance_id in seen_instances:
            raise ValueError("selection manifest has a missing or duplicate logical instance")
        seen_instances.add(instance_id)
        source = selector._resolve_manifest_entry(
            row, source_root, manifest_path
        )
        expected_sha = row.get("source_sha256", row.get("sha256"))
        actual_sha = hashlib.sha256(source.read_bytes()).hexdigest()
        if expected_sha and actual_sha != expected_sha:
            raise ValueError(f"selection source checksum mismatch: {row.get('source')}")
        payload = selector._canonical_payload(source)
        n, m = selector.validate_instance(payload)
        if selector._logical_instance_id(payload) != instance_id:
            raise ValueError(f"selection logical identity mismatch: {source_name}")
        canonical_hash = hashlib.sha256(
            selector._json_bytes(payload)
        ).hexdigest()
        if (n != row.get("n") or m != row.get("m")
                or not math.isclose(
                    float(payload["T_end"]), float(row["T_end"]),
                    rel_tol=1e-9, abs_tol=1e-9,
                )
                or canonical_hash != row.get("canonical_sha256")):
            raise ValueError(f"selection manifest metadata mismatch: {row.get('source')}")
        relative = Path(str(row["canonical_path"]))
        if not relative.is_relative_to("instances"):
            raise ValueError(f"selection canonical path is invalid: {relative}")
        destination = canonical / relative
        if not destination.resolve().is_relative_to(canonical.resolve()):
            raise ValueError(f"selection canonical path escapes dataset: {relative}")
        _write_canonical_payload(payload, destination)


def _copy_materialized_selection(source: Path,
                                 selection: dict[str, Any],
                                 canonical: Path) -> None:
    selector = _selection_module()
    rows = selection["instances"]
    source_root = selector._manifest_source_root(
        selection, source / "manifest.json"
    )
    if selector._dataset_fingerprint(source_root) != selection.get(
            "source_dataset_fingerprint"):
        raise ValueError("selection source dataset fingerprint mismatch")
    expected = selection.get("selected_instance_count", len(rows))
    if len(rows) != expected:
        raise ValueError(
            f"materialized manifest expects {expected} instances, has {len(rows)}"
        )
    seen_sources: set[str] = set()
    seen_instances: set[str] = set()
    for row in rows:
        if not isinstance(row, dict):
            raise ValueError("selection manifest instances must be objects")
        source_name = str(row.get("source", ""))
        if not source_name or source_name in seen_sources:
            raise ValueError("materialized manifest has a missing or duplicate source")
        seen_sources.add(source_name)
        instance_id = row.get("instance_id")
        if not isinstance(instance_id, str) or instance_id in seen_instances:
            raise ValueError("materialized manifest has a missing or duplicate logical instance")
        seen_instances.add(instance_id)
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
        if selector._logical_instance_id(payload) != instance_id:
            raise ValueError(f"materialized logical identity mismatch: {source_name}")
        canonical_hash = hashlib.sha256(
            selector._json_bytes(payload)
        ).hexdigest()
        if (n != row.get("n") or m != row.get("m")
                or canonical_hash != row.get("canonical_sha256")
                or not math.isclose(
                    float(payload["T_end"]), float(row["T_end"]),
                    rel_tol=1e-9, abs_tol=1e-9,
                )):
            raise ValueError(f"materialized instance metadata mismatch: {materialized}")
        original = source_root / str(row.get("relative_source", ""))
        expected_source_sha = row.get("source_sha256")
        if expected_source_sha:
            if not original.is_file() or hashlib.sha256(
                    original.read_bytes()
            ).hexdigest() != expected_source_sha:
                raise ValueError(f"selection source checksum mismatch: {source_name}")
        relative = Path(str(row.get("canonical_path", "")))
        if not relative.is_relative_to("instances"):
            raise ValueError(f"selection canonical path is invalid: {relative}")
        destination = canonical / relative
        _write_canonical_payload(payload, destination)


def command_preflight_snapshot(args: argparse.Namespace) -> None:
    destination = Path(args.experiment).resolve() / "preflight"
    destination.mkdir(parents=True, exist_ok=True)
    report = Path(args.report).resolve()
    if not report.is_file():
        raise ValueError(f"preflight report does not exist: {report}")
    shutil.copy2(report, destination / report.name)


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


def _planned_run_count(experiment: Path) -> int | None:
    path = experiment / "runs" / "plan.jsonl"
    if not path.is_file():
        return None
    count = sum(bool(line.strip()) for line in path.read_text(
        encoding="utf-8"
    ).splitlines())
    if count == 0:
        raise ValueError("immutable run plan contains no slots")
    return count


def command_validate(args: argparse.Namespace) -> None:
    exp = Path(args.experiment).resolve()
    aggregate_manifest_path = exp / "aggregates" / "aggregate_manifest.json"
    aggregate_manifest = (
        load_json(aggregate_manifest_path)
        if aggregate_manifest_path.is_file() else None
    )
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
    nonexact_count = len([name for name in algorithms
                          if name not in {"ip-kont", "branch-and-bound"}])
    expected = _planned_run_count(exp)
    if expected is None:
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
    joint_errors: list[str] = []
    for index, row in enumerate(records):
        if (str(row.get("objective", "")).lower() == "minmaxsum"
                and not bool_value(row.get("feasible", False))):
            continue
        try:
            validate_joint_result(row)
        except ValueError as error:
            joint_errors.append(f"row {index}: {error}")
    for objective in ("minmax", "minsum", "minmaxsum"):
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
                    experiment_path = exp / path
                    path = experiment_path if experiment_path.is_file() else ROOT / path
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
        (
            f"- Canonical aggregate complete: {aggregate_manifest.get('complete')}"
            if aggregate_manifest is not None
            else "- Canonical aggregate: legacy input (not available)"
        ),
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
    if joint_errors:
        report.extend(["", "## Invalid MinMaxSum records", ""])
        report.extend(f"- {error}" for error in joint_errors)
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
        "canonical_aggregate": aggregate_manifest,
    })
    if aggregate_manifest is not None:
        aggregate_counts = aggregate_manifest.get("classifications", {})
        structurally_incomplete = (
            aggregate_counts.get("missing_runs", 0) != 0
            or aggregate_counts.get("unexpected_runs", 0) != 0
            or aggregate_counts.get("invalid_runs", 0) != 0
            or aggregate_manifest.get("missing_run_keys")
            or aggregate_manifest.get("unexpected_run_keys")
            or aggregate_manifest.get("invalid_runs")
        )
        if structurally_incomplete:
            raise ValueError(
                "canonical aggregate has missing, invalid, unexpected, or "
                "duplicate run records; see aggregate_manifest.json and "
                "result_integrity_report.md"
            )
    if len(records) != expected or duplicates:
        raise ValueError(
            "master results do not match the expected unique logical run count; "
            "see result_integrity_report.md"
        )
    if joint_errors:
        raise ValueError(
            "MinMaxSum vector or dominance validation failed; see "
            "result_integrity_report.md"
        )


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
    heading = ("# Smoke / development validation summary" if profile == "smoke20"
               else "# Benchmark research summary")
    report = [heading, ""]
    if profile == "smoke20":
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
                   "| Algorithm | Objective | Runs | Runtime median | Q1 | Q3 | Min | Max | P95 | Objective median | Lower-bound median | Upper-bound median | Bound status | Certified-gap median | Heuristic-gap median | Empirical ratio median | Peak cost median | Integral cost median | Feasibility | Verification | Timeout | Failure | Iterations median | Static solves median |",
                   "|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|"])
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
        peak_values = []
        integral_values = []
        for row in rows:
            vector = row.get("objective_vector")
            vector = vector if isinstance(vector, dict) else row
            if vector.get("peak_cost") is not None:
                peak_values.append(float(vector["peak_cost"]))
            if vector.get("integral_cost") is not None:
                integral_values.append(float(vector["integral_cost"]))
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
            f"{median(peak_values):.6g}" if peak_values else "N/A",
            f"{median(integral_values):.6g}" if integral_values else "N/A",
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
    if args.policy not in {"best", "all-algorithms", "all-solutions"}:
        raise ValueError(
            "animation policy must be best, all-algorithms, or all-solutions"
        )
    selected = [part.strip() for part in args.instances.split(",") if part.strip()]
    if args.policy == "all-solutions":
        if selected or args.top_n:
            raise ValueError(
                "all-solutions visualization cannot restrict planned instances"
            )
        try:
            from visualize_experiment import generate as generate_visualizations
        except ImportError:
            from scripts.visualize_experiment import generate as generate_visualizations
        result = generate_visualizations(
            exp, fps=args.fps, frames=args.frames, dpi=args.dpi
        )
        report = [
            "# Animation and snapshot report", "",
            f"- Policy: `{args.policy}`",
            f"- Objectives: `{args.modes}`",
            f"- Planned solver slots: {result['planned_slots']}",
            f"- Comparison sheets: {result['expected_comparison_sheets']}",
            f"- Renderable solutions with snapshots: "
            f"{result['generated_solution_snapshots']}",
            f"- Per-solution animations: {result['generated_animations']}",
            f"- Ineligible slots: {result['ineligible_slots']}",
            f"- Rendering failures: {result['failed_slots']}",
            "- Feasibility, verification, and optimality are displayed from each "
            "run record; rendering does not imply certification.",
            "",
            "## Comparison sheets", "",
        ]
        report.extend(
            f"- `{sheet['path']}`" for sheet in result["comparison_sheets"]
        )
        report.extend(["", "## Per-solution animations", ""])
        report.extend(
            f"- `{slot['animation']['path']}`"
            for slot in result["slots"] if slot.get("animation")
        )
        if result["generated_animations"] == 0:
            report.append("- None: no planned run currently has a renderable solution.")
        manifest = load_json(exp / "experiment_manifest.json")
        selected = sorted({
            str(slot["instance_id"]) for slot in result["slots"]
        })
        manifest["visualization_manifest"] = (
            "reports/visualization_manifest.json"
        )
    else:
        try:
            from animate_best import run as run_animations
        except ImportError:
            from scripts.animate_best import run as run_animations
        generated = run_animations(
            exp / "batch", exp / "dataset", exp / "animations",
            mode=args.modes, fps=args.fps, frames=args.frames, dpi=args.dpi,
            top_n=args.top_n, all_algorithms=args.policy == "all-algorithms",
            selected_instances=selected or None,
        )
        report = [
            "# Animation report", "",
            f"- Policy: `{args.policy}`",
            f"- Objectives: `{args.modes}`",
            f"- Instance selection: "
            f"{', '.join(selected) if selected else 'all benchmark instances'}",
            f"- Generated animations: {len(generated)}",
            "- Each animation's metadata preserves its recorded verification and "
            "optimality status.",
            "",
        ]
        report.extend(
            f"- Animation path: {path.relative_to(exp).as_posix()}"
            for path in generated
        )
        if not generated:
            report.append("- Animation path: N/A")
        manifest = load_json(exp / "experiment_manifest.json")
    (exp / "reports" / "exact_animation_report.md").write_text(
        "\n".join(report) + "\n", encoding="utf-8"
    )
    manifest["animation_policy"] = args.policy
    manifest["animation_instance_selection"] = selected
    write_json(exp / "experiment_manifest.json", manifest)
    index_experiment_artifacts(exp)
    print(
        f"Generated {result['generated_animations']} per-solution animation(s) "
        f"and {result['expected_comparison_sheets']} comparison sheet(s)"
        if args.policy == "all-solutions"
        else f"Generated {len(generated)} animation(s)"
    )


def command_finalize(args: argparse.Namespace) -> None:
    exp = Path(args.experiment).resolve()
    main_manifest_path = exp / "experiment_manifest.json"
    manifest = load_json(main_manifest_path)
    storage_manifest_path = exp / "experiment.json"
    if storage_manifest_path.is_file():
        storage_manifest = load_json(storage_manifest_path)
        manifest["run_plan_sha256"] = storage_manifest.get("run_plan_sha256")
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
    required_stages = manifest.get("required_stages", REQUIRED_PIPELINE_STAGES)
    failures = [
        name for name in required_stages
        if state.get(name, {}).get("status") != "COMPLETED"
    ]
    failures.extend(
        name for name, item in state.items()
        if name not in required_stages
        and item.get("status") in {"FAILED", "SKIPPED"}
    )
    manifest["status"] = "PARTIAL" if failures else "COMPLETE"
    manifest["failed_or_skipped_stages"] = failures
    aggregate_path = exp / "aggregates" / "aggregate_manifest.json"
    if aggregate_path.is_file():
        aggregate_manifest = load_json(aggregate_path)
        manifest["aggregate_complete"] = aggregate_manifest.get("complete") is True
        manifest["aggregate_run_classifications"] = aggregate_manifest.get(
            "classifications", {}
        )
        if aggregate_manifest.get("complete") is not True:
            manifest["status"] = "PARTIAL"
            manifest["aggregate_incomplete_reason"] = (
                "One or more planned solver slots ended in a non-COMPLETED "
                "terminal state; see aggregate and per-run status records."
            )
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
    pipeline_name = manifest.get("pipeline", "full")
    is_smoke = pipeline_name == "smoke"
    sections = [
        {
            "smoke": "# Development / smoke validation report",
            "reference": "# Full dataset reference validation report",
            "full": "# Full scientific benchmark report",
        }.get(pipeline_name, "# Final experiment report"), "",
        *(
            ["This is a smoke / development validation run and must not be "
             "interpreted as the full scientific benchmark.", ""]
            if is_smoke else []
        ),
        "## 1. Experiment configuration", "",
        f"- Status: **{manifest['status']}**",
        f"- Experiment ID: `{manifest['experiment_id']}`",
        f"- Reproduction command: `{manifest.get('reproduction_command', 'bash scripts/run_experiment.sh')}`",
        f"- Pipeline: `{pipeline_name}` ({manifest.get('experiment_label')})",
        f"- Algorithms/profile: "
        f"`{', '.join(manifest.get('algorithm_set', [])) or manifest.get('algorithm_list')}` / "
        f"{manifest.get('benchmark_profile', 'debug')}",
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
        f"See the [data-correct comparative report](experiment_report.md) for the complete coverage, result, timing, bound, MinMaxSum, failure, family and artifact tables, figure generation statuses, and companion audit.",
        "",
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
        "from serialized solver output. Visualization artifacts show feasibility, verification, "
        "and optimality exactly as recorded; failed, skipped, invalid, and time-limited slots "
        "remain visible in comparison sheets. Review `visualization_manifest.json` and "
        "`result_integrity_report.md`.",
        "",
    ]
    reports.parent.mkdir(parents=True, exist_ok=True)
    reports.write_text("\n".join(sections), encoding="utf-8")
    index_experiment_artifacts(exp)


def command_check_figures(args: argparse.Namespace) -> None:
    experiment = Path(args.experiment).resolve()
    directory = experiment / "figures"
    report_path = directory / "REPORT.md"
    if not report_path.is_file():
        raise ValueError("missing figures/REPORT.md")
    status_path = directory / "figure_status.json"
    if not status_path.is_file():
        raise ValueError("missing figures/figure_status.json")
    status = load_json(status_path)
    if status.get("experiment_id") != Path(args.experiment).resolve().name:
        raise ValueError("figure status belongs to a different experiment")
    requested = status.get("requested")
    figures = status.get("figures")
    if not isinstance(requested, list) or not isinstance(figures, list):
        raise ValueError("figure status has an invalid catalog")
    by_name = {row.get("figure"): row for row in figures if isinstance(row, dict)}
    missing_names = sorted(set(requested) - set(by_name))
    if missing_names:
        raise ValueError("figure status omits requested figure(s): " + ", ".join(missing_names))
    missing_artifacts = []
    for name in requested:
        item = by_name[name]
        state = item.get("generation_status")
        if state not in {"GENERATED", "NOT_GENERATED"}:
            missing_artifacts.append(f"{name}: invalid generation status {state!r}")
            continue
        if state == "GENERATED" and item.get("path_png"):
            target = (directory / item["path_png"]).resolve()
            try:
                target.relative_to(Path(args.experiment).resolve())
            except ValueError:
                missing_artifacts.append(f"{name}: artifact escapes experiment")
                continue
            if not target.is_file() or target.stat().st_size == 0:
                missing_artifacts.append(str(target))
        if state == "GENERATED" and item.get("path_pdf"):
            target = (directory / item["path_pdf"]).resolve()
            if not target.is_file() or target.stat().st_size == 0:
                missing_artifacts.append(str(target))
        if state == "NOT_GENERATED" and not item.get("reason"):
            missing_artifacts.append(f"{name}: missing grounded not-generated reason")
    if missing_artifacts:
        raise ValueError("figure artifact audit failed: " + ", ".join(missing_artifacts))
    try:
        from report_experiment import _snapshot_figure_status
    except ImportError:
        from scripts.report_experiment import _snapshot_figure_status
    expected = _snapshot_figure_status(experiment)
    actual = by_name.get("three_time_solution_figures")
    if actual is None or any(
        actual.get(field) != expected.get(field)
        for field in (
            "generation_status", "path_manifest", "artifact_class",
            "snapshot_count", "reason",
        )
    ):
        raise ValueError(
            "figure status is stale for three_time_solution_figures; "
            "regenerate the figure report from the visualization manifest"
        )
    expected_status_line = (
        f"| three_time_solution_figures | {expected['generation_status']} |"
    )
    if expected_status_line not in report_path.read_text(encoding="utf-8"):
        raise ValueError(
            "figures/REPORT.md disagrees with the validated snapshot status"
        )


def command_check_tables(args: argparse.Namespace) -> None:
    experiment = Path(args.experiment).resolve()
    directory = experiment / "tables"
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
    try:
        from kdc_tools.reporting import audit_companions
    except ImportError:
        from scripts.kdc_tools.reporting import audit_companions
    errors = audit_companions(experiment, create_missing=False,
                              experiment_id=experiment.name)
    if errors:
        raise ValueError("recursive CSV/TSV companion audit failed: "
                         + "; ".join(errors))


def command_check_animations(args: argparse.Namespace) -> None:
    exp = Path(args.experiment).resolve()
    visualization_manifest = exp / "reports" / "visualization_manifest.json"
    if visualization_manifest.is_file():
        try:
            from visualize_experiment import audit as audit_visualizations
        except ImportError:
            from scripts.visualize_experiment import audit as audit_visualizations
        audit_visualizations(exp)
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
    new_id = subparsers.add_parser("new-id")
    new_id.add_argument("--profile", required=True)
    new_id.set_defaults(func=command_new_id)
    config = subparsers.add_parser("config")
    config.add_argument("--pipeline", choices=sorted(PIPELINE_NAMES), required=True)
    config.add_argument("--format", choices=("json", "shell"), default="json")
    config.set_defaults(func=command_config)
    init = subparsers.add_parser("init")
    init.add_argument("--experiment", required=True)
    init.add_argument("--pipeline", choices=sorted(PIPELINE_NAMES),
                      default="full")
    init.add_argument("--dataset", required=True)
    init.add_argument("--dataset-label", required=True)
    init.add_argument("--dataset-fingerprint", required=True)
    init.add_argument("--dataset-profile", choices=("full", "smoke20"),
                      default="full")
    init.add_argument("--benchmark-profile",
                      choices=("fast", "exact-reference", "debug"),
                      default="debug")
    init.add_argument("--source-dataset-fingerprint", default="")
    init.add_argument("--selection-fingerprint", default="")
    init.add_argument("--selection-manifest", default="")
    init.add_argument("--selection-count", type=int, default=20)
    init.add_argument("--selection-seed", type=int, default=0)
    init.add_argument("--selection-max-n", type=int, default=50)
    init.add_argument("--selection-max-m", type=int, default=25)
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
    init.add_argument("--animation-policy",
                      choices=("best", "all-algorithms", "all-solutions"),
                      default="best")
    init.add_argument("--animation-instances", default="")
    init.add_argument("--verify-after", type=int, choices=(0, 1), default=1)
    init.add_argument("--verify-each-iteration", type=int,
                      choices=(0, 1), default=0)
    init.add_argument("--save-traces", type=int, choices=(0, 1), default=1)
    init.add_argument("--save-solutions", type=int, choices=(0, 1), default=1)
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
    stage.add_argument("--duration-sec", type=float)
    stage.set_defaults(func=command_stage)
    dataset_parser = subparsers.add_parser("dataset")
    dataset_parser.add_argument("--experiment", required=True)
    dataset_parser.add_argument("--source", required=True)
    dataset_parser.add_argument("--resume", action="store_true")
    dataset_parser.add_argument("--canonical-fingerprint")
    dataset_parser.set_defaults(func=command_dataset)
    preflight_snapshot = subparsers.add_parser("preflight-snapshot")
    preflight_snapshot.add_argument("--experiment", required=True)
    preflight_snapshot.add_argument("--report", required=True)
    preflight_snapshot.set_defaults(func=command_preflight_snapshot)
    validate = subparsers.add_parser("validate")
    validate.add_argument("--experiment", required=True)
    validate.add_argument("--results", required=True)
    validate.set_defaults(func=command_validate)
    summary = subparsers.add_parser("summary")
    summary.add_argument("--experiment", required=True)
    summary.set_defaults(func=command_summary)
    emit_summary = subparsers.add_parser("emit-summary")
    emit_summary.add_argument("--experiment", required=True)
    emit_summary.set_defaults(func=command_emit_summary)
    select = subparsers.add_parser("select")
    select.add_argument("--experiment", required=True)
    select.add_argument("--names", default="")
    select.add_argument("--top-n", type=int, default=1)
    select.set_defaults(func=command_select)
    animations = subparsers.add_parser("animations")
    animations.add_argument("--experiment", required=True)
    animations.add_argument("--modes", required=True)
    animations.add_argument("--policy",
                            choices=("best", "all-algorithms", "all-solutions"),
                            default="best")
    animations.add_argument("--instances", default="")
    animations.add_argument("--top-n", type=int, default=0)
    animations.add_argument("--fps", type=int, default=24)
    animations.add_argument("--frames", type=int, default=120)
    animations.add_argument("--dpi", type=int, default=120)
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
