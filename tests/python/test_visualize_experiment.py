import importlib.util
import json
from pathlib import Path
import sys

import pytest


ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location(
    "visualize_experiment", ROOT / "scripts" / "visualize_experiment.py"
)
assert SPEC is not None and SPEC.loader is not None
visualize = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = visualize
SPEC.loader.exec_module(visualize)

ANIMATE_SPEC = importlib.util.spec_from_file_location(
    "animate_solution", ROOT / "scripts" / "animate_solution.py"
)
assert ANIMATE_SPEC is not None and ANIMATE_SPEC.loader is not None
animate_solution = importlib.util.module_from_spec(ANIMATE_SPEC)
sys.modules[ANIMATE_SPEC.name] = animate_solution
ANIMATE_SPEC.loader.exec_module(animate_solution)


def _instance():
    return {
        "id": 7,
        "name": "moving-two",
        "T_end": 2.0,
        "stations": [{"id": 0, "x": 0.0, "y": 0.0},
                     {"id": 1, "x": 4.0, "y": 0.0}],
        "trajectories": [
            {
                "t_breaks": [0.0, 1.0, 2.0],
                "waypoints": [{"x": 1.0, "y": 0.0},
                              {"x": 2.0, "y": 0.0},
                              {"x": 3.0, "y": 0.0}],
            },
            {
                "t_breaks": [0.0, 1.0, 2.0],
                "waypoints": [{"x": 4.0, "y": 2.0},
                              {"x": 4.0, "y": 1.0},
                              {"x": 4.0, "y": 0.5}],
            },
        ],
    }


def _solution(objective):
    return {
        "instance": {"name": "moving-two", "n": 2, "m": 2, "T_end": 2.0},
        "objective": objective,
        "summary": {
            "peak_cost": 23.0,
            "peak_time": 2.0,
            "total_integral": 17.0,
            "num_intervals": 2,
        },
        "intervals": [
            {
                "t_start": 0.0, "t_end": 1.0,
                "supporting_point": [0, 1], "assigned_points": [0, 1],
                "a": 1.0, "b": 0.0, "c": 0.0,
            },
            {
                "t_start": 1.0, "t_end": 2.0,
                "supporting_point": [1, 0], "assigned_points": [1, 0],
                "a": 1.0, "b": 0.0, "c": 0.0,
            },
        ],
    }


def _experiment(tmp_path, *, attempts=True):
    experiment = tmp_path / "experiment-x"
    (experiment / "input").mkdir(parents=True)
    (experiment / "runs").mkdir()
    (experiment / "reports").mkdir()
    (experiment / "input" / "inst-a.json").write_text(
        json.dumps(_instance()), encoding="utf-8"
    )
    (experiment / "experiment_manifest.json").write_text(json.dumps({
        "experiment_id": experiment.name,
        "selected_instance_count": 1,
        "objectives": ["minmax", "minsum", "minmaxsum"],
    }), encoding="utf-8")
    plan = []
    records = {}
    for objective in ("minmax", "minsum", "minmaxsum"):
        run_key = f"run-key-{objective}"
        entry = {
            "run_key": run_key,
            "instance_id": "inst-a",
            "algorithm": "greedy",
            "objective": objective,
            "repeat": 0,
            "seed": 4,
        }
        plan.append(entry)
        if attempts:
            run_id = f"run-{objective}"
            run_dir = experiment / "runs" / run_id
            (run_dir / "result").mkdir(parents=True)
            solution_path = run_dir / "result" / "solution.json"
            solution_path.write_text(
                json.dumps(_solution(objective)), encoding="utf-8"
            )
            records[run_key] = [{
                "run_id": run_id,
                "run_key": run_key,
                "experiment_id": experiment.name,
                "instance_id": "inst-a",
                "algorithm": "greedy",
                "objective": objective,
                "status": "TIME_LIMIT" if objective == "minsum" else "COMPLETED",
                "_visual_run_dir": str(run_dir),
                "result": {
                    "feasible": True,
                    "solver_result": {
                        "solution_json_path": "result/solution.json",
                        "optimality_status": "TIME_LIMIT"
                        if objective == "minsum" else "FEASIBLE",
                        "peak_cost": 23.0,
                        "integral_cost": 17.0,
                        "objective_vector": (
                            {"peak_cost": 23.0, "integral_cost": 17.0}
                            if objective == "minmaxsum" else None
                        ),
                        "summary": _solution(objective)["summary"],
                        "wall_time_sec": 0.2,
                    },
                    "error_message": None,
                },
                "verification": {
                    "kind": "EMPIRICAL",
                    "passed": True,
                },
            }]
    (experiment / "runs" / "plan.jsonl").write_text(
        "".join(json.dumps(entry) + "\n" for entry in plan),
        encoding="utf-8",
    )
    return experiment, records


def test_non_unit_horizon_boundary_and_changing_support_geometry():
    instance = _instance()
    solution = _solution("minmax")
    solution["_instance"] = instance

    assert visualize.position_at(instance, 0, 1.0) == (2.0, 0.0)
    assert visualize._interval_at(solution, 1.0) is solution["intervals"][1]
    assert visualize.radius_at(solution, 0, 0.0, instance) == 1.0
    assert visualize.radius_at(solution, 0, 1.0, instance) == pytest.approx(
        17.0 ** 0.5
    )
    assert visualize.radius_at(solution, 1, 1.0, instance) == 2.0


def test_snapshot_bounds_are_shared_square_and_translation_invariant():
    instance = _instance()
    solution = _solution("minmax")
    translated = json.loads(json.dumps(instance))
    for station in translated["stations"]:
        station["x"] += 1000.0
        station["y"] -= 250.0
    for trajectory in translated["trajectories"]:
        for waypoint in trajectory["waypoints"]:
            waypoint["x"] += 1000.0
            waypoint["y"] -= 250.0

    bounds = visualize._snapshot_bounds(
        translated, solution, (0.0, 1.0, 2.0)
    )
    xlim, ylim = bounds["xlim"], bounds["ylim"]

    assert xlim[1] - xlim[0] == pytest.approx(ylim[1] - ylim[0])
    assert xlim[0] > 900.0
    assert ylim[1] < 0.0
    for time in (0.0, 1.0, 2.0):
        for station_index, station in enumerate(translated["stations"]):
            radius = visualize.radius_at(
                solution, station_index, time, translated
            )
            assert xlim[0] <= station["x"] - radius
            assert xlim[1] >= station["x"] + radius
            assert ylim[0] <= station["y"] - radius
            assert ylim[1] >= station["y"] + radius

    with pytest.raises(ValueError, match="finite display times"):
        visualize._snapshot_bounds(translated, solution, ())


def test_panel_layers_and_status_use_canonical_run_record():
    instance = _instance()
    solution = _solution("minmax")
    plan = {
        "instance_id": "inst-a",
        "algorithm": "greedy",
        "objective": "minmax",
    }
    record = {
        "status": "COMPLETED",
        "result": {
            "feasible": True,
            "solver_result": {
                "optimality_status": "FEASIBLE",
                "peak_cost": 23.0,
            },
        },
        "verification": {"kind": "EMPIRICAL", "passed": True},
    }
    bounds = visualize._snapshot_bounds(instance, solution, (0.0, 1.0, 2.0))
    figure, axis = visualize.plt.subplots()
    visualize._draw_panel(
        axis, instance, solution, record, plan, 0.5, bounds,
        show_trajectories=True,
        point_label_policy="supports",
        show_support_connectors=False,
    )

    assert axis.get_xlim() == pytest.approx(bounds["xlim"])
    assert axis.get_ylim() == pytest.approx(bounds["ylim"])
    assert axis.get_aspect() == 1.0
    assert len(axis.patches) == 2
    assert len(axis.lines) == 2
    assert [text.get_text() for text in axis.texts] == ["P1/S0", "P0/S1"]
    title = visualize._snapshot_title(instance, record, plan, "run-1")
    assert "COMPLETED" in title
    assert "verified=True (EMPIRICAL)" in title
    assert "optimality=FEASIBLE" in title
    visualize.plt.close(figure)


def test_legacy_animation_parser_keeps_joint_objective_vector(tmp_path):
    instance = _instance()
    instance_path = tmp_path / "instance.json"
    instance_path.write_text(json.dumps(instance), encoding="utf-8")
    solution_path = tmp_path / "solution.json"
    solution_path.write_text(
        json.dumps(_solution("minmaxsum")), encoding="utf-8"
    )

    loaded_instance = animate_solution.load_instance(instance_path)
    loaded_solution = animate_solution.load_solution(
        solution_path, "minmaxsum", loaded_instance[2]
    )

    assert loaded_solution.mode == "minmaxsum"
    assert loaded_solution.summary["peak_cost"] == 23.0
    assert loaded_solution.summary["total_integral"] == 17.0
    assert animate_solution._mode_name("MinMaxSum") == "minmaxsum"


def test_generates_all_objectives_triptychs_comparison_rows_and_gifs(
    tmp_path, monkeypatch
):
    experiment, records = _experiment(tmp_path)
    monkeypatch.setattr(visualize, "_load_attempts", lambda _root: records)
    monkeypatch.setattr("animate_best.shutil.which", lambda _name: None)

    result = visualize.generate(experiment, fps=2, frames=3, dpi=40)

    assert result["expected_comparison_sheets"] == 3
    assert result["generated_solution_snapshots"] == 3
    assert result["generated_animations"] == 3
    assert len(result["slots"]) == 3
    assert {row["objective"] for row in result["slots"]} == {
        "minmax", "minsum", "minmaxsum"
    }
    assert all(row["run_status"] in {"COMPLETED", "TIME_LIMIT"}
               for row in result["slots"])
    assert all(row["verification_kind"] == "EMPIRICAL"
               for row in result["slots"])
    assert all(row["animation"]["path"].endswith(".gif")
               for row in result["slots"])
    vector = next(row for row in result["slots"]
                  if row["objective"] == "minmaxsum")
    assert vector["source_solution_sha256"]
    assert vector["snapshots"]["rendering_parameters"]["actual_times"] == [
        0.0, 1.0, 2.0
    ]
    settings = vector["snapshots"]["rendering_parameters"]
    assert settings["renderer_version"] == visualize.RENDERER_VERSION
    assert settings["spatial_bounds_policy"].startswith("one square")
    assert settings["trajectory_visibility"] == "visible"
    assert settings["point_label_policy"] == "supports"
    assert settings["support_connector_policy"] == "hidden"
    assert settings["layout_method"] == "reserved-gridspec"
    assert settings["output_format"] == "png"
    assert result["renderer_version"] == visualize.RENDERER_VERSION
    assert result["rendering_policy"]["opacity"]["coverage_disk"] == (
        visualize.DISK_ALPHA
    )
    html_path = experiment / next(
        sheet["path"] for sheet in result["comparison_sheets"]
        if sheet["path"].endswith("minsum.html")
    )
    sheet_html = html_path.read_text(encoding="utf-8")
    assert "TIME_LIMIT" in sheet_html
    assert "τ=0.5" in sheet_html
    for row in result["slots"]:
        panel_path = experiment / row["snapshots"]["panels"][0]["path"]
        panel_image = visualize.plt.imread(panel_path)
        panel_dimensions = row["snapshots"]["panels"][0]["image_dimensions"]
        assert panel_image.shape[1] == panel_dimensions["width"]
        assert panel_image.shape[0] == panel_dimensions["height"]
        triptych_path = experiment / row["snapshots"]["triptych"]["path"]
        assert triptych_path.is_file()
        image = visualize.plt.imread(triptych_path)
        dimensions = row["snapshots"]["triptych"]["image_dimensions"]
        assert image.shape[1] == dimensions["width"]
        assert image.shape[0] == dimensions["height"]
    assert visualize.audit(experiment)["rendered_slots"] == 3


def test_missing_solution_slots_remain_in_comparison_index(tmp_path, monkeypatch):
    experiment, _ = _experiment(tmp_path, attempts=False)
    monkeypatch.setattr(visualize, "_load_attempts", lambda _root: {})

    result = visualize.generate(experiment, fps=2, frames=3, dpi=40)

    assert result["expected_comparison_sheets"] == 3
    assert result["generated_animations"] == 0
    assert result["ineligible_slots"] == 3
    assert all(row["generation_status"] == "INELIGIBLE"
               for row in result["slots"])
    assert all(row["failure_reason"] == "NO_TERMINAL_RUN_RECORD"
               for row in result["slots"])
    for sheet in result["comparison_sheets"]:
        text = (experiment / sheet["path"]).read_text(encoding="utf-8")
        assert "PLANNED" in text
        assert "NO_TERMINAL_RUN_RECORD" in text


def test_invalid_solution_is_explicit_and_audit_detects_missing_artifact(
    tmp_path, monkeypatch
):
    experiment, records = _experiment(tmp_path)
    first = next(iter(records))
    solution = Path(records[first][0]["_visual_run_dir"]) / "result" / "solution.json"
    solution.write_text("{bad json", encoding="utf-8")
    monkeypatch.setattr(visualize, "_load_attempts", lambda _root: records)

    result = visualize.generate(experiment, fps=2, frames=3, dpi=40)

    invalid = next(row for row in result["slots"] if row["run_key"] == first)
    assert invalid["generation_status"] == "INELIGIBLE"
    assert "INVALID_SOLUTION" in invalid["failure_reason"]
    assert "JSONDecodeError" in invalid["failure_reason"]
    generated = next(row for row in result["slots"]
                     if row["generation_status"] == "GENERATED")
    artifact = experiment / generated["snapshots"]["triptych"]["path"]
    artifact.unlink()
    with pytest.raises(ValueError, match="missing or modified"):
        visualize.audit(experiment)


def test_no_overwrite_for_visualization_outputs(tmp_path, monkeypatch):
    experiment, records = _experiment(tmp_path)
    monkeypatch.setattr(visualize, "_load_attempts", lambda _root: records)
    monkeypatch.setattr("animate_best.shutil.which", lambda _name: None)
    visualize.generate(experiment, fps=2, frames=3, dpi=40)
    with pytest.raises(FileExistsError):
        visualize.generate(experiment, fps=2, frames=3, dpi=40)


def test_artifact_paths_cannot_escape_experiment(tmp_path):
    experiment = tmp_path / "experiment"
    experiment.mkdir()
    with pytest.raises(ValueError, match="escapes experiment"):
        visualize._contained(experiment, experiment.parent / "outside.png")
