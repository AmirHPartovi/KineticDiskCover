# Complete Run Coverage

- **Purpose:** All immutable plan slots, including planned, skipped, failed and time-limited rows.
- **Experiment:** `kdc-smoke20-all-registered-final-20261012T074200`
- **Source manifest/result:** `experiment_manifest.json, runs/plan.jsonl, validated runs/<run-id>/run.json and selection_manifest.json`
- **Generation:** `scripts/report_experiment.py --stage tables`
- **Rows:** 660
- **File scope:** One row per Every unique run_key in runs/plan.jsonl; terminal records validated against run_record.schema.json..
- **Sort order:** stable lexicographic order by the leading identity/group columns unless otherwise stated
- **Filters:** No run-status filter. Missing canonical run records are retained as PLANNED.
- **Status counts:** COMPLETED=487, FAILED=51, INVALID=39, SKIPPED=60, TIME_LIMIT=23
- **Missing/null handling:** unavailable or inapplicable measurements remain blank/null; no unrelated metric is substituted.
- **Limitations:** Planned slots are not solver outcomes; null result fields remain unavailable.

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
- `solution_available` — True only when the canonical run declares an existing solution artifact.
- `solution_path` — Experiment-relative path to the actual run solution, when available.
- `error_reason` — Recorded run/result error or a grounded report status reason.

## Preview

| experiment_id | run_key | run_id | instance_id | family | n | m | algorithm | algorithm_category | objective | repeat | seed | terminal_status | feasible | optimality_status | verification_passed | verification_kind | verification_continuous | requested_backend | selected_backend | actual_backend | selected_reference_backend | reference_designated | solution_available | solution_path | error_reason |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| kdc-smoke20-all-registered-final-20261012T074200 | 2600a48ab5f118e398fbd2c1a234547139908f7d6463cbc2eb1cc23f93ec00f9 | run_20261012T042830Z__branch-and-bound__minmax__C-lower-90fec4bb6118-801fe3006e__r00__65943c0b5504 | C-lower-90fec4bb6118-801fe3006e | c-lower | 34 | 25 | branch-and-bound | exact_reference | minmax | 0 | 42 | INVALID | False | FAILED | False | NONE | False | auto | branch-and-bound | branch-and-bound | branch-and-bound | True | False |  | Terminal status INVALID; no additional solver error message was recorded. |
| kdc-smoke20-all-registered-final-20261012T074200 | cd4ede709ec52f333a39e333adab24c0bbcbf0d253f6deb1e44432f49c4724d5 | run_20261012T042830Z__brute-force__minmax__C-lower-90fec4bb6118-801fe3006e__r00__71feb2324191 | C-lower-90fec4bb6118-801fe3006e | c-lower | 34 | 25 | brute-force | diagnostic | minmax | 0 | 42 | FAILED | False | FAILED | False | NONE | False | auto | branch-and-bound | brute-force | branch-and-bound | False | False |  | Terminal status FAILED; no additional solver error message was recorded. |
| kdc-smoke20-all-registered-final-20261012T074200 | 0b4c9c617d2c7077573ac5949365466236044a0b18e3fa20286e9b780b4b3d4c | run_20261012T042830Z__genetic__minmax__C-lower-90fec4bb6118-801fe3006e__r00__efd46495f85c | C-lower-90fec4bb6118-801fe3006e | c-lower | 34 | 25 | genetic | heuristic | minmax | 0 | 42 | INVALID | False | FAILED | False | NONE | False | auto | branch-and-bound | genetic | branch-and-bound | False | False |  | Terminal status INVALID; no additional solver error message was recorded. |
| kdc-smoke20-all-registered-final-20261012T074200 | d262838e5be9ce400e1e0b65242ce748cef86d05b314fd6bdbc0ac3e2ef5fc22 | run_20261012T042831Z__greedy__minmax__C-lower-90fec4bb6118-801fe3006e__r00__1f0be2ec88ee | C-lower-90fec4bb6118-801fe3006e | c-lower | 34 | 25 | greedy | heuristic | minmax | 0 | 42 | INVALID | False | FAILED | False | NONE | False | auto | branch-and-bound | greedy | branch-and-bound | False | False |  | Terminal status INVALID; no additional solver error message was recorded. |
| kdc-smoke20-all-registered-final-20261012T074200 | 6c1d7a2122c5171181cd367326295128ae43e777927c7b133bb264db71e4f6de | run_20261012T042831Z__ip-kont__minmax__C-lower-90fec4bb6118-801fe3006e__r00__f92c0d7f6ad2 | C-lower-90fec4bb6118-801fe3006e | c-lower | 34 | 25 | ip-kont | exact_candidate | minmax | 0 | 42 | SKIPPED | False | FAILED | False | NONE | False | auto | branch-and-bound |  | branch-and-bound | False | False |  | {"message": "ip-kont is registered but the native KONT/COPT backend is unavailable", "reason_code": "BACKEND_UNAVAILABLE", "status": "SKIPPED"} |
| kdc-smoke20-all-registered-final-20261012T074200 | 6047fcdd14184a4224b125523da66e444a189bd49928c6e430e4050fb8cc559b | run_20261012T042831Z__local-search__minmax__C-lower-90fec4bb6118-801fe3006e__r00__43ff012000d1 | C-lower-90fec4bb6118-801fe3006e | c-lower | 34 | 25 | local-search | heuristic | minmax | 0 | 42 | INVALID | False | FAILED | False | NONE | False | auto | branch-and-bound | local-search | branch-and-bound | False | False |  | Terminal status INVALID; no additional solver error message was recorded. |
| kdc-smoke20-all-registered-final-20261012T074200 | 2368c97066fca51035462e58ef38593761a738511b3411a5b23dd6f547ab208b | run_20261012T042831Z__lp-rounding__minmax__C-lower-90fec4bb6118-801fe3006e__r00__f6b5619205de | C-lower-90fec4bb6118-801fe3006e | c-lower | 34 | 25 | lp-rounding | heuristic | minmax | 0 | 42 | INVALID | False | FAILED | False | NONE | False | auto | branch-and-bound | lp-rounding | branch-and-bound | False | False |  | Terminal status INVALID; no additional solver error message was recorded. |
| kdc-smoke20-all-registered-final-20261012T074200 | 58f95ac2f8809fe22430b3b22832a95d2bd9b97cf8e4bf9324d647dfc498ee6b | run_20261012T042831Z__nn__minmax__C-lower-90fec4bb6118-801fe3006e__r00__3af19fe04a08 | C-lower-90fec4bb6118-801fe3006e | c-lower | 34 | 25 | nn | heuristic | minmax | 0 | 42 | INVALID | False | FAILED | False | NONE | False | auto | branch-and-bound | nn | branch-and-bound | False | False |  | Terminal status INVALID; no additional solver error message was recorded. |
| kdc-smoke20-all-registered-final-20261012T074200 | 0add9a72af46706071723b20d06457074ac9041413c3ce6675f1348dbc6ea92e | run_20261012T042832Z__primal-dual__minmax__C-lower-90fec4bb6118-801fe3006e__r00__84487952e28e | C-lower-90fec4bb6118-801fe3006e | c-lower | 34 | 25 | primal-dual | heuristic | minmax | 0 | 42 | INVALID | False | FAILED | False | NONE | False | auto | branch-and-bound | primal-dual | branch-and-bound | False | False |  | Terminal status INVALID; no additional solver error message was recorded. |
| kdc-smoke20-all-registered-final-20261012T074200 | 51057de565c61c65a317d164e37f2587db850efd9967a8761cf0664d86e660b5 | run_20261012T042832Z__sa__minmax__C-lower-90fec4bb6118-801fe3006e__r00__6f5348f42586 | C-lower-90fec4bb6118-801fe3006e | c-lower | 34 | 25 | sa | heuristic | minmax | 0 | 42 | INVALID | False | FAILED | False | NONE | False | auto | branch-and-bound | sa | branch-and-bound | False | False |  | Terminal status INVALID; no additional solver error message was recorded. |
| kdc-smoke20-all-registered-final-20261012T074200 | 92a04e83824cc815b349ce46c381f10cdd4cfe563f114df73754b4825d6686ab | run_20261012T042832Z__shifting__minmax__C-lower-90fec4bb6118-801fe3006e__r00__09abb453691f | C-lower-90fec4bb6118-801fe3006e | c-lower | 34 | 25 | shifting | heuristic | minmax | 0 | 42 | INVALID | False | FAILED | False | NONE | False | auto | branch-and-bound | shifting | branch-and-bound | False | False |  | Terminal status INVALID; no additional solver error message was recorded. |
| kdc-smoke20-all-registered-final-20261012T074200 | 1d7680d3464a0965c50d5a0cde9e7c2382fac2d60c9482848acdf1781e89b85b | run_20261012T042833Z__branch-and-bound__minmaxsum__C-lower-90fec4bb6118-801fe3006e__r00__83d725c4a5f5 | C-lower-90fec4bb6118-801fe3006e | c-lower | 34 | 25 | branch-and-bound | exact_reference | minmaxsum | 0 | 42 | TIME_LIMIT | False | TIME_LIMIT | False | NONE | False | auto | branch-and-bound | branch-and-bound | branch-and-bound | True | False |  | Time limit reached; no additional solver message was recorded. |

Preview shows 12 of 660 rows; the delimited file contains the complete population.
