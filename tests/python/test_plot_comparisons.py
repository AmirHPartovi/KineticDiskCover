import hashlib
import importlib.util
from pathlib import Path
import sys

import pandas as pd


ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location(
    "plot_comparisons", ROOT / "scripts" / "plot_comparisons.py"
)
assert SPEC is not None and SPEC.loader is not None
plot_comparisons = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = plot_comparisons
SPEC.loader.exec_module(plot_comparisons)


def sample_frame():
    rows = []
    for instance, size in (("random_n20_m5_seed1", 20),
                           ("fix_case2", 40)):
        for algorithm, factor in (("nn", 1.2), ("ip-kont", 1.0)):
            for objective, scale in (("minmax", 1.0), ("minsum", 0.5)):
                rows.append({
                    "instance_name": instance,
                    "algorithm_name": algorithm,
                    "objective": objective,
                    "n": size,
                    "m": 5,
                    "wall_time_sec": factor * scale,
                    "peak_memory_mb": 10.0 * factor,
                    "objective_value": 100.0 * factor * scale,
                    "gap": 0.1 * (factor - 0.8),
                    "verified": True,
                    "feasible": True,
                })
    rows.append({
        **rows[0],
        "instance_name": "failed",
        "verified": False,
        "feasible": False,
    })
    return pd.DataFrame(rows)


def test_prepare_frame_filters_failed_runs_and_computes_gap_pct():
    frame = plot_comparisons.prepare_frame(sample_frame())
    assert len(frame) == 8
    assert set(frame.instance_name) == {"random_n20_m5_seed1", "fix_case2"}
    assert (frame.gap_pct == frame.gap * 100).all()


def test_ecdf_and_power_law_helpers():
    x, y = plot_comparisons.ecdf([3, 1, 2])
    assert list(x) == [1, 1, 2, 3]
    assert list(y) == [0, 1 / 3, 2 / 3, 1]
    slope, confidence = plot_comparisons.fit_power_law([1, 2, 4], [1, 4, 16])
    assert abs(slope - 2.0) < 1e-12
    assert confidence[0] <= slope <= confidence[1]


def test_pareto_front_excludes_dominated_points():
    frame = pd.DataFrame({
        "wall_time_sec": [1.0, 2.0, 3.0, 4.0],
        "objective_value": [4.0, 5.0, 2.0, 3.0],
    })
    front = plot_comparisons.pareto_front(frame)
    assert list(zip(front.wall_time_sec, front.objective_value)) == [
        (1.0, 4.0), (3.0, 2.0)
    ]


def test_full_figure_set_report_and_file_pairs(tmp_path):
    plot_comparisons.configure_style(75)
    plot_comparisons.FIGURE_DPI = 75
    frame = plot_comparisons.prepare_frame(sample_frame())
    names = plot_comparisons.generate_figures(frame, tmp_path)
    report_path = plot_comparisons.write_report(frame, tmp_path, names)
    assert len(names) >= 25
    report = report_path.read_text()
    for name in names:
        png = tmp_path / f"{name}.png"
        pdf = tmp_path / f"{name}.pdf"
        assert png.is_file() and png.stat().st_size > 0
        assert pdf.is_file() and pdf.stat().st_size > 0
        assert report.count(f"![{name}]({name}.png)") == 1
    assert len(list(tmp_path.glob("*.png"))) == len(names)
    assert "D2_pareto_aggregated" in names


def test_png_generation_is_reproducible(tmp_path):
    plot_comparisons.configure_style(75)
    plot_comparisons.FIGURE_DPI = 75
    frame = plot_comparisons.prepare_frame(sample_frame())
    first_dir = tmp_path / "first"
    second_dir = tmp_path / "second"
    first_names = plot_comparisons.generate_figures(
        frame, first_dir, sections={"A2", "C2"}
    )
    second_names = plot_comparisons.generate_figures(
        frame, second_dir, sections={"A2", "C2"}
    )
    assert first_names == second_names == ["A2_runtime_box", "C2_gap_ecdf"]
    digest = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
    assert digest(first_dir / "C2_gap_ecdf.png") == digest(
        second_dir / "C2_gap_ecdf.png"
    )
    assert digest(first_dir / "A2_runtime_box.png") == digest(
        second_dir / "A2_runtime_box.png"
    )


def test_smoke_figure_report_is_labeled_as_development_validation(tmp_path):
    plot_comparisons.configure_style(50)
    frame = plot_comparisons.prepare_frame(sample_frame())
    frame["dataset_profile"] = "smoke10"

    report = plot_comparisons.write_report(frame, tmp_path, [])

    contents = report.read_text()
    assert "Smoke / development validation" in contents
    assert "not the full scientific benchmark" in contents


def test_convergence_grid_splits_large_instance_sets(tmp_path):
    frame = pd.concat([
        sample_frame().assign(instance_name=f"instance_{index:03d}")
        for index in range(25)
    ], ignore_index=True)
    plot_comparisons.configure_style(30)
    plot_comparisons.FIGURE_DPI = 30

    names = plot_comparisons.generate_figures(
        frame, tmp_path, sections={"F2"}
    )

    assert names == ["F2_convergence_grid_001", "F2_convergence_grid_002"]
    assert all((tmp_path / f"{name}.png").is_file() for name in names)
