import math
from pathlib import Path
import sys

import pytest


ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))

from kdc_tools.schemas import (  # noqa: E402
    SCHEMA_FILES,
    SchemaValidationError,
    dumps_json,
    load_schema,
    loads_json,
    validate_document,
)


def valid_run_record():
    return {
        "schema_version": 1,
        "run_id": "run-001",
        "run_key": "tiny/greedy/minmax/0",
        "experiment_id": "exp-001",
        "instance_id": "tiny",
        "family": "fixture",
        "n": 1,
        "m": 1,
        "algorithm": "greedy",
        "objective": "minmax",
        "repeat": 0,
        "seed": 42,
        "status": "COMPLETED",
        "requested_backend": "auto",
        "selected_backend": "branch-and-bound",
        "actual_backend": "branch-and-bound",
        "backend_details": {
            "native": False,
            "backend_version": "unknown",
            "backend_detection": {},
            "capabilities": {},
        },
        "started_at": "2026-10-05T10:00:00Z",
        "finished_at": "2026-10-05T10:00:01Z",
        "configuration_sha256": "a" * 64,
        "input_sha256": "b" * 64,
        "provenance": {
            "source_commit": "deadbeef",
            "solver_version": "1.0",
            "platform": "test",
        },
        "result": {
            "feasible": True,
            "objective_value": 1.0,
            "lower_bound": 0.5,
            "upper_bound": 1.0,
            "optimality_status": "FEASIBLE",
            "error_message": None,
            "solver_result": {},
        },
        "verification": {
            "schema_version": 1,
            "run_id": "run-001",
            "experiment_id": "exp-001",
            "result_sha256": "d" * 64,
            "verification_mode": "continuous",
            "status": "PASSED",
            "kind": "CERTIFIED_CONTINUOUS",
            "passed": True,
            "continuous": True,
            "checked_at": "2026-10-05T10:00:01Z",
            "message": None,
        },
        "artifacts": [{
            "path": "solution.json",
            "sha256": "c" * 64,
            "size_bytes": 24,
            "media_type": "application/json",
        }],
    }


def test_all_registered_schemas_are_valid_json_schema():
    assert SCHEMA_FILES
    for name in SCHEMA_FILES:
        assert load_schema(name)["$schema"].endswith("/2020-12/schema")


def test_valid_run_record_passes_schema():
    validate_document(valid_run_record(), "run_record")


def _joint_metadata():
    return {
        "minmax_component_status": "COMPLETED",
        "minsum_component_status": "COMPLETED",
        "minmax_component_optimality": "FEASIBLE",
        "minsum_component_optimality": "FEASIBLE",
        "joint_optimality_status": "FEASIBLE",
        "dominates_minmax": True,
        "dominates_minsum": True,
        "dominance_invariants_ok": True,
        "minmax_component_peak": 3.0,
        "minmax_component_integral": 2.0,
        "minsum_component_peak": 4.0,
        "minsum_component_integral": 1.5,
        "minmax_source_run": "run-mm",
        "minsum_source_run": "run-ms",
    }


def test_minmaxsum_record_requires_vector_and_joint_details():
    record = valid_run_record()
    record["objective"] = "minmaxsum"
    record["result"]["objective_value"] = None
    record["result"]["objective_vector"] = {
        "peak_cost": 2.0,
        "integral_cost": 1.25,
    }
    record["result"]["joint"] = _joint_metadata()
    validate_document(record, "run_record")

    missing_vector = {**record, "result": dict(record["result"])}
    del missing_vector["result"]["objective_vector"]
    with pytest.raises(SchemaValidationError):
        validate_document(missing_vector, "run_record")


def test_canonical_result_schema_accepts_minmaxsum_vector():
    validate_document({
        "schema_version": 1,
        "run_id": "run-001",
        "experiment_id": "exp-001",
        "run_key": "key-001",
        "execution_status": "COMPLETED",
        "solver_result": {
            "objective": "minmaxsum",
            "objective_value": None,
            "objective_vector": {
                "peak_cost": 2.0,
                "integral_cost": 1.25,
            },
            "joint": _joint_metadata(),
        },
        "error_message": None,
    }, "result")


@pytest.mark.parametrize(
    ("update", "message"),
    [
        ({"run_id": "../escape"}, "run_id"),
        ({"objective": "average"}, "objective"),
        ({"unexpected": True}, "Additional properties"),
        ({"input_sha256": "not-a-checksum"}, "input_sha256"),
    ],
)
def test_invalid_run_records_are_rejected(update, message):
    record = {**valid_run_record(), **update}
    with pytest.raises(SchemaValidationError, match=message):
        validate_document(record, "run_record")


def test_certified_verification_requires_continuous_check():
    verification = valid_run_record()["verification"]
    verification["continuous"] = False
    with pytest.raises(SchemaValidationError):
        validate_document(verification, "verification")


def test_json_helpers_reject_nonfinite_values_and_round_trip():
    with pytest.raises(ValueError):
        dumps_json({"objective": math.nan})
    with pytest.raises(ValueError, match="non-finite"):
        loads_json('{"objective": Infinity}')

    payload = {"z": 2, "a": 1}
    assert dumps_json(payload) == '{"a":1,"z":2}'
    assert loads_json(dumps_json(payload)) == payload
