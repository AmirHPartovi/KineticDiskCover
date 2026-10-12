# Objective Quality

- **Purpose:** Summarize feasible, verification-passed scalar MinMax and MinSum values.
- **Experiment:** `kdc-smoke20-all-registered-final-20261012T074200`
- **Source manifest/result:** `validated canonical run records`
- **Generation:** `scripts/report_experiment.py --stage tables`
- **Rows:** 22
- **File scope:** One row per algorithm-objective groups.
- **Sort order:** stable lexicographic order by the leading identity/group columns unless otherwise stated
- **Filters:** Only MinMax/MinSum rows with feasible=true, verification_passed=true and finite objective_value.
- **Status counts:** not applicable or status column absent
- **Missing/null handling:** unavailable or inapplicable measurements remain blank/null; no unrelated metric is substituted.
- **Limitations:** Verification kind remains separately reported; descriptive smoke20 results do not establish universal algorithm superiority.

## Derived metric formulas

objective_median is the NumPy linear-interpolated .5 quantile of included objective_value observations. included_rows is the filtered finite-observation count; excluded_rows=planned_count-included_rows.

## Column definitions

- `algorithm` — Algorithm identifier from the immutable run plan.
- `algorithm_category` — Algorithm role from the resolved run configuration (for example heuristic, exact_reference, diagnostic).
- `objective` — Objective identifier; MinMaxSum remains a two-component vector.
- `planned_count` — Number of logical slots in the immutable run plan for the group.
- `terminal_count` — Number of planned slots with a canonical terminal run record.
- `valid_objective_count` — Feasible, verification-passed rows with an available scalar objective.
- `objective_median` — Median feasible, verification-passed objective; reports are descriptive single-run smoke comparisons.
- `included_rows` — Population included in the reported statistic.
- `excluded_rows` — Planned rows excluded from the reported statistic.
- `filter` — Explicit population filter applied to the displayed statistic.

## Preview

| algorithm | algorithm_category | objective | planned_count | terminal_count | valid_objective_count | objective_median | included_rows | excluded_rows | filter |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| branch-and-bound | exact_reference | minmax | 20 | 20 | 13 | 1781.0715122467982 | 13 | 7 | feasible, verification-passed MinMax/MinSum records with finite scalar objective; verification kind retained per run |
| branch-and-bound | exact_reference | minsum | 20 | 20 | 20 | 2388.568945943133 | 20 | 0 | feasible, verification-passed MinMax/MinSum records with finite scalar objective; verification kind retained per run |
| brute-force | diagnostic | minmax | 20 | 20 | 7 | 1072.0043652792724 | 7 | 13 | feasible, verification-passed MinMax/MinSum records with finite scalar objective; verification kind retained per run |
| brute-force | diagnostic | minsum | 20 | 20 | 7 | 385.48355263763096 | 7 | 13 | feasible, verification-passed MinMax/MinSum records with finite scalar objective; verification kind retained per run |
| genetic | heuristic | minmax | 20 | 20 | 17 | 2455.7713594478446 | 17 | 3 | feasible, verification-passed MinMax/MinSum records with finite scalar objective; verification kind retained per run |
| genetic | heuristic | minsum | 20 | 20 | 20 | 2445.291875849447 | 20 | 0 | feasible, verification-passed MinMax/MinSum records with finite scalar objective; verification kind retained per run |
| greedy | heuristic | minmax | 20 | 20 | 15 | 2675.3745457835635 | 15 | 5 | feasible, verification-passed MinMax/MinSum records with finite scalar objective; verification kind retained per run |
| greedy | heuristic | minsum | 20 | 20 | 20 | 3548.0618451427404 | 20 | 0 | feasible, verification-passed MinMax/MinSum records with finite scalar objective; verification kind retained per run |
| ip-kont | exact_candidate | minmax | 20 | 20 | 0 |  | 0 | 20 | feasible, verification-passed MinMax/MinSum records with finite scalar objective; verification kind retained per run |
| ip-kont | exact_candidate | minsum | 20 | 20 | 0 |  | 0 | 20 | feasible, verification-passed MinMax/MinSum records with finite scalar objective; verification kind retained per run |
| local-search | heuristic | minmax | 20 | 20 | 16 | 2905.1439967570677 | 16 | 4 | feasible, verification-passed MinMax/MinSum records with finite scalar objective; verification kind retained per run |
| local-search | heuristic | minsum | 20 | 20 | 20 | 2495.1731194507247 | 20 | 0 | feasible, verification-passed MinMax/MinSum records with finite scalar objective; verification kind retained per run |

Preview shows 12 of 22 rows; the delimited file contains the complete population.
