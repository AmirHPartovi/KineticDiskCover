# Feasibility Verification

- **Purpose:** Count feasibility and verification outcomes while separating empirical and continuous verification.
- **Experiment:** `kdc-smoke20-all-registered-final-20261012T074200`
- **Source manifest/result:** `validated canonical run records and immutable plan`
- **Generation:** `scripts/report_experiment.py --stage tables`
- **Rows:** 54
- **File scope:** One row per algorithm-objective-verification-kind groups.
- **Sort order:** stable lexicographic order by the leading identity/group columns unless otherwise stated
- **Filters:** Rates use terminal records only as denominator; planned slots are counted separately.
- **Status counts:** not applicable or status column absent
- **Missing/null handling:** unavailable or inapplicable measurements remain blank/null; no unrelated metric is substituted.
- **Limitations:** EMPIRICAL and CERTIFIED_CONTINUOUS are not combined into a single certificate category.

## Derived metric formulas

Each outcome rate is its corresponding count divided by terminal_count. Rates are null if terminal_count is zero. Verification kind is grouped separately.

## Column definitions

- `algorithm` — Algorithm identifier from the immutable run plan.
- `algorithm_category` — Algorithm role from the resolved run configuration (for example heuristic, exact_reference, diagnostic).
- `objective` — Objective identifier; MinMaxSum remains a two-component vector.
- `verification_kind` — Verification kind; EMPIRICAL is distinct from continuous verification.
- `planned_count` — Number of logical slots in the immutable run plan for the group.
- `terminal_count` — Number of planned slots with a canonical terminal run record.
- `feasible_count` — Count of terminal records with a recorded feasible result.
- `verified_count` — Count of terminal records whose recorded verification passed.
- `empirical_verified_count` — Count of passed EMPIRICAL verification records.
- `continuous_verified_count` — Count of passed CERTIFIED_CONTINUOUS records.
- `feasible_rate` — Feasible terminal runs divided by terminal run count; null if no terminal runs.
- `verified_rate` — Passed-verification terminal runs divided by terminal run count; null if no terminal runs.
- `empirical_verified_rate` — Passed EMPIRICAL terminal runs divided by terminal run count.
- `continuous_verified_rate` — Passed CERTIFIED_CONTINUOUS terminal runs divided by terminal run count.
- `included_rows` — Population included in the reported statistic.
- `excluded_rows` — Planned rows excluded from the reported statistic.

## Preview

| algorithm | algorithm_category | objective | verification_kind | planned_count | terminal_count | feasible_count | verified_count | empirical_verified_count | continuous_verified_count | feasible_rate | verified_rate | empirical_verified_rate | continuous_verified_rate | included_rows | excluded_rows |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| branch-and-bound | exact_reference | minmax | CERTIFIED_CONTINUOUS | 13 | 13 | 13 | 13 | 0 | 13 | 1.0 | 1.0 | 0.0 | 1.0 | 13 | 0 |
| branch-and-bound | exact_reference | minmax | NONE | 7 | 7 | 0 | 0 | 0 | 0 | 0.0 | 0.0 | 0.0 | 0.0 | 7 | 0 |
| branch-and-bound | exact_reference | minmaxsum | CERTIFIED_CONTINUOUS | 12 | 12 | 12 | 12 | 0 | 12 | 1.0 | 1.0 | 0.0 | 1.0 | 12 | 0 |
| branch-and-bound | exact_reference | minmaxsum | NONE | 8 | 8 | 0 | 0 | 0 | 0 | 0.0 | 0.0 | 0.0 | 0.0 | 8 | 0 |
| branch-and-bound | exact_reference | minsum | CERTIFIED_CONTINUOUS | 20 | 20 | 20 | 20 | 0 | 20 | 1.0 | 1.0 | 0.0 | 1.0 | 20 | 0 |
| brute-force | diagnostic | minmax | CERTIFIED_CONTINUOUS | 7 | 7 | 7 | 7 | 0 | 7 | 1.0 | 1.0 | 0.0 | 1.0 | 7 | 0 |
| brute-force | diagnostic | minmax | NONE | 13 | 13 | 0 | 0 | 0 | 0 | 0.0 | 0.0 | 0.0 | 0.0 | 13 | 0 |
| brute-force | diagnostic | minmaxsum | CERTIFIED_CONTINUOUS | 7 | 7 | 7 | 7 | 0 | 7 | 1.0 | 1.0 | 0.0 | 1.0 | 7 | 0 |
| brute-force | diagnostic | minmaxsum | NONE | 13 | 13 | 0 | 0 | 0 | 0 | 0.0 | 0.0 | 0.0 | 0.0 | 13 | 0 |
| brute-force | diagnostic | minsum | CERTIFIED_CONTINUOUS | 7 | 7 | 7 | 7 | 0 | 7 | 1.0 | 1.0 | 0.0 | 1.0 | 7 | 0 |
| brute-force | diagnostic | minsum | NONE | 13 | 13 | 0 | 0 | 0 | 0 | 0.0 | 0.0 | 0.0 | 0.0 | 13 | 0 |
| genetic | heuristic | minmax | CERTIFIED_CONTINUOUS | 17 | 17 | 17 | 17 | 0 | 17 | 1.0 | 1.0 | 0.0 | 1.0 | 17 | 0 |

Preview shows 12 of 54 rows; the delimited file contains the complete population.
