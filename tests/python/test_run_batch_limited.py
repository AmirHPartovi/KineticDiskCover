import importlib.util
import json
from pathlib import Path
import sys
from types import SimpleNamespace


ROOT = Path(__file__).resolve().parents[2]
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


def test_clear_previous_output_removes_batch_artifacts_only(tmp_path):
    output = tmp_path / "output"
    stale_run = output / "runs" / "old" / "nn" / "minmax"
    stale_run.mkdir(parents=True)
    (stale_run / "result.json").write_text("{}")
    for filename in ("master_results.json", "master_results.csv",
                     "batch_summary.md"):
        (output / filename).write_text("stale")
    unrelated = output / "notes.txt"
    unrelated.write_text("keep")

    run_batch_limited.clear_previous_output(output)

    assert not (output / "runs").exists()
    assert not (output / "master_results.json").exists()
    assert not (output / "master_results.csv").exists()
    assert not (output / "batch_summary.md").exists()
    assert unrelated.read_text() == "keep"


def test_repeat_records_use_distinct_paths_and_csv_schema(tmp_path):
    first = {"instance_name": "one", "algorithm_name": "nn",
             "objective": "minmax", "repeat": 0}
    second = {**first, "repeat": 1}

    run_batch_limited._write_record_paths(first, tmp_path)
    run_batch_limited._write_record_paths(second, tmp_path)

    assert first["result_json_path"] != second["result_json_path"]
    assert Path(first["result_json_path"]).is_file()
    assert Path(second["result_json_path"]).is_file()
    assert len(run_batch_limited.CSV_COLUMNS) == len(
        set(run_batch_limited.CSV_COLUMNS)
    )


def test_main_calibrates_once_and_forwards_one_selected_exact_backend(
    tmp_path, monkeypatch
):
    instances = tmp_path / "instances"
    instances.mkdir()
    (instances / "one.json").write_text("{}")
    output = tmp_path / "output"
    solver = tmp_path / "kdc-solver"
    solver.touch()
    monkeypatch.setattr(run_batch_limited, "SOLVER", solver)

    commands = []
    def fake_subprocess_run(command, **_kwargs):
        commands.append(command)
        (output / "experiment_manifest.json").write_text(
            json.dumps({"selected_backend": "branch-and-bound"})
        )
        return SimpleNamespace(returncode=0, stdout="", stderr="")

    job_runs = []
    def fake_run_one(path, run_output, algorithm, objective, *args):
        job_runs.append((path, run_output, algorithm, objective, args[4]))
        return {"feasible": True}

    monkeypatch.setattr(run_batch_limited.subprocess, "run", fake_subprocess_run)
    monkeypatch.setattr(run_batch_limited, "run_one", fake_run_one)
    monkeypatch.setattr(run_batch_limited, "write_outputs", lambda *_args: None)

    assert run_batch_limited.main([
        "--instances", str(instances),
        "--output", str(output),
        "--algorithms", "nn",
        "--modes", "minmax",
        "--threads", "1",
    ]) == 0

    assert len(commands) == 1
    assert commands[0][1] == "calibrate"
    assert len(job_runs) == 2
    assert {run[2] for run in job_runs} == {"nn", "branch-and-bound"}
    assert {run[4] for run in job_runs} == {"branch-and-bound"}
    assert (output / "experiment_manifest.json").is_file()
