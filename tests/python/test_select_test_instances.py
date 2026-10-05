import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
from types import SimpleNamespace


ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location(
    "select_test_instances", ROOT / "scripts" / "select_test_instances.py"
)
assert SPEC is not None and SPEC.loader is not None
selector = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = selector
SPEC.loader.exec_module(selector)


def canonical(n, m):
    return {
        "name": "test",
        "T_end": 1.0,
        "stations": [{"id": i, "x": float(i), "y": 0.0} for i in range(m)],
        "trajectories": [{
            "t_breaks": [0.0, 1.0],
            "waypoints": [{"x": 0.0, "y": 0.0}, {"x": 1.0, "y": 1.0}],
        } for _ in range(n)],
    }


def legacy(n, m):
    return {
        "name": "legacy",
        "centers": [{"coord": [i, 0]} for i in range(m)],
        "moving_points": [
            {"start": {"coord": [0, 0]}, "end": {"coord": [i + 1, 1]}}
            for i in range(n)
        ],
    }


def write_json(path, payload):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload), encoding="utf-8")


def make_source(root):
    # Filenames deliberately contradict the actual instance sizes.
    write_json(root / "alpha" / "zz-large.mdc", legacy(8, 4))
    write_json(root / "alpha" / "aa-small.mdc", legacy(2, 2))
    write_json(root / "beta" / "00-m-not-small.json", canonical(3, 4))
    write_json(root / "beta" / "99-small.json", canonical(3, 2))
    write_json(root / "gamma" / "candidate.mdc", legacy(1, 3))
    write_json(root / "gamma" / "broken.mdc", {"moving_points": []})


def test_selects_smallest_by_parsed_dimensions_and_family(tmp_path):
    source = tmp_path / "source"
    make_source(source)
    candidates, errors = selector.discover_candidates(source)
    selected = selector.select_candidates(candidates, 3)

    assert len(selected) == 3
    assert [row["family"] for row in selected] == [
        "gamma", "alpha", "beta"
    ]
    assert [(row["n"], row["m"]) for row in selected] == [
        (1, 3), (2, 2), (3, 2)
    ]
    assert len({row["relative_source"] for row in selected}) == 3
    assert any("broken.mdc" in error for error in errors)


def test_global_fill_preserves_family_diversity_and_unique_sources(tmp_path):
    source = tmp_path / "source"
    make_source(source)
    candidates, _ = selector.discover_candidates(source)
    selected = selector.select_candidates(candidates, 4)

    assert len(selected) == 4
    assert len({row["family"] for row in selected}) == 3
    assert len({row["relative_source"] for row in selected}) == 4
    assert selected[-1]["relative_source"] == "beta/00-m-not-small.json"


def test_size_limits_and_oversized_fallback_are_explicit():
    candidates = [
        {"family": "small-a", "relative_source": "small-a/a.json",
         "n": 2, "m": 2, "file_size_bytes": 10},
        {"family": "small-b", "relative_source": "small-b/b.json",
         "n": 3, "m": 2, "file_size_bytes": 11},
        {"family": "large", "relative_source": "large/c.json",
         "n": 60, "m": 2, "file_size_bytes": 12},
    ]

    selected = selector.select_candidates(
        candidates, count=3, max_n=3, max_m=2
    )

    assert [row["relative_source"] for row in selected] == [
        "small-a/a.json", "small-b/b.json", "large/c.json"
    ]
    assert [row["exceeds_smoke_limits"] for row in selected] == [
        False, False, True
    ]
    assert "oversized family fallback" in selected[-1]["selection_reason"]
    assert all(row["smoke_limits"] == {"max_n": 3, "max_m": 2}
               for row in selected)


def test_flat_public_dataset_filenames_share_family_labels(tmp_path):
    write_json(tmp_path / "US-night-1.mdc", legacy(1, 1))
    write_json(tmp_path / "US-night-2.mdc", legacy(2, 1))
    write_json(tmp_path / "SBGDB-20200101-FPG-poly-1.mdc", legacy(1, 1))
    write_json(tmp_path / "SBGDB-20200101-PNTset-2.mdc", legacy(1, 1))

    candidates, errors = selector.discover_candidates(tmp_path)

    assert not errors
    assert {row["family"] for row in candidates} == {
        "us-night", "sbgdb-20200101-fpg-poly", "sbgdb-20200101-pntset"
    }


def test_materialization_manifest_and_rerun_are_deterministic(tmp_path):
    source = tmp_path / "source"
    output = tmp_path / "materialized"
    static_manifest = tmp_path / "smoke10.json"
    make_source(source)
    source_files = {
        path.relative_to(source).as_posix(): hashlib.sha256(path.read_bytes()).hexdigest()
        for path in source.rglob("*") if path.is_file()
    }

    first = selector.materialize(source, output, 3)
    first_manifest = json.loads((output / "manifest.json").read_text())
    files = sorted((output / "instances").rglob("*.json"))
    second = selector.materialize(source, output, 3)
    second_manifest = json.loads((output / "manifest.json").read_text())

    assert len(files) == 3
    assert first["instances"] == second["instances"]
    assert first_manifest["instances"] == second_manifest["instances"]
    assert all(row["materialized_file"] for row in second_manifest["instances"])
    assert len({row["source"] for row in second_manifest["instances"]}) == 3
    assert all(row["selection_rank"] for row in first_manifest["instances"])
    assert all(row["sha256"] and row["file_size_bytes"]
               and row["selection_reason"]
               for row in first_manifest["instances"])
    assert source_files == {
        path.relative_to(source).as_posix(): hashlib.sha256(path.read_bytes()).hexdigest()
        for path in source.rglob("*") if path.is_file()
    }

    # The checked-in profile deliberately contains paths relative to the repo.
    payload = selector.make_manifest(source, selector.select_candidates(
        selector.discover_candidates(source)[0], 3
    ), 3)
    static_manifest.write_text(json.dumps(payload), encoding="utf-8")
    assert all(not Path(row["source"]).is_absolute()
               for row in payload["instances"])


def test_materialized_directory_is_compatible_with_batch_inputs(tmp_path):
    source = tmp_path / "source"
    output = tmp_path / "smoke10"
    make_source(source)
    selector.materialize(source, output, 3)

    for script_name in ("run_batch.py", "run_batch_limited.py"):
        path = ROOT / "scripts" / script_name
        spec = importlib.util.spec_from_file_location(
            f"smoke_{path.stem}", path
        )
        assert spec is not None and spec.loader is not None
        module = importlib.util.module_from_spec(spec)
        sys.modules[spec.name] = module
        spec.loader.exec_module(module)
        resolved = module.resolve_instance_directory(output)
        assert resolved == output / "instances"
        assert len(list(resolved.rglob("*.json"))) == 3


def test_experiment_script_exposes_smoke_profile():
    completed = subprocess.run(
        ["bash", str(ROOT / "scripts" / "run_experiment.sh"),
         "--dataset-profile", "smoke10", "--help"],
        cwd=ROOT, check=True, text=True, capture_output=True,
    )
    assert "--dataset-profile full|smoke10" in completed.stdout


def test_pipeline_rejects_mismatched_dataset_profile():
    completed = subprocess.run(
        ["bash", str(ROOT / "scripts" / "run_experiment.sh"),
         "--pipeline", "smoke", "--dataset-profile", "full"],
        cwd=ROOT, text=True, capture_output=True,
    )
    assert completed.returncode == 2
    assert "requires dataset profile smoke10" in completed.stderr


def test_reference_pipeline_accepts_only_one_heuristic():
    completed = subprocess.run(
        ["bash", str(ROOT / "scripts" / "run_experiment.sh"),
         "--pipeline", "reference", "--algorithms", "all"],
        cwd=ROOT, text=True, capture_output=True,
    )
    assert completed.returncode == 2
    assert "exactly one heuristic algorithm" in completed.stderr


def test_named_pipeline_profiles_have_stable_required_stages():
    stages = [
        "init", "dataset", "build", "tests", "preflight",
        "backend_selection", "benchmark", "result_validation", "tables",
        "figures", "animations", "final_report", "summary",
    ]
    for name in ("smoke", "reference", "full"):
        completed = subprocess.run(
            ["python3", str(ROOT / "scripts" / "experiment_pipeline.py"),
             "config", "--pipeline", name, "--format", "json"],
            cwd=ROOT, check=True, text=True, capture_output=True,
        )
        profile = json.loads(completed.stdout)
        assert profile["pipeline"] == name
        assert profile["required_stages"] == stages
        assert profile["objectives"] == "both"
        assert profile["exact_backend"] == "auto"
        assert profile["tables"] and profile["figures"]
        if name == "smoke":
            assert profile["smoke_count"] == 10
            assert profile["smoke_max_n"] == 50
            assert profile["smoke_max_m"] == 25
            assert profile["animation"]["policy"] == "all-algorithms"
        elif name == "reference":
            assert profile["algorithms"] == "greedy"
            assert profile["dataset_profile"] == "full"
        else:
            assert profile["algorithms"] == "all"
            assert profile["repeats"] >= 2


def test_smoke_defaults_are_light_and_explicit_values_win():
    smoke = SimpleNamespace(
        dataset_profile="smoke10", threads=None, time_limit=None,
        fast_time_limit=None, exact_time_limit=None,
    )
    selector.apply_execution_defaults(smoke)
    assert (smoke.threads, smoke.time_limit, smoke.fast_time_limit,
            smoke.exact_time_limit) == (2, 2.0, 5.0, 10.0)

    overridden = SimpleNamespace(
        dataset_profile="smoke10", threads=1, time_limit=1.5,
        fast_time_limit=3.0, exact_time_limit=6.0,
    )
    selector.apply_execution_defaults(overridden)
    assert (overridden.threads, overridden.time_limit,
            overridden.fast_time_limit, overridden.exact_time_limit) == (
                1, 1.5, 3.0, 6.0
            )

    full = SimpleNamespace(
        dataset_profile="full", threads=None, time_limit=None,
        fast_time_limit=None, exact_time_limit=None,
    )
    selector.apply_execution_defaults(full)
    assert (full.threads, full.time_limit, full.fast_time_limit,
            full.exact_time_limit) == (
                max(1, __import__("os").cpu_count() or 1), 60.0, 30.0, 600.0
            )


def test_batch_manifest_and_results_keep_smoke_provenance(tmp_path):
    spec = importlib.util.spec_from_file_location(
        "run_batch_profile_test", ROOT / "scripts" / "run_batch_limited.py"
    )
    assert spec is not None and spec.loader is not None
    run_batch = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = run_batch
    spec.loader.exec_module(run_batch)
    source = tmp_path / "debug" / "tiny.json"
    source.parent.mkdir()
    source.write_text(json.dumps(canonical(1, 1)))
    experiment = tmp_path / "experiment"
    experiment.mkdir()
    args = SimpleNamespace(
        profile="fast",
        time_limit=1.0,
        fast_time_limit=2.0,
        exact_time_limit=3.0,
        minsum_refinement_policy="adaptive",
        threads=2,
        dataset_profile="smoke10",
        exact_reference="auto",
        repeats=1,
        seed=7,
    )

    plan, data = run_batch._build_plan(
        experiment,
        [source],
        ["greedy"],
        ("minmax",),
        "branch-and-bound",
        args,
        {"actual_backend": "branch-and-bound"},
        {"instances": [{
            "family": "debug",
            "source": "debug/tiny.mdc",
            "selection_policy": "one per family",
        }]},
    )

    config = data[plan[0]["run_key"]]["config"]
    assert config["dataset_profile"] == "smoke10"
    assert config["experiment_label"] == "smoke / development validation"
    assert config["backend_detection"]["selected"] == "branch-and-bound"
    assert len(plan) == 1
