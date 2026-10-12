import importlib.util
import json
from pathlib import Path
import sys


ROOT = Path(__file__).resolve().parents[2]
REPORTING_PATH = ROOT / "scripts" / "kdc_tools" / "reporting.py"
SPEC = importlib.util.spec_from_file_location("reporting_helpers", REPORTING_PATH)
assert SPEC is not None and SPEC.loader is not None
reporting = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = reporting
SPEC.loader.exec_module(reporting)

REPORT_PATH = ROOT / "scripts" / "report_experiment.py"
REPORT_SPEC = importlib.util.spec_from_file_location("report_experiment_tests", REPORT_PATH)
assert REPORT_SPEC is not None and REPORT_SPEC.loader is not None
report = importlib.util.module_from_spec(REPORT_SPEC)
sys.modules[REPORT_SPEC.name] = report
REPORT_SPEC.loader.exec_module(report)


def _companion(path, rows):
    return {
        "title": "Test export",
        "purpose": "Validate meaningful report metadata and bounded preview.",
        "experiment_id": "test-experiment",
        "source": "validated fixture records",
        "generation_command": "pytest tests/python/test_reporting.py",
        "file_scope": "fixture rows",
        "sort_order": "fixture insertion order",
        "filters": "none",
        "column_definitions": {
            "terminal_status": "Canonical fixture run state.",
            "metric": "Measured objective field; blank when unavailable.",
        },
        "rows": rows,
        "preview_limit": 5,
        "limitations": "Fixture data only; not a solver benchmark.",
    }


def test_csv_and_tsv_companions_include_metadata_and_bounded_preview(tmp_path):
    rows = [
        {"terminal_status": "COMPLETED", "metric": index}
        for index in range(40)
    ]
    rows.extend([
        {"terminal_status": "FAILED", "metric": None},
        {"terminal_status": "TIME_LIMIT", "metric": None},
    ])
    for suffix in (".csv", ".tsv"):
        path = tmp_path / f"results{suffix}"
        reporting.write_delimited(
            path, rows, columns=("terminal_status", "metric"),
            companion=_companion(path, rows),
        )
        companion = path.with_suffix(".md").read_text()
        assert "- **Rows:** 42" in companion
        assert "FAILED=1" in companion
        assert "TIME_LIMIT=1" in companion
        assert "Preview shows 5 of 42 rows" in companion
        assert "fixture insertion order" in companion
        assert reporting.audit_companions(tmp_path) == []


def test_reporting_reads_extensionless_selection_manifest(tmp_path):
    dataset = tmp_path / "dataset"
    dataset.mkdir()
    digest = "a" * 64
    instance = {
        "instance_id": digest,
        "family": "tiny",
        "family_classification_rule": "fixture",
        "source": "tiny.mdc",
        "relative_source": "tiny.mdc",
        "source_sha256": digest,
        "canonical_path": "instances/tiny.json",
        "canonical_sha256": "b" * 64,
        "n": 1,
        "m": 1,
        "T_end": 2.0,
        "file_size_bytes": 1,
        "selection_rank": 1,
        "selection_reason": "fixture selection",
        "size_limit_exception": None,
    }
    manifest = {
        "schema_version": 1,
        "created_at": "2026-01-01T00:00:00Z",
        "name": "smoke20",
        "profile": "smoke20",
        "policy_version": "1",
        "selector_version": "1",
        "selection_policy": "fixture policy",
        "policy_parameters": {
            "seed": 0, "ordering": "fixture", "family_fill": "round_robin",
        },
        "size_limits": {"max_n": 50, "max_m": 25},
        "count": 20,
        "source_root": ".",
        "source_dataset_fingerprint": digest,
        "selected_instance_count": 20,
        "family_coverage": {
            "available_eligible_families": 1,
            "represented_families": ["tiny"],
            "selected_counts": {"tiny": 20},
        },
        "classification_rule": "fixture",
        "instances": [instance] * 20,
        "rejected_candidates": [],
    }
    (dataset / "selection_manifest").write_text(json.dumps(manifest))
    loaded = report._read_selection_manifest(tmp_path)
    assert len(loaded["instances"]) == 20
    assert report._selection_manifest_path(tmp_path).name == "selection_manifest"


def test_recursive_audit_detects_missing_or_generic_companions(tmp_path):
    data = tmp_path / "nested" / "trace.csv"
    data.parent.mkdir()
    data.write_text("step,metric\n1,2.5\n", encoding="utf-8")
    errors = reporting.audit_companions(tmp_path)
    assert any("companion" in error for error in errors)
    reporting.audit_companions(tmp_path, create_missing=True,
                               experiment_id="fixture-exp")
    companion = data.with_suffix(".md")
    assert "Serialized source field `step`" in companion.read_text()
    assert reporting.audit_companions(tmp_path) == []
    companion.write_text("# Generic\n", encoding="utf-8")
    errors = reporting.audit_companions(tmp_path)
    assert any("non-informative companion" in error for error in errors)
    reporting.audit_companions(tmp_path, create_missing=True,
                               experiment_id="fixture-exp")
    companion.write_text(companion.read_text().replace(
        "- **Rows:** 1", "- **Rows:** 2"
    ), encoding="utf-8")
    errors = reporting.audit_companions(tmp_path)
    assert any("row count" in error for error in errors)


def _row(**updates):
    row = {
        "algorithm": "heuristic",
        "algorithm_category": "heuristic",
        "objective": "minmax",
        "planned_count": 1,
        "terminal_status": "COMPLETED",
        "solve_time_sec": 4.0,
        "wall_time_sec": 9.0,
        "feasible": True,
        "verification_passed": True,
        "verification_kind": "EMPIRICAL",
        "objective_value": 12.0,
        "reference_designated": False,
        "optimality_status": "FEASIBLE",
        "instance_id": "i-1",
        "family": "family-a",
        "run_id": "run-1",
        "actual_backend": "heuristic",
        "peak_cost": None,
        "integral_cost": None,
        "lower_bound": None,
        "upper_bound": None,
        "bound_status": None,
        "bound_provenance": None,
        "certified_gap": None,
        "heuristic_gap": None,
        "empirical_gap": None,
        "empirical_ratio_to_exact": None,
        "verification_continuous": False,
        "_verification_kind": "EMPIRICAL",
    }
    row.update(updates)
    return row


def test_runtime_statistics_exclude_censored_and_use_solve_time_only():
    rows = [
        _row(),
        _row(terminal_status="TIME_LIMIT", solve_time_sec=20.0),
        _row(terminal_status="FAILED", solve_time_sec=100.0),
        _row(terminal_status="PLANNED", solve_time_sec=None),
    ]
    result = report._runtime_rows(rows)[0]
    assert result["runtime_sample_count"] == 1
    assert result["runtime_median_sec"] == 4.0
    assert result["timeout_count"] == 1
    assert result["failure_count"] == 1
    assert result["excluded_rows"] == 3
    assert "COMPLETED" in result["filter"]
    assert rows[0]["wall_time_sec"] == 9.0


def test_verification_rates_keep_empirical_and_continuous_separate():
    rows = [
        _row(verification_kind="EMPIRICAL", _verification_kind="EMPIRICAL"),
        _row(verification_kind="CERTIFIED_CONTINUOUS",
             verification_continuous=True,
             _verification_kind="CERTIFIED_CONTINUOUS"),
    ]
    result = report._feasibility_rows(rows)
    by_kind = {row["verification_kind"]: row for row in result}
    assert by_kind["EMPIRICAL"]["empirical_verified_count"] == 1
    assert by_kind["EMPIRICAL"]["continuous_verified_count"] == 0
    assert by_kind["CERTIFIED_CONTINUOUS"]["empirical_verified_count"] == 0
    assert by_kind["CERTIFIED_CONTINUOUS"]["continuous_verified_count"] == 1


def test_minmaxsum_quality_stays_vector_and_exact_ratio_requires_proven_reference():
    ref = _row(
        algorithm="branch-and-bound",
        algorithm_category="exact_reference",
        reference_designated=True,
        selected_reference_backend="branch-and-bound",
        actual_backend="branch-and-bound",
        optimality_status="OPTIMAL",
        verification_kind="CERTIFIED_CONTINUOUS",
        verification_continuous=True,
        objective_value=10.0,
        _verification_kind="CERTIFIED_CONTINUOUS",
    )
    candidate = _row(objective_value=15.0)
    minmaxsum = _row(
        objective="minmaxsum",
        objective_value=None,
        peak_cost=8.0,
        integral_cost=13.0,
        objective_vector_status="COMPLETE",
    )
    assert report._quality_population([minmaxsum]) == []
    assert minmaxsum["peak_cost"] == 8.0
    assert minmaxsum["integral_cost"] == 13.0
    comparison = report._exact_reference_rows([ref, candidate])
    assert len(comparison) == 1
    assert comparison[0]["empirical_ratio_to_exact"] == 1.5
    assert "not a theoretical" in comparison[0]["ratio_semantics"]
    invalid_ref = dict(ref, verification_kind="EMPIRICAL",
                       _verification_kind="EMPIRICAL")
    assert report._exact_reference_rows([invalid_ref, candidate]) == []


def test_single_repeat_reporting_has_no_significance_claims():
    source = REPORT_PATH.read_text(encoding="utf-8").lower()
    assert "no significance tests" in source
    assert "single-run smoke comparison" in source


def test_dataset_profile_falls_back_to_validated_selection_manifest():
    assert report._dataset_profile(
        {"experiment_id": "smoke-plan"},
        {"profile": "smoke20"},
    ) == "smoke20"
    assert report._dataset_profile(
        {"dataset_profile": "full"},
        {"profile": "smoke20"},
    ) == "full"


def test_run_record_projection_keeps_objective_and_time_fields_distinct(tmp_path):
    experiment = tmp_path / "experiment"
    (experiment / "input").mkdir(parents=True)
    payload = {"name": "tiny", "T_end": 2.0, "stations": [{}],
               "trajectories": [{}]}
    (experiment / "input" / "tiny-abc.json").write_text(json.dumps(payload))
    plan = {
        "instance_id": "tiny-abc",
        "algorithm": "solver",
        "objective": "minmaxsum",
        "repeat": 0,
        "seed": 7,
        "run_key": "key",
        "requested_backend": "auto",
        "resolved_configuration": {
            "algorithm_category": "heuristic",
            "reference_designated": False,
        },
    }
    record = {
        "run_id": "run-1",
        "status": "COMPLETED",
        "result": {
            "feasible": True,
            "objective_value": None,
            "optimality_status": "FEASIBLE",
            "objective_vector": {"peak_cost": 3.0, "integral_cost": 5.0},
            "solver_result": {
                "wall_time_sec": 2.5,
                "solve_time_sec": 1.5,
                "verification_time_sec": .2,
                "serialization_time_sec": .1,
                "total_time_sec": 2.8,
            },
        },
        "verification": {
            "passed": True, "kind": "EMPIRICAL", "continuous": False,
        },
        "requested_backend": "auto",
        "selected_backend": None,
        "actual_backend": "native",
        "family": "tiny-family",
    }
    row = report._record_row(experiment, plan, record, {}, "branch-and-bound")
    assert row["solve_time_sec"] == 1.5
    assert row["wall_time_sec"] == 2.5
    assert row["verification_time_sec"] == .2
    assert row["serialization_time_sec"] == .1
    assert row["total_time_sec"] == 2.8
    assert row["objective_value"] is None
    assert row["peak_cost"] == 3.0
    assert row["integral_cost"] == 5.0
    assert row["verification_kind"] == "EMPIRICAL"


def test_figure_generation_only_emits_plots_for_valid_observations(tmp_path):
    rows = []
    for instance, family in (("i-1", "family-a"), ("i-2", "family-b")):
        for algorithm, value in (("greedy", 10.0), ("local-search", 8.0)):
            for objective in ("minmax", "minsum"):
                rows.append(_row(
                    algorithm=algorithm,
                    objective=objective,
                    instance_id=instance,
                    family=family,
                    n=20,
                    m=5,
                    solve_time_sec=value / 10,
                    terminal_status="COMPLETED",
                    objective_value=value if objective == "minmax" else value / 2,
                    verification_kind="CERTIFIED_CONTINUOUS",
                    verification_continuous=True,
                    _verification_kind="CERTIFIED_CONTINUOUS",
                ))
        for algorithm, peak, integral in (
            ("greedy", 9.0, 4.0), ("local-search", 8.0, 5.0),
        ):
            rows.append(_row(
                algorithm=algorithm,
                objective="minmaxsum",
                instance_id=instance,
                family=family,
                n=20,
                m=5,
                terminal_status="COMPLETED",
                objective_value=None,
                peak_cost=peak,
                integral_cost=integral,
                verification_kind="CERTIFIED_CONTINUOUS",
                verification_continuous=True,
                _verification_kind="CERTIFIED_CONTINUOUS",
            ))
    data = {"experiment": tmp_path, "rows": rows, "attempts": {}}
    statuses = report._plot_figures(data, dpi=80)
    by_name = {row["figure"]: row for row in statuses}
    for name in (
        "runtime_by_algorithm_objective",
        "runtime_distribution_by_algorithm",
        "objective_quality_minmax_minsum",
        "runtime_versus_quality",
        "per_instance_normalized_quality",
        "performance_vs_n_m",
        "reliability_comparison",
        "family_level_comparison",
        "minmaxsum_peak_integral",
        "minmaxsum_pareto",
    ):
        assert by_name[name]["generation_status"] == "GENERATED"
        for field in ("path_png", "path_pdf"):
            artifact = tmp_path / "figures" / by_name[name][field]
            assert artifact.is_file()
            assert artifact.stat().st_size > 0
    assert by_name["convergence_curves"]["generation_status"] == "NOT_GENERATED"
    assert "trace" in by_name["convergence_curves"]["reason"].lower()
