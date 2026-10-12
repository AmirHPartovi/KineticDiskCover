# Per Instance Run Rows

- **Purpose:** Provide a lossless long-form algorithm-by-objective row for every selected instance and preserve unsuccessful outcomes.
- **Experiment:** `kdc-smoke20-all-registered-final-20261012T074200`
- **Source manifest/result:** `validated smoke20 selection manifest, run plan and canonical run records`
- **Generation:** `scripts/report_experiment.py --stage tables`
- **Rows:** 660
- **File scope:** One row per all selected-instance/algorithm/objective/repeat plan slots.
- **Sort order:** stable lexicographic order by the leading identity/group columns unless otherwise stated
- **Filters:** No status filter; family labels come from the validated selection manifest.
- **Status counts:** COMPLETED=487, FAILED=51, INVALID=39, SKIPPED=60, TIME_LIMIT=23
- **Missing/null handling:** unavailable or inapplicable measurements remain blank/null; no unrelated metric is substituted.
- **Limitations:** Raw objective magnitudes are not directly comparable across differently scaled instances.

## Derived metric formulas

No metrics are calculated for this table; values are copied from the validated plan, run record, manifest, or artifact inventory.

## Column definitions

- `instance_id` — Canonical selected-instance identifier.
- `family` — Family label copied from the validated smoke20 selection manifest.
- `n` — Number of moving trajectories (dimensionless count).
- `m` — Number of stations/disks (dimensionless count).
- `algorithm` — Algorithm identifier from the immutable run plan.
- `algorithm_category` — Algorithm role from the resolved run configuration (for example heuristic, exact_reference, diagnostic).
- `objective` — Objective identifier; MinMaxSum remains a two-component vector.
- `terminal_status` — Canonical run status; PLANNED means no terminal record exists.
- `feasible` — Recorded feasibility value; blank when no result record exists.
- `verification_kind` — Verification kind; EMPIRICAL is distinct from continuous verification.
- `optimality_status` — Solver-reported proof/incumbent status; not inferred from run status.
- `objective_value` — Scalar MinMax or MinSum value: MinMax is peak disk-area cost (area units); MinSum is integrated disk-area cost (area×time units). Null for MinMaxSum.
- `peak_cost` — Peak instantaneous disk-area cost over the horizon (area units); MinMaxSum vector component.
- `integral_cost` — Time integral of disk-area cost (area×time units); MinMaxSum vector component.
- `solve_time_sec` — Solver-reported solve time in seconds; null is not replaced by wall time.
- `actual_backend` — Backend reported by the terminal run record.
- `run_id` — Immutable physical run identifier; null means the planned slot has not produced a run record.
- `run_key` — Stable logical run-plan slot identity.

## Preview

| instance_id | family | n | m | algorithm | algorithm_category | objective | terminal_status | feasible | verification_kind | optimality_status | objective_value | peak_cost | integral_cost | solve_time_sec | actual_backend | run_id | run_key |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| C-lower-90fec4bb6118-801fe3006e | c-lower | 34 | 25 | branch-and-bound | exact_reference | minmax | INVALID | False | NONE | FAILED | 0.0 | 0.0 | 0.0 | 0.0 | branch-and-bound | run_20261012T042830Z__branch-and-bound__minmax__C-lower-90fec4bb6118-801fe3006e__r00__65943c0b5504 | 2600a48ab5f118e398fbd2c1a234547139908f7d6463cbc2eb1cc23f93ec00f9 |
| C-lower-90fec4bb6118-801fe3006e | c-lower | 34 | 25 | brute-force | diagnostic | minmax | FAILED | False | NONE | FAILED | 0.0 | 0.0 | 0.0 | 1.334e-06 | brute-force | run_20261012T042830Z__brute-force__minmax__C-lower-90fec4bb6118-801fe3006e__r00__71feb2324191 | cd4ede709ec52f333a39e333adab24c0bbcbf0d253f6deb1e44432f49c4724d5 |
| C-lower-90fec4bb6118-801fe3006e | c-lower | 34 | 25 | genetic | heuristic | minmax | INVALID | False | NONE | FAILED | 0.0 | 0.0 | 0.0 | 0.0 | genetic | run_20261012T042830Z__genetic__minmax__C-lower-90fec4bb6118-801fe3006e__r00__efd46495f85c | 0b4c9c617d2c7077573ac5949365466236044a0b18e3fa20286e9b780b4b3d4c |
| C-lower-90fec4bb6118-801fe3006e | c-lower | 34 | 25 | greedy | heuristic | minmax | INVALID | False | NONE | FAILED | 0.0 | 0.0 | 0.0 | 0.0 | greedy | run_20261012T042831Z__greedy__minmax__C-lower-90fec4bb6118-801fe3006e__r00__1f0be2ec88ee | d262838e5be9ce400e1e0b65242ce748cef86d05b314fd6bdbc0ac3e2ef5fc22 |
| C-lower-90fec4bb6118-801fe3006e | c-lower | 34 | 25 | ip-kont | exact_candidate | minmax | SKIPPED | False | NONE | FAILED |  |  |  |  |  | run_20261012T042831Z__ip-kont__minmax__C-lower-90fec4bb6118-801fe3006e__r00__f92c0d7f6ad2 | 6c1d7a2122c5171181cd367326295128ae43e777927c7b133bb264db71e4f6de |
| C-lower-90fec4bb6118-801fe3006e | c-lower | 34 | 25 | local-search | heuristic | minmax | INVALID | False | NONE | FAILED | 0.0 | 0.0 | 0.0 | 0.0 | local-search | run_20261012T042831Z__local-search__minmax__C-lower-90fec4bb6118-801fe3006e__r00__43ff012000d1 | 6047fcdd14184a4224b125523da66e444a189bd49928c6e430e4050fb8cc559b |
| C-lower-90fec4bb6118-801fe3006e | c-lower | 34 | 25 | lp-rounding | heuristic | minmax | INVALID | False | NONE | FAILED | 0.0 | 0.0 | 0.0 | 0.0 | lp-rounding | run_20261012T042831Z__lp-rounding__minmax__C-lower-90fec4bb6118-801fe3006e__r00__f6b5619205de | 2368c97066fca51035462e58ef38593761a738511b3411a5b23dd6f547ab208b |
| C-lower-90fec4bb6118-801fe3006e | c-lower | 34 | 25 | nn | heuristic | minmax | INVALID | False | NONE | FAILED | 0.0 | 0.0 | 0.0 | 0.0 | nn | run_20261012T042831Z__nn__minmax__C-lower-90fec4bb6118-801fe3006e__r00__3af19fe04a08 | 58f95ac2f8809fe22430b3b22832a95d2bd9b97cf8e4bf9324d647dfc498ee6b |
| C-lower-90fec4bb6118-801fe3006e | c-lower | 34 | 25 | primal-dual | heuristic | minmax | INVALID | False | NONE | FAILED | 0.0 | 0.0 | 0.0 | 0.0 | primal-dual | run_20261012T042832Z__primal-dual__minmax__C-lower-90fec4bb6118-801fe3006e__r00__84487952e28e | 0add9a72af46706071723b20d06457074ac9041413c3ce6675f1348dbc6ea92e |
| C-lower-90fec4bb6118-801fe3006e | c-lower | 34 | 25 | sa | heuristic | minmax | INVALID | False | NONE | FAILED | 0.0 | 0.0 | 0.0 | 0.0 | sa | run_20261012T042832Z__sa__minmax__C-lower-90fec4bb6118-801fe3006e__r00__6f5348f42586 | 51057de565c61c65a317d164e37f2587db850efd9967a8761cf0664d86e660b5 |
| C-lower-90fec4bb6118-801fe3006e | c-lower | 34 | 25 | shifting | heuristic | minmax | INVALID | False | NONE | FAILED | 0.0 | 0.0 | 0.0 | 0.0 | shifting | run_20261012T042832Z__shifting__minmax__C-lower-90fec4bb6118-801fe3006e__r00__09abb453691f | 92a04e83824cc815b349ce46c381f10cdd4cfe563f114df73754b4825d6686ab |
| C-lower-90fec4bb6118-801fe3006e | c-lower | 34 | 25 | branch-and-bound | exact_reference | minmaxsum | TIME_LIMIT | False | NONE | TIME_LIMIT |  |  |  | 4.53745775 | branch-and-bound | run_20261012T042833Z__branch-and-bound__minmaxsum__C-lower-90fec4bb6118-801fe3006e__r00__83d725c4a5f5 | 1d7680d3464a0965c50d5a0cde9e7c2382fac2d60c9482848acdf1781e89b85b |

Preview shows 12 of 660 rows; the delimited file contains the complete population.
