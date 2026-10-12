# Smoke20 Comparative Tables

- Experiment: `kdc-smoke20-all-registered-final-20261012T074200`
- Planned logical runs: 660
- Terminal canonical records: 660
- Status counts: `{"COMPLETED": 487, "FAILED": 51, "INVALID": 39, "SKIPPED": 60, "TIME_LIMIT": 23}`
- Population: this experiment's validated run plan and canonical run records only.
- One repeat is a single-run smoke comparison; no significance or general superiority claim is made.
- Timing quantiles exclude censored runs and use explicit `solve_time_sec` only.
- MinMaxSum remains `(peak_cost, integral_cost)`; no scalarization is used.

Every CSV has an adjacent Markdown data dictionary, bounded preview, source population and metric definitions.

## Tables

- [Complete Run Coverage](complete_run_coverage.md) · [CSV](complete_run_coverage.csv)
- [Per Run Master Results](per_run_master_results.md) · [CSV](per_run_master_results.csv)
- [Runtime Comparison](runtime_comparison.md) · [CSV](runtime_comparison.csv)
- [Objective Quality](objective_quality.md) · [CSV](objective_quality.csv)
- [Minmaxsum Components](minmaxsum_components.md) · [CSV](minmaxsum_components.csv)
- [Feasibility Verification](feasibility_verification.md) · [CSV](feasibility_verification.csv)
- [Failures Timeouts Backends](failures_timeouts_backends.md) · [CSV](failures_timeouts_backends.csv)
- [Per Instance Comparison](per_instance_comparison.md) · [CSV](per_instance_comparison.csv)
- [Per Instance Run Rows](per_instance_run_rows.md) · [CSV](per_instance_run_rows.csv)
- [Per Family Comparison](per_family_comparison.md) · [CSV](per_family_comparison.csv)
- [Exact Reference Comparison](exact_reference_comparison.md) · [CSV](exact_reference_comparison.csv)
- [Bounds And Gaps](bounds_and_gaps.md) · [CSV](bounds_and_gaps.csv)
- [Completeness Artifacts](completeness_artifacts.md) · [CSV](completeness_artifacts.csv)

## Source artifacts

- [Run plan](../runs/plan.jsonl)
- [Experiment manifest](../experiment_manifest.json)
- [Validated selection manifest](../dataset/selection_manifest)
- [Visualization manifest](../reports/visualization_manifest.json)
