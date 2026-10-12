# Comparative Figures

- Experiment: `kdc-smoke20-all-registered-final-20261012T074200`
- Source: this experiment's validated selection, run plan and canonical run records only.
- Interpretation: one repeat is a single-run smoke comparison; no statistical significance or general superiority is claimed.
- Censored TIME_LIMIT/TIMED_OUT durations are not plotted as uncensored runtime observations.
- MinMaxSum is shown only as the `(peak_cost, integral_cost)` vector.
- Missing data are recorded as NOT_GENERATED; no empty placeholder chart is represented as a result.

| Requested figure | Status | Artifact | Explanation |
|---|---|---|---|
| runtime_by_algorithm_objective | GENERATED | [PNG](report_runtime_by_algorithm_objective.png) · [PDF](report_runtime_by_algorithm_objective.pdf) |  |
| runtime_distribution_by_algorithm | GENERATED | [PNG](report_runtime_distribution_by_algorithm.png) · [PDF](report_runtime_distribution_by_algorithm.pdf) |  |
| objective_quality_minmax_minsum | GENERATED | [PNG](report_objective_quality_minmax_minsum.png) · [PDF](report_objective_quality_minmax_minsum.pdf) |  |
| runtime_versus_quality | GENERATED | [PNG](report_runtime_versus_quality.png) · [PDF](report_runtime_versus_quality.pdf) |  |
| per_instance_normalized_quality | GENERATED | [PNG](report_per_instance_normalized_quality.png) · [PDF](report_per_instance_normalized_quality.pdf) |  |
| performance_vs_n_m | GENERATED | [PNG](report_performance_vs_n_m.png) · [PDF](report_performance_vs_n_m.pdf) |  |
| reliability_comparison | GENERATED | [PNG](report_reliability_comparison.png) · [PDF](report_reliability_comparison.pdf) |  |
| family_level_comparison | GENERATED | [PNG](report_family_level_comparison.png) · [PDF](report_family_level_comparison.pdf) |  |
| convergence_curves | NOT_GENERATED | — | No valid run-associated trace CSV files with finite supported x/y columns are present. 600 declared trace file(s) were missing or invalid. |
| minmaxsum_peak_integral | GENERATED | [PNG](report_minmaxsum_peak_integral.png) · [PDF](report_minmaxsum_peak_integral.pdf) |  |
| minmaxsum_pareto | GENERATED | [PNG](report_minmaxsum_pareto.png) · [PDF](report_minmaxsum_pareto.pdf) |  |
| three_time_solution_figures | NOT_GENERATED | — | No actual renderable run solution snapshots; plan-only slots are not visualized as solver results. |

The Prompt 6 per-instance/objective comparison sheets and solution snapshots, when present, are indexed in [the visualization manifest](../reports/visualization_manifest.json).
