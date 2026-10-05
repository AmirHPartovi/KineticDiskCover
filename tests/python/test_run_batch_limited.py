import importlib.util
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
from pathlib import Path
import sys
from types import SimpleNamespace

import pytest


ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))
from kdc_tools.storage import validate_run_directory  # noqa: E402

SPEC = importlib.util.spec_from_file_location(
    "run_batch_limited", ROOT / "scripts" / "run_batch_limited.py"
)
assert SPEC is not None and SPEC.loader is not None
run_batch_limited = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = run_batch_limited
SPEC.loader.exec_module(run_batch_limited)


def test_algorithms_are_ordered_by_execution_phase():
    selected = {"branch-and-bound", "nn", "ip-kont", "greedy", "sa"}

    assert run_batch_limited.order_algorithms(selected) == [
        "nn", "greedy", "sa", "ip-kont", "branch-and-bound"
    ]


def test_run_id_and_run_key_are_distinct_and_stable():
    config = {"limit": 2.0}
    first = run_batch_limited.make_run_key(
        input_sha256="a" * 64,
        algorithm="greedy",
        objective="minmax",
        seed=3,
        repeat=0,
        resolved_backend="not-applicable",
        configuration=config,
    )
    second = run_batch_limited.make_run_key(
        input_sha256="a" * 64,
        algorithm="greedy",
        objective="minmax",
        seed=3,
        repeat=0,
        resolved_backend="not-applicable",
        configuration=config,
    )
    assert first == second
    assert run_batch_limited.make_run_id("greedy", "minmax", "tiny", 0) != \
        run_batch_limited.make_run_id("greedy", "minmax", "tiny", 0)


def test_legacy_shared_batch_directory_is_rejected():
    with pytest.raises(ValueError, match="legacy shared path"):
        run_batch_limited._experiment_root(ROOT / "results" / "batch")


def test_main_creates_an_immutable_plan_and_reserves_no_empty_runs(
    tmp_path, monkeypatch
):
    instances = tmp_path / "instances"
    instances.mkdir()
    (instances / "one.json").write_text(json.dumps({
        "name": "one",
        "T_end": 1,
        "stations": [{"x": 0, "y": 0}],
        "trajectories": [{
            "t_breaks": [0, 1],
            "waypoints": [{"x": 0, "y": 0}, {"x": 1, "y": 1}],
        }],
    }))
    output = tmp_path / "experiment" / "batch"
    output.parent.mkdir()
    (output.parent / "experiment_manifest.json").write_text("{}")
    solver = tmp_path / "kdc-solver"
    solver.touch()
    monkeypatch.setattr(run_batch_limited, "SOLVER", solver)

    commands = []
    def fake_calibration(command, **_kwargs):
        commands.append(command)
        calibration_dir = Path(command[command.index("--output") + 1])
        calibration_dir.mkdir(parents=True, exist_ok=True)
        (calibration_dir / "experiment_manifest.json").write_text(
            json.dumps({"selected_backend": "branch-and-bound"})
        )
        return SimpleNamespace(returncode=0, stdout="", stderr="")

    calls = []
    def fake_run_one(**kwargs):
        calls.append(kwargs)
        return {"run_id": "unused"}

    monkeypatch.setattr(run_batch_limited.subprocess, "run", fake_calibration)
    monkeypatch.setattr(run_batch_limited, "run_one", fake_run_one)

    assert run_batch_limited.main([
        "--instances", str(instances),
        "--output", str(output),
        "--algorithms", "nn",
        "--modes", "minmax",
        "--threads", "1",
    ]) == 0
    plan_path = output.parent / "runs" / "plan.jsonl"
    assert plan_path.is_file()
    plan = [json.loads(line) for line in plan_path.read_text().splitlines()]
    assert {item["algorithm"] for item in plan} == {"nn", "branch-and-bound"}
    assert all(item["run_key"] for item in plan)
    assert len(calls) == len(plan)
    assert (output.parent / "input").is_dir()
    assert not list((output.parent / "runs").glob("*/request.json"))


def test_resume_requires_the_original_plan(tmp_path, monkeypatch):
    instances = tmp_path / "instances"
    instances.mkdir()
    (instances / "tiny.json").write_text("{}")
    solver = tmp_path / "solver"
    solver.touch()
    monkeypatch.setattr(run_batch_limited, "SOLVER", solver)
    with pytest.raises(SystemExit):
        run_batch_limited.main([
            "--instances", str(instances),
            "--output", str(tmp_path / "experiment" / "batch"),
            "--resume",
        ])


def test_parallel_solver_workers_write_isolated_valid_run_packages(
    tmp_path, monkeypatch
):
    experiment = tmp_path / "experiment"
    experiment.mkdir()
    input_bytes = json.dumps({
        "name": "tiny",
        "T_end": 1,
        "stations": [{"x": 0, "y": 0}],
        "trajectories": [{
            "t_breaks": [0, 1],
            "waypoints": [{"x": 0, "y": 0}, {"x": 1, "y": 1}],
        }],
    }).encode()
    input_path = experiment / "input" / "tiny.json"
    input_path.parent.mkdir()
    input_path.write_bytes(input_bytes)
    digest = hashlib.sha256(input_bytes).hexdigest()
    solver = tmp_path / "solver"
    solver.touch()
    monkeypatch.setattr(run_batch_limited, "SOLVER", solver)

    class FakeProcess:
        def __init__(self, command, **_kwargs):
            self.pid = 1000 + len(created)
            created.append(command)
            engine = Path(command[command.index("--output") + 1])
            algorithm = command[command.index("--algorithms") + 1]
            objective = command[command.index("--modes") + 1]
            result_dir = engine / "runs" / "tiny" / algorithm / objective
            result_dir.mkdir(parents=True)
            solution = result_dir / "solution.json"
            solution.write_text('{"intervals":[]}')
            trace = result_dir / "trace.csv"
            trace.write_text(
                "iter,t_max,objective,lower_bound,gap,wall_time,num_ip_solves\n"
                "0,1,1,0.5,0.5,0.1,1\n"
            )
            row = {
                "schema_version": 3,
                "instance_name": "tiny",
                "algorithm_name": algorithm,
                "objective": objective,
                "algorithm_category": "heuristic",
                "requested_backend": "auto",
                "selected_backend": algorithm,
                "actual_backend": algorithm,
                "solver_name": algorithm,
                "solver_version": "test",
                "native_kont": False,
                "feasible": True,
                "verified": True,
                "verification_kind": "CERTIFIED_CONTINUOUS",
                "optimality_status": "FEASIBLE",
                "objective_value": 1.0,
                "lower_bound": 0.5,
                "upper_bound": 1.0,
                "wall_time_sec": 0.1,
                "failed": False,
                "time_limited": False,
                "solution_json_path": str(solution),
                "trace_csv_path": str(trace),
                "git_commit": "deadbeef",
            }
            engine.mkdir(parents=True, exist_ok=True)
            (engine / "master_results.json").write_text(json.dumps([row]))

        def wait(self, timeout=None):
            assert timeout is None
            return 0

    created = []
    monkeypatch.setattr(run_batch_limited.subprocess, "Popen", FakeProcess)
    monkeypatch.setattr(run_batch_limited.platform, "platform", lambda: "test")
    config = {
        "requested_backend": "auto",
        "benchmark_profile": "FAST",
        "per_static_time_limit_sec": 1.0,
        "fast_time_limit_sec": 2.0,
        "exact_time_limit_sec": 3.0,
        "runtime_sec": 0.1,
    }

    def execute(index):
        algorithm = f"greedy-{index}"
        return run_batch_limited.run_one(
            experiment=experiment,
            input_path=input_path,
            instance_id="tiny",
            instance_name="tiny",
            family="test",
            n=1,
            m=1,
            input_sha256=digest,
            source_sha256=digest,
            source_path=str(input_path),
            algorithm=algorithm,
            objective="minmax",
            repeat=0,
            seed=index,
            exact_backend="branch-and-bound",
            config=config,
            profile="fast",
            minsum_refinement_policy="adaptive",
            safety_timeout=None,
            rerun_of=None,
            save_solutions=True,
            save_traces=True,
        )

    with ThreadPoolExecutor(max_workers=3) as executor:
        results = list(executor.map(execute, range(3)))
    run_ids = {result["run_id"] for result in results}
    assert len(run_ids) == 3
    for run_id in run_ids:
        run_dir = experiment / "runs" / run_id
        record = validate_run_directory(run_dir, experiment_id="experiment")
        assert record["status"] == "COMPLETED"
        assert (run_dir / "execution" / "stdout.log").is_file()
        assert (run_dir / "execution" / "stderr.log").is_file()
        assert (run_dir / "result" / "solution.json").is_file()
        assert (run_dir / "result" / "trace.jsonl").is_file()
