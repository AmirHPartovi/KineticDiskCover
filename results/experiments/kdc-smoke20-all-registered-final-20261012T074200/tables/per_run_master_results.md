# Per Run Master Results

- **Purpose:** Auditable result, timing, objective, bound and provenance fields for each planned logical run.
- **Experiment:** `kdc-smoke20-all-registered-final-20261012T074200`
- **Source manifest/result:** `experiment_manifest.json, runs/plan.jsonl, validated runs/<run-id>/run.json and selection_manifest.json`
- **Generation:** `scripts/report_experiment.py --stage tables`
- **Rows:** 660
- **File scope:** One row per All immutable plan slots joined to validated canonical run records by run_key..
- **Sort order:** stable lexicographic order by the leading identity/group columns unless otherwise stated
- **Filters:** No success-only filter. Each timing and gap field is copied only from its corresponding source field.
- **Status counts:** COMPLETED=487, FAILED=51, INVALID=39, SKIPPED=60, TIME_LIMIT=23
- **Missing/null handling:** unavailable or inapplicable measurements remain blank/null; no unrelated metric is substituted.
- **Limitations:** Empirical gaps and ratios are not approximation guarantees; MinMaxSum has no scalar objective value.

## Derived metric formulas

No metrics are calculated for this table; values are copied from the validated plan, run record, manifest, or artifact inventory.

## Column definitions

- `experiment_id` — The active experiment identifier; rows from other experiments are excluded.
- `run_key` — Stable logical run-plan slot identity.
- `run_id` — Immutable physical run identifier; null means the planned slot has not produced a run record.
- `instance_id` — Canonical selected-instance identifier.
- `family` — Family label copied from the validated smoke20 selection manifest.
- `n` — Number of moving trajectories (dimensionless count).
- `m` — Number of stations/disks (dimensionless count).
- `algorithm` — Algorithm identifier from the immutable run plan.
- `algorithm_category` — Algorithm role from the resolved run configuration (for example heuristic, exact_reference, diagnostic).
- `objective` — Objective identifier; MinMaxSum remains a two-component vector.
- `repeat` — Zero-based repeat index.
- `seed` — Random seed recorded by the run plan.
- `terminal_status` — Canonical run status; PLANNED means no terminal record exists.
- `feasible` — Recorded feasibility value; blank when no result record exists.
- `optimality_status` — Solver-reported proof/incumbent status; not inferred from run status.
- `verification_passed` — Recorded verification pass flag.
- `verification_kind` — Verification kind; EMPIRICAL is distinct from continuous verification.
- `verification_continuous` — Recorded continuous-verification boolean.
- `requested_backend` — Backend requested for the run.
- `selected_backend` — Reference/implementation backend selected during planning.
- `actual_backend` — Backend reported by the terminal run record.
- `selected_reference_backend` — One experiment-level selected exact-reference backend.
- `reference_designated` — Whether the plan designates this slot as the selected reference run.
- `solve_time_sec` — Solver-reported solve time in seconds; null is not replaced by wall time.
- `wall_time_sec` — Solver-reported wall time in seconds; distinct from solve time.
- `verification_time_sec` — Verification duration in seconds.
- `serialization_time_sec` — Serialization duration in seconds.
- `total_time_sec` — Reported total duration in seconds.
- `objective_value` — Scalar MinMax or MinSum value: MinMax is peak disk-area cost (area units); MinSum is integrated disk-area cost (area×time units). Null for MinMaxSum.
- `peak_cost` — Peak instantaneous disk-area cost over the horizon (area units); MinMaxSum vector component.
- `integral_cost` — Time integral of disk-area cost (area×time units); MinMaxSum vector component.
- `lower_bound` — Reported lower bound; not inferred from objective or upper-bound fields.
- `upper_bound` — Reported upper bound; not inferred from objective or lower-bound fields.
- `bound_status` — Solver-reported bound status.
- `bound_provenance` — Solver-reported provenance/type of the bound.
- `certified_gap` — Explicitly reported certified gap; never reconstructed from heuristic bounds.
- `heuristic_gap` — Explicitly reported heuristic gap; kept separate from certified gaps.
- `empirical_gap` — Explicitly reported empirical gap; kept separate from certified gaps.
- `empirical_ratio_to_exact` — Observed ratio to a valid proven exact reference; not a theoretical approximation ratio.
- `dominates_minmax` — Recorded dominance relation against the MinMax component result.
- `dominates_minsum` — Recorded dominance relation against the MinSum component result.
- `dominance_invariants_ok` — Recorded check of MinMaxSum dominance invariants.
- `solution_available` — True only when the canonical run declares an existing solution artifact.
- `solution_path` — Experiment-relative path to the actual run solution, when available.
- `error_reason` — Recorded run/result error or a grounded report status reason.

## Preview

| experiment_id | run_key | run_id | instance_id | family | n | m | algorithm | algorithm_category | objective | repeat | seed | terminal_status | feasible | optimality_status | verification_passed | verification_kind | verification_continuous | requested_backend | selected_backend | actual_backend | selected_reference_backend | reference_designated | solve_time_sec | wall_time_sec | verification_time_sec | serialization_time_sec | total_time_sec | objective_value | peak_cost | integral_cost | lower_bound | upper_bound | bound_status | bound_provenance | certified_gap | heuristic_gap | empirical_gap | empirical_ratio_to_exact | dominates_minmax | dominates_minsum | dominance_invariants_ok | solution_available | solution_path | error_reason |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| kdc-smoke20-all-registered-final-20261012T074200 | 2600a48ab5f118e398fbd2c1a234547139908f7d6463cbc2eb1cc23f93ec00f9 | run_20261012T042830Z__branch-and-bound__minmax__C-lower-90fec4bb6118-801fe3006e__r00__65943c0b5504 | C-lower-90fec4bb6118-801fe3006e | c-lower | 34 | 25 | branch-and-bound | exact_reference | minmax | 0 | 42 | INVALID | False | FAILED | False | NONE | False | auto | branch-and-bound | branch-and-bound | branch-and-bound | True | 0.0 | 2.252280375 | 0.0 | 5.6667e-05 |  | 0.0 | 0.0 | 0.0 | 0.0 |  | NONE |  |  | 0.0 |  |  |  |  |  | False |  | Terminal status INVALID; no additional solver error message was recorded. |
| kdc-smoke20-all-registered-final-20261012T074200 | cd4ede709ec52f333a39e333adab24c0bbcbf0d253f6deb1e44432f49c4724d5 | run_20261012T042830Z__brute-force__minmax__C-lower-90fec4bb6118-801fe3006e__r00__71feb2324191 | C-lower-90fec4bb6118-801fe3006e | c-lower | 34 | 25 | brute-force | diagnostic | minmax | 0 | 42 | FAILED | False | FAILED | False | NONE | False | auto | branch-and-bound | brute-force | branch-and-bound | False | 1.334e-06 | 0.00042091700000000004 | 0.0 | 0.00031183299999999997 |  | 0.0 | 0.0 | 0.0 | 0.0 |  | NONE |  |  | 0.0 |  |  |  |  |  | False |  | Terminal status FAILED; no additional solver error message was recorded. |
| kdc-smoke20-all-registered-final-20261012T074200 | 0b4c9c617d2c7077573ac5949365466236044a0b18e3fa20286e9b780b4b3d4c | run_20261012T042830Z__genetic__minmax__C-lower-90fec4bb6118-801fe3006e__r00__efd46495f85c | C-lower-90fec4bb6118-801fe3006e | c-lower | 34 | 25 | genetic | heuristic | minmax | 0 | 42 | INVALID | False | FAILED | False | NONE | False | auto | branch-and-bound | genetic | branch-and-bound | False | 0.0 | 0.21323345800000001 | 0.0 | 5.4041e-05 |  | 0.0 | 0.0 | 0.0 | 0.0 |  | NONE |  |  | 0.0 |  |  |  |  |  | False |  | Terminal status INVALID; no additional solver error message was recorded. |
| kdc-smoke20-all-registered-final-20261012T074200 | d262838e5be9ce400e1e0b65242ce748cef86d05b314fd6bdbc0ac3e2ef5fc22 | run_20261012T042831Z__greedy__minmax__C-lower-90fec4bb6118-801fe3006e__r00__1f0be2ec88ee | C-lower-90fec4bb6118-801fe3006e | c-lower | 34 | 25 | greedy | heuristic | minmax | 0 | 42 | INVALID | False | FAILED | False | NONE | False | auto | branch-and-bound | greedy | branch-and-bound | False | 0.0 | 0.163917126 | 0.0 | 5.3917e-05 |  | 0.0 | 0.0 | 0.0 | 0.0 |  | NONE |  |  | 0.0 |  |  |  |  |  | False |  | Terminal status INVALID; no additional solver error message was recorded. |
| kdc-smoke20-all-registered-final-20261012T074200 | 6c1d7a2122c5171181cd367326295128ae43e777927c7b133bb264db71e4f6de | run_20261012T042831Z__ip-kont__minmax__C-lower-90fec4bb6118-801fe3006e__r00__f92c0d7f6ad2 | C-lower-90fec4bb6118-801fe3006e | c-lower | 34 | 25 | ip-kont | exact_candidate | minmax | 0 | 42 | SKIPPED | False | FAILED | False | NONE | False | auto | branch-and-bound |  | branch-and-bound | False |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  |  | False |  | {"message": "ip-kont is registered but the native KONT/COPT backend is unavailable", "reason_code": "BACKEND_UNAVAILABLE", "status": "SKIPPED"} |
| kdc-smoke20-all-registered-final-20261012T074200 | 6047fcdd14184a4224b125523da66e444a189bd49928c6e430e4050fb8cc559b | run_20261012T042831Z__local-search__minmax__C-lower-90fec4bb6118-801fe3006e__r00__43ff012000d1 | C-lower-90fec4bb6118-801fe3006e | c-lower | 34 | 25 | local-search | heuristic | minmax | 0 | 42 | INVALID | False | FAILED | False | NONE | False | auto | branch-and-bound | local-search | branch-and-bound | False | 0.0 | 0.130252167 | 0.0 | 5.0625e-05 |  | 0.0 | 0.0 | 0.0 | 0.0 |  | NONE |  |  | 0.0 |  |  |  |  |  | False |  | Terminal status INVALID; no additional solver error message was recorded. |
| kdc-smoke20-all-registered-final-20261012T074200 | 2368c97066fca51035462e58ef38593761a738511b3411a5b23dd6f547ab208b | run_20261012T042831Z__lp-rounding__minmax__C-lower-90fec4bb6118-801fe3006e__r00__f6b5619205de | C-lower-90fec4bb6118-801fe3006e | c-lower | 34 | 25 | lp-rounding | heuristic | minmax | 0 | 42 | INVALID | False | FAILED | False | NONE | False | auto | branch-and-bound | lp-rounding | branch-and-bound | False | 0.0 | 0.205245249 | 0.0 | 5.2833e-05 |  | 0.0 | 0.0 | 0.0 | 0.0 |  | NONE |  |  | 0.0 |  |  |  |  |  | False |  | Terminal status INVALID; no additional solver error message was recorded. |
| kdc-smoke20-all-registered-final-20261012T074200 | 58f95ac2f8809fe22430b3b22832a95d2bd9b97cf8e4bf9324d647dfc498ee6b | run_20261012T042831Z__nn__minmax__C-lower-90fec4bb6118-801fe3006e__r00__3af19fe04a08 | C-lower-90fec4bb6118-801fe3006e | c-lower | 34 | 25 | nn | heuristic | minmax | 0 | 42 | INVALID | False | FAILED | False | NONE | False | auto | branch-and-bound | nn | branch-and-bound | False | 0.0 | 0.260567583 | 0.0 | 5.6375e-05 |  | 0.0 | 0.0 | 0.0 | 0.0 |  | NONE |  |  | 0.0 |  |  |  |  |  | False |  | Terminal status INVALID; no additional solver error message was recorded. |
| kdc-smoke20-all-registered-final-20261012T074200 | 0add9a72af46706071723b20d06457074ac9041413c3ce6675f1348dbc6ea92e | run_20261012T042832Z__primal-dual__minmax__C-lower-90fec4bb6118-801fe3006e__r00__84487952e28e | C-lower-90fec4bb6118-801fe3006e | c-lower | 34 | 25 | primal-dual | heuristic | minmax | 0 | 42 | INVALID | False | FAILED | False | NONE | False | auto | branch-and-bound | primal-dual | branch-and-bound | False | 0.0 | 0.138691417 | 0.0 | 5.5584e-05 |  | 0.0 | 0.0 | 0.0 | 0.0 |  | NONE |  |  | 0.0 |  |  |  |  |  | False |  | Terminal status INVALID; no additional solver error message was recorded. |
| kdc-smoke20-all-registered-final-20261012T074200 | 51057de565c61c65a317d164e37f2587db850efd9967a8761cf0664d86e660b5 | run_20261012T042832Z__sa__minmax__C-lower-90fec4bb6118-801fe3006e__r00__6f5348f42586 | C-lower-90fec4bb6118-801fe3006e | c-lower | 34 | 25 | sa | heuristic | minmax | 0 | 42 | INVALID | False | FAILED | False | NONE | False | auto | branch-and-bound | sa | branch-and-bound | False | 0.0 | 0.149650792 | 0.0 | 5.9375e-05 |  | 0.0 | 0.0 | 0.0 | 0.0 |  | NONE |  |  | 0.0 |  |  |  |  |  | False |  | Terminal status INVALID; no additional solver error message was recorded. |
| kdc-smoke20-all-registered-final-20261012T074200 | 92a04e83824cc815b349ce46c381f10cdd4cfe563f114df73754b4825d6686ab | run_20261012T042832Z__shifting__minmax__C-lower-90fec4bb6118-801fe3006e__r00__09abb453691f | C-lower-90fec4bb6118-801fe3006e | c-lower | 34 | 25 | shifting | heuristic | minmax | 0 | 42 | INVALID | False | FAILED | False | NONE | False | auto | branch-and-bound | shifting | branch-and-bound | False | 0.0 | 0.387793374 | 0.0 | 5.1291e-05 |  | 0.0 | 0.0 | 0.0 | 0.0 |  | NONE |  |  | 0.0 |  |  |  |  |  | False |  | Terminal status INVALID; no additional solver error message was recorded. |
| kdc-smoke20-all-registered-final-20261012T074200 | 1d7680d3464a0965c50d5a0cde9e7c2382fac2d60c9482848acdf1781e89b85b | run_20261012T042833Z__branch-and-bound__minmaxsum__C-lower-90fec4bb6118-801fe3006e__r00__83d725c4a5f5 | C-lower-90fec4bb6118-801fe3006e | c-lower | 34 | 25 | branch-and-bound | exact_reference | minmaxsum | 0 | 42 | TIME_LIMIT | False | TIME_LIMIT | False | NONE | False | auto | branch-and-bound | branch-and-bound | branch-and-bound | True | 4.53745775 | 4.538315165999999 | 0.0 | 0.0005590829999999999 |  |  |  |  |  |  | NONE |  |  |  |  |  | False | False | False | False |  | Time limit reached; no additional solver message was recorded. |

Preview shows 12 of 660 rows; the delimited file contains the complete population.
