import importlib.util
import json
from pathlib import Path
import sys

import pandas as pd


ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location(
    "build_tables", ROOT / "scripts" / "build_tables.py"
)
assert SPEC is not None and SPEC.loader is not None
build_tables = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = build_tables
SPEC.loader.exec_module(build_tables)


def sample_frame():
    rows = []
    for instance, n in [("random_n20_m5_seed1", 20),
                        ("random_n20_m5_seed2", 20),
                        ("fix_case3", 75)]:
        for algorithm, cost in [("nn", 123.4567), ("greedy", 145.0),
                                ("ip-kont", 100.0)]:
            for mode, factor in [("minmax", 1.0), ("minsum", 0.5)]:
                rows.append({
                    "instance_name": instance,
                    "algorithm_name": algorithm,
                    "objective": mode,
                    "n": n,
                    "m": 5,
                    "wall_time_sec": 0.1234,
                    "cpu_time_sec": 0.1,
                    "peak_memory_mb": 8.25,
                    "objective_value": cost * factor,
                    "lower_bound": 90.0,
                    "gap": 0.05,
                    "num_iterations": 2,
                    "num_ip_solves": 3,
                    "verified": True,
                    "feasible": True,
                    "error_message": "",
                })
    rows.append({
        **rows[0],
        "instance_name": "bad_instance",
        "algorithm_name": "nn",
        "feasible": False,
        "verified": False,
        "error_message": "solver failed",
    })
    return pd.DataFrame(rows)


def test_master_has_all_columns():
    master = build_tables.build_master(sample_frame())
    assert list(master.columns) == build_tables.MASTER_COLUMNS


def test_master_best_is_bold():
    df = sample_frame()
    rendered = build_tables.render_master(df)
    assert "**100**" in rendered
    assert "| random_n20_m5_seed1 |" in rendered


def test_per_algorithm_one_file_per_algorithm(tmp_path):
    raw = sample_frame()
    raw, good, failed = _prepare(raw)
    build_tables.write_tables(raw, good, failed, tmp_path, {"markdown", "csv"})
    paths = list((tmp_path / "per_algorithm").glob("*.md"))
    assert {path.stem for path in paths} == {"nn", "greedy", "ip-kont"}


def test_per_instance_one_file_per_instance(tmp_path):
    raw, good, failed = _prepare(sample_frame())
    build_tables.write_tables(raw, good, failed, tmp_path, {"markdown", "csv"})
    paths = list((tmp_path / "per_instance").glob("*.md"))
    assert {path.stem for path in paths} == {
        "random_n20_m5_seed1", "random_n20_m5_seed2", "fix_case3"
    }


def test_per_family_aggregation(tmp_path):
    raw, good, failed = _prepare(sample_frame())
    build_tables.write_tables(raw, good, failed, tmp_path, {"csv"})
    family = pd.read_csv(tmp_path / "per_family" / "random_n.csv")
    row = family[family.algorithm_name == "nn"].iloc[0]
    assert row.minmax_num_instances == 2
    assert row.minmax_median_objective == 123.4567
    assert "minsum_median_objective" in family.columns
    assert "minmax_p95_time_sec" in family.columns


def test_failed_runs_separated(tmp_path):
    raw, good, failed = _prepare(sample_frame())
    build_tables.write_tables(raw, good, failed, tmp_path,
                              {"markdown", "csv"})
    master = (tmp_path / "master.md").read_text()
    failed_md = (tmp_path / "failed_runs.md").read_text()
    assert "bad_instance" not in master
    assert "bad_instance" in failed_md
    assert "solver failed" in failed_md


def test_index_links_exist(tmp_path):
    raw, good, failed = _prepare(sample_frame())
    build_tables.write_tables(raw, good, failed, tmp_path,
                              {"markdown", "csv"})
    index = (tmp_path / "00_index.md").read_text()
    assert "[Master table: master.md](master.md)" in index
    for path in tmp_path.rglob("*.md"):
        if path.name == "00_index.md":
            continue
        relative = path.relative_to(tmp_path).as_posix()
        assert f"]({relative})" in index


def test_numeric_formatting():
    assert build_tables._format_value("objective_value", 123.4567) == "123.457"
    assert build_tables._format_value("gap_pct", 12.345) == "12.35%"
    assert build_tables._format_value("wall_time_sec", 0.12345) == "0.123s"


def test_cli_loads_json_and_writes_tables(tmp_path):
    source = tmp_path / "results.json"
    source.write_text(json.dumps(sample_frame().to_dict(orient="records")))
    destination = tmp_path / "tables"
    assert build_tables.main(["--input", str(source), "--output",
                              str(destination)]) == 0
    assert (destination / "master.csv").is_file()
    assert (destination / "00_index.md").is_file()


def _prepare(frame):
    temporary = frame.copy()
    for column in build_tables.NUMERIC_COLUMNS:
        temporary[column] = pd.to_numeric(temporary[column]).astype("float64")
    temporary["gap_pct"] = temporary["gap"] * 100.0
    temporary["instance_family"] = temporary.instance_name.map(
        build_tables.instance_family
    )
    return temporary, temporary[temporary.feasible & temporary.verified], \
        temporary[~(temporary.feasible & temporary.verified)]
