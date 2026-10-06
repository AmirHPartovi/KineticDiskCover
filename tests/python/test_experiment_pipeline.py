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


def test_smoke_pipeline_includes_joint_objective():
    config = pipeline.load_pipeline_config("smoke")
    assert config["objectives"] == "all"
    assert config["animation"]["mode"] == "all"
    assert pipeline.load_pipeline_config("reference")["objectives"] == "both"
    assert pipeline.load_pipeline_config("full")["objectives"] == "both"


def test_integrity_report_keeps_timeouts_and_counts_exact_once(tmp_path):
    experiment = tmp_path / "experiment"
    (experiment / "batch").mkdir(parents=True)
    (experiment / "reports").mkdir()
    (experiment / "experiment_manifest.json").write_text(json.dumps({
        "instance_count": 1,
        "objectives": ["minmax", "minsum", "minmaxsum"],
    }))
    (experiment / "batch" / "experiment_manifest.json").write_text(json.dumps({
        "dataset": {"instance_count": 0},
        "algorithm_set": ["greedy", "branch-and-bound"],
        "seed_policy": {"repeats": 3},
    }))
    rows = []
    for objective in ("minmax", "minsum", "minmaxsum"):
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
                **({
                    "objective_value": None,
                    "peak_cost": 2.0,
                    "integral_cost": 1.25,
                    "objective_vector": {
                        "peak_cost": 2.0,
                        "integral_cost": 1.25,
                    },
                    "joint": {
                        "minmax_component_status": (
                            "TIME_LIMIT" if repeat == 2 else "COMPLETED"
                        ),
                        "minsum_component_status": (
                            "TIME_LIMIT" if repeat == 2 else "COMPLETED"
                        ),
                        "minmax_component_optimality": (
                            "TIME_LIMIT" if repeat == 2 else "FEASIBLE"
                        ),
                        "minsum_component_optimality": (
                            "TIME_LIMIT" if repeat == 2 else "FEASIBLE"
                        ),
                        "joint_optimality_status": (
                            "TIME_LIMIT" if repeat == 2 else "FEASIBLE"
                        ),
                        "dominates_minmax": True,
                        "dominates_minsum": True,
                        "dominance_invariants_ok": True,
                        "minmax_component_peak": 3.0,
                        "minmax_component_integral": 2.0,
                        "minsum_component_peak": 4.0,
                        "minsum_component_integral": 1.5,
                        "minmax_source_run": "run-mm",
                        "minsum_source_run": "run-ms",
                    },
                    "verification_kind": "CERTIFIED_CONTINUOUS",
                } if objective == "minmaxsum" else {}),
            }
            for repeat in range(1, 4)
        ])
        exact_row = {
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
        }
        if objective == "minmaxsum":
            exact_row.update({
                "objective_value": None,
                "peak_cost": 2.0,
                "integral_cost": 1.25,
                "objective_vector": {
                    "peak_cost": 2.0,
                    "integral_cost": 1.25,
                },
                "joint": {
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
                },
                "verification_kind": "CERTIFIED_CONTINUOUS",
            })
        rows.append(exact_row)
    results = experiment / "batch" / "master_results.json"
    results.write_text(json.dumps(rows))

    pipeline.command_validate(SimpleNamespace(
        experiment=str(experiment),
        results=str(results),
    ))

    report = (experiment / "reports" / "result_integrity_report.md").read_text()
    assert "Runs: 12 (expected 12)" in report
    assert "Timeouts: 3" in report
    assert "TIME_LIMIT" in report


def test_dataset_stage_copies_only_materialized_smoke_instances(tmp_path):
    selector_path = ROOT / "scripts" / "select_test_instances.py"
    selector_spec = importlib.util.spec_from_file_location(
        "selector_for_pipeline_test", selector_path
    )
    assert selector_spec is not None and selector_spec.loader is not None
    selector = importlib.util.module_from_spec(selector_spec)
    sys.modules[selector_spec.name] = selector
    selector_spec.loader.exec_module(selector)

    source = tmp_path / "source"
    for family, n, m in (("a", 1, 1), ("b", 2, 2)):
        payload = {
            "T_end": 1.0,
            "stations": [{"x": index, "y": 0} for index in range(m)],
            "trajectories": [{
                "t_breaks": [0.0, 1.0],
                "waypoints": [{"x": 0, "y": 0}, {"x": 1, "y": 1}],
            } for _ in range(n)],
        }
        family_dir = source / family
        family_dir.mkdir(parents=True)
        (family_dir / "instance.json").write_text(json.dumps(payload))
    selected = tmp_path / "smoke10"
    selector.materialize(source, selected, 2)

    experiment = tmp_path / "experiment"
    (experiment / "dataset").mkdir(parents=True)
    (experiment / "experiment_manifest.json").write_text(json.dumps({
        "dataset_profile": "smoke10",
    }))
    pipeline.command_dataset(SimpleNamespace(
        experiment=str(experiment),
        source=str(selected),
        resume=False,
        canonical_fingerprint=None,
    ))

    manifest = json.loads(
        (experiment / "experiment_manifest.json").read_text()
    )
    canonical_files = list((experiment / "dataset").rglob("*.json"))
    assert len(canonical_files) == 2
    assert manifest["instance_count"] == 2
    assert manifest["selected_instance_count"] == 2
    assert all(path.name != "manifest.json" for path in canonical_files)


def test_dataset_stage_materializes_static_selection_manifest(tmp_path):
    selector_path = ROOT / "scripts" / "select_test_instances.py"
    selector_spec = importlib.util.spec_from_file_location(
        "selector_for_static_manifest_test", selector_path
    )
    assert selector_spec is not None and selector_spec.loader is not None
    selector = importlib.util.module_from_spec(selector_spec)
    sys.modules[selector_spec.name] = selector
    selector_spec.loader.exec_module(selector)

    source = tmp_path / "source"
    for family, n, m in (("a", 1, 1), ("b", 2, 2)):
        payload = {
            "centers": [{"coord": [index, 0]} for index in range(m)],
            "moving_points": [
                {"start": {"coord": [0, 0]}, "end": {"coord": [index + 1, 1]}}
                for index in range(n)
            ],
        }
        family_dir = source / family
        family_dir.mkdir(parents=True)
        (family_dir / "tiny.mdc").write_text(json.dumps(payload))
    candidates, _ = selector.discover_candidates(source)
    selection = selector.make_manifest(
        source, selector.select_candidates(candidates, 2), 2
    )
    selection_path = source / "smoke10.json"
    selection_path.write_text(json.dumps(selection))

    experiment = tmp_path / "experiment"
    (experiment / "dataset").mkdir(parents=True)
    (experiment / "experiment_manifest.json").write_text(json.dumps({
        "dataset_profile": "smoke10",
    }))
    pipeline.command_dataset(SimpleNamespace(
        experiment=str(experiment),
        source=str(selection_path),
        resume=False,
        canonical_fingerprint=None,
    ))

    manifest = json.loads(
        (experiment / "experiment_manifest.json").read_text()
    )
    assert manifest["instance_count"] == 2
    assert len(list((experiment / "dataset").rglob("*.json"))) == 2
    assert manifest["dataset_conversion"] == "mdc-to-canonical-json"
