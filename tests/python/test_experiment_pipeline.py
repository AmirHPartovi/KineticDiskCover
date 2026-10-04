import importlib.util
import json
from pathlib import Path
import sys
from types import SimpleNamespace


ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location(
    "experiment_pipeline", ROOT / "scripts" / "experiment_pipeline.py"
)
assert SPEC is not None and SPEC.loader is not None
pipeline = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = pipeline
SPEC.loader.exec_module(pipeline)


def test_exact_animation_gate_requires_all_proof_metadata():
    proven = {
        "algorithm_category": "exact_reference",
        "feasible": True,
        "verified": True,
        "verification_kind": "CERTIFIED_CONTINUOUS",
        "optimality_status": "OPTIMAL",
    }
    assert pipeline.exact_proven(proven)

    for update in (
        {"algorithm_category": "heuristic"},
        {"feasible": False},
        {"verified": False},
        {"verification_kind": "EMPIRICAL"},
        {"optimality_status": "FEASIBLE"},
        {"optimality_status": "TIME_LIMIT"},
    ):
        assert not pipeline.exact_proven({**proven, **update})


def test_integrity_report_keeps_timeouts_and_counts_exact_once(tmp_path):
    experiment = tmp_path / "experiment"
    (experiment / "batch").mkdir(parents=True)
    (experiment / "reports").mkdir()
    (experiment / "experiment_manifest.json").write_text(json.dumps({
        "instance_count": 1,
        "objectives": ["minmax", "minsum"],
    }))
    (experiment / "batch" / "experiment_manifest.json").write_text(json.dumps({
        "dataset": {"instance_count": 0},
        "algorithm_set": ["greedy", "branch-and-bound"],
        "seed_policy": {"repeats": 3},
    }))
    rows = []
    for objective in ("minmax", "minsum"):
        rows.extend([
            {
                "instance_name": "tiny",
                "algorithm_name": "greedy",
                "objective": objective,
                "repeat": repeat,
                "optimality_status": "TIME_LIMIT" if repeat == 2 else "FEASIBLE",
                "timeout": repeat == 2,
                "failed": False,
                "feasible": True,
                "verified": True,
            }
            for repeat in range(1, 4)
        ])
        rows.append({
            "instance_name": "tiny",
            "algorithm_name": "branch-and-bound",
            "algorithm_category": "exact_reference",
            "objective": objective,
            "repeat": 1,
            "optimality_status": "FEASIBLE",
            "timeout": False,
            "failed": False,
            "feasible": True,
            "verified": True,
        })
    results = experiment / "batch" / "master_results.json"
    results.write_text(json.dumps(rows))

    pipeline.command_validate(SimpleNamespace(
        experiment=str(experiment),
        results=str(results),
    ))

    report = (experiment / "reports" / "result_integrity_report.md").read_text()
    assert "Runs: 8 (expected 8)" in report
    assert "Timeouts: 2" in report
    assert "TIME_LIMIT" in report
