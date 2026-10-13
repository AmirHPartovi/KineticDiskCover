#!/usr/bin/env python3
"""Build data-correct, experiment-scoped tables and figures from canonical runs."""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import math
from pathlib import Path
import re
import sys
from typing import Any, Iterable, Mapping, Sequence

import numpy as np

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))

from kdc_tools.reporting import audit_companions, write_delimited  # noqa: E402
from kdc_tools.schemas import loads_json, validate_document  # noqa: E402
from kdc_tools.storage import (  # noqa: E402
    read_json,
    sha256_file,
    validate_complete_aggregate,
    validate_run_directory,
)


OBJECTIVES = ("minmax", "minsum", "minmaxsum")
RUN_TERMINAL_STATES = {
    "COMPLETED", "FAILED", "TIME_LIMIT", "TIMED_OUT", "CANCELLED",
    "INVALID", "SKIPPED", "ABANDONED",
}
EXACT_VERIFICATION_KINDS = {"CERTIFIED_CONTINUOUS"}

FIELD_DEFINITIONS = {
    "experiment_id": "The active experiment identifier; rows from other experiments are excluded.",
    "run_id": "Immutable physical run identifier; null means the planned slot has not produced a run record.",
    "run_key": "Stable logical run-plan slot identity.",
    "instance_id": "Canonical selected-instance identifier.",
    "family": "Family label copied from the validated smoke20 selection manifest.",
    "n": "Number of moving trajectories (dimensionless count).",
    "m": "Number of stations/disks (dimensionless count).",
    "algorithm": "Algorithm identifier from the immutable run plan.",
    "algorithm_category": "Algorithm role from the resolved run configuration (for example heuristic, exact_reference, diagnostic).",
    "objective": "Objective identifier; MinMaxSum remains a two-component vector.",
    "repeat": "Zero-based repeat index.",
    "seed": "Random seed recorded by the run plan.",
    "terminal_status": "Canonical run status; PLANNED means no terminal record exists.",
    "optimality_status": "Solver-reported proof/incumbent status; not inferred from run status.",
    "feasible": "Recorded feasibility value; blank when no result record exists.",
    "verification_passed": "Recorded verification pass flag.",
    "verification_kind": "Verification kind; EMPIRICAL is distinct from continuous verification.",
    "verification_continuous": "Recorded continuous-verification boolean.",
    "requested_backend": "Backend requested for the run.",
    "selected_backend": "Reference/implementation backend selected during planning.",
    "actual_backend": "Backend reported by the terminal run record.",
    "selected_reference_backend": "One experiment-level selected exact-reference backend.",
    "reference_designated": "Whether the plan designates this slot as the selected reference run.",
    "solve_time_sec": "Solver-reported solve time in seconds; null is not replaced by wall time.",
    "wall_time_sec": "Solver-reported wall time in seconds; distinct from solve time.",
    "verification_time_sec": "Verification duration in seconds.",
    "serialization_time_sec": "Serialization duration in seconds.",
    "total_time_sec": "Reported total duration in seconds.",
    "objective_value": "Scalar MinMax or MinSum value: MinMax is peak disk-area cost (area units); MinSum is integrated disk-area cost (area×time units). Null for MinMaxSum.",
    "peak_cost": "Peak instantaneous disk-area cost over the horizon (area units); MinMaxSum vector component.",
    "integral_cost": "Time integral of disk-area cost (area×time units); MinMaxSum vector component.",
    "objective_vector_status": "Availability/validation status of the two-component objective vector.",
    "dominates_minmax": "Recorded dominance relation against the MinMax component result.",
    "dominates_minsum": "Recorded dominance relation against the MinSum component result.",
    "dominance_invariants_ok": "Recorded check of MinMaxSum dominance invariants.",
    "lower_bound": "Reported lower bound; not inferred from objective or upper-bound fields.",
    "upper_bound": "Reported upper bound; not inferred from objective or lower-bound fields.",
    "bound_status": "Solver-reported bound status.",
    "bound_provenance": "Solver-reported provenance/type of the bound.",
    "certified_gap": "Explicitly reported certified gap; never reconstructed from heuristic bounds.",
    "heuristic_gap": "Explicitly reported heuristic gap; kept separate from certified gaps.",
    "empirical_gap": "Explicitly reported empirical gap; kept separate from certified gaps.",
    "empirical_ratio_to_exact": "Observed ratio to a valid proven exact reference; not a theoretical approximation ratio.",
    "solution_available": "True only when the canonical run declares an existing solution artifact.",
    "solution_path": "Experiment-relative path to the actual run solution, when available.",
    "error_reason": "Recorded run/result error or a grounded report status reason.",
    "planned_count": "Number of logical slots in the immutable run plan for the group.",
    "terminal_count": "Number of planned slots with a canonical terminal run record.",
    "runtime_sample_count": "Number of COMPLETED records with an explicit solve_time_sec.",
    "runtime_median_sec": "Median solve_time_sec over the uncensored runtime population only.",
    "runtime_q1_sec": "25th percentile of uncensored solve_time_sec observations.",
    "runtime_q3_sec": "75th percentile of uncensored solve_time_sec observations.",
    "runtime_p95_sec": "95th percentile of uncensored solve_time_sec observations.",
    "runtime_min_sec": "Minimum uncensored solve_time_sec observation.",
    "runtime_max_sec": "Maximum uncensored solve_time_sec observation.",
    "timeout_count": "Count of TIME_LIMIT or TIMED_OUT logical slots.",
    "failure_count": "Count of FAILED, INVALID, ABANDONED or CANCELLED slots.",
    "valid_objective_count": "Feasible, verification-passed rows with an available scalar objective.",
    "objective_median": "Median feasible, verification-passed objective; reports are descriptive single-run smoke comparisons.",
    "feasible_count": "Count of terminal records with a recorded feasible result.",
    "verified_count": "Count of terminal records whose recorded verification passed.",
    "empirical_verified_count": "Count of passed EMPIRICAL verification records.",
    "continuous_verified_count": "Count of passed CERTIFIED_CONTINUOUS records.",
    "feasible_rate": "Feasible terminal runs divided by terminal run count; null if no terminal runs.",
    "verified_rate": "Passed-verification terminal runs divided by terminal run count; null if no terminal runs.",
    "empirical_verified_rate": "Passed EMPIRICAL terminal runs divided by terminal run count.",
    "continuous_verified_rate": "Passed CERTIFIED_CONTINUOUS terminal runs divided by terminal run count.",
    "included_rows": "Population included in the reported statistic.",
    "excluded_rows": "Planned rows excluded from the reported statistic.",
    "aggregate_complete": "Whether the canonical aggregate explicitly reports completeness.",
    "generated_snapshots": "Count of successfully generated solution snapshot image artifacts.",
    "generated_animations": "Count of successfully generated per-solution animations.",
    "generated_comparison_sheets": "Count of Prompt 6 per-instance/objective comparison sheets.",
    "artifact_count": "Count of files in the indicated experiment artifact scope.",
    "artifact_scope": "Experiment-relative directory or artifact grouping.",
    "artifact_status": "Observed presence/status of artifacts in this scope.",
    "result_status": "Run status represented by this table row.",
    "population": "Input population for the displayed group.",
    "filter": "Explicit population filter applied to the displayed statistic.",
    "metric": "Metric name in the summary row.",
    "value": "Metric value; null when unavailable or invalid for the row.",
    "denominator": "Denominator used for the reported count/rate.",
    "reason": "Grounded reason for an unavailable output or excluded row.",
    "figure": "Deterministic figure filename or requested figure name.",
    "generation_status": "GENERATED or NOT_GENERATED; absent data never produces a result-looking plot.",
    "path_png": "Experiment-relative PNG artifact path, or null.",
    "path_pdf": "Experiment-relative PDF artifact path, or null.",
    "source_run_id": "Run providing the observation.",
    "source_solution_sha256": "SHA-256 digest of the serialized run solution.",
}


TABLE_SCHEMAS: dict[str, list[str]] = {
    "complete_run_coverage": [
        "experiment_id", "run_key", "run_id", "instance_id", "family", "n", "m",
        "algorithm", "algorithm_category", "objective", "repeat", "seed",
        "terminal_status", "feasible", "optimality_status", "verification_passed",
        "verification_kind", "verification_continuous", "requested_backend",
        "selected_backend", "actual_backend", "selected_reference_backend",
        "reference_designated", "solution_available", "solution_path", "error_reason",
    ],
    "per_run_master_results": [
        "experiment_id", "run_key", "run_id", "instance_id", "family", "n", "m",
        "algorithm", "algorithm_category", "objective", "repeat", "seed",
        "terminal_status", "feasible", "optimality_status", "verification_passed",
        "verification_kind", "verification_continuous", "requested_backend",
        "selected_backend", "actual_backend", "selected_reference_backend",
        "reference_designated", "solve_time_sec", "wall_time_sec",
        "verification_time_sec", "serialization_time_sec", "total_time_sec",
        "objective_value", "peak_cost", "integral_cost", "lower_bound",
        "upper_bound", "bound_status", "bound_provenance", "certified_gap",
        "heuristic_gap", "empirical_gap", "empirical_ratio_to_exact",
        "dominates_minmax", "dominates_minsum", "dominance_invariants_ok",
        "solution_available", "solution_path", "error_reason",
    ],
    "runtime_comparison": [
        "algorithm", "algorithm_category", "objective", "planned_count",
        "terminal_count", "runtime_sample_count", "runtime_median_sec",
        "runtime_q1_sec", "runtime_q3_sec", "runtime_p95_sec", "runtime_min_sec",
        "runtime_max_sec", "timeout_count", "failure_count", "included_rows",
        "excluded_rows", "filter",
    ],
    "objective_quality": [
        "algorithm", "algorithm_category", "objective", "planned_count",
        "terminal_count", "valid_objective_count", "objective_median",
        "included_rows", "excluded_rows", "filter",
    ],
    "minmaxsum_components": [
        "run_key", "run_id", "instance_id", "family", "algorithm",
        "algorithm_category", "terminal_status", "feasible", "verification_kind",
        "verification_passed", "optimality_status", "peak_cost", "integral_cost",
        "objective_vector_status", "dominates_minmax", "dominates_minsum",
        "dominance_invariants_ok", "minmax_component_peak",
        "minmax_component_integral", "minsum_component_peak",
        "minsum_component_integral", "error_reason",
    ],
    "feasibility_verification": [
        "algorithm", "algorithm_category", "objective", "verification_kind",
        "planned_count", "terminal_count", "feasible_count", "verified_count",
        "empirical_verified_count", "continuous_verified_count", "feasible_rate",
        "verified_rate", "empirical_verified_rate", "continuous_verified_rate",
        "included_rows", "excluded_rows",
    ],
    "failures_timeouts_backends": [
        "run_key", "run_id", "instance_id", "family", "algorithm",
        "algorithm_category", "objective", "requested_backend", "selected_backend",
        "actual_backend", "terminal_status", "error_reason",
    ],
    "per_instance_comparison": [
        "instance_id", "family", "n", "m", "objective",
    ],
    "per_instance_run_rows": [
        "instance_id", "family", "n", "m", "algorithm", "algorithm_category",
        "objective", "terminal_status", "feasible", "verification_kind",
        "optimality_status", "objective_value", "peak_cost", "integral_cost",
        "solve_time_sec", "actual_backend", "run_id", "run_key",
    ],
    "per_family_comparison": [
        "family", "algorithm", "algorithm_category", "objective", "planned_count",
        "terminal_count", "valid_objective_count", "objective_median",
        "runtime_sample_count", "runtime_median_sec", "feasible_count",
        "verified_count", "timeout_count", "failure_count", "included_rows",
        "excluded_rows", "filter",
    ],
    "exact_reference_comparison": [
        "instance_id", "family", "algorithm", "objective", "run_id",
        "reference_run_id", "selected_reference_backend", "terminal_status",
        "reference_status", "objective_value", "reference_objective_value",
        "empirical_ratio_to_exact", "ratio_semantics", "verification_kind",
    ],
    "bounds_and_gaps": [
        "run_key", "run_id", "instance_id", "algorithm", "objective",
        "terminal_status", "objective_value", "lower_bound", "upper_bound",
        "bound_status", "bound_provenance", "certified_gap", "heuristic_gap",
        "empirical_gap", "empirical_ratio_to_exact",
    ],
    "completeness_artifacts": [
        "artifact_scope", "artifact_status", "planned_count", "terminal_count",
        "artifact_count", "generated_snapshots", "generated_animations",
        "generated_comparison_sheets", "aggregate_complete", "reason",
    ],
}


def _finite_number(value: Any) -> float | None:
    if isinstance(value, bool) or value is None:
        return None
    try:
        number = float(value)
    except (TypeError, ValueError):
        return None
    return number if math.isfinite(number) else None


def _boolean(value: Any) -> bool | None:
    return value if isinstance(value, bool) else None


def _strict_json(path: Path) -> Any:
    return loads_json(path.read_text(encoding="utf-8"))


def _selection_manifest_path(experiment: Path) -> Path | None:
    for name in ("selection_manifest.json", "selection_manifest", "manifest.json"):
        candidate = experiment / "dataset" / name
        if candidate.is_file():
            return candidate
    return None


def _read_selection_manifest(experiment: Path) -> dict[str, Any]:
    path = _selection_manifest_path(experiment)
    if path is None:
        return {}
    return read_json(path, schema_name="dataset_selection")


def _selection_index(
    manifest: Mapping[str, Any],
) -> dict[str, dict[str, Any]]:
    result: dict[str, dict[str, Any]] = {}
    for item in manifest.get("instances", []):
        result[str(item["canonical_sha256"])] = item
    return result


def _dataset_profile(
    experiment_manifest: Mapping[str, Any],
    selection_manifest: Mapping[str, Any],
) -> str | None:
    return (
        experiment_manifest.get("dataset_profile")
        or selection_manifest.get("profile")
    )


def _load_plan(experiment: Path) -> list[dict[str, Any]]:
    path = experiment / "runs" / "plan.jsonl"
    if not path.is_file():
        raise ValueError(f"missing immutable run plan: {path}")
    rows = []
    for line_number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        if not line.strip():
            continue
        entry = loads_json(line)
        try:
            validate_document(entry, "run_plan_entry")
        except ValueError as error:
            raise ValueError(f"invalid run plan line {line_number}: {error}") from error
        rows.append(entry)
    if not rows:
        raise ValueError("immutable run plan contains no slots")
    keys = [row["run_key"] for row in rows]
    if len(keys) != len(set(keys)):
        raise ValueError("immutable run plan contains duplicate run keys")
    metadata_path = experiment / "experiment.json"
    if metadata_path.is_file():
        metadata = read_json(metadata_path)
        actual_sha = sha256_file(path)
        expected_sha = metadata.get("run_plan_sha256")
        if expected_sha and expected_sha != actual_sha:
            raise ValueError("run plan checksum does not match experiment metadata")
        expected_count = metadata.get("expected_run_count")
        if expected_count is not None and expected_count != len(rows):
            raise ValueError(
                f"run plan has {len(rows)} slots but experiment metadata expects "
                f"{expected_count}"
            )
    return rows


def _read_run_attempts(experiment: Path) -> tuple[dict[str, list[dict[str, Any]]], list[str]]:
    attempts: dict[str, list[dict[str, Any]]] = {}
    errors: list[str] = []
    runs = experiment / "runs"
    if not runs.is_dir():
        return attempts, errors
    for directory in sorted(path for path in runs.iterdir() if path.is_dir()):
        if directory.name == "plan.jsonl" or not (directory / "run.json").is_file():
            continue
        try:
            record = validate_run_directory(directory, experiment_id=experiment.name)
            attempts.setdefault(record["run_key"], []).append(record)
        except (OSError, ValueError, KeyError, TypeError) as error:
            errors.append(f"{directory.relative_to(experiment)}: {error}")
            try:
                record = _strict_json(directory / "run.json")
            except (OSError, ValueError):
                continue
            run_key = record.get("run_key") if isinstance(record, dict) else None
            if isinstance(run_key, str):
                record = dict(record)
                record["status"] = "INVALID"
                record["_report_validation_error"] = str(error)
                attempts.setdefault(run_key, []).append(record)
    for records in attempts.values():
        records.sort(key=lambda record: (
            str(record.get("finished_at", "")), str(record.get("run_id", ""))
        ))
    return attempts, errors


def _choose_record(records: list[dict[str, Any]] | None) -> dict[str, Any] | None:
    return records[-1] if records else None


def _input_metadata(experiment: Path, instance_id: str,
                    selection: Mapping[str, dict[str, Any]]) -> dict[str, Any]:
    path = experiment / "input" / f"{instance_id}.json"
    if not path.is_file():
        return {}
    payload = _strict_json(path)
    digest = sha256_file(path)
    selected = selection.get(digest, {})
    trajectories = payload.get("trajectories", payload.get("moving_points", []))
    stations = payload.get("stations", payload.get("centers", []))
    return {
        "family": selected.get("family"),
        "n": selected.get("n", len(trajectories)),
        "m": selected.get("m", len(stations)),
        "T_end": selected.get("T_end", payload.get("T_end")),
        "input_sha256": digest,
    }


def _solver_result(record: Mapping[str, Any] | None) -> dict[str, Any]:
    if not record:
        return {}
    result = record.get("result")
    if not isinstance(result, dict):
        return {}
    nested = result.get("solver_result")
    return nested if isinstance(nested, dict) else {}


def _verification(record: Mapping[str, Any] | None) -> dict[str, Any]:
    value = record.get("verification") if record else None
    return value if isinstance(value, dict) else {}


def _record_row(
    experiment: Path,
    plan: Mapping[str, Any],
    record: Mapping[str, Any] | None,
    selection: Mapping[str, dict[str, Any]],
    selected_reference: str | None,
) -> dict[str, Any]:
    instance_id = str(plan["instance_id"])
    input_info = _input_metadata(experiment, instance_id, selection)
    resolved = plan.get("resolved_configuration", {})
    resolved = resolved if isinstance(resolved, dict) else {}
    result = record.get("result", {}) if record else {}
    result = result if isinstance(result, dict) else {}
    solver = _solver_result(record)
    verification = _verification(record)
    vector = result.get("objective_vector")
    if not isinstance(vector, dict):
        vector = solver.get("objective_vector")
    vector = vector if isinstance(vector, dict) else {}
    joint = result.get("joint")
    if not isinstance(joint, dict):
        joint = solver.get("joint")
    joint = joint if isinstance(joint, dict) else {}
    status = str(record.get("status", "INVALID")) if record else "PLANNED"
    if not record:
        error_reason = "No canonical terminal run record exists for this planned slot."
    else:
        error_reason = (
            record.get("_report_validation_error")
            or result.get("error_message")
        )
        if status == "TIME_LIMIT" and not error_reason:
            error_reason = "Time limit reached; no additional solver message was recorded."
        elif status == "TIMED_OUT" and not error_reason:
            error_reason = "Execution was time-censored; no additional wrapper message was recorded."
        elif status == "SKIPPED" and not error_reason:
            error_reason = "Run was skipped; no structured skip reason was recorded."
        elif status not in {"COMPLETED"} and not error_reason:
            error_reason = (
                f"Terminal status {status}; no additional solver error message was recorded."
            )
    solution_path: str | None = None
    solution_available = False
    solution_relative = solver.get("solution_json_path")
    if record and solution_relative:
        candidate = experiment / "runs" / str(record.get("run_id")) / str(solution_relative)
        try:
            candidate.resolve().relative_to(experiment.resolve())
            if candidate.is_file():
                solution_available = True
                solution_path = candidate.relative_to(experiment).as_posix()
        except (ValueError, OSError):
            error_reason = (
                f"{error_reason}; declared solution path escapes the experiment or is invalid"
                if error_reason else
                "Declared solution path escapes the experiment or is invalid."
            )
    objective = str(plan["objective"]).lower()
    scalar_value = result.get("objective_value")
    if objective == "minmaxsum":
        scalar_value = None
    elif scalar_value is None:
        scalar_value = solver.get("objective_value")
    row = {
        "experiment_id": experiment.name,
        "run_key": plan["run_key"],
        "run_id": record.get("run_id") if record else None,
        "instance_id": instance_id,
        "family": (record.get("family") if record else None) or input_info.get("family") or "unknown",
        "n": record.get("n") if record and record.get("n") is not None else input_info.get("n"),
        "m": record.get("m") if record and record.get("m") is not None else input_info.get("m"),
        "algorithm": plan["algorithm"],
        "algorithm_category": resolved.get("algorithm_category"),
        "objective": objective,
        "repeat": plan["repeat"],
        "seed": plan["seed"],
        "terminal_status": status,
        "feasible": result.get("feasible") if record else None,
        "optimality_status": result.get("optimality_status"),
        "verification_passed": verification.get("passed"),
        "verification_kind": verification.get("kind"),
        "verification_continuous": verification.get("continuous"),
        "requested_backend": plan.get("requested_backend"),
        "selected_backend": resolved.get("selected_backend"),
        "actual_backend": record.get("actual_backend") if record else None,
        "selected_reference_backend": selected_reference,
        "reference_designated": resolved.get("reference_designated") is True,
        "solve_time_sec": _finite_number(solver.get("solve_time_sec")),
        "wall_time_sec": _finite_number(solver.get("wall_time_sec")),
        "verification_time_sec": _finite_number(solver.get("verification_time_sec")),
        "serialization_time_sec": _finite_number(solver.get("serialization_time_sec")),
        "total_time_sec": _finite_number(solver.get("total_time_sec")),
        "objective_value": _finite_number(scalar_value),
        "peak_cost": _finite_number(vector.get("peak_cost", solver.get("peak_cost"))),
        "integral_cost": _finite_number(vector.get("integral_cost", solver.get("integral_cost"))),
        "objective_vector_status": (
            "NOT_RUN" if not record else
            "COMPLETE" if all(
                _finite_number(vector.get(key, solver.get(key))) is not None
                for key in ("peak_cost", "integral_cost")
            ) else "MISSING_OR_INVALID"
        ) if objective == "minmaxsum" else "NOT_APPLICABLE",
        "dominates_minmax": joint.get("dominates_minmax"),
        "dominates_minsum": joint.get("dominates_minsum"),
        "dominance_invariants_ok": joint.get("dominance_invariants_ok"),
        "minmax_component_peak": _finite_number(joint.get("minmax_component_peak")),
        "minmax_component_integral": _finite_number(joint.get("minmax_component_integral")),
        "minsum_component_peak": _finite_number(joint.get("minsum_component_peak")),
        "minsum_component_integral": _finite_number(joint.get("minsum_component_integral")),
        "lower_bound": _finite_number(result.get("lower_bound")),
        "upper_bound": _finite_number(result.get("upper_bound")),
        "bound_status": solver.get("bound_status"),
        "bound_provenance": solver.get("bound_provenance", solver.get("lower_bound_kind")),
        "certified_gap": _finite_number(solver.get("certified_gap")),
        "heuristic_gap": _finite_number(solver.get("heuristic_gap")),
        "empirical_gap": _finite_number(solver.get("empirical_gap")),
        "empirical_ratio_to_exact": _finite_number(solver.get("empirical_ratio_to_exact")),
        "solution_available": solution_available,
        "solution_path": solution_path,
        "error_reason": error_reason,
        "_verification_status": verification.get("status"),
        "_verification_kind": verification.get("kind", "NONE") if record else "NONE",
        "_input_sha256": input_info.get("input_sha256"),
    }
    return row


def _load_experiment(experiment: Path) -> dict[str, Any]:
    exp = experiment.resolve()
    if not (exp / "experiment_manifest.json").is_file():
        raise ValueError(f"not an experiment directory: {exp}")
    manifest = read_json(exp / "experiment_manifest.json")
    if manifest.get("experiment_id") != exp.name:
        raise ValueError("experiment manifest ID does not match directory name")
    plan = _load_plan(exp)
    aggregate_manifest = None
    aggregate_path = exp / "aggregates" / "aggregate_manifest.json"
    if aggregate_path.is_file():
        aggregate_manifest = read_json(aggregate_path)
        if aggregate_manifest.get("complete") is True:
            validate_complete_aggregate(exp)
    selection_manifest = _read_selection_manifest(exp)
    selection = _selection_index(selection_manifest)
    dataset_profile = _dataset_profile(manifest, selection_manifest)
    attempts, validation_errors = _read_run_attempts(exp)
    if dataset_profile == "smoke20":
        if not selection:
            validation_errors.append("smoke20 selection manifest is missing or empty")
        selected_count = manifest.get("selected_instance_count",
                                      manifest.get("instance_count"))
        if selected_count is not None and len(selection) != int(selected_count):
            validation_errors.append(
                f"selection manifest has {len(selection)} canonical checksums; "
                f"experiment manifest expects {selected_count}"
            )
    unknown = sorted(set(attempts) - {row["run_key"] for row in plan})
    if unknown:
        validation_errors.extend(f"terminal record has unplanned run_key: {key}"
                                 for key in unknown)
    selected_reference = manifest.get("selected_exact_backend")
    rows = [
        _record_row(
            exp, entry, _choose_record(attempts.get(entry["run_key"])),
            selection, selected_reference,
        )
        for entry in plan
    ]
    if dataset_profile == "smoke20":
        for instance_id in sorted({row["instance_id"] for row in rows}):
            instance_rows = [row for row in rows if row["instance_id"] == instance_id]
            if any(row["_input_sha256"] not in selection for row in instance_rows):
                validation_errors.append(
                    f"smoke20 input snapshot for {instance_id} does not match a "
                    "canonical checksum in the selection manifest"
                )
            if any(row["family"] == "unknown" for row in instance_rows):
                validation_errors.append(
                    f"smoke20 family label for {instance_id} is unavailable"
                )
        if not manifest.get("selected_exact_backend"):
            validation_errors.append(
                "smoke20 experiment does not record one selected exact-reference backend"
            )
        designated: dict[tuple[str, str, int], int] = {}
        for row in rows:
            if row["reference_designated"]:
                key = (row["instance_id"], row["objective"], int(row["repeat"]))
                designated[key] = designated.get(key, 0) + 1
        expected_groups = {
            (row["instance_id"], row["objective"], int(row["repeat"]))
            for row in rows
        }
        for key in sorted(expected_groups):
            if designated.get(key, 0) != 1:
                validation_errors.append(
                    f"smoke20 plan designates {designated.get(key, 0)} reference "
                    f"backends for {key}; exactly one is required"
                )
    return {
        "experiment": exp,
        "manifest": manifest,
        "dataset_profile": dataset_profile,
        "plan": plan,
        "rows": rows,
        "validation_errors": validation_errors,
        "aggregate_manifest": aggregate_manifest,
        "selection_count": len(selection),
        "attempts": attempts,
    }


def _float_values(rows: Iterable[Mapping[str, Any]], field: str) -> list[float]:
    return [float(value) for row in rows
            if (value := _finite_number(row.get(field))) is not None]


def _q(values: Sequence[float], fraction: float) -> float | None:
    return float(np.quantile(values, fraction, method="linear")) if values else None


def _is_failure(row: Mapping[str, Any]) -> bool:
    return row["terminal_status"] in {"FAILED", "INVALID", "ABANDONED", "CANCELLED"}


def _runtime_population(rows: Sequence[Mapping[str, Any]]) -> list[Mapping[str, Any]]:
    return [
        row for row in rows
        if row["terminal_status"] == "COMPLETED"
        and _finite_number(row.get("solve_time_sec")) is not None
    ]


def _quality_population(rows: Sequence[Mapping[str, Any]]) -> list[Mapping[str, Any]]:
    return [
        row for row in rows
        if row["objective"] in {"minmax", "minsum"}
        and row.get("feasible") is True
        and row.get("verification_passed") is True
        and _finite_number(row.get("objective_value")) is not None
    ]


def _status_counts(rows: Sequence[Mapping[str, Any]]) -> dict[str, int]:
    counts: dict[str, int] = {}
    for row in rows:
        key = str(row["terminal_status"])
        counts[key] = counts.get(key, 0) + 1
    return counts


def _runtime_rows(rows: Sequence[dict[str, Any]]) -> list[dict[str, Any]]:
    keys = sorted({(row["algorithm"], row["objective"]) for row in rows})
    output = []
    for algorithm, objective in keys:
        group = [row for row in rows
                 if row["algorithm"] == algorithm and row["objective"] == objective]
        runtime = _runtime_population(group)
        values = _float_values(runtime, "solve_time_sec")
        output.append({
            "algorithm": algorithm,
            "algorithm_category": group[0]["algorithm_category"],
            "objective": objective,
            "planned_count": len(group),
            "terminal_count": sum(row["terminal_status"] != "PLANNED" for row in group),
            "runtime_sample_count": len(values),
            "runtime_median_sec": _q(values, .5),
            "runtime_q1_sec": _q(values, .25),
            "runtime_q3_sec": _q(values, .75),
            "runtime_p95_sec": _q(values, .95),
            "runtime_min_sec": min(values) if values else None,
            "runtime_max_sec": max(values) if values else None,
            "timeout_count": sum(row["terminal_status"] in {"TIME_LIMIT", "TIMED_OUT"}
                                 for row in group),
            "failure_count": sum(_is_failure(row) for row in group),
            "included_rows": len(runtime),
            "excluded_rows": len(group) - len(runtime),
            "filter": "COMPLETED with explicit solve_time_sec only; time-censored/failed/planned slots excluded",
        })
    return output


def _quality_rows(rows: Sequence[dict[str, Any]]) -> list[dict[str, Any]]:
    output = []
    for algorithm, objective in sorted({
        (row["algorithm"], row["objective"]) for row in rows
        if row["objective"] in {"minmax", "minsum"}
    }):
        group = [row for row in rows
                 if row["algorithm"] == algorithm and row["objective"] == objective]
        included = _quality_population(group)
        values = _float_values(included, "objective_value")
        output.append({
            "algorithm": algorithm,
            "algorithm_category": group[0]["algorithm_category"],
            "objective": objective,
            "planned_count": len(group),
            "terminal_count": sum(row["terminal_status"] != "PLANNED" for row in group),
            "valid_objective_count": len(values),
            "objective_median": _q(values, .5),
            "included_rows": len(values),
            "excluded_rows": len(group) - len(values),
            "filter": "feasible, verification-passed MinMax/MinSum records with finite scalar objective; verification kind retained per run",
        })
    return output


def _feasibility_rows(rows: Sequence[dict[str, Any]]) -> list[dict[str, Any]]:
    output = []
    keys = sorted({(row["algorithm"], row["objective"], row["_verification_kind"])
                   for row in rows})
    for algorithm, objective, verification_kind in keys:
        group = [row for row in rows if (
            row["algorithm"] == algorithm and row["objective"] == objective
            and row["_verification_kind"] == verification_kind
        )]
        terminal = [row for row in group if row["terminal_status"] != "PLANNED"]
        denominator = len(terminal)
        feasible = sum(row.get("feasible") is True for row in terminal)
        verified = sum(row.get("verification_passed") is True for row in terminal)
        empirical = sum(row.get("verification_passed") is True and
                        row.get("verification_kind") == "EMPIRICAL" for row in terminal)
        continuous = sum(row.get("verification_passed") is True and
                         row.get("verification_kind") == "CERTIFIED_CONTINUOUS"
                         for row in terminal)
        rate = lambda count: count / denominator if denominator else None
        output.append({
            "algorithm": algorithm,
            "algorithm_category": group[0]["algorithm_category"],
            "objective": objective,
            "verification_kind": verification_kind,
            "planned_count": len(group),
            "terminal_count": denominator,
            "feasible_count": feasible,
            "verified_count": verified,
            "empirical_verified_count": empirical,
            "continuous_verified_count": continuous,
            "feasible_rate": rate(feasible),
            "verified_rate": rate(verified),
            "empirical_verified_rate": rate(empirical),
            "continuous_verified_rate": rate(continuous),
            "included_rows": denominator,
            "excluded_rows": len(group) - denominator,
        })
    return output


def _family_rows(rows: Sequence[dict[str, Any]]) -> list[dict[str, Any]]:
    output = []
    for family, algorithm, objective in sorted({
        (row["family"], row["algorithm"], row["objective"]) for row in rows
    }):
        group = [row for row in rows if (
            row["family"] == family and row["algorithm"] == algorithm
            and row["objective"] == objective
        )]
        qualities = _quality_population(group)
        runtimes = _runtime_population(group)
        quality = _float_values(qualities, "objective_value")
        runtime = _float_values(runtimes, "solve_time_sec")
        terminal = [row for row in group if row["terminal_status"] != "PLANNED"]
        output.append({
            "family": family,
            "algorithm": algorithm,
            "algorithm_category": group[0]["algorithm_category"],
            "objective": objective,
            "planned_count": len(group),
            "terminal_count": len(terminal),
            "valid_objective_count": len(quality),
            "objective_median": _q(quality, .5),
            "runtime_sample_count": len(runtime),
            "runtime_median_sec": _q(runtime, .5),
            "feasible_count": sum(row.get("feasible") is True for row in terminal),
            "verified_count": sum(row.get("verification_passed") is True for row in terminal),
            "timeout_count": sum(row["terminal_status"] in {"TIME_LIMIT", "TIMED_OUT"}
                                 for row in group),
            "failure_count": sum(_is_failure(row) for row in group),
            "included_rows": len(qualities),
            "excluded_rows": len(group) - len(qualities),
            "filter": "family labels from validated selection manifest; quality uses feasible, verified scalar rows",
        })
    return output


def _exact_reference_rows(rows: Sequence[dict[str, Any]]) -> list[dict[str, Any]]:
    designated: dict[tuple[str, str], dict[str, Any]] = {}
    for row in rows:
        if not row["reference_designated"]:
            continue
        verification_kind = row.get("verification_kind")
        if (
            row["algorithm_category"] == "exact_reference"
            and row["terminal_status"] == "COMPLETED"
            and row.get("feasible") is True
            and row.get("verification_passed") is True
            and verification_kind in EXACT_VERIFICATION_KINDS
            and row.get("optimality_status") == "OPTIMAL"
        ):
            designated[(row["instance_id"], row["objective"])] = row
    output: list[dict[str, Any]] = []
    for row in rows:
        reference = designated.get((row["instance_id"], row["objective"]))
        if reference is None or row["algorithm"] == reference["algorithm"]:
            continue
        candidate_value = _finite_number(row.get("objective_value"))
        reference_value = _finite_number(reference.get("objective_value"))
        if (
            row["objective"] not in {"minmax", "minsum"}
            or row.get("feasible") is not True
            or row.get("verification_passed") is not True
            or candidate_value is None
            or reference_value in (None, 0)
        ):
            continue
        output.append({
            "instance_id": row["instance_id"],
            "family": row["family"],
            "algorithm": row["algorithm"],
            "objective": row["objective"],
            "run_id": row["run_id"],
            "reference_run_id": reference["run_id"],
            "selected_reference_backend": reference["actual_backend"],
            "terminal_status": row["terminal_status"],
            "reference_status": reference["optimality_status"],
            "objective_value": candidate_value,
            "reference_objective_value": reference_value,
            "empirical_ratio_to_exact": candidate_value / reference_value,
            "ratio_semantics": (
                "descriptive empirical ratio to a valid proven optimum; "
                "not a theoretical approximation ratio"
            ),
            "verification_kind": row["verification_kind"],
        })
    return output


def _definitions(columns: Sequence[str]) -> dict[str, str]:
    definitions = {}
    for column in columns:
        if column in FIELD_DEFINITIONS:
            definitions[column] = FIELD_DEFINITIONS[column]
        elif column.endswith("_sec"):
            definitions[column] = "Duration in seconds; null when not explicitly reported or not valid for the population."
        elif column.endswith("_rate"):
            definitions[column] = "Count divided by the explicitly stated terminal-run denominator; null when denominator is zero."
        elif column.endswith("_count"):
            definitions[column] = "Count of records satisfying the population filter documented in the companion."
        elif column.endswith("_sha256"):
            definitions[column] = "SHA-256 checksum of the referenced source artifact."
        else:
            definitions[column] = f"Recorded {column} value; not inferred from another field."
    return definitions


def _write_table(
    exp: Path,
    table_name: str,
    rows: Sequence[Mapping[str, Any]],
    columns: Sequence[str],
    *,
    source: str,
    purpose: str,
    population: str,
    filters: str,
    limitations: str,
    formulas: str | None = None,
) -> None:
    path = exp / "tables" / f"{table_name}.csv"
    write_delimited(
        path, rows, columns=columns,
        companion={
            "title": table_name.replace("_", " ").title(),
            "purpose": purpose,
            "experiment_id": exp.name,
            "source": source,
            "generation_command": "scripts/report_experiment.py --stage tables",
            "file_scope": f"One row per {population}.",
            "sort_order": "stable lexicographic order by the leading identity/group columns unless otherwise stated",
            "filters": filters,
            "column_definitions": _definitions(columns),
            "limitations": limitations,
            "formulas": formulas or (
                "No metrics are calculated for this table; values are copied "
                "from the validated plan, run record, manifest, or artifact inventory."
            ),
        },
    )


def _artifact_count(path: Path) -> int:
    return sum(1 for child in path.rglob("*") if child.is_file()) if path.is_dir() else int(path.is_file())


def build_tables(data: dict[str, Any]) -> None:
    exp: Path = data["experiment"]
    manifest = data["manifest"]
    rows: list[dict[str, Any]] = data["rows"]
    plan_count = len(data["plan"])
    terminal_count = sum(row["terminal_status"] != "PLANNED" for row in rows)
    aggregate = data["aggregate_manifest"]
    visual_path = exp / "reports" / "visualization_manifest.json"
    visual = read_json(visual_path) if visual_path.is_file() else {}
    snapshot_count = int(visual.get("generated_solution_snapshots", 0) or 0)
    animation_count = int(visual.get("generated_animations", 0) or 0)
    comparison_count = len(visual.get("comparison_sheets", []))
    status_counts = _status_counts(rows)

    coverage = [{key: row.get(key) for key in TABLE_SCHEMAS["complete_run_coverage"]}
                for row in rows]
    master = [{key: row.get(key) for key in TABLE_SCHEMAS["per_run_master_results"]}
              for row in rows]
    grouped_tables: list[tuple[str, Sequence[Mapping[str, Any]], str, str, str, str]] = [
        ("complete_run_coverage", coverage, "All immutable plan slots, including planned, skipped, failed and time-limited rows.",
         "Every unique run_key in runs/plan.jsonl; terminal records validated against run_record.schema.json.",
         "No run-status filter. Missing canonical run records are retained as PLANNED.",
         "Planned slots are not solver outcomes; null result fields remain unavailable."),
        ("per_run_master_results", master, "Auditable result, timing, objective, bound and provenance fields for each planned logical run.",
         "All immutable plan slots joined to validated canonical run records by run_key.",
         "No success-only filter. Each timing and gap field is copied only from its corresponding source field.",
         "Empirical gaps and ratios are not approximation guarantees; MinMaxSum has no scalar objective value."),
    ]
    for name, table_rows, purpose, population, filters, limitations in grouped_tables:
        _write_table(exp, name, table_rows, TABLE_SCHEMAS[name],
                     source="experiment_manifest.json, runs/plan.jsonl, validated runs/<run-id>/run.json and selection_manifest.json",
                     purpose=purpose, population=population, filters=filters,
                     limitations=limitations)

    runtime_rows = _runtime_rows(rows)
    _write_table(exp, "runtime_comparison", runtime_rows,
                 TABLE_SCHEMAS["runtime_comparison"],
                 source="validated canonical run records joined to the immutable plan",
                 purpose="Summarize uncensored solve-time observations with planned, timeout and failure counts.",
                 population="algorithm-objective groups",
                 filters="Runtime quantiles include only COMPLETED runs with explicit solve_time_sec; TIME_LIMIT/TIMED_OUT are censored and excluded.",
                 limitations="With one repeat this is a single-run smoke comparison, not an inferential statistic or scaling claim.",
                 formulas="Quantiles use NumPy linear interpolation over explicit solve_time_sec values from COMPLETED records only (Q1=.25, median=.5, Q3=.75, P95=.95). Minimum and maximum are observed extrema. Censored time limits and all other records are excluded from the runtime sample but retained in the planned/timeout/failure counts.")

    quality_rows = _quality_rows(rows)
    _write_table(exp, "objective_quality", quality_rows,
                 TABLE_SCHEMAS["objective_quality"],
                 source="validated canonical run records",
                 purpose="Summarize feasible, verification-passed scalar MinMax and MinSum values.",
                 population="algorithm-objective groups",
                 filters="Only MinMax/MinSum rows with feasible=true, verification_passed=true and finite objective_value.",
                 limitations="Verification kind remains separately reported; descriptive smoke20 results do not establish universal algorithm superiority.",
                 formulas="objective_median is the NumPy linear-interpolated .5 quantile of included objective_value observations. included_rows is the filtered finite-observation count; excluded_rows=planned_count-included_rows.")

    joint = [{key: row.get(key) for key in TABLE_SCHEMAS["minmaxsum_components"]}
             for row in rows if row["objective"] == "minmaxsum"]
    _write_table(exp, "minmaxsum_components", joint,
                 TABLE_SCHEMAS["minmaxsum_components"],
                 source="validated canonical run records and run plan",
                 purpose="Report MinMaxSum peak/integral vector components and the repository-recorded component/dominance data.",
                 population="planned MinMaxSum slots",
                 filters="No status filter; missing/unavailable vector values remain null.",
                 limitations="No weighted/scalar score is computed. Dominance flags are reported from the validated record.")

    reliability = _feasibility_rows(rows)
    _write_table(exp, "feasibility_verification", reliability,
                 TABLE_SCHEMAS["feasibility_verification"],
                 source="validated canonical run records and immutable plan",
                 purpose="Count feasibility and verification outcomes while separating empirical and continuous verification.",
                 population="algorithm-objective-verification-kind groups",
                 filters="Rates use terminal records only as denominator; planned slots are counted separately.",
                 limitations="EMPIRICAL and CERTIFIED_CONTINUOUS are not combined into a single certificate category.",
                 formulas="Each outcome rate is its corresponding count divided by terminal_count. Rates are null if terminal_count is zero. Verification kind is grouped separately.")

    unsuccessful = [row for row in rows if row["terminal_status"] != "COMPLETED"]
    failures = [{key: row.get(key) for key in TABLE_SCHEMAS["failures_timeouts_backends"]}
                for row in unsuccessful]
    _write_table(exp, "failures_timeouts_backends", failures,
                 TABLE_SCHEMAS["failures_timeouts_backends"],
                 source="validated canonical run records and immutable plan",
                 purpose="Retain all non-completed statuses, backend provenance, and recorded or grounded reasons.",
                 population="planned slots with status other than COMPLETED",
                 filters="Includes PLANNED, TIME_LIMIT, TIMED_OUT, FAILED, INVALID, SKIPPED, CANCELLED and ABANDONED.",
                 limitations="A planned row has no terminal reason; it is explicitly identified as lacking a terminal run record.")

    instance_rows = [{key: row.get(key) for key in TABLE_SCHEMAS["per_instance_run_rows"]}
                     for row in rows]
    _write_table(exp, "per_instance_run_rows", instance_rows,
                 TABLE_SCHEMAS["per_instance_run_rows"],
                 source="validated smoke20 selection manifest, run plan and canonical run records",
                 purpose="Provide a lossless long-form algorithm-by-objective row for every selected instance and preserve unsuccessful outcomes.",
                 population="all selected-instance/algorithm/objective/repeat plan slots",
                 filters="No status filter; family labels come from the validated selection manifest.",
                 limitations="Raw objective magnitudes are not directly comparable across differently scaled instances.")

    algorithms = sorted({row["algorithm"] for row in rows})
    repeats = sorted({int(row["repeat"]) for row in rows})
    matrix_columns = list(TABLE_SCHEMAS["per_instance_comparison"])
    matrix_fields = (
        ("status", "terminal_status"), ("run_id", "run_id"),
        ("objective_value", "objective_value"), ("peak_cost", "peak_cost"),
        ("integral_cost", "integral_cost"), ("solve_time_sec", "solve_time_sec"),
        ("verification_kind", "verification_kind"), ("actual_backend", "actual_backend"),
    )
    for algorithm in algorithms:
        suffix = re.sub(r"[^A-Za-z0-9_]+", "_", algorithm).strip("_")
        for repeat in repeats:
            for label, _ in matrix_fields:
                matrix_columns.append(f"{suffix}__r{repeat}__{label}")
    matrix_groups: dict[tuple[str, str], list[dict[str, Any]]] = {}
    for row in rows:
        matrix_groups.setdefault((row["instance_id"], row["objective"]), []).append(row)
    matrix_rows = []
    for (instance_id, objective), group in sorted(matrix_groups.items()):
        output = {key: group[0].get(key)
                  for key in TABLE_SCHEMAS["per_instance_comparison"]}
        for row in group:
            suffix = re.sub(r"[^A-Za-z0-9_]+", "_", row["algorithm"]).strip("_")
            repeat = int(row["repeat"])
            for label, source_name in matrix_fields:
                output[f"{suffix}__r{repeat}__{label}"] = row.get(source_name)
        matrix_rows.append(output)
    _write_table(exp, "per_instance_comparison", matrix_rows, matrix_columns,
                 source="validated smoke20 selection manifest, immutable run plan and canonical run records",
                 purpose="Place each algorithm's status and result fields side by side for every selected instance/objective.",
                 population="selected-instance/objective groups; each algorithm and repeat has separate columns",
                 filters="No status filter; unexecuted slots show PLANNED and unavailable metrics remain blank.",
                 limitations="Algorithm columns use stable sanitized identifiers. Comparisons remain descriptive and do not imply cross-instance scale equivalence.")

    family_rows = _family_rows(rows)
    _write_table(exp, "per_family_comparison", family_rows,
                 TABLE_SCHEMAS["per_family_comparison"],
                 source="validated selection manifest and canonical run records",
                 purpose="Summarize objective/runtime and reliability by the selection manifest's family labels.",
                 population="family-algorithm-objective groups",
                 filters="Quality uses feasible, verification-passed scalar outcomes; runtime uses COMPLETED explicit solve_time_sec.",
                 limitations="Smoke20 family groups are small; results are descriptive and not evidence of population scaling.",
                 formulas="Objective and runtime medians are NumPy linear-interpolated .5 quantiles of their separately filtered samples; outcome columns are raw counts.")

    exact_rows = _exact_reference_rows(rows)
    _write_table(exp, "exact_reference_comparison", exact_rows,
                 TABLE_SCHEMAS["exact_reference_comparison"],
                 source="designated reference run records, experiment manifest and canonical plan",
                 purpose="Compare candidate scalar objectives only against the one designated, valid, proven exact reference.",
                 population="candidate rows with a matching proven exact reference",
                 filters="Reference must be designated exact_reference, COMPLETED, feasible, verification-passed, CERTIFIED_CONTINUOUS and OPTIMAL; MinMaxSum excluded from scalar ratios.",
                 limitations="empirical_ratio_to_exact is descriptive and is not a theoretical approximation ratio. No valid reference means no comparison row.",
                 formulas="empirical_ratio_to_exact=candidate objective_value/reference objective_value; computed only when both are finite, the reference objective is nonzero, and the designated reference passes the documented proof/verification filter.")

    bounds = [{key: row.get(key) for key in TABLE_SCHEMAS["bounds_and_gaps"]}
              for row in rows]
    _write_table(exp, "bounds_and_gaps", bounds,
                 TABLE_SCHEMAS["bounds_and_gaps"],
                 source="validated canonical run records",
                 purpose="Keep lower/upper bounds, bound provenance, certified/heuristic/empirical gaps distinct.",
                 population="all planned slots",
                 filters="No status filter; only explicitly stored fields are reported.",
                 limitations="No gap is reconstructed from unrelated objective/bound fields; null means unavailable.")

    scopes = ("runs", "aggregates", "batch", "tables", "figures",
              "snapshots", "animations", "certification", "reports", "logs")
    completeness_rows = []
    for scope in scopes:
        path = exp / scope
        artifact_count = _artifact_count(path)
        completeness_rows.append({
            "artifact_scope": scope,
            "artifact_status": "PRESENT" if path.exists() else "NOT_CREATED",
            "planned_count": plan_count if scope == "runs" else None,
            "terminal_count": terminal_count if scope == "runs" else None,
            "artifact_count": artifact_count,
            "generated_snapshots": snapshot_count if scope == "snapshots" else None,
            "generated_animations": animation_count if scope == "animations" else None,
            "generated_comparison_sheets": comparison_count if scope == "snapshots" else None,
            "aggregate_complete": aggregate.get("complete") if aggregate else None,
            "reason": (
                "No canonical aggregate manifest is present."
                if scope == "aggregates" and aggregate is None else
                f"Run status counts: {json.dumps(status_counts, sort_keys=True)}"
                if scope == "runs" else ""
            ),
        })
    _write_table(exp, "completeness_artifacts", completeness_rows,
                 TABLE_SCHEMAS["completeness_artifacts"],
                 source="experiment manifest, canonical run plan/records, aggregate and visualization manifests",
                 purpose="Compare planned/terminal slot coverage with aggregate and artifact availability.",
                 population="experiment artifact scopes",
                 filters="Counts are scoped to this experiment directory only.",
                 limitations="Artifact presence does not imply that a solution was scientifically valid; consult run records and visualization manifest.")

    index_lines = [
        "# Smoke20 Comparative Tables",
        "",
        f"- Experiment: `{exp.name}`",
        f"- Planned logical runs: {plan_count}",
        f"- Terminal canonical records: {terminal_count}",
        f"- Status counts: `{json.dumps(status_counts, sort_keys=True)}`",
        "- Population: this experiment's validated run plan and canonical run records only.",
        "- One repeat is a single-run smoke comparison; no significance or general superiority claim is made.",
        "- Timing quantiles exclude censored runs and use explicit `solve_time_sec` only.",
        "- MinMaxSum remains `(peak_cost, integral_cost)`; no scalarization is used.",
        "",
        "Every CSV has an adjacent Markdown data dictionary, bounded preview, source population and metric definitions.",
        "",
        "## Tables",
        "",
    ]
    for name in TABLE_SCHEMAS:
        index_lines.append(f"- [{name.replace('_', ' ').title()}]({name}.md) · [CSV]({name}.csv)")
    index_lines.extend([
        "",
        "## Source artifacts",
        "",
        "- [Run plan](../runs/plan.jsonl)",
        "- [Experiment manifest](../experiment_manifest.json)",
    ])
    selection_path = _selection_manifest_path(exp)
    if selection_path is not None:
        index_lines.append(
            f"- [Validated selection manifest](../dataset/{selection_path.name})"
        )
    if (exp / "reports" / "visualization_manifest.json").is_file():
        index_lines.append("- [Visualization manifest](../reports/visualization_manifest.json)")
    (exp / "tables" / "00_index.md").write_text("\n".join(index_lines) + "\n",
                                                encoding="utf-8")
    errors = audit_companions(exp, create_missing=True, experiment_id=exp.name)
    if errors:
        raise ValueError("delimited companion audit failed: " + "; ".join(errors))


FIGURE_DEFINITIONS = [
    ("runtime_by_algorithm_objective", "Runtime comparison by algorithm and objective"),
    ("runtime_distribution_by_algorithm", "Runtime distribution by algorithm with sample counts"),
    ("objective_quality_minmax_minsum", "Objective-quality comparison for MinMax and MinSum"),
    ("runtime_versus_quality", "Runtime versus valid objective quality"),
    ("per_instance_normalized_quality", "Per-instance normalized objective comparison"),
    ("performance_vs_n_m", "Smoke-dataset runtime/objective versus n and m (not a scaling study)"),
    ("reliability_comparison", "Feasibility, verification, timeout and failure rates"),
    ("family_level_comparison", "Family-level objective and runtime comparison"),
    ("convergence_curves", "Convergence curves from valid solver traces"),
    ("minmaxsum_peak_integral", "MinMaxSum peak-versus-integral components"),
    ("minmaxsum_pareto", "MinMaxSum component dominance/Pareto visualization"),
    ("three_time_solution_figures", "Three-time geometry snapshots generated by Prompt 6"),
]


def _snapshot_figure_status(experiment: Path) -> dict[str, Any]:
    manifest_path = experiment / "reports" / "visualization_manifest.json"
    if not manifest_path.is_file():
        return {
            "figure": "three_time_solution_figures",
            "generation_status": "NOT_GENERATED",
            "path_png": None,
            "path_pdf": None,
            "path_manifest": None,
            "artifact_class": "solution_snapshots",
            "snapshot_count": 0,
            "reason": "No visualization manifest exists; snapshot artifacts are not generated.",
        }

    try:
        from visualize_experiment import audit as audit_visualizations
    except ImportError:
        from scripts.visualize_experiment import audit as audit_visualizations

    audit_result = audit_visualizations(experiment)
    manifest = read_json(manifest_path)
    snapshot_count = sum(
        1 for slot in manifest.get("slots", [])
        if isinstance(slot, dict)
        and isinstance(slot.get("snapshots"), dict)
        and isinstance(slot["snapshots"].get("triptych"), dict)
    )
    if snapshot_count != audit_result.get("rendered_slots"):
        raise ValueError(
            "visualization manifest snapshot count disagrees with validated slots"
        )
    state = "GENERATED" if snapshot_count else "NOT_GENERATED"
    return {
        "figure": "three_time_solution_figures",
        "generation_status": state,
        "path_png": None,
        "path_pdf": None,
        "path_manifest": "../reports/visualization_manifest.json",
        "artifact_class": "solution_snapshots",
        "snapshot_count": snapshot_count,
        "reason": (
            f"{snapshot_count} verified run triptychs are indexed in the "
            "authoritative visualization manifest; snapshots are separate from "
            "report figures owned by this pipeline."
            if snapshot_count else
            "The validated visualization manifest contains no generated triptych snapshots."
        ),
    }


def _replace_snapshot_figure_status(
    experiment: Path, statuses: Sequence[Mapping[str, Any]]
) -> list[dict[str, Any]]:
    updated = [dict(item) for item in statuses]
    snapshot_status = _snapshot_figure_status(experiment)
    for index, item in enumerate(updated):
        if item.get("figure") == "three_time_solution_figures":
            updated[index] = snapshot_status
            break
    else:
        updated.append(snapshot_status)
    return updated


def _save_plot(figure: Any, folder: Path, name: str, dpi: int) -> tuple[str, str]:
    folder.mkdir(parents=True, exist_ok=True)
    png = folder / f"{name}.png"
    pdf = folder / f"{name}.pdf"
    figure.savefig(png, dpi=dpi, bbox_inches="tight")
    figure.savefig(pdf, bbox_inches="tight")
    import matplotlib.pyplot as plt
    plt.close(figure)
    if not png.is_file() or png.stat().st_size == 0 or not pdf.is_file() or pdf.stat().st_size == 0:
        raise ValueError(f"figure output validation failed for {name}")
    return png.name, pdf.name


def _plot_figures(data: dict[str, Any], dpi: int) -> list[dict[str, Any]]:
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    exp: Path = data["experiment"]
    rows: list[dict[str, Any]] = data["rows"]
    output = exp / "figures"
    output.mkdir(parents=True, exist_ok=True)
    plt.rcParams.update({
        "figure.dpi": dpi,
        "savefig.dpi": dpi,
        "font.size": 9,
        "axes.titlesize": 11,
        "axes.labelsize": 9,
    })
    statuses: list[dict[str, Any]] = []

    def emit(name: str, draw: Any, reason: str) -> None:
        if reason:
            statuses.append({
                "figure": name, "generation_status": "NOT_GENERATED",
                "path_png": None, "path_pdf": None, "reason": reason,
            })
            return
        figure = draw()
        png, pdf = _save_plot(figure, output, f"report_{name}", dpi)
        statuses.append({
            "figure": name, "generation_status": "GENERATED",
            "path_png": png, "path_pdf": pdf, "reason": "",
        })

    completed_runtime = _runtime_population(rows)
    runtime_reason = "No COMPLETED runs with explicit solve_time_sec."
    if completed_runtime:
        def runtime_by_algorithm():
            fig, ax = plt.subplots(figsize=(11, 5))
            algorithms = sorted({r["algorithm"] for r in completed_runtime})
            objectives = [o for o in OBJECTIVES if any(
                r["objective"] == o for r in completed_runtime
            )]
            width = .8 / max(1, len(objectives))
            for i, objective in enumerate(objectives):
                subset = [r for r in completed_runtime if r["objective"] == objective]
                medians = []
                for algorithm in algorithms:
                    values = _float_values(
                        [r for r in subset if r["algorithm"] == algorithm],
                        "solve_time_sec",
                    )
                    medians.append(_q(values, .5))
                positions = np.arange(len(algorithms)) + i * width
                ax.bar(positions, [np.nan if v is None else v for v in medians], width=width,
                       label=objective)
                for pos, algorithm in zip(positions, algorithms):
                    count = sum(r["algorithm"] == algorithm and r["objective"] == objective
                                for r in subset)
                    ax.text(pos, 0, f"n={count}", rotation=90, va="bottom", ha="center",
                            fontsize=7)
            ax.set_xticks(np.arange(len(algorithms)) + width * max(0, len(objectives)-1) / 2)
            ax.set_xticklabels(algorithms, rotation=35, ha="right")
            ax.set_ylabel("Solve time (s; COMPLETED only)")
            ax.set_title("Smoke20 single-run runtime comparison")
            ax.legend(title="Objective")
            ax.grid(axis="y", alpha=.25)
            return fig
        emit("runtime_by_algorithm_objective", runtime_by_algorithm, "")

        def runtime_distribution():
            fig, ax = plt.subplots(figsize=(11, 5))
            algorithms = sorted({r["algorithm"] for r in completed_runtime})
            samples = [[float(r["solve_time_sec"]) for r in completed_runtime
                        if r["algorithm"] == algorithm] for algorithm in algorithms]
            ax.boxplot(samples, labels=algorithms, showfliers=True)
            ax.set_ylabel("Solve time (s; COMPLETED only)")
            ax.set_title("Runtime distribution by algorithm")
            ax.tick_params(axis="x", rotation=35)
            for x, values in enumerate(samples, 1):
                ax.text(x, ax.get_ylim()[0], f"n={len(values)}", ha="center", va="bottom", fontsize=8)
            ax.grid(axis="y", alpha=.25)
            return fig
        emit("runtime_distribution_by_algorithm", runtime_distribution, "")
    else:
        emit("runtime_by_algorithm_objective", lambda: None, runtime_reason)
        emit("runtime_distribution_by_algorithm", lambda: None, runtime_reason)

    quality = _quality_population(rows)
    if quality:
        def quality_plot():
            objectives = [o for o in ("minmax", "minsum")
                          if any(r["objective"] == o for r in quality)]
            fig, axes = plt.subplots(1, len(objectives),
                                     figsize=(6 * len(objectives), 5),
                                     squeeze=False)
            for axis, objective in zip(axes[0], objectives):
                subset = [r for r in quality if r["objective"] == objective]
                for algorithm in sorted({r["algorithm"] for r in subset}):
                    points = [r for r in subset if r["algorithm"] == algorithm]
                    axis.scatter([r["instance_id"] for r in points],
                                 [r["objective_value"] for r in points],
                                 label=algorithm, alpha=.75)
                axis.set_title(objective)
                axis.set_ylabel("Objective value (recorded units)")
                axis.tick_params(axis="x", rotation=70, labelsize=7)
                axis.grid(axis="y", alpha=.2)
            axes[0][-1].legend(bbox_to_anchor=(1.03, 1), loc="upper left", fontsize=7)
            fig.suptitle("Feasible, verification-passed objective quality "
                         f"({', '.join(objectives)})")
            fig.tight_layout()
            return fig
        emit("objective_quality_minmax_minsum", quality_plot, "")

        runtime_quality = [
            r for r in quality if r["terminal_status"] == "COMPLETED"
            and _finite_number(r.get("solve_time_sec")) is not None
        ]
        if runtime_quality:
            def runtime_quality_plot():
                objectives = [o for o in ("minmax", "minsum")
                              if any(r["objective"] == o for r in runtime_quality)]
                fig, axes = plt.subplots(1, len(objectives),
                                         figsize=(5.5 * len(objectives), 4.5),
                                         squeeze=False)
                for ax, objective in zip(axes[0], objectives):
                    subset = [r for r in runtime_quality if r["objective"] == objective]
                    for algorithm in sorted({r["algorithm"] for r in subset}):
                        points = [r for r in subset if r["algorithm"] == algorithm]
                        ax.scatter([r["solve_time_sec"] for r in points],
                                   [r["objective_value"] for r in points],
                                   label=algorithm, alpha=.75)
                    ax.set_xlabel("Solve time (s; COMPLETED only)")
                    ax.set_ylabel("Objective value")
                    ax.set_title(objective)
                    ax.grid(alpha=.2)
                axes[0][-1].legend(bbox_to_anchor=(1.03, 1), loc="upper left", fontsize=7)
                fig.suptitle("Valid runtime versus feasible verified quality")
                fig.tight_layout()
                return fig
            emit("runtime_versus_quality", runtime_quality_plot, "")
        else:
            emit("runtime_versus_quality", lambda: None,
                 "No rows have both valid scalar quality and uncensored completed solve time.")

        normalized = []
        for objective in ("minmax", "minsum"):
            per_instance: dict[str, list[dict[str, Any]]] = {}
            for row in quality:
                if row["objective"] == objective:
                    per_instance.setdefault(row["instance_id"], []).append(row)
            for instance_id, group in per_instance.items():
                if len(group) < 2:
                    continue
                best = min(float(row["objective_value"]) for row in group)
                if best <= 0:
                    continue
                normalized.extend((objective, row["algorithm"], row["objective_value"] / best)
                                  for row in group)
        if normalized:
            def normalized_plot():
                objectives = [o for o in ("minmax", "minsum")
                              if any(item[0] == o for item in normalized)]
                fig, axes = plt.subplots(1, len(objectives),
                                         figsize=(5.5 * len(objectives), 4.5),
                                         squeeze=False)
                for ax, objective in zip(axes[0], objectives):
                    algorithms = sorted({a for o, a, _ in normalized if o == objective})
                    values = [[value for o, a, value in normalized if o == objective and a == alg]
                              for alg in algorithms]
                    if any(values):
                        ax.boxplot(values, labels=algorithms)
                    ax.set_title(objective)
                    ax.set_ylabel("Objective / best valid value for same instance")
                    ax.tick_params(axis="x", rotation=35)
                    ax.grid(axis="y", alpha=.2)
                fig.suptitle("Per-instance normalized quality; descriptive smoke comparison")
                fig.tight_layout()
                return fig
            emit("per_instance_normalized_quality", normalized_plot, "")
        else:
            emit("per_instance_normalized_quality", lambda: None,
                 "Fewer than two positive valid objective observations per instance.")
    else:
        reason = "No feasible, verification-passed MinMax/MinSum records with finite objective values."
        for name in ("objective_quality_minmax_minsum", "runtime_versus_quality",
                     "per_instance_normalized_quality"):
            emit(name, lambda: None, reason)

    scaled = [row for row in rows if row["terminal_status"] == "COMPLETED"
              and _finite_number(row.get("solve_time_sec")) is not None]
    if scaled and all(_finite_number(row.get("n")) is not None and
                      _finite_number(row.get("m")) is not None for row in scaled):
        def scale_plot():
            fig, axes = plt.subplots(1, 2, figsize=(10, 4.3))
            for ax, field in zip(axes, ("n", "m")):
                for objective in OBJECTIVES:
                    group = [row for row in scaled if row["objective"] == objective]
                    ax.scatter([row[field] for row in group],
                               [row["solve_time_sec"] for row in group],
                               label=objective, alpha=.7)
                ax.set_xlabel(f"{field} (small smoke dataset)")
                ax.set_ylabel("Solve time (s; COMPLETED only)")
                ax.grid(alpha=.2)
            axes[0].legend(title="Objective")
            fig.suptitle("Smoke20 performance versus n and m — not a scaling study")
            fig.tight_layout()
            return fig
        emit("performance_vs_n_m", scale_plot, "")
    else:
        emit("performance_vs_n_m", lambda: None,
             "No completed runs with explicit solve_time_sec and both n and m.")

    terminal_rows = [row for row in rows if row["terminal_status"] != "PLANNED"]
    if terminal_rows:
        def reliability_plot():
            fig, ax = plt.subplots(figsize=(11, 5))
            algorithms = sorted({row["algorithm"] for row in terminal_rows})
            metrics = (
                ("Feasible", lambda r: r.get("feasible") is True),
                ("EMPIRICAL verified", lambda r: r.get("verification_passed") is True and r.get("verification_kind") == "EMPIRICAL"),
                ("Continuous verified", lambda r: r.get("verification_passed") is True and r.get("verification_kind") == "CERTIFIED_CONTINUOUS"),
                ("Timeout", lambda r: r["terminal_status"] in {"TIME_LIMIT", "TIMED_OUT"}),
                ("Failure/invalid", _is_failure),
            )
            width = .8 / len(metrics)
            for i, (label, predicate) in enumerate(metrics):
                rates = []
                for algorithm in algorithms:
                    group = [row for row in terminal_rows if row["algorithm"] == algorithm]
                    rates.append(sum(predicate(row) for row in group) / len(group) if group else 0)
                ax.bar(np.arange(len(algorithms)) + i * width, rates, width, label=label)
            ax.set_xticks(np.arange(len(algorithms)) + width * 2)
            ax.set_xticklabels(algorithms, rotation=35, ha="right")
            ax.set_ylabel("Rate among terminal records")
            ax.set_ylim(0, 1.05)
            ax.set_title("Reliability outcomes; empirical and continuous verification separated")
            ax.legend(fontsize=7)
            ax.grid(axis="y", alpha=.2)
            return fig
        emit("reliability_comparison", reliability_plot, "")
    else:
        emit("reliability_comparison", lambda: None, "No terminal run records.")

    family_valid = [row for row in quality if row["family"] != "unknown"]
    if family_valid:
        def family_plot():
            fig, ax = plt.subplots(figsize=(11, 5))
            families = sorted({row["family"] for row in family_valid})
            algorithms = sorted({row["algorithm"] for row in family_valid})
            x = np.arange(len(families))
            width = .8 / max(1, len(algorithms))
            for i, algorithm in enumerate(algorithms):
                values = []
                for family in families:
                    group = [row for row in family_valid if row["family"] == family
                             and row["algorithm"] == algorithm]
                    observations = _float_values(group, "objective_value")
                    values.append(_q(observations, .5))
                ax.bar(x + i * width,
                       [np.nan if value is None else value for value in values],
                       width=width, label=algorithm)
            ax.set_xticks(x + width * max(0, len(algorithms)-1) / 2)
            ax.set_xticklabels(families, rotation=45, ha="right")
            ax.set_ylabel("Median feasible verified objective")
            ax.set_title("Smoke20 family-level comparison")
            ax.legend(fontsize=7, ncol=2)
            ax.grid(axis="y", alpha=.2)
            return fig
        emit("family_level_comparison", family_plot, "")
    else:
        emit("family_level_comparison", lambda: None,
             "No valid objective observations with selection-manifest family labels.")

    traces = []
    invalid_trace_count = 0
    for row in rows:
        if not row.get("run_id"):
            continue
        record = _choose_record(data["attempts"].get(row.get("run_key", "")))
        solver = _solver_result(record)
        relative = solver.get("trace_csv_path")
        if not relative or not record:
            continue
        trace = (exp / "runs" / str(record["run_id"]) / str(relative)).resolve()
        try:
            trace.relative_to(exp.resolve())
        except ValueError:
            invalid_trace_count += 1
            continue
        if not trace.is_file():
            invalid_trace_count += 1
            continue
        try:
            with trace.open(encoding="utf-8", newline="") as source:
                reader = csv.DictReader(source)
                fields = reader.fieldnames or []
                data_rows = list(reader)
            x_name = next((name for name in ("iteration", "time_sec", "elapsed_sec")
                           if name in fields), None)
            y_name = next((name for name in ("objective_value", "cost", "best_cost")
                           if name in fields), None)
            if not x_name or not y_name:
                invalid_trace_count += 1
                continue
            x = [_finite_number(item.get(x_name)) for item in data_rows]
            y = [_finite_number(item.get(y_name)) for item in data_rows]
            points = [(a, b) for a, b in zip(x, y) if a is not None and b is not None]
            if points and row["objective"] in {"minmax", "minsum"}:
                traces.append((row, x_name, y_name, points))
            else:
                invalid_trace_count += 1
        except (OSError, csv.Error, UnicodeError):
            invalid_trace_count += 1
    if traces:
        def convergence_plot():
            fig, axes = plt.subplots(1, 2, figsize=(11, 4.5))
            for row, x_name, y_name, points in traces:
                axis = axes[0 if row["objective"] == "minmax" else 1]
                axis.plot([point[0] for point in points],
                          [point[1] for point in points],
                          label=f"{row['algorithm']} · {row['instance_id']}")
            for axis, objective in zip(axes, ("minmax", "minsum")):
                axis.set_title(objective)
                relevant = [trace for trace in traces
                            if trace[0]["objective"] == objective]
                if relevant:
                    axis.set_xlabel(f"Recorded {relevant[0][1]}")
                    axis.set_ylabel(f"Recorded {relevant[0][2]} (source units)")
                if axis.lines:
                    axis.legend(fontsize=6)
                axis.grid(alpha=.2)
            fig.suptitle("Recorded solver traces; only parseable trace columns shown")
            fig.tight_layout()
            return fig
        emit("convergence_curves", convergence_plot, "")
    else:
        reason = "No valid run-associated trace CSV files with finite supported x/y columns are present."
        if invalid_trace_count:
            reason += f" {invalid_trace_count} declared trace file(s) were missing or invalid."
        emit("convergence_curves", lambda: None, reason)

    joint = [row for row in rows if row["objective"] == "minmaxsum"
             and row.get("feasible") is True
             and row.get("verification_passed") is True
             and _finite_number(row.get("peak_cost")) is not None
             and _finite_number(row.get("integral_cost")) is not None]
    if joint:
        joint_instances = sorted({row["instance_id"] for row in joint})
        joint_algorithms = sorted({row["algorithm"] for row in joint})
        joint_colors = {
            algorithm: plt.get_cmap("tab20")(index % 20)
            for index, algorithm in enumerate(joint_algorithms)
        }
        columns = min(4, len(joint_instances))
        nrows = math.ceil(len(joint_instances) / columns)

        def joint_scatter():
            fig, axes = plt.subplots(
                nrows, columns, figsize=(columns * 3.2, nrows * 2.8),
                squeeze=False,
            )
            for index, instance_id in enumerate(joint_instances):
                axis = axes[index // columns][index % columns]
                group = [row for row in joint if row["instance_id"] == instance_id]
                for algorithm in joint_algorithms:
                    points = [row for row in group if row["algorithm"] == algorithm]
                    if points:
                        axis.scatter(
                            [row["peak_cost"] for row in points],
                            [row["integral_cost"] for row in points],
                            color=joint_colors[algorithm], label=algorithm, alpha=.8,
                        )
                axis.set_title(instance_id, fontsize=8)
                axis.set_xlabel("Peak cost (area units)", fontsize=7)
                axis.set_ylabel("Integral cost (area×time)", fontsize=7)
                axis.tick_params(labelsize=6)
                axis.grid(alpha=.2)
            for index in range(len(joint_instances), nrows * columns):
                axes[index // columns][index % columns].set_visible(False)
            handles, labels = axes[0][0].get_legend_handles_labels()
            fig.legend(handles, labels, loc="upper center", ncol=min(6, len(labels)),
                       fontsize=7)
            fig.suptitle("Verified feasible MinMaxSum vector components by instance")
            fig.tight_layout(rect=(0, 0, 1, .96))
            return fig
        emit("minmaxsum_peak_integral", joint_scatter, "")

        def pareto_scatter():
            fig, axes = plt.subplots(
                nrows, columns, figsize=(columns * 3.2, nrows * 2.8),
                squeeze=False,
            )
            for index, instance_id in enumerate(joint_instances):
                axis = axes[index // columns][index % columns]
                group = [row for row in joint if row["instance_id"] == instance_id]
                nondominated = [
                    row for row in group if not any(
                        other is not row
                        and other["peak_cost"] <= row["peak_cost"]
                        and other["integral_cost"] <= row["integral_cost"]
                        and (
                            other["peak_cost"] < row["peak_cost"]
                            or other["integral_cost"] < row["integral_cost"]
                        )
                        for other in group
                    )
                ]
                for algorithm in joint_algorithms:
                    points = [row for row in group if row["algorithm"] == algorithm]
                    if points:
                        axis.scatter(
                            [row["peak_cost"] for row in points],
                            [row["integral_cost"] for row in points],
                            color=joint_colors[algorithm], label=algorithm, alpha=.7,
                        )
                front = sorted({
                    (row["peak_cost"], row["integral_cost"])
                    for row in nondominated
                })
                if len(front) > 1:
                    axis.plot([point[0] for point in front],
                              [point[1] for point in front],
                              color="black", linestyle="--",
                              label="Observed nondominated set")
                axis.set_title(instance_id, fontsize=8)
                axis.set_xlabel("Peak cost (area units)", fontsize=7)
                axis.set_ylabel("Integral cost (area×time)", fontsize=7)
                axis.tick_params(labelsize=6)
                axis.grid(alpha=.2)
            for index in range(len(joint_instances), nrows * columns):
                axes[index // columns][index % columns].set_visible(False)
            handles, labels = axes[0][0].get_legend_handles_labels()
            fig.legend(handles, labels, loc="upper center", ncol=min(6, len(labels)),
                       fontsize=7)
            fig.suptitle("Observed MinMaxSum nondominated sets by instance "
                         "(not optimality certificates)")
            fig.tight_layout(rect=(0, 0, 1, .96))
            return fig
        emit("minmaxsum_pareto", pareto_scatter, "")
    else:
        reason = ("No MinMaxSum record has both finite vector components, "
                  "feasibility=true, and a passing recorded verification.")
        emit("minmaxsum_peak_integral", lambda: None, reason)
        emit("minmaxsum_pareto", lambda: None, reason)

    return _replace_snapshot_figure_status(exp, statuses)


def _write_figure_report(data: dict[str, Any], statuses: Sequence[Mapping[str, Any]]) -> None:
    exp: Path = data["experiment"]
    figure_dir = exp / "figures"
    lines = [
        "# Comparative Figures",
        "",
        f"- Experiment: `{exp.name}`",
        "- Source: this experiment's validated selection, run plan and canonical run records only.",
        "- Interpretation: one repeat is a single-run smoke comparison; no statistical significance or general superiority is claimed.",
        "- Censored TIME_LIMIT/TIMED_OUT durations are not plotted as uncensored runtime observations.",
        "- MinMaxSum is shown only as the `(peak_cost, integral_cost)` vector.",
        "- Missing data are recorded as NOT_GENERATED; no empty placeholder chart is represented as a result.",
        "- Solution snapshots are a separate artifact class owned by the visualization manifest, not by this report-figure generator.",
        "",
        "| Requested figure | Status | Artifact | Explanation |",
        "|---|---|---|---|",
    ]
    for item in statuses:
        artifact = (
            f"[Manifest]({item['path_manifest']})"
            if item.get("path_manifest") else
            f"[PNG]({item['path_png']}) · [PDF]({item['path_pdf']})"
            if item.get("path_png") and item.get("path_pdf") else
            f"[PNG]({item['path_png']})"
            if item.get("path_png") else
            "—"
        )
        lines.append(
            f"| {item['figure']} | {item['generation_status']} | {artifact} | "
            f"{item.get('reason', '')} |"
        )
    lines.extend([
        "",
        "The Prompt 6 per-instance/objective comparison sheets and solution snapshots, when present, are indexed in "
        "[the visualization manifest](../reports/visualization_manifest.json).",
    ])
    (figure_dir / "REPORT.md").write_text("\n".join(lines) + "\n",
                                          encoding="utf-8")
    (figure_dir / "figure_status.json").write_text(
        json.dumps({
            "experiment_id": exp.name,
            "requested": [name for name, _ in FIGURE_DEFINITIONS],
            "figures": list(statuses),
        }, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )


def _write_experiment_report(data: dict[str, Any], figures: Sequence[Mapping[str, Any]]) -> None:
    exp: Path = data["experiment"]
    manifest = data["manifest"]
    rows = data["rows"]
    status_counts = _status_counts(rows)
    plan_count = len(data["plan"])
    terminal_count = sum(row["terminal_status"] != "PLANNED" for row in rows)
    visual = {}
    visual_path = exp / "reports" / "visualization_manifest.json"
    if visual_path.is_file():
        visual = read_json(visual_path)
    actual_figures = [row for row in figures if row["generation_status"] == "GENERATED"]
    lines = [
        "# Smoke20 Comparative Experiment Report",
        "",
        f"- **Experiment ID:** `{exp.name}`",
        f"- **Pipeline status:** `{manifest.get('status', 'unknown')}`",
        f"- **Git commit:** `{manifest.get('git_commit')}` (dirty={manifest.get('git_dirty')})",
        f"- **Dataset profile:** `{data.get('dataset_profile') or 'unknown'}`; selected instances={manifest.get('instance_count', data['selection_count'])}",
        f"- **Objectives:** {', '.join(manifest.get('objectives', []))}",
        f"- **Selected exact reference backend:** `{manifest.get('selected_exact_backend')}`",
        f"- **Reproduction command:** `{manifest.get('reproduction_command', 'not recorded')}`",
        f"- **Run slots:** planned={plan_count}, terminal={terminal_count}, status counts=`{json.dumps(status_counts, sort_keys=True)}`",
        "",
        "## Scientific interpretation",
        "",
        "This is a small smoke dataset, not a scaling study. With one repeat, reported values are a single-run smoke comparison. "
        "No significance tests, confidence claims, universal speedups, or general algorithm-superiority conclusions are made.",
        "Runtime quantiles include only completed runs with an explicit `solve_time_sec`; time-limited and timed-out runs are counted separately as censored outcomes. "
        "Wall, solve, verification, serialization and total times remain separate. Missing measurements remain null.",
        "MinMaxSum is the two-component vector `(peak_cost, integral_cost)`; no scalar score is manufactured. "
        "Empirical ratios to exact are included only against the one designated valid, proven, continuously verified exact reference and are not theoretical approximation ratios.",
        "EMPIRICAL/sample verification is reported separately from continuous verification. A rendered solution is not by itself a feasibility, verification, or optimality proof.",
        "",
        "## Tables",
        "",
        "See [the table index](../tables/00_index.md). Every CSV has an adjacent Markdown companion with source, population, filters, formulas, null handling, column definitions and a bounded preview.",
        "",
        "| Table | CSV | Markdown companion |",
        "|---|---|---|",
    ]
    for name in TABLE_SCHEMAS:
        lines.append(f"| {name.replace('_', ' ').title()} | [CSV](../tables/{name}.csv) | [MD](../tables/{name}.md) |")
    lines.extend([
        "",
        "## Figures",
        "",
        f"Generated figures: {len(actual_figures)} of {len(FIGURE_DEFINITIONS)} requested.",
        "See [figure report](../figures/REPORT.md) and [figure status index](../figures/figure_status.json).",
    ])
    lines.extend(f"- [{item['figure']}](../figures/{item['path_png']})"
                 for item in actual_figures if item.get("path_png") and item["path_png"].endswith(".png"))
    lines.extend([
        "",
        "## Geometry and animations",
        "",
        f"- Three-time solution snapshots generated: {visual.get('generated_solution_snapshots', 0)}.",
        f"- Per-solution animations generated: {visual.get('generated_animations', 0)}.",
        f"- Per-instance/objective comparison sheets: {len(visual.get('comparison_sheets', []))}.",
        "- [Visualization manifest](visualization_manifest.json)",
        "- [Visualization audit/report](visualization_report.md)",
        "- [Completeness/artifact table](../tables/completeness_artifacts.csv) and [companion](../tables/completeness_artifacts.md)",
    ])
    if visual.get("comparison_sheets"):
        lines.extend(["", "### Per-instance/objective comparison sheets", ""])
        lines.extend(
            f"- [{item['instance_id']} / {item['objective']}]"
            f"(../{item['path']})"
            for item in visual["comparison_sheets"]
        )
    generated_snapshots = []
    generated_animations = []
    for slot in visual.get("slots", []):
        snapshot = slot.get("snapshots")
        if isinstance(snapshot, dict) and snapshot.get("triptych", {}).get("path"):
            generated_snapshots.append((slot, snapshot["triptych"]["path"]))
        animation = slot.get("animation")
        if isinstance(animation, dict) and animation.get("path"):
            generated_animations.append((slot, animation["path"]))
    if generated_snapshots:
        lines.extend(["", "### Three-time solution snapshots", ""])
        lines.extend(
            f"- [{slot['instance_id']} / {slot['algorithm']} / {slot['objective']}]"
            f"(../{path})"
            for slot, path in generated_snapshots
        )
    else:
        lines.extend(["", "### Three-time solution snapshots", "",
                      "No renderable solution snapshot exists for this experiment."])
    if generated_animations:
        lines.extend(["", "### Per-solution animations", ""])
        lines.extend(
            f"- [{slot['instance_id']} / {slot['algorithm']} / {slot['objective']}]"
            f"(../{path})"
            for slot, path in generated_animations
        )
    else:
        lines.extend(["", "### Per-solution animations", "",
                      "No eligible per-solution animation exists for this experiment."])
    integrity_lines = [
        "",
        "## Integrity and provenance",
        "",
        "- [Experiment manifest](../experiment_manifest.json)",
        "- [Run plan](../runs/plan.jsonl)",
        "- [Reporting/companion integrity](reporting_integrity.json)",
        "- [Experiment artifact inventory](../artifacts.json)",
    ]
    selection_path = _selection_manifest_path(exp)
    if selection_path is not None:
        integrity_lines.append(
            f"- [Selection manifest](../dataset/{selection_path.name})"
        )
    if (exp / "reports" / "selection_manifest_recovery.md").is_file():
        integrity_lines.append(
            "- [Selection manifest recovery note](selection_manifest_recovery.md)"
        )
    if (exp / "reports" / "result_integrity_report.md").is_file():
        integrity_lines.append("- [Result integrity report](result_integrity_report.md)")
    else:
        integrity_lines.append(
            "- Result integrity report: not generated; no terminal run records are present."
            if terminal_count == 0 else
            "- Result integrity report: not generated by the pipeline."
        )
    if (exp / "aggregates" / "aggregate_manifest.json").is_file():
        integrity_lines.append("- [Aggregate manifest](../aggregates/aggregate_manifest.json)")
    else:
        integrity_lines.append(
            "- Canonical aggregate manifest: not present; no aggregate export is available."
        )
    lines.extend(integrity_lines)
    if data["validation_errors"]:
        lines.extend(["", "### Invalid/unusable run records", ""])
        lines.extend(f"- `{error}`" for error in data["validation_errors"])
    (exp / "reports" / "experiment_report.md").write_text(
        "\n".join(lines) + "\n", encoding="utf-8"
    )
    (exp / "reports" / "benchmark_summary.md").write_text(
        "This compatibility summary is generated from the validated experiment-scoped report.\n\n"
        "[Open the complete comparative report](experiment_report.md).\n",
        encoding="utf-8",
    )


def _write_table_status_json(
    data: dict[str, Any], *, companion_audit: str
) -> None:
    exp: Path = data["experiment"]
    rows = data["rows"]
    (exp / "reports" / "reporting_integrity.json").write_text(
        json.dumps({
            "experiment_id": exp.name,
            "run_plan_sha256": sha256_file(exp / "runs" / "plan.jsonl"),
            "planned_count": len(rows),
            "terminal_count": sum(row["terminal_status"] != "PLANNED" for row in rows),
            "status_counts": _status_counts(rows),
            "run_record_validation_errors": data["validation_errors"],
            "aggregate_complete": (
                data["aggregate_manifest"].get("complete")
                if data["aggregate_manifest"] else None
            ),
            "csv_tsv_companion_audit": companion_audit,
        }, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )


def _refresh_completeness_artifact_counts(data: dict[str, Any]) -> None:
    exp: Path = data["experiment"]
    table_path = exp / "tables" / "completeness_artifacts.csv"
    if not table_path.is_file():
        return
    with table_path.open(encoding="utf-8", newline="") as source:
        reader = csv.DictReader(source)
        columns = list(reader.fieldnames or [])
        rows = list(reader)
    for row in rows:
        row["artifact_count"] = _artifact_count(exp / row["artifact_scope"])
    write_delimited(
        table_path, rows, columns=columns,
        companion={
            "title": "Completeness and Artifact Inventory",
            "purpose": "Report planned versus terminal run coverage and exact observed artifact counts by experiment directory.",
            "experiment_id": exp.name,
            "source": "active experiment manifest, canonical run plan/records and artifact directory inventory",
            "generation_command": "scripts/report_experiment.py --stage final",
            "file_scope": "One row per artifact directory scope.",
            "sort_order": "declared experiment artifact directory order",
            "filters": "Counts include all currently present files in each named scope; no files from another experiment are read.",
            "column_definitions": _definitions(columns),
            "limitations": "A present artifact is not by itself evidence of a valid solver result; inspect canonical run and visualization manifests.",
            "formulas": "artifact_count is the recursive number of files present in the scope when this final report is generated. Run planned/terminal counts come from the immutable run plan and validated terminal records.",
        },
    )


def run(experiment_dir: str | Path, stage: str, dpi: int = 300) -> None:
    data = _load_experiment(Path(experiment_dir))
    figures = []
    if stage in {"tables", "all"}:
        build_tables(data)
    if stage == "final":
        build_tables(data)
    if stage in {"figures", "all"}:
        figures = _plot_figures(data, dpi)
        _write_figure_report(data, figures)
    if stage in {"tables", "final"}:
        # Preserve existing visualization status while reporting tables.
        figure_status_path = data["experiment"] / "figures" / "figure_status.json"
        if figure_status_path.is_file():
            saved = read_json(figure_status_path)
            figures = saved.get("figures", [])
        else:
            figures = [
                {
                    "figure": name, "generation_status": "NOT_GENERATED",
                    "path_png": None, "path_pdf": None,
                    "reason": "Figure stage has not run.",
                }
                for name, _ in FIGURE_DEFINITIONS
            ]
        figures = _replace_snapshot_figure_status(
            data["experiment"], figures
        )
        _write_figure_report(data, figures)
    _write_experiment_report(data, figures)
    _write_table_status_json(data, companion_audit="pending")
    _refresh_completeness_artifact_counts(data)
    errors = audit_companions(
        data["experiment"], create_missing=False, experiment_id=data["experiment"].name
    )
    if errors:
        raise ValueError("recursive CSV/TSV companion audit failed: " + "; ".join(errors))
    _write_table_status_json(data, companion_audit="passed")
    from kdc_tools.storage import index_experiment_artifacts
    index_experiment_artifacts(data["experiment"])


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--experiment", required=True)
    parser.add_argument("--stage", choices=("tables", "figures", "all", "final", "audit"),
                        default="all")
    parser.add_argument("--dpi", type=int, default=300)
    args = parser.parse_args(argv)
    if args.dpi <= 0:
        parser.error("--dpi must be positive")
    try:
        if args.stage == "audit":
            errors = audit_companions(args.experiment, create_missing=False)
            if errors:
                raise ValueError("; ".join(errors))
            print("CSV/TSV companion audit passed.")
        else:
            run(args.experiment, args.stage, args.dpi)
            print(f"Wrote {args.stage} report for {Path(args.experiment).name}.")
    except (OSError, ValueError, KeyError, TypeError, json.JSONDecodeError) as error:
        print(f"report_experiment.py: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
