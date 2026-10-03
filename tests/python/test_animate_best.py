import importlib.util
import hashlib
import json
from pathlib import Path
import sys

import numpy as np
import pandas as pd


ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location(
    "animate_best", ROOT / "scripts" / "animate_best.py"
)
assert SPEC is not None and SPEC.loader is not None
animate_best = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = animate_best
SPEC.loader.exec_module(animate_best)


def sample_instance():
    return {
        "id": 1,
        "name": "sample",
        "T_end": 1.0,
        "stations": [{"id": 0, "x": 0.0, "y": 0.0}],
        "trajectories": [{
            "t_breaks": [0.0, 1.0],
            "waypoints": [{"x": 0.0, "y": 0.0}, {"x": 2.0, "y": 0.0}],
        }],
    }


def sample_solution(instance):
    return {
        "instance": {"name": "sample", "n": 1, "m": 1, "T_end": 1.0},
        "objective": "minmax",
        "summary": {
            "peak_cost": 4.0 * np.pi,
            "peak_time": 1.0,
            "total_integral": 4.0 * np.pi / 3.0,
            "num_intervals": 1,
        },
        "intervals": [{
            "t_start": 0.0,
            "t_end": 1.0,
            "supporting_point": [0],
            "assigned_points": [0],
            "a": 2.0 * np.pi,
            "b": 0.0,
            "c": 0.0,
        }],
        "_instance": instance,
    }


def test_selects_best_algorithm():
    frame = pd.DataFrame([
        {"instance_name": "x", "objective": "minmax", "algorithm_name": "nn",
         "objective_value": 20, "gap": 0.1, "wall_time_sec": 1,
         "verified": True, "feasible": True},
        {"instance_name": "x", "objective": "minmax", "algorithm_name": "ip",
         "objective_value": 10, "gap": 0.2, "wall_time_sec": 2,
         "verified": True, "feasible": True},
        {"instance_name": "x", "objective": "minmax", "algorithm_name": "bad",
         "objective_value": 1, "gap": 0, "wall_time_sec": 0.1,
         "verified": False, "feasible": True},
    ])
    assert animate_best.select_best(frame, "x", "minmax").algorithm_name == "ip"


def test_tie_break_gap():
    frame = pd.DataFrame([
        {"instance_name": "x", "objective": "minmax", "algorithm_name": "a",
         "objective_value": 10, "gap": 0.2, "wall_time_sec": 1},
        {"instance_name": "x", "objective": "minmax", "algorithm_name": "b",
         "objective_value": 10, "gap": 0.1, "wall_time_sec": 2},
    ])
    assert animate_best.select_best(frame, "x", "minmax").algorithm_name == "b"


def test_tie_break_time():
    frame = pd.DataFrame([
        {"instance_name": "x", "objective": "minsum", "algorithm_name": "a",
         "objective_value": 10, "gap": 0.1, "wall_time_sec": 2},
        {"instance_name": "x", "objective": "minsum", "algorithm_name": "b",
         "objective_value": 10, "gap": 0.1, "wall_time_sec": 1},
    ])
    assert animate_best.select_best(frame, "x", "minsum").algorithm_name == "b"


def test_radius_at_time():
    instance = sample_instance()
    solution = sample_solution(instance)
    assert animate_best.radius_at(solution, 0, 0.5) == 1.0


def test_interval_boundary_uses_post_handover_ownership():
    instance = sample_instance()
    solution = sample_solution(instance)
    first = solution["intervals"][0]
    second = dict(first, t_start=0.5, t_end=1.0, assigned_points=[0])
    first["t_end"] = 0.5
    solution["intervals"].append(second)

    assert animate_best._interval_at(solution, 0.5) is second


def test_cost_at_time():
    instance = sample_instance()
    solution = sample_solution(instance)
    assert np.isclose(animate_best.cost_at_time(solution, 0.5), np.pi)


def test_integral_accumulation():
    times = np.linspace(0, 1, 1001)
    costs = 2 * np.pi * times**2
    cumulative = animate_best.integrate_cost(times, costs)
    assert np.isclose(cumulative[-1], 2 * np.pi / 3, atol=1e-4)


def make_animation_inputs(root):
    instance = sample_instance()
    instances = root / "instances"
    batch = root / "batch"
    instance_dir = instances / "family"
    instance_dir.mkdir(parents=True)
    (instance_dir / "sample.json").write_text(json.dumps(instance))
    run_dir = batch / "runs" / "sample" / "nn" / "minmax"
    run_dir.mkdir(parents=True)
    solution = sample_solution(instance)
    solution.pop("_instance")
    (run_dir / "solution.json").write_text(json.dumps(solution))
    record = {
        "instance_name": "sample",
        "algorithm_name": "nn",
        "objective": "minmax",
        "objective_value": 4 * np.pi,
        "lower_bound": 0,
        "gap": 0.1,
        "wall_time_sec": 0.02,
        "verified": True,
        "feasible": True,
        "solution_json_path": str(run_dir / "solution.json"),
    }
    (batch / "master_results.json").write_text(json.dumps([record]))
    return batch, instances


def test_animation_generation_smoke(tmp_path):
    batch, instances = make_animation_inputs(tmp_path)
    output = tmp_path / "animations"
    created = animate_best.run(batch, instances, output, "minmax",
                               fps=2, frames=5, dpi=70)
    assert len(created) == 1
    assert created[0].is_file()
    assert created[0].stat().st_size > 10_000


def test_generates_one_animation_per_verified_algorithm(tmp_path):
    batch, instances = make_animation_inputs(tmp_path)
    records = json.loads((batch / "master_results.json").read_text())
    original = records[0]
    greedy_solution = (
        batch / "runs" / "sample" / "greedy" / "minmax" / "solution.json"
    )
    greedy_solution.parent.mkdir(parents=True)
    greedy_solution.write_text(
        (batch / "runs" / "sample" / "nn" / "minmax" / "solution.json")
        .read_text()
    )
    greedy_record = dict(original)
    greedy_record["algorithm_name"] = "greedy"
    greedy_record["solution_json_path"] = str(greedy_solution)
    records.append(greedy_record)
    (batch / "master_results.json").write_text(json.dumps(records))

    created = animate_best.run(
        batch, instances, tmp_path / "animations", "minmax",
        fps=2, frames=5, dpi=70, all_algorithms=True,
    )

    assert len(created) == 2
    assert {path.parent.name for path in created} == {"nn", "greedy"}
    assert all(path.is_file() for path in created)


def test_algorithm_filter_limits_generated_animations(tmp_path):
    batch, instances = make_animation_inputs(tmp_path)
    records = json.loads((batch / "master_results.json").read_text())
    greedy_solution = (
        batch / "runs" / "sample" / "greedy" / "minmax" / "solution.json"
    )
    greedy_solution.parent.mkdir(parents=True)
    greedy_solution.write_text(
        (batch / "runs" / "sample" / "nn" / "minmax" / "solution.json")
        .read_text()
    )
    greedy_record = dict(records[0])
    greedy_record["algorithm_name"] = "greedy"
    greedy_record["solution_json_path"] = str(greedy_solution)
    records.append(greedy_record)
    (batch / "master_results.json").write_text(json.dumps(records))

    created = animate_best.run(
        batch, instances, tmp_path / "animations", "minmax",
        fps=2, frames=5, dpi=70, all_algorithms=True, algorithm="greedy",
    )

    assert len(created) == 1
    assert created[0].parent.name == "greedy"
    assert created[0].is_file()


def test_skips_instance_without_verified_run(tmp_path, caplog):
    batch, instances = make_animation_inputs(tmp_path)
    records = json.loads((batch / "master_results.json").read_text())
    records[0]["verified"] = False
    (batch / "master_results.json").write_text(json.dumps(records))
    created = animate_best.run(batch, instances, tmp_path / "animations",
                               "minmax", frames=5, dpi=70)
    assert created == []
    assert "No feasible verified minmax result" in caplog.text


def test_reproducibility(tmp_path):
    batch, instances = make_animation_inputs(tmp_path)
    first = animate_best.run(batch, instances, tmp_path / "first",
                             "minmax", fps=2, frames=5, dpi=70)[0]
    second = animate_best.run(batch, instances, tmp_path / "second",
                              "minmax", fps=2, frames=5, dpi=70)[0]
    assert hashlib.sha256(first.read_bytes()).digest() == hashlib.sha256(
        second.read_bytes()
    ).digest()
