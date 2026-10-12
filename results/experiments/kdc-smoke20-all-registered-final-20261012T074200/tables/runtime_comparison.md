# Runtime Comparison

- **Purpose:** Summarize uncensored solve-time observations with planned, timeout and failure counts.
- **Experiment:** `kdc-smoke20-all-registered-final-20261012T074200`
- **Source manifest/result:** `validated canonical run records joined to the immutable plan`
- **Generation:** `scripts/report_experiment.py --stage tables`
- **Rows:** 33
- **File scope:** One row per algorithm-objective groups.
- **Sort order:** stable lexicographic order by the leading identity/group columns unless otherwise stated
- **Filters:** Runtime quantiles include only COMPLETED runs with explicit solve_time_sec; TIME_LIMIT/TIMED_OUT are censored and excluded.
- **Status counts:** not applicable or status column absent
- **Missing/null handling:** unavailable or inapplicable measurements remain blank/null; no unrelated metric is substituted.
- **Limitations:** With one repeat this is a single-run smoke comparison, not an inferential statistic or scaling claim.

## Derived metric formulas

Quantiles use NumPy linear interpolation over explicit solve_time_sec values from COMPLETED records only (Q1=.25, median=.5, Q3=.75, P95=.95). Minimum and maximum are observed extrema. Censored time limits and all other records are excluded from the runtime sample but retained in the planned/timeout/failure counts.

## Column definitions

- `algorithm` — Algorithm identifier from the immutable run plan.
- `algorithm_category` — Algorithm role from the resolved run configuration (for example heuristic, exact_reference, diagnostic).
- `objective` — Objective identifier; MinMaxSum remains a two-component vector.
- `planned_count` — Number of logical slots in the immutable run plan for the group.
- `terminal_count` — Number of planned slots with a canonical terminal run record.
- `runtime_sample_count` — Number of COMPLETED records with an explicit solve_time_sec.
- `runtime_median_sec` — Median solve_time_sec over the uncensored runtime population only.
- `runtime_q1_sec` — 25th percentile of uncensored solve_time_sec observations.
- `runtime_q3_sec` — 75th percentile of uncensored solve_time_sec observations.
- `runtime_p95_sec` — 95th percentile of uncensored solve_time_sec observations.
- `runtime_min_sec` — Minimum uncensored solve_time_sec observation.
- `runtime_max_sec` — Maximum uncensored solve_time_sec observation.
- `timeout_count` — Count of TIME_LIMIT or TIMED_OUT logical slots.
- `failure_count` — Count of FAILED, INVALID, ABANDONED or CANCELLED slots.
- `included_rows` — Population included in the reported statistic.
- `excluded_rows` — Planned rows excluded from the reported statistic.
- `filter` — Explicit population filter applied to the displayed statistic.

## Preview

| algorithm | algorithm_category | objective | planned_count | terminal_count | runtime_sample_count | runtime_median_sec | runtime_q1_sec | runtime_q3_sec | runtime_p95_sec | runtime_min_sec | runtime_max_sec | timeout_count | failure_count | included_rows | excluded_rows | filter |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| branch-and-bound | exact_reference | minmax | 20 | 20 | 10 | 0.0004913729999999999 | 0.0002890625 | 0.02337384475 | 0.06216440089999997 | 0.0001425 | 0.073683707 | 3 | 7 | 10 | 10 | COMPLETED with explicit solve_time_sec only; time-censored/failed/planned slots excluded |
| branch-and-bound | exact_reference | minmaxsum | 20 | 20 | 10 | 0.0024618125 | 0.00090010375 | 0.0905872285 | 0.27456133545 | 0.000544417 | 0.280491417 | 10 | 0 | 10 | 10 | COMPLETED with explicit solve_time_sec only; time-censored/failed/planned slots excluded |
| branch-and-bound | exact_reference | minsum | 20 | 20 | 10 | 0.0008262695 | 0.00029638475 | 0.046844980749999994 | 0.17925703169999993 | 0.00023462499999999997 | 0.20961357900000002 | 10 | 0 | 10 | 10 | COMPLETED with explicit solve_time_sec only; time-censored/failed/planned slots excluded |
| brute-force | diagnostic | minmax | 20 | 20 | 7 | 0.000456126 | 0.00023389650000000002 | 0.0007505820000000001 | 0.0009629918 | 0.000131248 | 0.0009740419999999999 | 0 | 13 | 7 | 13 | COMPLETED with explicit solve_time_sec only; time-censored/failed/planned slots excluded |
| brute-force | diagnostic | minmaxsum | 20 | 20 | 7 | 0.002285125 | 0.0006168125 | 0.0028751875 | 0.003986933199999999 | 0.000466625 | 0.004292833 | 0 | 13 | 7 | 13 | COMPLETED with explicit solve_time_sec only; time-censored/failed/planned slots excluded |
| brute-force | diagnostic | minsum | 20 | 20 | 7 | 0.0008562500000000001 | 0.00023779200000000002 | 0.001239499 | 0.0018557704999999994 | 0.000209375 | 0.0020209579999999998 | 0 | 13 | 7 | 13 | COMPLETED with explicit solve_time_sec only; time-censored/failed/planned slots excluded |
| genetic | heuristic | minmax | 20 | 20 | 17 | 0.11684833500000001 | 0.073647541 | 0.275027625 | 0.5356537165999997 | 0.037256668 | 0.7611250829999998 | 0 | 3 | 17 | 3 | COMPLETED with explicit solve_time_sec only; time-censored/failed/planned slots excluded |
| genetic | heuristic | minmaxsum | 20 | 20 | 19 | 0.733982583 | 0.2303174585 | 1.0534171875 | 1.471637071099999 | 0.10914875 | 1.839017333 | 0 | 1 | 19 | 1 | COMPLETED with explicit solve_time_sec only; time-censored/failed/planned slots excluded |
| genetic | heuristic | minsum | 20 | 20 | 20 | 0.36661537499999997 | 0.16533839625 | 0.5352834485 | 0.91013049465 | 0.08376879200000001 | 0.9789744989999999 | 0 | 0 | 20 | 0 | COMPLETED with explicit solve_time_sec only; time-censored/failed/planned slots excluded |
| greedy | heuristic | minmax | 20 | 20 | 15 | 0.014112333999999997 | 0.000399729 | 0.14606670700000002 | 0.2606160463999998 | 0.000210416 | 0.37220937299999995 | 0 | 5 | 15 | 5 | COMPLETED with explicit solve_time_sec only; time-censored/failed/planned slots excluded |
| greedy | heuristic | minmaxsum | 20 | 20 | 19 | 0.16596375 | 0.002618917 | 0.7465399994999999 | 1.1848973874999993 | 0.000488458 | 1.4447365 | 0 | 1 | 19 | 1 | COMPLETED with explicit solve_time_sec only; time-censored/failed/planned slots excluded |
| greedy | heuristic | minsum | 20 | 20 | 20 | 0.101019541 | 0.0007822912500000001 | 0.36178317675 | 0.6568348030000001 | 0.000210542 | 0.707694041 | 0 | 0 | 20 | 0 | COMPLETED with explicit solve_time_sec only; time-censored/failed/planned slots excluded |

Preview shows 12 of 33 rows; the delimited file contains the complete population.
