import importlib.util
import json
from pathlib import Path
import shutil
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


def _create_external_backup(experiment, backup_dir):
    payload = backup_dir / "payload"
    files = [
        path for path in (experiment / "snapshots").rglob("*")
        if path.is_file() and path.suffix.lower() in {".png", ".pdf"}
    ]
    files.extend(
        experiment / relative
        for relative in (
            "reports/visualization_manifest.json",
            "reports/visualization_report.md",
            "reports/snapshot_regeneration_report.md",
            "artifacts.json",
        )
        if (experiment / relative).is_file()
    )
    entries = []
    for source in sorted(files):
        relative = source.relative_to(experiment)
        destination = payload / relative
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, destination)
        entries.append({
            "path": relative.as_posix(),
            "size_bytes": destination.stat().st_size,
            "sha256": visualize.sha256_file(destination),
        })
    backup_dir.mkdir(parents=True, exist_ok=True)
    (backup_dir / "backup_sha256.json").write_text(
        json.dumps({"experiment_id": experiment.name, "files": entries}),
        encoding="utf-8",
    )
    return backup_dir


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


def test_publication_palette_is_stable_and_distinct_above_twenty_stations():
    stations = [
        {"id": index, "x": float(index), "y": float(index % 3)}
        for index in range(25)
    ]

    colors = visualize._publication_station_colors(stations)
    reversed_colors = visualize._publication_station_colors(list(reversed(stations)))

    assert colors == reversed_colors
    assert len(set(colors.values())) == 25


def test_publication_panel_preserves_geometry_and_shared_supports():
    instance = _instance()
    solution = _solution("minmax")
    solution["intervals"][0]["supporting_point"] = [0, 0]
    solution["intervals"][1]["supporting_point"] = [0, 0]
    time = 0.5
    state = visualize._publication_state(instance, solution, time)
    bounds = visualize._snapshot_bounds(instance, solution, (0.0, 1.0, 2.0))
    colors = visualize._publication_station_colors(instance["stations"])
    figure, axis = visualize.plt.subplots()

    visualize._draw_publication_panel(
        axis, instance, state, bounds, colors,
        panel_label="(b)  t = T/2",
        show_trajectories=True,
        show_support_connectors=False,
    )

    assert len(axis.patches) == 2
    assert [patch.center for patch in axis.patches] == [
        (0.0, 0.0), (4.0, 0.0)
    ]
    assert [patch.radius for patch in axis.patches] == pytest.approx(
        [1.5, 2.5]
    )
    assert len(axis.lines) == 2
    assert len(axis.collections) == 6  # 2 stations, 2 points, 2 support diamonds
    assert axis.get_xlim() == pytest.approx(bounds["xlim"])
    assert axis.get_ylim() == pytest.approx(bounds["ylim"])
    assert axis.get_aspect() == 1.0
    assert axis.get_title() == "(b)  t = T/2"
    assert axis.get_xlabel() == "x"
    assert axis.get_ylabel() == "y"
    assert not axis.texts
    support_colors = [
        tuple(collection.get_edgecolors()[0])
        for collection in axis.collections[-2:]
    ]
    expected_support_colors = [
        colors[visualize._station_identity(station)]
        for station in instance["stations"]
    ]
    assert support_colors == expected_support_colors
    visualize.plt.close(figure)


def test_publication_state_keeps_empty_station_radius_at_zero():
    instance = _instance()
    solution = _solution("minmax")
    solution["intervals"][0]["supporting_point"] = [-1, 1]

    state = visualize._publication_state(instance, solution, 0.5)

    assert state["supports"] == [-1, 1]
    assert state["radii"][0] == 0.0
    assert state["radii"][1] == pytest.approx(1.5)


def test_publication_figures_emit_print_sized_pngs_and_vector_pdfs(tmp_path):
    instance = _instance()
    solution = _solution("minmax")
    plan = {
        "instance_id": "inst-a",
        "algorithm": "greedy",
        "objective": "minmax",
    }

    rendered = visualize._render_publication_solution_images(
        tmp_path, instance, solution, plan, "run-1", 600, "a" * 64
    )

    artifacts = [*rendered["panels"], rendered["triptych"]]
    assert [Path(item["path"]).name for item in rendered["panels"]] == [
        "tau-0.png", "tau-0.5.png", "tau-1.png"
    ]
    assert Path(rendered["triptych"]["path"]).name == "three-time.png"
    assert [Path(item["pdf"]["path"]).name for item in artifacts] == [
        "tau-0.pdf", "tau-0.5.pdf", "tau-1.pdf", "three-time.pdf"
    ]
    for item in artifacts:
        image_path = tmp_path / item["path"]
        pdf_path = tmp_path / item["pdf"]["path"]
        from PIL import Image

        with Image.open(image_path) as image:
            assert image.info["dpi"] == pytest.approx((600, 600), rel=0.005)
            assert image.size == (
                round(item["physical_dimensions_inches"]["width"] * 600),
                round(item["physical_dimensions_inches"]["height"] * 600),
            )
        pdf_bytes = pdf_path.read_bytes()
        visualize._validate_pdf_bytes(pdf_bytes, path=pdf_path)
        assert b"/Subtype /Image" not in pdf_bytes
        assert b"/FontFile2" in pdf_bytes or b"/FontFile3" in pdf_bytes
        assert item["source_solution_sha256"] == "a" * 64
        assert item["pdf"]["vector_geometry"] is True
    assert rendered["rendering_parameters"]["point_labels"] == "none"
    assert rendered["rendering_parameters"]["renderer"] == "publication"
    assert rendered["rendering_parameters"]["image_dimensions"]["individual_panel"][
        "width"
    ] == round(visualize.PUBLICATION_PANEL_SIZE[0] * 600)


def test_pdf_stream_validation_respects_declared_lengths():
    vector_content = (
        b"0 0 m\n1 1 l\n1 1 1 2 2 2 c\n" * 20
        + b">> stream\n"
    )
    compressor = visualize.zlib.compressobj(level=0)
    compressed = compressor.compress(vector_content) + compressor.flush()
    pdf = (
        b"%PDF-1.4\n"
        b"1 0 obj\n<< /Type /Pages /Count 1 >>\nendobj\n"
        b"2 0 obj\n<< /Type /Page /MediaBox [0 0 10 10] >>\nendobj\n"
        b"3 0 obj\n<< /FontFile2 4 0 R >>\nendobj\n"
        + f"5 0 obj\n<< /Length {len(compressed)} /Filter /FlateDecode >>\n".encode()
        + b"stream\n"
        + compressed
        + b"\nendstream\nendobj\n%%EOF\n"
    )

    visualize._validate_pdf_bytes(pdf)


def test_publication_single_panel_legend_fits_and_clears_axis_label():
    instance = _instance()
    solution = _solution("minmax")
    bounds = visualize._snapshot_bounds(instance, solution, (0.0, 1.0, 2.0))
    state = visualize._publication_state(instance, solution, 0.0)
    figure = visualize._build_publication_panel(
        instance, state, bounds,
        visualize._publication_station_colors(instance["stations"]),
        panel_label="t = 0",
        show_trajectories=True,
        show_support_connectors=False,
        dpi=100,
    )
    figure.canvas.draw()
    renderer = figure.canvas.get_renderer()
    axis, legend_axis = figure.axes
    legend = legend_axis.get_legend()
    legend_bounds = legend.get_window_extent(renderer)
    figure_bounds = figure.bbox
    x_label_bounds = axis.xaxis.label.get_window_extent(renderer)
    title_bounds = axis.title.get_window_extent(renderer)

    assert title_bounds.y1 < figure_bounds.y1
    assert legend_bounds.x0 >= figure_bounds.x0
    assert legend_bounds.y0 >= figure_bounds.y0
    assert legend_bounds.x1 <= figure_bounds.x1
    assert legend_bounds.y1 <= figure_bounds.y1
    assert not legend_bounds.overlaps(x_label_bounds)
    assert [text.get_text() for text in legend.get_texts()] == [
        "Trajectory", "Coverage disk", "Station", "Point (owner color)",
        "Support point",
    ]
    assert all(text.get_fontsize() >= 6 for text in legend.get_texts())
    visualize.plt.close(figure)


def test_snapshots_only_cli_replaces_only_publication_artifacts(
    tmp_path, monkeypatch, capsys
):
    experiment, records = _experiment(tmp_path)
    instance_sha256 = visualize.sha256_file(experiment / "input" / "inst-a.json")
    for attempts in records.values():
        attempts[0]["input_sha256"] = instance_sha256
    figures = experiment / "figures"
    figures.mkdir()
    (figures / "figure_status.json").write_text(
        json.dumps({"figures": []}), encoding="utf-8"
    )
    (figures / "REPORT.md").write_text(
        "| Figure | Status |\n|---|---|\n", encoding="utf-8"
    )
    (experiment / "reports" / "final_smoke20_audit.json").write_text(
        '{"audit_status":"FAIL"}\n', encoding="utf-8"
    )
    (experiment / "reports" / "final_smoke20_audit.md").write_text(
        "# Existing audit\n", encoding="utf-8"
    )
    monkeypatch.setattr(visualize, "_load_attempts", lambda _root: records)

    def fake_animation(_instance, _solution, record, output, _objective, **_kwargs):
        output.mkdir(parents=True, exist_ok=True)
        path = output / f"{record['run_id']}.gif"
        path.write_bytes(b"GIF89a")
        return path

    monkeypatch.setattr(visualize, "make_animation", fake_animation)
    initial = visualize.generate(experiment, fps=2, frames=3, dpi=40)
    backup_dir = _create_external_backup(experiment, tmp_path / "backup")
    protected_reports = {
        path: path.read_bytes()
        for path in (
            figures / "figure_status.json",
            figures / "REPORT.md",
            experiment / "reports" / "final_smoke20_audit.json",
            experiment / "reports" / "final_smoke20_audit.md",
        )
    }
    html_before = {
        sheet["path"]: visualize.sha256_file(experiment / sheet["path"])
        for sheet in initial["comparison_sheets"]
    }
    animations_before = {
        slot["animation"]["path"]: visualize.sha256_file(
            experiment / slot["animation"]["path"]
        )
        for slot in initial["slots"]
    }

    status = visualize.main([
        "--experiment", str(experiment),
        "--snapshots-only",
        "--replace-existing",
        "--formats", "pdf,png",
        "--dpi", "80",
        "--point-labels", "none",
        "--staging-parent", str(tmp_path),
        "--backup-dir", str(backup_dir),
    ])

    assert status == 0
    result = json.loads(capsys.readouterr().out)
    assert result["regenerated_pngs"] == 12
    assert result["regenerated_pdfs"] == 12
    assert visualize.audit(experiment)["rendered_slots"] == 3
    updated = visualize.read_json(
        experiment / "reports" / "visualization_manifest.json"
    )
    assert updated["publication_renderer_version"] == (
        visualize.PUBLICATION_RENDERER_VERSION
    )
    assert updated["generated_snapshot_pdfs"] == 12
    for slot in updated["slots"]:
        if not slot.get("snapshots"):
            continue
        for item in [slot["snapshots"]["triptych"], *slot["snapshots"]["panels"]]:
            assert (experiment / item["path"]).is_file()
            assert (experiment / item["pdf"]["path"]).is_file()
            assert item["source_solution_sha256"] == slot["source_solution_sha256"]
    for path, digest in html_before.items():
        assert visualize.sha256_file(experiment / path) == digest
    for path, digest in animations_before.items():
        assert visualize.sha256_file(experiment / path) == digest
    assert {path: path.read_bytes() for path in protected_reports} == protected_reports
    assert not list(tmp_path.glob(f".{experiment.name}.snapshot-stage-*"))
    index = visualize.read_json(
        experiment / "artifacts.json", schema_name="artifact_manifest"
    )
    media_types = {item["path"]: item["media_type"] for item in index["artifacts"]}
    assert media_types["snapshots/inst-a/minmax/greedy/run-minmax/tau-0.pdf"] == (
        "application/pdf"
    )


def test_snapshots_only_rolls_back_artifacts_and_metadata_on_failure(
    tmp_path, monkeypatch, capsys
):
    experiment, records = _experiment(tmp_path)
    instance_sha256 = visualize.sha256_file(experiment / "input" / "inst-a.json")
    for attempts in records.values():
        attempts[0]["input_sha256"] = instance_sha256
    figures = experiment / "figures"
    figures.mkdir()
    (figures / "figure_status.json").write_text(
        json.dumps({"figures": []}), encoding="utf-8"
    )
    (figures / "REPORT.md").write_text(
        "| Figure | Status |\n|---|---|\n", encoding="utf-8"
    )
    (experiment / "reports" / "final_smoke20_audit.json").write_text(
        '{"audit_status":"FAIL"}\n', encoding="utf-8"
    )
    (experiment / "reports" / "final_smoke20_audit.md").write_text(
        "# Existing audit\n", encoding="utf-8"
    )
    monkeypatch.setattr(visualize, "_load_attempts", lambda _root: records)

    def fake_animation(_instance, _solution, record, output, _objective, **_kwargs):
        output.mkdir(parents=True, exist_ok=True)
        path = output / f"{record['run_id']}.gif"
        path.write_bytes(b"GIF89a")
        return path

    monkeypatch.setattr(visualize, "make_animation", fake_animation)
    initial = visualize.generate(experiment, fps=2, frames=3, dpi=40)
    backup_dir = _create_external_backup(experiment, tmp_path / "backup")
    tracked_paths = [
        experiment / "reports" / "visualization_manifest.json",
        experiment / "reports" / "visualization_report.md",
        experiment / "artifacts.json",
        figures / "figure_status.json",
        figures / "REPORT.md",
        experiment / "reports" / "final_smoke20_audit.json",
        experiment / "reports" / "final_smoke20_audit.md",
    ]
    before_metadata = {path: path.read_bytes() for path in tracked_paths}
    before_snapshots = {
        experiment / artifact["path"]: visualize.sha256_file(
            experiment / artifact["path"]
        )
        for slot in initial["slots"]
        if slot.get("snapshots")
        for artifact in [slot["snapshots"]["triptych"], *slot["snapshots"]["panels"]]
    }

    def fail_audit(_experiment):
        raise ValueError("injected post-replacement audit failure")

    monkeypatch.setattr(visualize, "audit", fail_audit)
    status = visualize.main([
        "--experiment", str(experiment),
        "--snapshots-only",
        "--replace-existing",
        "--formats", "png,pdf",
        "--dpi", "80",
        "--staging-parent", str(tmp_path),
        "--backup-dir", str(backup_dir),
    ])

    assert status == 1
    assert "injected post-replacement audit failure" in capsys.readouterr().err
    assert {path: path.read_bytes() for path in tracked_paths} == before_metadata
    assert {
        path: visualize.sha256_file(path) for path in before_snapshots
    } == before_snapshots
    assert not list((experiment / "snapshots").rglob("*.pdf"))
    failed_stages = list(tmp_path.glob(f".{experiment.name}.snapshot-stage-*"))
    assert failed_stages
    assert list((failed_stages[0] / "snapshots").rglob("*.pdf"))


def test_publication_format_validation_requires_both_formats():
    assert visualize._parse_publication_formats("pdf,png") == ("pdf", "png")
    with pytest.raises(ValueError, match="both png and pdf"):
        visualize._parse_publication_formats("png")
    with pytest.raises(ValueError, match="exactly once"):
        visualize._parse_publication_formats("png,pdf,pdf")


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
