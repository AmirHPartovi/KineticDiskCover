"""Create-only run storage primitives for experiment evidence."""

from __future__ import annotations

import hashlib
import csv
import datetime as dt
import os
from pathlib import Path
import re
import tempfile
import uuid
from typing import Any

from .schemas import dumps_json, loads_json, validate_document


_RUN_ID = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._-]{0,127}$")
_RUN_STATE_TRANSITIONS = {
    "PLANNED": {"RUNNING", "FAILED", "CANCELLED", "SKIPPED"},
    "RUNNING": {
        "COMPLETED", "FAILED", "TIME_LIMIT", "TIMED_OUT", "CANCELLED",
        "INVALID", "ABANDONED",
    },
    "COMPLETED": set(),
    "FAILED": set(),
    "TIME_LIMIT": set(),
    "CANCELLED": set(),
    "INVALID": set(),
    "SKIPPED": set(),
    "ABANDONED": set(),
    "TIMED_OUT": set(),
}
_EXPERIMENT_STATE_TRANSITIONS = {
    "CREATED": {"RUNNING", "FAILED", "CANCELLED"},
    "RUNNING": {"COMPLETE", "PARTIAL", "FAILED", "CANCELLED"},
    "PARTIAL": {"RUNNING", "FAILED", "CANCELLED"},
    "COMPLETE": set(),
    "FAILED": set(),
    "CANCELLED": set(),
}


def _safe_relative_path(relative_path: str | Path) -> Path:
    path = Path(relative_path)
    if (
        path.is_absolute()
        or not path.parts
        or any(part in ("", ".", "..") for part in path.parts)
        or "\x00" in str(relative_path)
    ):
        raise ValueError(f"expected a safe relative path, got {relative_path!r}")
    return path


def _reject_terminal_write(run_directory: Path) -> None:
    status_path = run_directory / "execution" / "status.json"
    if not status_path.is_file():
        return
    try:
        status = read_json(status_path, schema_name="run_status")
    except (OSError, ValueError):
        return
    if status["state"] in _RUN_STATE_TRANSITIONS and not _RUN_STATE_TRANSITIONS[
        status["state"]
    ]:
        raise ValueError(
            f"cannot add canonical artifacts to terminal run {run_directory.name}"
        )


def _ensure_within(root: Path, candidate: Path) -> Path:
    resolved = candidate.resolve()
    try:
        resolved.relative_to(root)
    except ValueError as exc:
        raise ValueError(f"path escapes run directory: {candidate}") from exc
    return resolved


def create_run_directory(experiment_dir: str | Path, run_id: str) -> Path:
    """Create one run directory exclusively; an existing ID is never reused."""
    if not _RUN_ID.fullmatch(run_id):
        raise ValueError(f"invalid run ID: {run_id!r}")

    experiment = Path(experiment_dir).resolve()
    runs = experiment / "runs"
    runs.mkdir(parents=True, exist_ok=True)
    runs = _ensure_within(experiment, runs)
    run_directory = runs / run_id
    run_directory.mkdir()
    return run_directory


def write_json_create_only(
    run_directory: str | Path,
    relative_path: str | Path,
    payload: Any,
    *,
    schema_name: str | None = None,
) -> Path:
    """Atomically publish strict JSON without replacing an existing artifact."""
    root = Path(run_directory).resolve()
    _reject_terminal_write(root)
    relative = _safe_relative_path(relative_path)
    if schema_name is not None:
        validate_document(payload, schema_name)

    destination = root / relative
    _ensure_within(root, destination.parent)
    destination.parent.mkdir(parents=True, exist_ok=True)
    parent = _ensure_within(root, destination.parent)
    destination = parent / relative.name
    serialized = (dumps_json(payload, pretty=True) + "\n").encode("utf-8")

    descriptor, temporary_name = tempfile.mkstemp(
        prefix=f".{destination.name}.",
        suffix=".tmp",
        dir=parent,
    )
    temporary = Path(temporary_name)
    try:
        with os.fdopen(descriptor, "wb") as output:
            output.write(serialized)
            output.flush()
            os.fsync(output.fileno())
        os.link(temporary, destination)
        directory_fd = os.open(parent, os.O_RDONLY)
        try:
            os.fsync(directory_fd)
        finally:
            os.close(directory_fd)
    finally:
        try:
            temporary.unlink()
        except FileNotFoundError:
            pass
    return destination


def write_bytes_create_only(
    run_directory: str | Path,
    relative_path: str | Path,
    contents: bytes,
) -> Path:
    """Atomically publish immutable bytes within a run directory."""
    root = Path(run_directory).resolve()
    _reject_terminal_write(root)
    relative = _safe_relative_path(relative_path)
    destination = root / relative
    _ensure_within(root, destination.parent)
    destination.parent.mkdir(parents=True, exist_ok=True)
    parent = _ensure_within(root, destination.parent)
    destination = parent / relative.name
    descriptor, temporary_name = tempfile.mkstemp(
        prefix=f".{destination.name}.", suffix=".tmp", dir=parent
    )
    temporary = Path(temporary_name)
    try:
        with os.fdopen(descriptor, "wb") as output:
            output.write(contents)
            output.flush()
            os.fsync(output.fileno())
        os.link(temporary, destination)
    finally:
        temporary.unlink(missing_ok=True)
    return destination


def _atomic_replace_json(path: Path, payload: Any) -> None:
    """Atomically replace mutable derived state owned by this module."""
    path.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary_name = tempfile.mkstemp(
        prefix=f".{path.name}.", suffix=".tmp", dir=path.parent
    )
    temporary = Path(temporary_name)
    try:
        with os.fdopen(descriptor, "w", encoding="utf-8") as output:
            output.write(dumps_json(payload, pretty=True) + "\n")
            output.flush()
            os.fsync(output.fileno())
        os.replace(temporary, path)
    finally:
        temporary.unlink(missing_ok=True)


def write_derived_json(path: str | Path, payload: Any) -> Path:
    """Atomically replace a derived, non-canonical JSON view."""
    destination = Path(path).resolve()
    _atomic_replace_json(destination, payload)
    return destination


def write_derived_bytes(path: str | Path, contents: bytes) -> Path:
    """Atomically replace a non-canonical derived artifact."""
    destination = Path(path).resolve()
    _atomic_replace_bytes(destination, contents)
    return destination


def utc_now() -> str:
    return dt.datetime.now(dt.timezone.utc).isoformat().replace("+00:00", "Z")


def safe_id(value: str, *, fallback: str = "unnamed") -> str:
    cleaned = re.sub(r"[^A-Za-z0-9._-]+", "_", value).strip("._-")
    return cleaned[:80] or fallback


def make_run_id(
    algorithm: str,
    objective: str,
    instance_id: str,
    repeat: int,
    *,
    now: dt.datetime | None = None,
) -> str:
    """Return a readable, collision-resistant physical execution identifier."""
    timestamp = (now or dt.datetime.now(dt.timezone.utc)).strftime(
        "%Y%m%dT%H%M%SZ"
    )
    return (
        f"run_{timestamp}__{safe_id(algorithm)[:20]}__"
        f"{safe_id(objective)[:12]}__{safe_id(instance_id)[:38]}__"
        f"r{repeat:02d}__{uuid.uuid4().hex[:12]}"
    )


def make_run_key(
    *,
    input_sha256: str,
    algorithm: str,
    objective: str,
    seed: int,
    repeat: int,
    resolved_backend: str,
    configuration: dict[str, Any],
) -> str:
    """Hash logical execution identity independently of physical run ID."""
    identity = {
        "input_sha256": input_sha256,
        "algorithm": algorithm,
        "objective": objective,
        "seed": seed,
        "repeat": repeat,
        "resolved_backend": resolved_backend,
        "configuration": configuration,
    }
    import json

    return hashlib.sha256(dumps_json(identity).encode("utf-8")).hexdigest()


def reserve_run(
    experiment_dir: str | Path,
    *,
    run_id: str,
    run_key: str,
    experiment_id: str,
    instance_id: str,
    algorithm: str,
    objective: str,
    repeat: int,
    seed: int,
    requested_backend: str,
    configuration: dict[str, Any],
    input_reference: dict[str, Any],
    rerun_of: str | None = None,
) -> Path:
    """Reserve a run before execution and persist immutable request/input data."""
    run_dir = create_run_directory(experiment_dir, run_id)
    write_json_create_only(
        run_dir,
        "request.json",
        {
            "schema_version": 1,
            "experiment_id": experiment_id,
            "run_id": run_id,
            "run_key": run_key,
            "instance_id": instance_id,
            "algorithm": algorithm,
            "objective": objective,
            "repeat": repeat,
            "seed": seed,
            "requested_backend": requested_backend,
            "resolved_configuration": configuration,
            "rerun_of": rerun_of,
        },
        schema_name="request",
    )
    write_json_create_only(
        run_dir,
        "input_ref.json",
        input_reference,
        schema_name="input_ref",
    )
    write_json_create_only(
        run_dir,
        "execution/status.json",
        {
            "schema_version": 1,
            "run_id": run_id,
            "state": "PLANNED",
            "updated_at": utc_now(),
            "process_id": None,
        },
        schema_name="run_status",
    )
    return run_dir


def create_experiment(
    experiment_dir: str | Path,
    *,
    profile: str,
    config: dict[str, Any],
    dataset_sha256: str,
    source_commit: str | None,
    pipeline_managed: bool = False,
) -> Path:
    """Create or register one managed experiment without reusing another."""
    root = Path(experiment_dir).resolve()
    if not root.exists():
        root.parent.mkdir(parents=True, exist_ok=True)
        root.mkdir()
    elif not root.is_dir():
        raise FileExistsError(f"experiment path is not a directory: {root}")

    path = root / "experiment.json"
    config_hash = hashlib.sha256(dumps_json(config).encode("utf-8")).hexdigest()
    if path.exists():
        existing = read_json(path, schema_name="experiment")
        if (
            existing["experiment_id"] != root.name
            or existing["profile"] != profile
            or existing["request_config_sha256"] != config_hash
            or existing["dataset_manifest"]["sha256"] != dataset_sha256
        ):
            raise ValueError(
                "experiment resume refused: identity, configuration or dataset differs"
            )
        return path
    if any(root.iterdir()) and not pipeline_managed:
        raise FileExistsError(
            f"refusing to register a non-empty unmanaged experiment: {root}"
        )
    manifest = {
        "schema_version": 1,
        "experiment_id": root.name,
        "created_at": utc_now(),
        "profile": profile,
        "status": "RUNNING",
        "config": config,
        "config_sha256": config_hash,
        "request_config_sha256": config_hash,
        "source_commit": source_commit,
        "dataset_manifest": {
            "path": "input",
            "sha256": dataset_sha256,
        },
        "expected_run_count": 0,
    }
    return write_json_create_only(
        root, "experiment.json", manifest, schema_name="experiment"
    )


def update_experiment(
    experiment_dir: str | Path,
    *,
    status: str | None = None,
    expected_run_count: int | None = None,
    run_plan_sha256: str | None = None,
    config: dict[str, Any] | None = None,
    source_commit: str | None = None,
) -> dict[str, Any]:
    """Update the mutable experiment envelope before/after execution."""
    root = Path(experiment_dir).resolve()
    path = root / "experiment.json"
    manifest = read_json(path, schema_name="experiment")
    if config is not None:
        manifest["config"] = config
        manifest["config_sha256"] = hashlib.sha256(
            dumps_json(config).encode("utf-8")
        ).hexdigest()
    if source_commit is not None:
        manifest["source_commit"] = source_commit
    if expected_run_count is not None:
        manifest["expected_run_count"] = expected_run_count
    if run_plan_sha256 is not None:
        manifest["run_plan_sha256"] = run_plan_sha256
    if status is not None:
        current = manifest["status"]
        if status not in _EXPERIMENT_STATE_TRANSITIONS:
            raise ValueError(f"unknown experiment state: {status!r}")
        if status != current and status not in _EXPERIMENT_STATE_TRANSITIONS[current]:
            raise ValueError(f"invalid experiment-state transition: {current} -> {status}")
        manifest["status"] = status
    validate_document(manifest, "experiment")
    _atomic_replace_json(path, manifest)
    return manifest


def transition_run(
    run_dir: str | Path,
    requested_state: str,
    *,
    process_id: int | None = None,
    message: str | None = None,
) -> None:
    """Persist a legal state transition; terminal status cannot be changed."""
    path = Path(run_dir).resolve() / "execution" / "status.json"
    current = read_json(path, schema_name="run_status")
    validate_state_transition(current["state"], requested_state)
    updated = {
        **current,
        "state": requested_state,
        "updated_at": utc_now(),
        "process_id": process_id,
        "message": message,
    }
    if requested_state == "RUNNING" and current["state"] == "PLANNED":
        updated["started_at"] = updated["updated_at"]
    validate_document(updated, "run_status")
    _atomic_replace_json(path, updated)


def _artifact_records(run_dir: Path) -> list[dict[str, Any]]:
    artifacts = []
    for path in sorted(run_dir.rglob("*")):
        if not path.is_file():
            continue
        relative = path.relative_to(run_dir).as_posix()
        if relative in {"artifacts.json", "run.json", "execution/status.json"}:
            continue
        artifacts.append({
            "path": relative,
            "sha256": sha256_file(path),
            "size_bytes": path.stat().st_size,
            "media_type": (
                "application/json" if path.suffix == ".json"
                else "text/csv" if path.suffix == ".csv"
                else "application/jsonl" if path.suffix == ".jsonl"
                else "text/plain"
            ),
        })
    return artifacts


def finalize_run(
    run_dir: str | Path,
    *,
    state: str,
    result_document: dict[str, Any],
    verification_document: dict[str, Any],
    backend: dict[str, Any],
    provenance: dict[str, Any],
    source_commit: str | None,
) -> Path:
    """Validate, checksum and finalize a reserved run as a create-only package."""
    if state not in {
        "COMPLETED", "FAILED", "TIME_LIMIT", "CANCELLED", "INVALID",
        "SKIPPED", "ABANDONED", "TIMED_OUT",
    }:
        raise ValueError(f"not a terminal run state: {state}")
    root = Path(run_dir).resolve()
    current = read_json(root / "execution" / "status.json", schema_name="run_status")
    if current["state"] == "PLANNED":
        transition_run(root, "RUNNING")
        current = read_json(root / "execution" / "status.json", schema_name="run_status")

    request = read_json(root / "request.json", schema_name="request")
    input_reference = read_json(root / "input_ref.json", schema_name="input_ref")
    validate_document(result_document, "result")
    write_json_create_only(
        root, "result/result.json", result_document, schema_name="result"
    )
    verification = {
        **verification_document,
        "schema_version": 1,
        "run_id": request["run_id"],
        "experiment_id": request["experiment_id"],
        "result_sha256": sha256_file(root / "result" / "result.json"),
        "checked_at": verification_document.get("checked_at", utc_now()),
    }
    validate_document(verification, "verification")
    write_json_create_only(root, "verification/verification.json", verification,
                           schema_name="verification")

    artifacts = _artifact_records(root)

    solver_result = result_document.get("solver_result") or {}
    summary = {
        "feasible": bool(solver_result.get("feasible", False)),
        "objective_value": _finite_or_none(solver_result.get("objective_value")),
        "lower_bound": _finite_or_none(solver_result.get("lower_bound")),
        "upper_bound": _finite_or_none(solver_result.get("upper_bound")),
        "optimality_status": _canonical_optimality(solver_result),
        "error_message": result_document.get("error_message"),
        "solver_result": solver_result,
    }
    record = {
        "schema_version": 1,
        "run_id": request["run_id"],
        "run_key": request["run_key"],
        "experiment_id": request["experiment_id"],
        "instance_id": request["instance_id"],
        "algorithm": request["algorithm"],
        "objective": request["objective"],
        "repeat": request["repeat"],
        "seed": request["seed"],
        "rerun_of": request.get("rerun_of"),
        "status": state,
        "requested_backend": request["requested_backend"],
        "selected_backend": backend.get("selected"),
        "actual_backend": backend.get("actual"),
        "backend_details": {
            "native": bool(backend.get("native", False)),
            "backend_version": str(backend.get("version", "unknown")),
            "backend_detection": backend.get("detection", {}),
            "capabilities": backend.get("capabilities", {}),
        },
        "started_at": current.get("started_at", current["updated_at"]),
        "finished_at": utc_now(),
        "configuration_sha256": hashlib.sha256(
            dumps_json(request["resolved_configuration"]).encode("utf-8")
        ).hexdigest(),
        "input_sha256": input_reference["canonical_sha256"],
        "n": input_reference["n"],
        "m": input_reference["m"],
        "family": input_reference["family"],
        "resolved_configuration": request["resolved_configuration"],
        "provenance": {
            "source_commit": source_commit,
            "solver_version": provenance.get("solver_version", "unknown"),
            "platform": provenance.get("platform", "unknown"),
        },
        "result": summary,
        "verification": verification,
        "artifacts": artifacts,
    }
    validate_document(record, "run_record")
    record_path = write_json_create_only(
        root, "run.json", record, schema_name="run_record"
    )
    final_artifacts = _artifact_records(root) + [{
        "path": "run.json",
        "sha256": sha256_file(record_path),
        "size_bytes": record_path.stat().st_size,
        "media_type": "application/json",
    }]
    final_artifacts.sort(key=lambda artifact: artifact["path"])
    artifact_manifest = {"schema_version": 1, "artifacts": final_artifacts}
    write_json_create_only(
        root, "artifacts.json", artifact_manifest,
        schema_name="artifact_manifest",
    )
    transition_run(root, state)
    return record_path


def _finite_or_none(value: Any) -> float | None:
    if value is None or isinstance(value, bool):
        return None
    try:
        number = float(value)
    except (TypeError, ValueError):
        return None
    import math

    return number if math.isfinite(number) else None


def _canonical_optimality(result: dict[str, Any]) -> str:
    status = str(result.get("optimality_status", "FAILED")).upper()
    return status if status in {"OPTIMAL", "FEASIBLE", "TIME_LIMIT", "FAILED"} else "FAILED"


def validate_run_directory(run_dir: str | Path, *, experiment_id: str | None = None) -> dict[str, Any]:
    """Validate canonical run metadata, result/verification and checksums."""
    root = Path(run_dir).resolve()
    record = read_json(root / "run.json", schema_name="run_record")
    status = read_json(root / "execution" / "status.json", schema_name="run_status")
    if record["run_id"] != root.name:
        raise ValueError(f"run ID does not match directory: {root}")
    if record["status"] != status["state"]:
        raise ValueError(f"run manifest/status disagree: {root}")
    if experiment_id is not None and record["experiment_id"] != experiment_id:
        raise ValueError(f"run belongs to another experiment: {root}")
    read_json(root / "request.json", schema_name="request")
    input_reference = read_json(root / "input_ref.json", schema_name="input_ref")
    canonical_input = _ensure_within(
        root, root / _safe_relative_path(input_reference["canonical_path"])
    )
    if not canonical_input.is_file():
        raise ValueError(f"canonical run input is missing: {canonical_input}")
    if sha256_file(canonical_input) != record["input_sha256"]:
        raise ValueError("canonical input checksum mismatch")
    read_json(root / "result" / "result.json", schema_name="result")
    read_json(
        root / "verification" / "verification.json",
        schema_name="verification",
    )
    manifest = read_json(root / "artifacts.json", schema_name="artifact_manifest")
    for artifact in manifest["artifacts"]:
        path = _ensure_within(root, root / _safe_relative_path(artifact["path"]))
        if not path.is_file():
            raise ValueError(f"registered artifact is missing: {artifact['path']}")
        if sha256_file(path) != artifact["sha256"]:
            raise ValueError(f"artifact checksum mismatch: {artifact['path']}")
        if path.stat().st_size != artifact["size_bytes"]:
            raise ValueError(f"artifact size mismatch: {artifact['path']}")
    return record


def write_run_plan(experiment_dir: str | Path, entries: list[dict[str, Any]]) -> Path:
    """Create an immutable, deterministic plan before any run is launched."""
    path = Path(experiment_dir).resolve() / "runs" / "plan.jsonl"
    normalized = sorted(
        entries,
        key=lambda entry: (
            entry["instance_id"], entry["objective"], entry["algorithm"],
            entry["repeat"], entry["run_key"],
        ),
    )
    content = "".join(dumps_json(entry) + "\n" for entry in normalized)
    write_bytes_create_only(Path(experiment_dir), "runs/plan.jsonl", content.encode())
    return path


def aggregate_experiment(experiment_dir: str | Path) -> dict[str, Any]:
    """Validate every terminal run and generate deterministic aggregate exports."""
    experiment = Path(experiment_dir).resolve()
    experiment_id = experiment.name
    plan_path = experiment / "runs" / "plan.jsonl"
    plan = []
    if plan_path.is_file():
        with plan_path.open(encoding="utf-8") as source:
            for line_number, line in enumerate(source, 1):
                if not line.strip():
                    raise ValueError(f"empty run-plan line {line_number}")
                entry = loads_json(line)
                validate_document(entry, "run_plan_entry")
                plan.append(entry)

    records: list[dict[str, Any]] = []
    invalid: list[dict[str, str]] = []
    runs_root = experiment / "runs"
    for run_dir in sorted(path for path in runs_root.iterdir() if path.is_dir()):
        if run_dir.name == "plan.jsonl":
            continue
        try:
            records.append(validate_run_directory(run_dir, experiment_id=experiment_id))
        except (OSError, ValueError) as error:
            invalid.append({"run_id": run_dir.name, "error": str(error)})

    records.sort(key=lambda record: (
        record["instance_id"], record["objective"], record["algorithm"],
        record["repeat"], record["run_id"],
    ))
    attempts_by_key: dict[str, list[dict[str, Any]]] = {}
    for record in records:
        attempts_by_key.setdefault(record["run_key"], []).append(record)
    missing = [
        entry["run_key"] for entry in plan
        if entry["run_key"] not in attempts_by_key
    ]
    attempts = {
        key: [row["run_id"] for row in rows]
        for key, rows in attempts_by_key.items() if len(rows) > 1
    }
    unexpected = [
        key for key in attempts_by_key
        if plan and key not in {entry["run_key"] for entry in plan}
    ]
    latest_by_key = {
        key: max(rows, key=lambda row: (row["finished_at"], row["run_id"]))
        for key, rows in attempts_by_key.items()
    }
    classifications = {
        "expected_runs": len(plan),
        "valid_runs": len(records),
        "completed_runs": sum(row["status"] == "COMPLETED" for row in records),
        "failed_runs": sum(row["status"] == "FAILED" for row in records),
        "time_limit_runs": sum(row["status"] in {"TIME_LIMIT", "TIMED_OUT"} for row in records),
        "invalid_runs": len(invalid),
        "missing_runs": len(missing),
        "rerun_attempt_groups": len(attempts),
        "extra_attempts": sum(len(rows) - 1 for rows in attempts.values()),
        "unexpected_runs": len(unexpected),
        "latest_completed_runs": sum(
            row["status"] == "COMPLETED" for row in latest_by_key.values()
        ),
        "latest_failed_runs": sum(
            row["status"] == "FAILED" for row in latest_by_key.values()
        ),
        "latest_time_limit_runs": sum(
            row["status"] in {"TIME_LIMIT", "TIMED_OUT"}
            for row in latest_by_key.values()
        ),
    }
    aggregates = experiment / "aggregates"
    aggregates.mkdir(parents=True, exist_ok=True)
    jsonl = "".join(dumps_json(record) + "\n" for record in records)
    _atomic_replace_bytes(aggregates / "results.jsonl", jsonl.encode("utf-8"))
    _atomic_replace_json(aggregates / "results.json", records)
    _write_results_csv(aggregates / "results.csv", records)
    index_rows = [{
        "run_id": record["run_id"],
        "run_key": record["run_key"],
        "instance_id": record["instance_id"],
        "algorithm_id": record["algorithm"],
        "objective": record["objective"],
        "repeat": record["repeat"],
        "seed": record["seed"],
        "status": record["status"],
        "verification_status": (
            "PASSED" if record["verification"]["passed"] else "FAILED"
        ),
        "actual_backend": record["actual_backend"],
        "objective_value": record["result"]["objective_value"],
        "runtime": (
            record["result"]["solver_result"].get("wall_time_sec")
            if record["result"].get("solver_result") else None
        ),
        "result_path": f"runs/{record['run_id']}/result/result.json",
    } for record in records]
    _atomic_replace_bytes(
        experiment / "runs" / "index.jsonl",
        "".join(dumps_json(row) + "\n" for row in index_rows).encode("utf-8"),
    )
    plan_keys = {entry["run_key"] for entry in plan}
    manifest = {
        "schema_version": 1,
        "experiment_id": experiment_id,
        "generated_at": utc_now(),
        "plan_sha256": sha256_file(plan_path) if plan_path.is_file() else None,
        "record_count": len(records),
        "classifications": classifications,
        "missing_run_keys": missing,
        "rerun_attempts": attempts,
        "unexpected_run_keys": unexpected,
        "invalid_runs": invalid,
        "complete": bool(plan) and not (
            missing or unexpected or invalid
        ) and all(
            row["status"] == "COMPLETED" for row in latest_by_key.values()
        ) and set(latest_by_key) == plan_keys,
    }
    # Legacy consumers receive exports derived exclusively from validated run
    # records; these are compatibility views, never solver destinations.
    _atomic_replace_json(experiment / "batch" / "master_results.json", [
        _legacy_result(record) for record in records
    ])
    _write_results_csv(
        experiment / "batch" / "master_results.csv",
        [_legacy_result(record) for record in records],
    )
    export_paths = (
        "aggregates/results.jsonl",
        "aggregates/results.json",
        "aggregates/results.csv",
        "runs/index.jsonl",
        "batch/master_results.json",
        "batch/master_results.csv",
    )
    manifest["exports"] = {
        relative: {
            "sha256": sha256_file(experiment / relative),
            "size_bytes": (experiment / relative).stat().st_size,
        }
        for relative in export_paths
    }
    _atomic_replace_json(aggregates / "aggregate_manifest.json", manifest)
    return manifest


def validate_complete_aggregate(experiment_dir: str | Path) -> dict[str, Any]:
    """Verify that derived exports are complete, current and checksummed."""
    experiment = Path(experiment_dir).resolve()
    manifest_path = experiment / "aggregates" / "aggregate_manifest.json"
    manifest = read_json(manifest_path)
    if manifest.get("experiment_id") != experiment.name:
        raise ValueError("aggregate belongs to a different experiment")
    if manifest.get("complete") is not True:
        raise ValueError("experiment aggregate is incomplete")
    plan_path = experiment / "runs" / "plan.jsonl"
    current_plan_sha = sha256_file(plan_path) if plan_path.is_file() else None
    if manifest.get("plan_sha256") != current_plan_sha:
        raise ValueError("aggregate run plan checksum is stale")
    exports = manifest.get("exports")
    if not isinstance(exports, dict) or not exports:
        raise ValueError("aggregate export checksums are missing")
    for relative, metadata in exports.items():
        path = _ensure_within(experiment, experiment / _safe_relative_path(relative))
        if not path.is_file():
            raise ValueError(f"aggregate export is missing: {relative}")
        if sha256_file(path) != metadata.get("sha256"):
            raise ValueError(f"aggregate export checksum mismatch: {relative}")
        if path.stat().st_size != metadata.get("size_bytes"):
            raise ValueError(f"aggregate export size mismatch: {relative}")
    return manifest


def validate_results_input(path: str | Path) -> None:
    """Validate managed experiment exports before a reporting tool reads them."""
    source = Path(path).resolve()
    experiment = (
        source.parent.parent
        if source.parent.name in {"aggregates", "batch"}
        else source.parent
    )
    if (experiment / "experiment.json").is_file():
        validate_complete_aggregate(experiment)


def recover_experiment(experiment_dir: str | Path) -> list[str]:
    """Mark stale RUNNING reservations abandoned without rewriting evidence."""
    experiment = Path(experiment_dir).resolve()
    recovered = []
    runs = experiment / "runs"
    if not runs.is_dir():
        return recovered
    for run_dir in sorted(path for path in runs.iterdir() if path.is_dir()):
        status_path = run_dir / "execution" / "status.json"
        if not status_path.is_file():
            continue
        try:
            status = read_json(status_path, schema_name="run_status")
        except (OSError, ValueError):
            continue
        if status["state"] not in {"PLANNED", "RUNNING"}:
            continue
        process_id = status.get("process_id")
        if process_id is not None:
            try:
                os.kill(process_id, 0)
            except ProcessLookupError:
                pass
            except PermissionError:
                continue
            else:
                continue
        run_manifest = run_dir / "run.json"
        if run_manifest.is_file():
            try:
                record = read_json(run_manifest, schema_name="run_record")
                validate_run_directory(run_dir, experiment_id=experiment.name)
                if record["status"] in _RUN_STATE_TRANSITIONS[status["state"]]:
                    transition_run(run_dir, record["status"])
                    recovered.append(run_dir.name)
                    continue
            except (OSError, ValueError):
                pass
        result_document = {
            "schema_version": 1,
            "run_id": status["run_id"],
            "experiment_id": experiment.name,
            "run_key": read_json(run_dir / "request.json", schema_name="request")[
                "run_key"
            ],
            "execution_status": "ABANDONED",
            "solver_result": None,
            "error_message": "Recovered stale RUNNING execution; process is not active.",
        }
        verification = {
            "verification_mode": "not_run",
            "status": "NOT_RUN",
            "kind": "NONE",
            "passed": False,
            "continuous": False,
            "message": "Solver process terminated before verification.",
        }
        backend = {
            "selected": None,
            "actual": None,
        }
        try:
            finalize_run(
                run_dir,
                state="ABANDONED",
                result_document=result_document,
                verification_document=verification,
                backend=backend,
                provenance={"solver_version": "unknown", "platform": os.name},
                source_commit=None,
            )
        except (OSError, ValueError) as error:
            recovery_path = run_dir / "execution" / "recovery.json"
            current_status = read_json(
                run_dir / "execution" / "status.json", schema_name="run_status"
            )
            if not recovery_path.exists() and current_status["state"] in {
                "PLANNED", "RUNNING"
            }:
                write_json_create_only(run_dir, "execution/recovery.json", {
                    "schema_version": 1,
                    "detected_at": utc_now(),
                    "classification": "ABANDONED",
                    "finalization_error": str(error),
                })
            if current_status["state"] == "PLANNED":
                transition_run(run_dir, "RUNNING")
            if current_status["state"] in {"PLANNED", "RUNNING"}:
                transition_run(
                    run_dir,
                    "ABANDONED",
                    message=(
                        "Recovered orphaned execution; package finalization failed."
                    ),
                )
        recovered.append(run_dir.name)
    return recovered


def _atomic_replace_bytes(path: Path, contents: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary_name = tempfile.mkstemp(
        prefix=f".{path.name}.", suffix=".tmp", dir=path.parent
    )
    temporary = Path(temporary_name)
    try:
        with os.fdopen(descriptor, "wb") as output:
            output.write(contents)
            output.flush()
            os.fsync(output.fileno())
        os.replace(temporary, path)
    finally:
        temporary.unlink(missing_ok=True)


def _legacy_result(record: dict[str, Any]) -> dict[str, Any]:
    result = record["result"]
    solver_result = result.get("solver_result") or {}
    solution_path = solver_result.get("solution_json_path")
    trace_path = solver_result.get("trace_csv_path")
    return {
        **solver_result,
        "schema_version": solver_result.get("schema_version", 1),
        "run_id": record["run_id"],
        "run_key": record["run_key"],
        "experiment_id": record["experiment_id"],
        "instance_name": solver_result.get("instance_name", record["instance_id"]),
        "n": record.get("n", 0),
        "m": record.get("m", 0),
        "algorithm_name": record["algorithm"],
        "objective": record["objective"],
        "repeat": record["repeat"],
        "seed": record["seed"],
        "requested_backend": record["requested_backend"],
        "actual_backend": record["actual_backend"],
        "verification_kind": record["verification"]["kind"],
        "verified": record["verification"]["passed"],
        "result_json_path": f"runs/{record['run_id']}/result/result.json",
        "solution_json_path": (
            f"runs/{record['run_id']}/{solution_path}"
            if solution_path else None
        ),
        "trace_csv_path": (
            f"runs/{record['run_id']}/{trace_path}"
            if trace_path else None
        ),
        "trace_jsonl_path": (
            f"runs/{record['run_id']}/result/trace.jsonl"
            if trace_path else None
        ),
        "run_status": record["status"],
        "failed": record["status"] in {"FAILED", "INVALID", "ABANDONED"},
        "time_limited": record["status"] in {"TIME_LIMIT", "TIMED_OUT"},
        "timeout": record["status"] in {"TIME_LIMIT", "TIMED_OUT"},
        "feasible": result["feasible"],
        "verified": record["verification"]["passed"],
        "error_message": result["error_message"],
    }


def _write_results_csv(path: Path, records: list[dict[str, Any]]) -> None:
    if not records:
        _atomic_replace_bytes(path, b"run_id,run_key,instance_id,algorithm,objective,status\n")
        return
    rows = [
        _legacy_result(row) if "run_key" in row and "algorithm" in row else row
        for row in records
    ]
    rows = [{
        key: dumps_json(value) if isinstance(value, (dict, list)) else value
        for key, value in row.items()
    } for row in rows]
    fields = sorted({key for row in rows for key in row})
    path.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary_name = tempfile.mkstemp(
        prefix=f".{path.name}.", suffix=".tmp", dir=path.parent
    )
    temporary = Path(temporary_name)
    try:
        with os.fdopen(descriptor, "w", encoding="utf-8", newline="") as output:
            writer = csv.DictWriter(output, fieldnames=fields, extrasaction="ignore")
            writer.writeheader()
            writer.writerows(rows)
            output.flush()
            os.fsync(output.fileno())
        os.replace(temporary, path)
    finally:
        temporary.unlink(missing_ok=True)


def sha256_file(path: str | Path) -> str:
    """Return a streaming SHA-256 digest for an artifact."""
    digest = hashlib.sha256()
    with Path(path).open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def validate_state_transition(current: str, requested: str) -> None:
    """Reject unsupported run-state transitions, including terminal rewrites."""
    allowed = _RUN_STATE_TRANSITIONS.get(current)
    if allowed is None:
        raise ValueError(f"unknown current run state: {current!r}")
    if requested not in _RUN_STATE_TRANSITIONS:
        raise ValueError(f"unknown requested run state: {requested!r}")
    if requested not in allowed:
        raise ValueError(
            f"invalid run-state transition: {current} -> {requested}"
        )


def read_json(path: str | Path, *, schema_name: str | None = None) -> Any:
    """Read strict JSON and optionally validate it against a named schema."""
    document = loads_json(Path(path).read_text(encoding="utf-8"))
    if schema_name is not None:
        validate_document(document, schema_name)
    return document
