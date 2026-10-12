# Per Family Comparison

- **Purpose:** Summarize objective/runtime and reliability by the selection manifest's family labels.
- **Experiment:** `kdc-smoke20-all-registered-final-20261012T074200`
- **Source manifest/result:** `validated selection manifest and canonical run records`
- **Generation:** `scripts/report_experiment.py --stage tables`
- **Rows:** 660
- **File scope:** One row per family-algorithm-objective groups.
- **Sort order:** stable lexicographic order by the leading identity/group columns unless otherwise stated
- **Filters:** Quality uses feasible, verification-passed scalar outcomes; runtime uses COMPLETED explicit solve_time_sec.
- **Status counts:** not applicable or status column absent
- **Missing/null handling:** unavailable or inapplicable measurements remain blank/null; no unrelated metric is substituted.
- **Limitations:** Smoke20 family groups are small; results are descriptive and not evidence of population scaling.

## Derived metric formulas

Objective and runtime medians are NumPy linear-interpolated .5 quantiles of their separately filtered samples; outcome columns are raw counts.

## Column definitions

- `family` — Family label copied from the validated smoke20 selection manifest.
- `algorithm` — Algorithm identifier from the immutable run plan.
- `algorithm_category` — Algorithm role from the resolved run configuration (for example heuristic, exact_reference, diagnostic).
- `objective` — Objective identifier; MinMaxSum remains a two-component vector.
- `planned_count` — Number of logical slots in the immutable run plan for the group.
- `terminal_count` — Number of planned slots with a canonical terminal run record.
- `valid_objective_count` — Feasible, verification-passed rows with an available scalar objective.
- `objective_median` — Median feasible, verification-passed objective; reports are descriptive single-run smoke comparisons.
- `runtime_sample_count` — Number of COMPLETED records with an explicit solve_time_sec.
- `runtime_median_sec` — Median solve_time_sec over the uncensored runtime population only.
- `feasible_count` — Count of terminal records with a recorded feasible result.
- `verified_count` — Count of terminal records whose recorded verification passed.
- `timeout_count` — Count of TIME_LIMIT or TIMED_OUT logical slots.
- `failure_count` — Count of FAILED, INVALID, ABANDONED or CANCELLED slots.
- `included_rows` — Population included in the reported statistic.
- `excluded_rows` — Planned rows excluded from the reported statistic.
- `filter` — Explicit population filter applied to the displayed statistic.

## Preview

| family | algorithm | algorithm_category | objective | planned_count | terminal_count | valid_objective_count | objective_median | runtime_sample_count | runtime_median_sec | feasible_count | verified_count | timeout_count | failure_count | included_rows | excluded_rows | filter |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| att48 | branch-and-bound | exact_reference | minmax | 1 | 1 | 1 | 2318.289942578307 | 1 | 0.073683707 | 1 | 1 | 0 | 0 | 1 | 0 | family labels from validated selection manifest; quality uses feasible, verified scalar rows |
| att48 | branch-and-bound | exact_reference | minmaxsum | 1 | 1 | 0 |  | 1 | 0.267313458 | 1 | 1 | 0 | 0 | 0 | 1 | family labels from validated selection manifest; quality uses feasible, verified scalar rows |
| att48 | branch-and-bound | exact_reference | minsum | 1 | 1 | 1 | 1312.3028836964759 | 1 | 0.142154585 | 1 | 1 | 0 | 0 | 1 | 0 | family labels from validated selection manifest; quality uses feasible, verified scalar rows |
| att48 | brute-force | diagnostic | minmax | 1 | 1 | 0 |  | 0 |  | 0 | 0 | 0 | 1 | 0 | 1 | family labels from validated selection manifest; quality uses feasible, verified scalar rows |
| att48 | brute-force | diagnostic | minmaxsum | 1 | 1 | 0 |  | 0 |  | 0 | 0 | 0 | 1 | 0 | 1 | family labels from validated selection manifest; quality uses feasible, verified scalar rows |
| att48 | brute-force | diagnostic | minsum | 1 | 1 | 0 |  | 0 |  | 0 | 0 | 0 | 1 | 0 | 1 | family labels from validated selection manifest; quality uses feasible, verified scalar rows |
| att48 | genetic | heuristic | minmax | 1 | 1 | 1 | 2455.7713594478446 | 1 | 0.11684833500000001 | 1 | 1 | 0 | 0 | 1 | 0 | family labels from validated selection manifest; quality uses feasible, verified scalar rows |
| att48 | genetic | heuristic | minmaxsum | 1 | 1 | 0 |  | 1 | 0.480207125 | 1 | 1 | 0 | 0 | 0 | 1 | family labels from validated selection manifest; quality uses feasible, verified scalar rows |
| att48 | genetic | heuristic | minsum | 1 | 1 | 1 | 1429.3910185169443 | 1 | 0.304575794 | 1 | 1 | 0 | 0 | 1 | 0 | family labels from validated selection manifest; quality uses feasible, verified scalar rows |
| att48 | greedy | heuristic | minmax | 1 | 1 | 1 | 2675.3745457835635 | 1 | 0.014112333999999997 | 1 | 1 | 0 | 0 | 1 | 0 | family labels from validated selection manifest; quality uses feasible, verified scalar rows |
| att48 | greedy | heuristic | minmaxsum | 1 | 1 | 0 |  | 1 | 0.070082292 | 1 | 1 | 0 | 0 | 0 | 1 | family labels from validated selection manifest; quality uses feasible, verified scalar rows |
| att48 | greedy | heuristic | minsum | 1 | 1 | 1 | 2269.1212957959656 | 1 | 0.035483293 | 1 | 1 | 0 | 0 | 1 | 0 | family labels from validated selection manifest; quality uses feasible, verified scalar rows |

Preview shows 12 of 660 rows; the delimited file contains the complete population.
