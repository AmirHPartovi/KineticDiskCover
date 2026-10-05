from concurrent.futures import ThreadPoolExecutor
import hashlib
from pathlib import Path
import sys

import pytest


ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))

from kdc_tools.storage import (  # noqa: E402
    aggregate_experiment,
    create_experiment,
    create_run_directory,
    finalize_run,
    read_json,
    reserve_run,
    sha256_file,
    validate_state_transition,
    validate_run_directory,
    validate_complete_aggregate,
    validate_results_input,
    write_bytes_create_only,
    write_json_create_only,
    write_run_plan,
)
from kdc_tools.schemas import SchemaValidationError  # noqa: E402


def test_run_directory_is_created_once(tmp_path):
    run = create_run_directory(tmp_path / "experiment", "run-001")
    assert run.is_dir()
    with pytest.raises(FileExistsError):
        create_run_directory(tmp_path / "experiment", "run-001")


@pytest.mark.parametrize("run_id", ["../escape", "", ".hidden", "with/slash"])
def test_run_directory_rejects_unsafe_ids(tmp_path, run_id):
    with pytest.raises(ValueError, match="invalid run ID"):
        create_run_directory(tmp_path, run_id)


def test_artifact_write_is_atomic_create_only_and_checksummed(tmp_path):
    run = create_run_directory(tmp_path, "run-001")
    artifact = write_json_create_only(
        run,
        "records/result.json",
        {"value": 1},
    )
    assert read_json(artifact) == {"value": 1}
    assert sha256_file(artifact) == hashlib.sha256(
        artifact.read_bytes()
    ).hexdigest()

    with pytest.raises(FileExistsError):
        write_json_create_only(run, "records/result.json", {"value": 2})
    assert read_json(artifact) == {"value": 1}


def test_artifact_write_rejects_path_escape_and_invalid_schema(tmp_path):
    run = create_run_directory(tmp_path, "run-001")
    with pytest.raises(ValueError, match="safe relative path"):
        write_json_create_only(run, "../outside.json", {"value": 1})
    with pytest.raises(SchemaValidationError):
        write_json_create_only(
            run,
            "run.json",
            {"schema_version": 1},
            schema_name="run_record",
        )
    assert list(run.iterdir()) == []


def test_concurrent_run_creation_has_one_winner(tmp_path):
    experiment = tmp_path / "experiment"

    def create():
        try:
            return create_run_directory(experiment, "shared-run")
        except FileExistsError:
            return None

    with ThreadPoolExecutor(max_workers=8) as executor:
        outcomes = list(executor.map(lambda _: create(), range(8)))
    assert sum(outcome is not None for outcome in outcomes) == 1


def test_concurrent_create_only_artifact_write_has_one_winner(tmp_path):
    run = create_run_directory(tmp_path, "run-001")

    def write(value):
        try:
            return write_json_create_only(run, "result.json", {"value": value})
        except FileExistsError:
            return None

    with ThreadPoolExecutor(max_workers=8) as executor:
        outcomes = list(executor.map(write, range(8)))
    assert sum(outcome is not None for outcome in outcomes) == 1
    assert read_json(run / "result.json")["value"] in range(8)


@pytest.mark.parametrize(
    ("current", "requested"),
    [
        ("PLANNED", "RUNNING"),
        ("RUNNING", "COMPLETED"),
        ("RUNNING", "FAILED"),
        ("RUNNING", "TIMED_OUT"),
    ],
)
def test_allowed_state_transitions(current, requested):
    validate_state_transition(current, requested)


@pytest.mark.parametrize(
    ("current", "requested"),
    [
        ("PLANNED", "COMPLETED"),
        ("COMPLETED", "RUNNING"),
        ("FAILED", "COMPLETED"),
    ],
)
def test_terminal_or_skipped_state_transitions_are_rejected(current, requested):
    with pytest.raises(ValueError, match="invalid run-state transition"):
        validate_state_transition(current, requested)


def _reserve_and_finalize(experiment, run_id, run_key, *, state="COMPLETED",
                         rerun_of=None):
    payload = b'{"name":"tiny","trajectories":[],"stations":[]}\n'
    input_sha = hashlib.sha256(payload).hexdigest()
    config = {
        "per_static_time_limit_sec": 1.0,
        "fast_time_limit_sec": 2.0,
        "exact_time_limit_sec": 3.0,
        "runtime_sec": 0.25,
    }
    run = reserve_run(
        experiment,
        run_id=run_id,
        run_key=run_key,
        experiment_id=Path(experiment).name,
        instance_id="tiny",
        algorithm="greedy",
        objective="minmax",
        repeat=0,
        seed=42,
        requested_backend="auto",
        configuration=config,
        input_reference={
            "schema_version": 1,
            "instance_id": "tiny",
            "canonical_path": "input/instance.json",
            "canonical_sha256": input_sha,
            "source_path": "fixture.json",
            "source_sha256": input_sha,
            "family": "fixture",
            "n": 0,
            "m": 0,
        },
        rerun_of=rerun_of,
    )
    write_bytes_create_only(run, "input/instance.json", payload)
    result = {
        "schema_version": 1,
        "run_id": run_id,
        "experiment_id": Path(experiment).name,
        "run_key": run_key,
        "execution_status": state,
        "solver_result": {
            "instance_name": "tiny",
            "algorithm_name": "greedy",
            "objective": "minmax",
            "feasible": True,
            "verified": True,
            "objective_value": 1.0,
            "lower_bound": 0.5,
            "upper_bound": 1.0,
            "optimality_status": "FEASIBLE",
            "wall_time_sec": 0.25,
        },
        "error_message": None,
    }
    verification = {
        "verification_mode": "continuous",
        "status": "PASSED",
        "kind": "CERTIFIED_CONTINUOUS",
        "passed": True,
        "continuous": True,
        "message": None,
    }
    finalize_run(
        run,
        state=state,
        result_document=result,
        verification_document=verification,
        backend={"selected": "greedy", "actual": "greedy"},
        provenance={"solver_version": "test", "platform": "test"},
        source_commit="deadbeef",
    )
    return run


def test_run_finalization_validates_checksums_and_rejects_terminal_writes(tmp_path):
    experiment = tmp_path / "exp-001"
    experiment.mkdir()
    run = _reserve_and_finalize(experiment, "run-001", "key-001")
    record = validate_run_directory(run, experiment_id="exp-001")
    assert record["status"] == "COMPLETED"
    assert any(row["path"] == "run.json"
               for row in read_json(run / "artifacts.json")["artifacts"])
    with pytest.raises(ValueError, match="terminal run"):
        write_json_create_only(run, "result/late.json", {"late": True})

    solution = run / "input" / "instance.json"
    solution.write_text('{"changed":true}')
    with pytest.raises(ValueError, match="checksum mismatch"):
        validate_run_directory(run)


def test_aggregate_is_derived_from_run_packages_and_reports_plan_gaps(tmp_path):
    experiment = tmp_path / "exp-aggregate"
    experiment.mkdir()
    _reserve_and_finalize(experiment, "run-001", "key-001")
    plan = [{
        "schema_version": 1,
        "run_key": "key-001",
        "instance_id": "tiny",
        "algorithm": "greedy",
        "objective": "minmax",
        "repeat": 0,
        "seed": 42,
        "configuration_sha256": "a" * 64,
        "requested_backend": "auto",
        "resolved_configuration": {},
    }, {
        "schema_version": 1,
        "run_key": "missing-key",
        "instance_id": "tiny",
        "algorithm": "greedy",
        "objective": "minsum",
        "repeat": 0,
        "seed": 42,
        "configuration_sha256": "b" * 64,
        "requested_backend": "auto",
        "resolved_configuration": {},
    }]
    write_run_plan(experiment, plan)
    report = aggregate_experiment(experiment)
    assert report["classifications"]["valid_runs"] == 1
    assert report["classifications"]["missing_runs"] == 1
    assert report["missing_run_keys"] == ["missing-key"]
    compatibility = read_json(experiment / "batch" / "master_results.json")
    assert compatibility[0]["run_id"] == "run-001"
    assert compatibility[0]["objective_value"] == 1.0


def test_reporting_requires_complete_checksummed_aggregate(tmp_path):
    experiment = tmp_path / "exp-report"
    experiment.mkdir()
    create_experiment(
        experiment,
        profile="fast",
        config={},
        dataset_sha256="a" * 64,
        source_commit=None,
    )
    _reserve_and_finalize(experiment, "run-001", "key-001")
    write_run_plan(experiment, [{
        "schema_version": 1,
        "run_key": "key-001",
        "instance_id": "tiny",
        "algorithm": "greedy",
        "objective": "minmax",
        "repeat": 0,
        "seed": 42,
        "configuration_sha256": "a" * 64,
        "requested_backend": "auto",
        "resolved_configuration": {},
    }])
    manifest = aggregate_experiment(experiment)
    assert manifest["complete"] is True
    validate_results_input(experiment / "batch" / "master_results.json")
    validate_complete_aggregate(experiment)

    export = experiment / "batch" / "master_results.json"
    export.write_text("[]\n")
    with pytest.raises(ValueError, match="aggregate export checksum mismatch"):
        validate_results_input(export)
