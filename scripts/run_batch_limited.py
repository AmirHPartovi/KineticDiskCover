#!/usr/bin/env python3
"""Execute batches as isolated, immutable runs and derive experiment aggregates."""

from __future__ import annotations

import argparse
from concurrent.futures import ThreadPoolExecutor, as_completed
import csv
import hashlib
import json
import math
import os
from pathlib import Path
import platform
import shlex
import shutil
import subprocess
import sys
import time
from typing import Any

ROOT = Path(__file__).resolve().parents[1]
SOLVER = ROOT / "build" / "kdc-solver"
sys.path.insert(0, str(ROOT / "scripts"))
from kdc_tools.schemas import dumps_json, loads_json, validate_document
from kdc_tools.storage import (
    aggregate_experiment,
    create_experiment,
    finalize_run,
    index_experiment_artifacts,
    make_experiment_id,
    make_run_id,
    make_run_key,
    read_json,
    recover_experiment,
    reserve_run,
    safe_id,
    sha256_file,
    transition_run,
    update_experiment,
    utc_now,
    validate_run_directory,
    write_bytes_create_only,
    write_derived_bytes,
    write_derived_json,
    write_json_create_only,
    write_run_plan,
)
from select_test_instances import (
    _validate_selection_identity,
    apply_execution_defaults,
    resolve_instance_directory,
)

ALGORITHM_PHASES = (
    ("ip-kont", "branch-and-bound"),
    ("nn", "greedy", "brute-force"),
    ("primal-dual", "local-search", "sa", "genetic",
     "lp-rounding", "shifting"),
)
OBJECTIVES = ("minmax", "minsum")
ALL_OBJECTIVES = ("minmax", "minsum", "minmaxsum")
TERMINAL_STATES = {
    "COMPLETED", "FAILED", "TIME_LIMIT", "TIMED_OUT", "INVALID",
    "CANCELLED", "ABANDONED", "SKIPPED",
}


def order_algorithms(selected: set[str]) -> list[str]:
    ordered = [algorithm for phase in ALGORITHM_PHASES
               for algorithm in phase if algorithm in selected]
    return ordered + sorted(selected - set(ordered))


def registered_algorithms() -> tuple[str, ...]:
    result = subprocess.run(
        [str(SOLVER), "list-algorithms"], cwd=ROOT,
        stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, check=False,
    )
    if result.returncode != 0:
        raise ValueError(
            "unable to query static solver registry: "
            + (result.stderr or result.stdout).strip()[-2000:]
        )
    names = tuple(line.strip() for line in result.stdout.splitlines()
                  if line.strip())
    if not names or len(names) != len(set(names)):
        raise ValueError("static solver registry returned no names or duplicates")
    return names


def _load_selection(path: str | None) -> dict[str, Any]:
    if not path:
        return {}
    selection = loads_json(Path(path).read_text(encoding="utf-8"))
    if not isinstance(selection, dict) or not isinstance(
        selection.get("instances"), list
    ):
        raise ValueError("dataset manifest must contain an instances array")
    return selection


def _experiment_root(output: Path) -> tuple[Path, Path]:
    output = output.resolve()
    legacy_batch = (ROOT / "results" / "batch").resolve()
    if output == legacy_batch:
        raise ValueError(
            "results/batch is a legacy shared path; use a unique experiment "
            "directory or omit --output"
        )
    return (output.parent, output) if output.name == "batch" else (output, output / "batch")


def _read_plan(path: Path) -> list[dict[str, Any]]:
    if not path.is_file():
        return []
    entries = []
    with path.open(encoding="utf-8") as source:
        for line_number, line in enumerate(source, 1):
            if not line.strip():
                raise ValueError(f"empty run-plan line {line_number}")
            entry = loads_json(line)
            validate_document(entry, "run_plan_entry")
            entries.append(entry)
    return entries


def _run_attempts(experiment: Path, run_key: str) -> list[dict[str, Any]]:
    attempts = []
    for path in sorted((experiment / "runs").glob("*/request.json")):
        try:
            request = read_json(path, schema_name="request")
        except (OSError, ValueError):
            continue
        if request.get("run_key") != run_key:
            continue
        record_path = path.parent / "run.json"
        if record_path.is_file():
            try:
                record = validate_run_directory(
                    path.parent, experiment_id=experiment.name
                )
            except (OSError, ValueError):
                status_path = path.parent / "execution" / "status.json"
                state = "INVALID"
                if status_path.is_file():
                    try:
                        state = read_json(
                            status_path, schema_name="run_status"
                        )["state"]
                    except (OSError, ValueError):
                        pass
                record = {"run_id": path.parent.name, "status": state}
        else:
            status_path = path.parent / "execution" / "status.json"
            state = "INVALID"
            if status_path.is_file():
                try:
                    state = read_json(
                        status_path, schema_name="run_status"
                    )["state"]
                except (OSError, ValueError):
                    pass
            record = {"run_id": path.parent.name, "status": state}
        attempts.append(record)
    return sorted(
        attempts,
        key=lambda row: (row.get("finished_at", ""), row["run_id"]),
    )


def _trace_to_jsonl(
    source: Path, run_id: str, destination_run: Path
) -> None:
    lines = []
    with source.open(newline="", encoding="utf-8") as input_file:
        for sequence, row in enumerate(csv.DictReader(input_file)):
            data: dict[str, Any] = {}
            for key, value in row.items():
                if value is None:
                    continue
                try:
                    data[key] = float(value)
                except ValueError:
                    data[key] = value
            lines.append(dumps_json({
                "schema_version": 1,
                "run_id": run_id,
                "sequence": sequence,
                "timestamp": utc_now(),
                "event": "iteration",
                "data": data,
            }))
    if lines:
        write_bytes_create_only(
            destination_run, "result/trace.jsonl",
            ("\n".join(lines) + "\n").encode("utf-8"),
        )


def run_one(
    *,
    experiment: Path,
    input_path: Path,
    instance_id: str,
    instance_name: str,
    family: str,
    n: int,
    m: int,
    input_sha256: str,
    source_sha256: str,
    source_path: str,
    algorithm: str,
    objective: str,
    repeat: int,
    seed: int,
    exact_backend: str,
    config: dict[str, Any],
    profile: str,
    minsum_refinement_policy: str,
    safety_timeout: float | None,
    rerun_of: str | None,
    save_solutions: bool,
    save_traces: bool,
    skip_reason: dict[str, str] | None = None,
) -> dict[str, Any]:
    """Reserve, execute, validate and finalize exactly one solver invocation."""
    backend_identity = (
        exact_backend if algorithm in {"ip-kont", "branch-and-bound"}
        else "not-applicable"
    )
    run_key = make_run_key(
        input_sha256=input_sha256,
        algorithm=algorithm,
        objective=objective,
        seed=seed,
        repeat=repeat,
        resolved_backend=backend_identity,
        configuration=config,
    )
    input_reference = {
        "schema_version": 1,
        "instance_id": instance_id,
        "canonical_path": "input/instance.json",
        "canonical_sha256": input_sha256,
        "source_path": source_path,
        "source_sha256": source_sha256,
        "family": family,
        "n": n,
        "m": m,
    }
    run_dir = None
    for _ in range(5):
        run_id = make_run_id(algorithm, objective, instance_id, repeat)
        try:
            run_dir = reserve_run(
                experiment,
                run_id=run_id,
                run_key=run_key,
                experiment_id=experiment.name,
                instance_id=instance_id,
                algorithm=algorithm,
                objective=objective,
                repeat=repeat,
                seed=seed,
                requested_backend=config["requested_backend"],
                configuration=config,
                input_reference=input_reference,
                rerun_of=rerun_of,
            )
            break
        except FileExistsError:
            continue
    if run_dir is None:
        raise FileExistsError("could not allocate unique run ID after 5 attempts")
    input_bytes = input_path.read_bytes()
    if hashlib.sha256(input_bytes).hexdigest() != input_sha256:
        raise ValueError(f"input changed after planning: {input_path}")
    write_bytes_create_only(run_dir, "input/instance.json", input_bytes)

    engine_output = run_dir / "execution" / "solver"
    command = [
        str(SOLVER), "batch",
        "--instances", str(run_dir / "input"),
        "--output", str(engine_output),
        "--algorithms", algorithm,
        "--modes", objective,
        "--time-limit", str(config["per_static_time_limit_sec"]),
        "--fast-time-limit", str(config["fast_time_limit_sec"]),
        "--exact-time-limit", str(config["exact_time_limit_sec"]),
        "--minsum-refinement-policy", minsum_refinement_policy,
        "--minmax-budget-fraction",
        str(config.get("minmax_budget_fraction", 0.5)),
        "--minsum-budget-fraction",
        str(config.get("minsum_budget_fraction", 0.5)),
        "--exact-reference", exact_backend,
        "--profile", profile,
        "--seed", str(seed),
        "--repeats", "1",
    ]
    if algorithm != exact_backend:
        command.append("--allow-no-exact-reference")
    if save_solutions:
        command.append("--save-solutions")
    if save_traces:
        command.append("--save-traces")
    if algorithm == "brute-force":
        command.extend([
            "--brute-force-max-assignments",
            str(config["diagnostic_max_assignments"]),
        ])
    if skip_reason is not None:
        write_json_create_only(
            run_dir, "execution/skip_reason.json", skip_reason
        )
        write_bytes_create_only(run_dir, "execution/stdout.log", b"")
        write_bytes_create_only(
            run_dir, "execution/stderr.log",
            (skip_reason["message"] + "\n").encode("utf-8"),
        )
        disposition = json.dumps(skip_reason, sort_keys=True)
        result_document = {
            "schema_version": 1,
            "run_id": run_id,
            "experiment_id": experiment.name,
            "run_key": run_key,
            "execution_status": "SKIPPED",
            "solver_result": None,
            "error_message": disposition,
        }
        finalize_run(
            run_dir,
            state="SKIPPED",
            result_document=result_document,
            verification_document={
                "verification_mode": "not-run",
                "status": "NOT_RUN",
                "kind": "NONE",
                "passed": False,
                "continuous": False,
                "message": skip_reason["message"],
            },
            backend={
                "selected": exact_backend,
                "actual": None,
                "native": False,
                "version": "unknown",
                "detection": config.get("backend_detection", {}),
                "capabilities": config.get("backend_capabilities", {}),
            },
            provenance={"solver_version": "unknown", "platform": platform.platform()},
            source_commit=None,
        )
        return {
            "run_id": run_id,
            "run_key": run_key,
            "status": "SKIPPED",
            "record_path": str(run_dir / "run.json"),
            "error": disposition,
        }
    write_bytes_create_only(
        run_dir, "execution/command.txt",
        ("\n".join(command) + "\n").encode("utf-8"),
    )

    execution_env = None
    profile_request = os.environ.get("KDC_PROFILE_PHASES")
    if profile_request:
        profile_output = run_dir / "execution" / "profile_phases.json"
        write_json_create_only(run_dir, "execution/profile_configuration.json", {
            "requested": profile_request,
            "output": "execution/profile_phases.json",
        })
        execution_env = os.environ.copy()
        execution_env["KDC_PROFILE_PHASES"] = str(profile_output)

    stdout_path = run_dir / "execution" / "stdout.log"
    stderr_path = run_dir / "execution" / "stderr.log"
    return_code: int | None = None
    external_timeout = False
    launch_error = None
    started = time.monotonic()
    try:
        with stdout_path.open("xb") as stdout_file, stderr_path.open("xb") as stderr_file:
            process = subprocess.Popen(
                command, cwd=ROOT, stdout=stdout_file, stderr=stderr_file,
                env=execution_env,
            )
            transition_run(run_dir, "RUNNING", process_id=process.pid)
            try:
                return_code = process.wait(timeout=safety_timeout)
            except subprocess.TimeoutExpired:
                external_timeout = True
                process.kill()
                return_code = process.wait()
    except OSError as error:
        launch_error = str(error)
        if not stdout_path.exists():
            write_bytes_create_only(run_dir, "execution/stdout.log", b"")
        if not stderr_path.exists():
            write_bytes_create_only(
                run_dir, "execution/stderr.log",
                (launch_error + "\n").encode("utf-8"),
            )
    elapsed = time.monotonic() - started

    row = None
    master_path = engine_output / "master_results.json"
    parse_error = None
    if master_path.is_file():
        try:
            rows = loads_json(master_path.read_text(encoding="utf-8"))
            if not isinstance(rows, list):
                raise ValueError("solver master result must be an array")
            row = next((
                item for item in rows
                if item.get("algorithm_name") == algorithm
                and item.get("objective") == objective
            ), None)
        except (OSError, ValueError, AttributeError) as error:
            parse_error = str(error)

    error_message = launch_error or parse_error
    if row is None and error_message is None:
        error_message = (
            f"solver exited {return_code} without a matching result record"
        )
    if row is not None:
        row.update({
            "run_id": run_id,
            "run_key": run_key,
            "experiment_id": experiment.name,
            "instance_id": instance_id,
            "instance_name": instance_name,
            "algorithm_name": algorithm,
            "objective": objective,
            "repeat": repeat,
            "seed": seed,
            "wrapper_wall_time_sec": elapsed,
            "external_timeout": external_timeout,
            "actual_threads": 1,
            "dataset_profile": config.get("dataset_profile"),
            "experiment_label": config.get("experiment_label"),
        })
        if launch_error:
            row["wrapper_error"] = launch_error
    if external_timeout:
        state = "TIME_LIMIT"
    elif row is None:
        state = "FAILED" if return_code not in (None, 0) or launch_error else "INVALID"
    elif row.get("time_limited") or row.get("optimality_status") == "TIME_LIMIT":
        state = "TIME_LIMIT"
    elif row.get("failed"):
        state = "FAILED"
    elif return_code != 0:
        state = "FAILED"
    elif row.get("verify_after", True) and not row.get("verified"):
        state = "INVALID"
    elif (
        algorithm == exact_backend
        and config["requested_backend"] != "auto"
        and row.get("actual_backend") != config["requested_backend"]
    ):
        state = "INVALID"
    else:
        state = "COMPLETED"

    solution_target = None
    trace_target = None
    if row is not None:
        for field, target, relative in (
            ("solution_json_path", "solution_target", "result/solution.json"),
            ("trace_csv_path", "trace_target", "result/trace.csv"),
        ):
            source_name = row.get(field)
            if not source_name:
                continue
            source = Path(source_name).resolve()
            try:
                source.relative_to(engine_output.resolve())
            except ValueError:
                error_message = f"solver artifact path escaped run directory: {source}"
                state = "INVALID"
                continue
            if source.is_file():
                write_bytes_create_only(run_dir, relative, source.read_bytes())
                if target == "solution_target":
                    solution_target = relative
                else:
                    trace_target = relative
                    _trace_to_jsonl(source, run_id, run_dir)
        row["solution_json_path"] = solution_target
        row["trace_csv_path"] = "result/trace.csv" if trace_target else None
        row["trace_jsonl_path"] = "result/trace.jsonl" if trace_target else None
        row["result_json_path"] = "result/result.json"

    result_document = {
        "schema_version": 1,
        "run_id": run_id,
        "experiment_id": experiment.name,
        "run_key": run_key,
        "execution_status": state,
        "solver_result": row,
        "error_message": error_message,
    }
    status_before_finalization = read_json(
        run_dir / "execution" / "status.json", schema_name="run_status"
    )
    write_json_create_only(run_dir, "execution/timing.json", {
        "schema_version": 1,
        "started_at": status_before_finalization.get("started_at"),
        "finished_at": utc_now(),
        "wall_time_sec": elapsed,
        "solver_return_code": return_code,
        "external_timeout": external_timeout,
        "error": error_message,
    })
    verification_kind = (
        str(row.get("verification_kind", "NONE")).upper()
        if row else "NONE"
    )
    verified = bool(row and row.get("verified"))
    verification = {
        "verification_mode": "continuous" if "CONTINUOUS" in verification_kind
        else "solver-reported",
        "status": "PASSED" if verified else (
            "FAILED" if row is not None else "NOT_RUN"
        ),
        "kind": verification_kind if verification_kind in {
            "NONE", "EMPIRICAL", "CERTIFIED_CONTINUOUS",
            "CONTINUOUS_UNCERTIFIED",
        } else "NONE",
        "passed": verified,
        "continuous": "CONTINUOUS" in verification_kind,
        "message": error_message,
    }
    backend_actual = row.get("actual_backend") if row else None
    backend_selected = row.get("selected_backend") if row else (
        exact_backend if algorithm in {"ip-kont", "branch-and-bound"} else algorithm
    )
    record_path = finalize_run(
        run_dir,
        state=state,
        result_document=result_document,
        verification_document=verification,
        backend={
            "selected": backend_selected,
            "actual": backend_actual,
            "native": bool(row and row.get("native_kont")),
            "version": (
                row.get("solver_version", row.get("kont_version", "unknown"))
                if row else "unknown"
            ),
            "detection": config.get("backend_detection", {}),
            "capabilities": config.get("backend_capabilities", {}),
        },
        provenance={
            "solver_version": (
                row.get("solver_version", row.get("kont_version", "unknown"))
                if row else "unknown"
            ),
            "platform": platform.platform(),
        },
        source_commit=row.get("git_commit") if row else None,
    )
    return {
        "run_id": run_id,
        "run_key": run_key,
        "status": state,
        "record_path": str(record_path),
        "error": error_message,
    }


def _build_plan(
    experiment: Path,
    instance_paths: list[Path],
    algorithms: list[str],
    objectives: tuple[str, ...],
    exact_backend: str,
    args: argparse.Namespace,
    backend_manifest: dict[str, Any],
    selection: dict[str, Any],
) -> tuple[list[dict[str, Any]], dict[str, dict[str, Any]]]:
    plan = []
    task_data: dict[str, dict[str, Any]] = {}
    config_base = {
        "benchmark_profile": args.profile.upper().replace("-", "_"),
        "per_static_time_limit_sec": args.time_limit,
        "fast_time_limit_sec": args.fast_time_limit,
        "exact_time_limit_sec": args.exact_time_limit,
        "minsum_refinement_policy": args.minsum_refinement_policy,
        "minmax_budget_fraction": getattr(
            args, "minmax_budget_fraction", 0.5
        ),
        "minsum_budget_fraction": getattr(
            args, "minsum_budget_fraction", 0.5
        ),
        "requested_threads": args.threads,
        "actual_threads": 1,
        "dataset_profile": args.dataset_profile,
        "experiment_label": (
            "smoke / development validation"
            if args.dataset_profile == "smoke20"
            else "scientific benchmark dataset"
        ),
        "backend_detection": {
            "requested": args.exact_reference,
            "selected": exact_backend,
            "actual": backend_manifest.get("actual_backend"),
        },
        "backend_capabilities": backend_manifest.get(
            "backend_capabilities", {}
        ),
        "brute_force_max_assignments": getattr(
            args, "brute_force_max_assignments", 100_000
        ),
        "brute_force_time_limit_sec": getattr(
            args, "brute_force_time_limit_sec", 0.5
        ),
    }
    source_provenance = {}
    for row in selection.get("instances", []):
        if not isinstance(row, dict):
            continue
        relative = Path(str(row.get("relative_source") or row.get("source") or ""))
        source_provenance[(
            str(row.get("family", "root")),
            relative.stem,
        )] = row
    for source_path in instance_paths:
        original = loads_json(source_path.read_text(encoding="utf-8"))
        if not isinstance(original, dict):
            raise ValueError(f"instance root must be an object: {source_path}")
        canonical = source_path.read_bytes()
        input_sha = hashlib.sha256(canonical).hexdigest()
        provenance_row = source_provenance.get((source_path.parent.name, source_path.stem))
        source_sha = (
            str(provenance_row["source_sha256"])
            if provenance_row and provenance_row.get("source_sha256")
            else sha256_file(source_path)
        )
        instance_name = str(original.get("name") or source_path.stem)
        instance_id = f"{safe_id(source_path.stem)}-{input_sha[:10]}"
        snapshot_path = experiment / "input" / f"{instance_id}.json"
        if snapshot_path.exists():
            if sha256_file(snapshot_path) != input_sha:
                raise ValueError(
                    f"existing experiment input changed: {snapshot_path}"
                )
        else:
            write_bytes_create_only(
                experiment, f"input/{instance_id}.json", canonical
            )
        family = source_path.parent.name or "root"
        n = len(original.get("trajectories", []))
        m = len(original.get("stations", []))
        for algorithm in algorithms:
            for objective in objectives:
                for repeat in range(args.repeats):
                    seed = args.seed + repeat
                    config = {
                        **config_base,
                        "requested_algorithm": algorithm,
                        "resolved_algorithm": algorithm,
                        "requested_backend": args.exact_reference,
                        "selected_backend": exact_backend,
                        "requested_time_limit_sec": (
                            args.brute_force_time_limit_sec
                            if algorithm == "brute-force"
                            else args.exact_time_limit if algorithm in {
                                "ip-kont", "branch-and-bound"
                            }
                            else args.fast_time_limit
                        ),
                        "actual_time_limit_sec": (
                            args.brute_force_time_limit_sec if algorithm == "brute-force"
                            else args.exact_time_limit if algorithm in {
                                "ip-kont", "branch-and-bound"
                            }
                            else args.fast_time_limit
                        ),
                        "algorithm_category": (
                            "diagnostic" if algorithm == "brute-force"
                            else "exact_reference" if algorithm == exact_backend
                            else "exact_candidate" if algorithm in {
                                "ip-kont", "branch-and-bound"
                            }
                            else "heuristic"
                        ),
                        "reference_designated": algorithm == exact_backend,
                        "diagnostic_max_assignments": (
                            args.brute_force_max_assignments
                            if algorithm == "brute-force" else None
                        ),
                    }
                    if algorithm == "brute-force":
                        config["diagnostic_time_limit_sec"] = (
                            args.brute_force_time_limit_sec
                        )
                        config["per_static_time_limit_sec"] = min(
                            args.time_limit, args.brute_force_time_limit_sec
                        )
                        config["fast_time_limit_sec"] = min(
                            args.fast_time_limit, args.brute_force_time_limit_sec
                        )
                        config["exact_time_limit_sec"] = min(
                            args.exact_time_limit, args.brute_force_time_limit_sec
                        )
                    skip_reason = None
                    if (algorithm == "ip-kont"
                            and not backend_manifest.get(
                                "native_kont_available",
                                backend_manifest.get(
                                    "backend_capabilities", {}
                                ).get("native_kont_available", False),
                            )):
                        skip_reason = {
                            "status": "SKIPPED",
                            "reason_code": "BACKEND_UNAVAILABLE",
                            "message": (
                                "ip-kont is registered but the native KONT/COPT "
                                "backend is unavailable"
                            ),
                        }
                        config["execution_disposition"] = skip_reason
                    backend_identity = (
                        exact_backend if algorithm in {
                            "ip-kont", "branch-and-bound"
                        } else "not-applicable"
                    )
                    run_key = make_run_key(
                        input_sha256=input_sha,
                        algorithm=algorithm,
                        objective=objective,
                        seed=seed,
                        repeat=repeat,
                        resolved_backend=backend_identity,
                        configuration=config,
                    )
                    entry = {
                        "schema_version": 1,
                        "run_key": run_key,
                        "instance_id": instance_id,
                        "algorithm": algorithm,
                        "objective": objective,
                        "repeat": repeat,
                        "seed": seed,
                        "configuration_sha256": hashlib.sha256(
                            dumps_json(config).encode("utf-8")
                        ).hexdigest(),
                        "requested_backend": args.exact_reference,
                        "resolved_configuration": config,
                    }
                    validate_document(entry, "run_plan_entry")
                    plan.append(entry)
                    task_data[run_key] = {
                        "input_bytes": canonical,
                        "input_path": snapshot_path,
                        "instance_id": instance_id,
                        "instance_name": instance_name,
                        "family": family,
                        "n": n,
                        "m": m,
                        "input_sha256": input_sha,
                        "source_sha256": source_sha,
                        "source_path": (
                            str(provenance_row.get("source"))
                            if provenance_row else str(source_path.resolve())
                        ),
                        "algorithm": algorithm,
                        "objective": objective,
                        "repeat": repeat,
                        "seed": seed,
                        "config": config,
                        "skip_reason": skip_reason,
                    }
    plan.sort(key=lambda entry: (
        entry["instance_id"], entry["objective"], entry["algorithm"],
        entry["repeat"], entry["run_key"],
    ))
    return plan, task_data


def _write_batch_summary(experiment: Path, manifest: dict[str, Any]) -> None:
    counts = manifest["classifications"]
    lines = [
        "# Immutable run batch summary", "",
        f"- **Expected runs:** {counts['expected_runs']}",
        f"- **Valid run attempts:** {counts['valid_runs']}",
        f"- **Completed attempts:** {counts['completed_runs']}",
        f"- **Failed attempts:** {counts['failed_runs']}",
        f"- **Skipped attempts:** {counts['skipped_runs']}",
        f"- **Time-limited attempts:** {counts['time_limit_runs']}",
        f"- **Missing expected runs:** {counts['missing_runs']}",
        f"- **Invalid run directories:** {counts['invalid_runs']}",
        f"- **Aggregate complete:** {manifest['complete']}", "",
        "Canonical results are stored under `runs/<run_id>/result/result.json`.",
        "The JSON/CSV batch files are derived compatibility exports.",
    ]
    write_derived_bytes(
        experiment / "batch" / "batch_summary.md",
        ("\n".join(lines) + "\n").encode("utf-8"),
    )


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--instances", required=True,
                        help="canonical JSON instances or a selection manifest directory")
    parser.add_argument("--dataset-profile", choices=("full", "smoke20"),
                        default="full")
    parser.add_argument("--dataset-manifest")
    parser.add_argument("--output", default=None)
    parser.add_argument("--algorithms", default="all")
    parser.add_argument(
        "--plan-only", action="store_true",
        help="write and report an immutable run plan without executing solver slots",
    )
    parser.add_argument("--profile", choices=("fast", "exact-reference", "debug"),
                        default="fast")
    parser.add_argument("--seed", type=int, default=42)
    parser.add_argument("--repeats", type=int, default=1)
    parser.add_argument("--resume", action="store_true")
    parser.add_argument("--safety-timeout", type=float, default=None)
    parser.add_argument("--modes",
                        choices=("minmax", "minsum", "minmaxsum", "both", "all"),
                        default="both")
    parser.add_argument("--exact-reference", choices=("ip-kont", "branch-and-bound", "auto"),
                        default="auto")
    parser.add_argument("--threads", type=int, default=None)
    parser.add_argument("--time-limit", type=float, default=None)
    parser.add_argument("--fast-time-limit", type=float, default=None)
    parser.add_argument("--exact-time-limit", type=float, default=None)
    parser.add_argument("--minsum-refinement-policy",
                        choices=("adaptive", "sampled"), default="adaptive")
    parser.add_argument("--minmax-budget-fraction", type=float, default=0.5)
    parser.add_argument("--minsum-budget-fraction", type=float, default=0.5)
    parser.add_argument("--brute-force-max-assignments", type=int, default=100_000)
    parser.add_argument("--brute-force-time-limit-sec", type=float, default=0.5)
    parser.add_argument("--save-solutions", action="store_true")
    parser.add_argument("--save-traces", action="store_true")
    args = parser.parse_args(argv)

    if (not math.isfinite(args.minmax_budget_fraction)
            or not math.isfinite(args.minsum_budget_fraction)
            or args.minmax_budget_fraction <= 0
            or args.minsum_budget_fraction <= 0):
        parser.error("MinMaxSum budget fractions must be positive finite numbers")
    if (args.modes in {"minmaxsum", "all"}
            and not math.isclose(args.minmax_budget_fraction
                                 + args.minsum_budget_fraction,
                                 1.0, rel_tol=0.0, abs_tol=1e-9)):
        parser.error("MinMaxSum budget fractions must sum to one")
    if not SOLVER.is_file():
        parser.error(f"solver executable not found: {SOLVER}")
    source_instances = Path(args.instances).resolve()
    if not source_instances.is_dir():
        parser.error(f"instance directory not found: {source_instances}")
    try:
        resolved = resolve_instance_directory(source_instances)
    except (OSError, ValueError, json.JSONDecodeError) as error:
        parser.error(f"invalid dataset manifest: {error}")
    if resolved != source_instances:
        if args.dataset_manifest is None:
            args.dataset_manifest = str(source_instances / "manifest.json")
        source_instances = resolved
        if args.dataset_profile == "full":
            args.dataset_profile = "smoke20"
    elif (args.dataset_manifest is None
          and source_instances.name == "instances"
          and (source_instances.parent / "manifest.json").is_file()):
        args.dataset_manifest = str(source_instances.parent / "manifest.json")
    if args.dataset_profile == "smoke20" and not args.dataset_manifest:
        parser.error("smoke20 requires the selection manifest for provenance")
    try:
        selection = _load_selection(args.dataset_manifest)
    except (OSError, ValueError, json.JSONDecodeError) as error:
        parser.error(f"invalid dataset selection manifest: {error}")
    if args.dataset_profile == "smoke20":
        try:
            _validate_selection_identity(selection, 20, 50, 25)
        except ValueError as error:
            parser.error(f"invalid smoke20 selection manifest: {error}")
    if selection:
        selected_count = int(selection.get(
            "selected_instance_count", len(selection["instances"])
        ))
        sources = [
            row.get("source") for row in selection["instances"]
            if isinstance(row, dict)
        ]
        file_count = len(list(source_instances.rglob("*.json")))
        if (len(selection["instances"]) != selected_count
                or len(sources) != selected_count
                or len(set(sources)) != selected_count
                or file_count != selected_count):
            parser.error(
                "dataset selection manifest count/uniqueness does not match "
                f"the {file_count} canonical instance files"
            )
    apply_execution_defaults(args)
    if (args.threads <= 0 or args.time_limit <= 0
            or args.fast_time_limit <= 0 or args.exact_time_limit <= 0
            or args.brute_force_max_assignments <= 0
            or not math.isfinite(args.brute_force_time_limit_sec)
            or args.brute_force_time_limit_sec <= 0
            or args.repeats <= 0 or args.seed < 0
            or (args.safety_timeout is not None and args.safety_timeout <= 0)):
        parser.error("threads, repeats, and time limits must be positive; seed nonnegative")
    instance_paths = sorted(source_instances.rglob("*.json"))
    if not instance_paths:
        parser.error("no canonical JSON instances found")
    dataset_fingerprint = hashlib.sha256(
        "".join(sorted(sha256_file(path) for path in instance_paths)).encode(
            "ascii"
        )
    ).hexdigest()

    if args.output is None:
        output = ROOT / "results" / "experiments" / (
            make_experiment_id(args.profile)
        ) / "batch"
    else:
        output = Path(args.output)
        if not output.is_absolute():
            output = ROOT / output
    try:
        experiment, batch_output = _experiment_root(output)
    except ValueError as error:
        parser.error(str(error))
    experiment_preexisting = experiment.exists()
    if (
        experiment_preexisting
        and not (experiment / "experiment_manifest.json").is_file()
        and not (experiment / "runs" / "plan.jsonl").is_file()
        and any(experiment.iterdir())
        and not args.resume
    ):
        parser.error(
            "output directory is non-empty and is not a managed experiment; "
            "choose a new path or use a valid --resume experiment"
        )
    requested_config = {
        "dataset_profile": args.dataset_profile,
        "algorithms": args.algorithms,
        "objectives": args.modes,
        "seed": args.seed,
        "repeats": args.repeats,
        "threads": args.threads,
        "fast_time_limit_sec": args.fast_time_limit,
        "exact_time_limit_sec": args.exact_time_limit,
        "per_static_time_limit_sec": args.time_limit,
        "requested_backend": args.exact_reference,
        "profile": args.profile,
        "minsum_refinement_policy": args.minsum_refinement_policy,
        "save_solutions": args.save_solutions,
        "save_traces": args.save_traces,
        "safety_timeout_sec": args.safety_timeout,
        "brute_force_max_assignments": args.brute_force_max_assignments,
        "brute_force_time_limit_sec": args.brute_force_time_limit_sec,
    }
    pipeline_managed = (experiment / "experiment_manifest.json").is_file()
    create_experiment(
        experiment,
        profile=args.profile,
        config=requested_config,
        dataset_sha256=dataset_fingerprint,
        source_commit=None,
        pipeline_managed=pipeline_managed,
        resume=args.resume,
    )
    batch_output.mkdir(parents=True, exist_ok=True)
    plan_path = experiment / "runs" / "plan.jsonl"
    existing_plan = _read_plan(plan_path)
    if existing_plan and not args.resume:
        parser.error("run plan already exists; use --resume or choose a new experiment")
    if not existing_plan and args.resume and not pipeline_managed:
        parser.error("cannot resume: experiment has no immutable run plan")

    # Reuse the experiment's existing calibration evidence; otherwise write a
    # new calibration in a unique metadata directory.
    calibration_candidates = [
        experiment / "calibration" / "experiment_manifest.json",
        batch_output / "experiment_manifest.json",
        *(
            sorted(
                (experiment / "calibration" / "attempts").glob(
                    "*/experiment_manifest.json"
                ),
                key=lambda path: path.stat().st_mtime,
                reverse=True,
            )
            if (experiment / "calibration" / "attempts").is_dir()
            else []
        ),
    ]
    calibration_manifest_path = next(
        (path for path in calibration_candidates if path.is_file()), None
    )
    if calibration_manifest_path is None:
        calibration_dir = (
            experiment / "calibration" / "attempts"
            / make_experiment_id("calibration")
        )
        calibration = subprocess.run(
            [
                str(SOLVER), "calibrate", "--dataset", str(source_instances),
                "--output", str(calibration_dir),
                "--exact-reference", args.exact_reference,
            ],
            cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            text=True, check=False,
        )
        calibration_manifest_path = calibration_dir / "experiment_manifest.json"
        if calibration.returncode != 0 or not calibration_manifest_path.is_file():
            parser.error(
                "exact-reference calibration failed: "
                + (calibration.stderr or calibration.stdout).strip()[-2000:]
            )
    backend_manifest = read_json(calibration_manifest_path)
    exact_backend = backend_manifest.get("selected_backend")
    if exact_backend not in {"ip-kont", "branch-and-bound"}:
        parser.error("calibration manifest has no valid selected backend")
    if args.exact_reference != "auto" and exact_backend != args.exact_reference:
        parser.error(
            f"requested exact backend {args.exact_reference} is unavailable; "
            f"calibration selected {exact_backend}"
        )

    requested = [name.strip() for name in args.algorithms.split(",") if name.strip()]
    wants_all_registered = "all-registered" in {
        name.lower() for name in requested
    }
    try:
        registered = registered_algorithms()
    except ValueError as error:
        parser.error(str(error))
    if ("ip-kont" in requested and "branch-and-bound" in requested
            and not wants_all_registered):
        parser.error("strict benchmark mode cannot select both exact backends")
    all_requested = not requested or any(
        name.lower() in {"all", "all-fast", "all-comparison"} for name in requested
    )
    selected = set(registered) if wants_all_registered else (
        set(registered) - {"brute-force"} if all_requested else set(requested)
    )
    if args.profile == "exact-reference":
        selected = {exact_backend}
    if not wants_all_registered:
        if exact_backend == "ip-kont":
            selected.discard("branch-and-bound")
        else:
            selected.discard("ip-kont")
        selected.discard("brute-force")
        selected.add(exact_backend)
    unknown = selected - set(registered)
    if unknown:
        parser.error("unknown algorithms: " + ", ".join(sorted(unknown)))
    algorithms = order_algorithms(selected)
    objectives = (
        OBJECTIVES if args.modes == "both"
        else ALL_OBJECTIVES if args.modes == "all"
        else (args.modes,)
    )

    plan, task_data = _build_plan(
        experiment, instance_paths, algorithms, objectives, exact_backend,
        args, backend_manifest, selection,
    )
    if existing_plan:
        if existing_plan != plan:
            parser.error("resume refused: existing run plan differs from configuration")
        recover_experiment(experiment)
    else:
        write_run_plan(experiment, plan)
    experiment_state = read_json(experiment / "experiment.json", schema_name="experiment")
    update_experiment(
        experiment,
        status=(
            "RUNNING" if experiment_state["status"] != "COMPLETE" else None
        ),
        expected_run_count=len(plan),
        run_plan_sha256=sha256_file(plan_path),
        config={
            **requested_config,
            "resolved_exact_backend": exact_backend,
            "resolved_algorithms": algorithms,
        },
        source_commit=backend_manifest.get("git_commit"),
    )
    manifest_path = experiment / "experiment_manifest.json"
    pipeline_manifest = (
        read_json(manifest_path) if manifest_path.is_file() else {}
    )
    git_commit = subprocess.run(
        ["git", "rev-parse", "HEAD"], cwd=ROOT, text=True,
        capture_output=True, check=False,
    )
    git_status = subprocess.run(
        ["git", "status", "--porcelain"], cwd=ROOT, text=True,
        capture_output=True, check=False,
    )
    cache_values = {}
    cache_path = ROOT / "build" / "CMakeCache.txt"
    if cache_path.is_file():
        for line in cache_path.read_text(encoding="utf-8", errors="replace").splitlines():
            if line.startswith(("CMAKE_CXX_COMPILER:FILEPATH=",
                                "CMAKE_BUILD_TYPE:STRING=")):
                key, value = line.split("=", 1)
                cache_values[key.split(":", 1)[0]] = value
    selection_path = None
    selection_fingerprint = None
    if args.dataset_manifest:
        source_selection = Path(args.dataset_manifest).resolve()
        selection_path = experiment / "dataset" / "selection_manifest.json"
        selection_fingerprint = sha256_file(source_selection)
        if selection_path.is_file():
            if sha256_file(selection_path) != selection_fingerprint:
                parser.error("resume refused: selected-instance manifest differs")
        else:
            selection_path.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(source_selection, selection_path)
    pipeline_manifest.update({
        "experiment_id": experiment.name,
        "timestamp": pipeline_manifest.get("timestamp", utc_now()),
        "git_commit": (
            git_commit.stdout.strip() if git_commit.returncode == 0 else None
        ),
        "git_dirty": bool(git_status.stdout.strip())
        if git_status.returncode == 0 else None,
        "host": {
            "platform": f"{platform.system()} {platform.release()}",
            "machine": platform.machine(),
            "logical_cpu_count": os.cpu_count(),
        },
        "python_version": platform.python_version(),
        "source_dataset_fingerprint": pipeline_manifest.get(
            "source_dataset_fingerprint", dataset_fingerprint
        ),
        "selection_manifest": (
            pipeline_manifest.get("selection_manifest")
            or ("dataset/selection_manifest.json" if selection_path else None)
        ),
        "selection_manifest_fingerprint": pipeline_manifest.get(
            "selection_manifest_fingerprint", selection_fingerprint
        ),
        "selected_instance_count": pipeline_manifest.get(
            "selected_instance_count"
        ) or len(instance_paths),
        "dataset_canonical_path": pipeline_manifest.get(
            "dataset_canonical_path", "input"
        ),
        "dataset_fingerprint": pipeline_manifest.get(
            "dataset_fingerprint"
        ) or dataset_fingerprint,
        "algorithm_set": algorithms,
        "objectives": list(objectives),
        "seed": args.seed,
        "repeats": args.repeats,
        "thread_count": args.threads,
        "fast_time_limit_sec": args.fast_time_limit,
        "exact_time_limit_sec": args.exact_time_limit,
        "per_static_time_limit_sec": args.time_limit,
        "requested_exact_backend": args.exact_reference,
        "selected_exact_backend": exact_backend,
        "actual_exact_backend": backend_manifest.get("actual_backend"),
        "verification_policy": {
            "verify_after": True,
            "verify_each_iteration": False,
        },
        "run_plan_sha256": sha256_file(plan_path),
        "reproduction_command": shlex.join(sys.argv),
        "compiler": backend_manifest.get("compiler")
        or cache_values.get("CMAKE_CXX_COMPILER"),
        "build_type": backend_manifest.get("build_type")
        or cache_values.get("CMAKE_BUILD_TYPE"),
        "stages": pipeline_manifest.get("stages", {}),
        "required_stages": pipeline_manifest.get("required_stages", []),
        "status": pipeline_manifest.get("status", "RUNNING"),
    })
    write_derived_json(manifest_path, pipeline_manifest)
    write_derived_json(batch_output / "experiment_manifest.json", {
        **backend_manifest,
        "compiler": backend_manifest.get("compiler")
        or cache_values.get("CMAKE_CXX_COMPILER"),
        "build_type": backend_manifest.get("build_type")
        or cache_values.get("CMAKE_BUILD_TYPE"),
        "experiment_id": experiment.name,
        "selected_backend": exact_backend,
        "algorithm_set": algorithms,
        "dataset_profile": args.dataset_profile,
        "dataset_path": str(source_instances),
        "dataset_fingerprint": dataset_fingerprint,
        "time_limits": {
            "fast_global_sec": args.fast_time_limit,
            "exact_global_sec": args.exact_time_limit,
            "per_static_solve_sec": args.time_limit,
        },
        "seed_policy": {"base_seed": args.seed, "repeats": args.repeats},
    })

    if args.plan_only:
        report_path = experiment / "reports" / "run_plan_report.md"
        report_lines = [
            "# Run plan (not executed)", "",
            f"- Experiment: `{experiment.name}`",
            f"- Registered algorithms: {len(registered)}",
            f"- Selected algorithms: {len(algorithms)}",
            f"- Objectives: {', '.join(objectives)}",
            f"- Instances: {len(instance_paths)}",
            f"- Repeats configured: {args.repeats}",
            f"- Planned logical slots: {len(plan)}",
            f"- Selected exact reference: `{exact_backend}`",
            f"- Actual exact backend: `{backend_manifest.get('actual_backend')}`",
            f"- Native KONT/COPT available: `{backend_manifest.get('native_kont_available', False)}`",
            f"- Brute-force budget: `{args.brute_force_max_assignments}` assignments, "
            f"`{args.brute_force_time_limit_sec:g}` seconds per run",
            "",
            "## Registered algorithm inventory",
            "",
            *[f"- `{name}`" for name in registered],
            "",
            "## Planned objective inventory",
            "",
            *[f"- `{objective}`" for objective in objectives],
            "",
            "No planned solver slots were executed by this plan-only invocation.",
        ]
        write_derived_bytes(
            report_path, ("\n".join(report_lines) + "\n").encode("utf-8")
        )
        index_experiment_artifacts(experiment)
        print("\n".join(report_lines))
        return 0

    pending = []
    for entry in plan:
        attempts = _run_attempts(experiment, entry["run_key"])
        latest = attempts[-1] if attempts else None
        if latest and latest.get("status") == "COMPLETED":
            continue
        if latest and latest.get("status") in {"RUNNING", "PLANNED"}:
            parser.error(
                f"run {latest['run_id']} is still active or was not recovered; "
                "refusing to launch a concurrent retry"
            )
        data = task_data[entry["run_key"]]
        data["rerun_of"] = latest["run_id"] if latest else None
        data["exact_backend"] = exact_backend
        data["profile"] = args.profile
        data["minsum_refinement_policy"] = args.minsum_refinement_policy
        data["safety_timeout"] = args.safety_timeout
        data["save_solutions"] = args.save_solutions
        data["save_traces"] = args.save_traces
        data["skip_reason"] = data.get("skip_reason")
        pending.append(data)
    if experiment_state["status"] == "COMPLETE" and pending:
        parser.error(
            "completed experiments are immutable; create a new experiment "
            "for additional attempts"
        )

    print(
        f"{len(instance_paths)} instances × {len(algorithms)} algorithms × "
        f"{len(objectives)} objectives; {len(pending)} new attempts of "
        f"{len(plan)} planned logical runs.",
        flush=True,
    )
    with ThreadPoolExecutor(max_workers=args.threads) as executor:
        futures = [executor.submit(
            run_one,
            experiment=experiment,
            input_path=item["input_path"],
            instance_id=item["instance_id"],
            instance_name=item["instance_name"],
            family=item["family"],
            n=item["n"],
            m=item["m"],
            input_sha256=item["input_sha256"],
            source_sha256=item["source_sha256"],
            source_path=item["source_path"],
            algorithm=item["algorithm"],
            objective=item["objective"],
            repeat=item["repeat"],
            seed=item["seed"],
            exact_backend=item["exact_backend"],
            config=item["config"],
            profile=item["profile"],
            minsum_refinement_policy=item["minsum_refinement_policy"],
            safety_timeout=item["safety_timeout"],
            rerun_of=item["rerun_of"],
            save_solutions=item["save_solutions"],
            save_traces=item["save_traces"],
            skip_reason=item["skip_reason"],
        ) for item in pending]
        for completed, future in enumerate(as_completed(futures), start=1):
            try:
                future.result()
            except Exception as error:
                print(f"run worker error: {error}", file=sys.stderr)
            if completed % 25 == 0 or completed == len(futures):
                print(f"Finished {completed}/{len(futures)} run attempts.", flush=True)

    aggregate = aggregate_experiment(experiment)
    experiment_state = read_json(experiment / "experiment.json", schema_name="experiment")
    if experiment_state["status"] != "COMPLETE":
        update_experiment(
            experiment,
            status="COMPLETE" if aggregate["complete"] else "PARTIAL",
        )
    if not pipeline_managed:
        pipeline_manifest = read_json(manifest_path)
        pipeline_manifest["status"] = (
            "COMPLETE" if aggregate["complete"] else "PARTIAL"
        )
        pipeline_manifest.setdefault("stages", {})["benchmark"] = {
            "status": "COMPLETED" if aggregate["complete"] else "PARTIAL",
            "exit_code": 0,
        }
        write_derived_json(manifest_path, pipeline_manifest)
    _write_batch_summary(experiment, aggregate)
    index_experiment_artifacts(experiment)
    print(f"Aggregate: {experiment / 'aggregates' / 'results.json'}")
    print(f"Aggregate complete: {aggregate['complete']}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
