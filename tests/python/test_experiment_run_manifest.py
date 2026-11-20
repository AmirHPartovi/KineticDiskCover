import hashlib
from pathlib import Path
import subprocess
import sys

import pytest


ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))

from kdc_tools.experiment_run_manifest import (  # noqa: E402
    capture_local_git_state,
    create_run_manifest_snapshot,
    read_run_manifest,
    validate_run_manifest,
    write_run_manifest_snapshot,
)
from kdc_tools.schemas import loads_json  # noqa: E402
from kdc_tools.storage import (  # noqa: E402
    finalize_run,
    read_json,
    reserve_run,
    sha256_file,
    validate_run_directory,
    write_derived_json,
    write_bytes_create_only,
    write_json_create_only,
)


def _finalized_run(tmp_path, *, manifest_context=None):
    experiment = tmp_path / "experiment-001"
    experiment.mkdir()
    run_id = "run-001"
    input_bytes = b'{"name":"tiny","trajectories":[],"stations":[]}\n'
    input_sha = hashlib.sha256(input_bytes).hexdigest()
    run = reserve_run(
        experiment,
        run_id=run_id,
        run_key="key-001",
        experiment_id=experiment.name,
        instance_id="tiny",
        algorithm="greedy",
        objective="minmax",
        repeat=0,
        seed=42,
        requested_backend="auto",
        configuration={
            "algorithm_category": "heuristic",
            "fast_time_limit_sec": 2.0,
            "max_oracle_calls": 5,
            "epsilon_abs": 0.01,
            "numeric_mode": "FAST_FLOAT",
        },
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
    )
    write_bytes_create_only(run, "input/instance.json", input_bytes)
    write_json_create_only(run, "solution/solution.json", {"assignment": []})
    write_bytes_create_only(run, "trace/events.jsonl", b'{"event":"done"}\n')
    result = {
        "schema_version": 1,
        "run_id": run_id,
        "experiment_id": experiment.name,
        "run_key": "key-001",
        "execution_status": "COMPLETED",
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
            "numeric_mode": "FAST_FLOAT",
            "solution_json_path": "solution/solution.json",
            "trace_jsonl_path": "trace/events.jsonl",
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
        state="COMPLETED",
        result_document=result,
        verification_document=verification,
        backend={
            "selected": "greedy",
            "actual": "greedy",
            "version": "1.2",
        },
        provenance={
            "solver_version": "test-version",
            "platform": "test-platform",
            "compiler": "test-c++",
            "compiler_version": "test-c++ 1.0",
            "build_type": "Debug",
            "library_versions": {"solver": "1.2"},
        },
        source_commit="fixture-commit",
        manifest_context=manifest_context,
    )
    return run


def test_v2_manifest_round_trips_with_independent_lifecycle_and_artifact_roles(
    tmp_path,
):
    context = {
        "generator_version": "generator-v3",
        "registry_version": "registry-v2",
        "prng": "PCG64-v1",
        "model_regime": "Model A",
        "input_lifecycle": {
            "designed": True,
            "generated": False,
            "validated": True,
            "used_in_run": False,
        },
        "backends": {
            "exact": {
                "status": "NOT_USED",
                "name": "NOT_USED",
                "version": "NOT_APPLICABLE",
            },
            "heuristic": {
                "status": "USED",
                "name": "greedy",
                "version": "1.2",
            },
            "relaxation": {
                "status": "NOT_USED",
                "name": "NOT_USED",
                "version": "NOT_APPLICABLE",
            },
        },
        "budgets": {"wall_seconds": 10, "oracle_calls": 5},
        "tolerances": {"epsilon_abs": 0.01},
    }
    run = _finalized_run(tmp_path, manifest_context=context)
    snapshot_path, = sorted((run / "manifest_snapshots").glob("*.json"))
    snapshot = read_run_manifest(snapshot_path, run_root=run)
    assert loads_json(snapshot_path.read_bytes()) == snapshot
    assert snapshot["schema_version"] == 2
    assert snapshot["experiment_id"] == run.parent.parent.name
    assert snapshot["run_id"] == run.name
    assert snapshot["input"]["generator_version"] == "generator-v3"
    assert snapshot["input"]["registry_version"] == "registry-v2"
    assert snapshot["input"]["lifecycle"] == context["input_lifecycle"]
    assert snapshot["backends"]["heuristic"]["name"] == "greedy"
    assert snapshot["budgets"] == context["budgets"]
    assert snapshot["tolerances"] == context["tolerances"]
    assert snapshot["build"]["compiler_version"] == "test-c++ 1.0"
    assert isinstance(snapshot["git"]["dirty"], bool)
    assert snapshot["git"]["commit"]
    assert len(snapshot["git"]["status_sha256"]) == 64
    assert snapshot["artifacts"]["solution"][0]["path"] == "solution/solution.json"
    assert snapshot["artifacts"]["trace"][0]["path"] == "trace/events.jsonl"
    assert validate_run_directory(run)["run_id"] == "run-001"


def test_manifest_checksum_mismatch_is_rejected(tmp_path):
    run = _finalized_run(tmp_path)
    manifest_path, = sorted((run / "manifest_snapshots").glob("*.json"))
    manifest = loads_json(manifest_path.read_bytes())
    manifest["numeric_mode"] = "VALIDATED"
    with pytest.raises(ValueError, match="SHA-256 mismatch"):
        validate_run_manifest(manifest, run_root=run)

    (run / "solution" / "solution.json").write_text("changed\n", encoding="utf-8")
    with pytest.raises(ValueError, match="checksum mismatch"):
        read_run_manifest(manifest_path, run_root=run)


def test_backend_roles_are_explicit_when_algorithm_category_is_known(tmp_path):
    run = _finalized_run(tmp_path)
    manifest_path, = sorted((run / "manifest_snapshots").glob("*.json"))
    snapshot = read_run_manifest(manifest_path, run_root=run)
    assert snapshot["backends"]["heuristic"]["status"] == "USED"
    assert snapshot["backends"]["exact"]["status"] == "NOT_USED"
    assert snapshot["backends"]["relaxation"]["status"] == "NOT_USED"
    assert snapshot["input"]["generator_version"] == "UNRESOLVED"
    assert snapshot["input"]["registry_version"] == "UNRESOLVED"
    assert snapshot["input"]["prng"] == "UNRESOLVED"


def test_missing_referenced_artifact_is_rejected(tmp_path):
    run = _finalized_run(tmp_path)
    manifest_path, = sorted((run / "manifest_snapshots").glob("*.json"))
    (run / "solution" / "solution.json").unlink()
    with pytest.raises(FileNotFoundError, match="missing"):
        read_run_manifest(manifest_path, run_root=run)


def test_legacy_v1_run_without_v2_sidecar_remains_readable(tmp_path):
    run = _finalized_run(tmp_path)
    snapshots = run / "manifest_snapshots"
    for path in snapshots.glob("*.json"):
        path.unlink()
    snapshots.rmdir()
    artifacts = read_json(run / "artifacts.json")
    artifacts["artifacts"] = [
        item for item in artifacts["artifacts"]
        if not item["path"].startswith("manifest_snapshots/")
    ]
    write_derived_json(run / "artifacts.json", artifacts)
    assert validate_run_directory(run)["schema_version"] == 1


def test_invalid_schema_is_rejected_before_hash_validation(tmp_path):
    run = _finalized_run(tmp_path)
    manifest_path, = sorted((run / "manifest_snapshots").glob("*.json"))
    manifest = loads_json(manifest_path.read_bytes())
    del manifest["budgets"]
    with pytest.raises(ValueError, match="schema failed"):
        validate_run_manifest(manifest, run_root=run)


def test_new_snapshot_uses_a_new_id_and_existing_id_is_create_only(tmp_path):
    run = _finalized_run(tmp_path)
    request = read_json(run / "request.json")
    input_reference = read_json(run / "input_ref.json")
    result = read_json(run / "result" / "result.json")
    git_state = read_json(run / "execution" / "git_snapshot.json")
    artifacts = [
        {
            "path": path.relative_to(run).as_posix(),
            "sha256": sha256_file(path),
            "size_bytes": path.stat().st_size,
        }
        for path in run.rglob("*")
        if path.is_file()
    ]
    arguments = {
        "run_root": run,
        "experiment_id": request["experiment_id"],
        "run_id": request["run_id"],
        "run_status": "COMPLETED",
        "request": request,
        "input_reference": input_reference,
        "result_document": result,
        "backend": {"selected": "greedy", "actual": "greedy", "version": "1.2"},
        "provenance": {"solver_version": "test-version"},
        "git_state": git_state,
        "known_artifacts": artifacts,
    }
    first = create_run_manifest_snapshot(**arguments)
    second = create_run_manifest_snapshot(**arguments)
    assert first["manifest_id"] != second["manifest_id"]
    first_path = write_run_manifest_snapshot(run, first)
    assert write_run_manifest_snapshot(run, second) != first_path
    with pytest.raises(FileExistsError, match="manifest ID already exists"):
        write_run_manifest_snapshot(run, first)


def test_local_git_capture_records_dirty_state_without_remote_access(tmp_path):
    repository = tmp_path / "local-repo"
    repository.mkdir()
    subprocess.run(["git", "init", "-q"], cwd=repository, check=True)
    (repository / "tracked.txt").write_text("baseline\n", encoding="utf-8")
    subprocess.run(["git", "add", "tracked.txt"], cwd=repository, check=True)
    subprocess.run(
        [
            "git", "-c", "user.name=Test", "-c",
            "user.email=test@example.invalid", "commit", "-qm", "baseline",
        ],
        cwd=repository,
        check=True,
    )
    clean = capture_local_git_state(repository)
    (repository / "tracked.txt").write_text("changed\n", encoding="utf-8")
    dirty = capture_local_git_state(repository)
    assert clean["commit"] == dirty["commit"]
    assert clean["dirty"] is False
    assert dirty["dirty"] is True
    assert len(dirty["status_sha256"]) == 64
