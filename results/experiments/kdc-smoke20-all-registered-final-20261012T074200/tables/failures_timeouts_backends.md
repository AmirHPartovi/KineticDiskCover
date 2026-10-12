# Failures Timeouts Backends

- **Purpose:** Retain all non-completed statuses, backend provenance, and recorded or grounded reasons.
- **Experiment:** `kdc-smoke20-all-registered-final-20261012T074200`
- **Source manifest/result:** `validated canonical run records and immutable plan`
- **Generation:** `scripts/report_experiment.py --stage tables`
- **Rows:** 173
- **File scope:** One row per planned slots with status other than COMPLETED.
- **Sort order:** stable lexicographic order by the leading identity/group columns unless otherwise stated
- **Filters:** Includes PLANNED, TIME_LIMIT, TIMED_OUT, FAILED, INVALID, SKIPPED, CANCELLED and ABANDONED.
- **Status counts:** FAILED=51, INVALID=39, SKIPPED=60, TIME_LIMIT=23
- **Missing/null handling:** unavailable or inapplicable measurements remain blank/null; no unrelated metric is substituted.
- **Limitations:** A planned row has no terminal reason; it is explicitly identified as lacking a terminal run record.

## Derived metric formulas

No metrics are calculated for this table; values are copied from the validated plan, run record, manifest, or artifact inventory.

## Column definitions

- `run_key` — Stable logical run-plan slot identity.
- `run_id` — Immutable physical run identifier; null means the planned slot has not produced a run record.
- `instance_id` — Canonical selected-instance identifier.
- `family` — Family label copied from the validated smoke20 selection manifest.
- `algorithm` — Algorithm identifier from the immutable run plan.
- `algorithm_category` — Algorithm role from the resolved run configuration (for example heuristic, exact_reference, diagnostic).
- `objective` — Objective identifier; MinMaxSum remains a two-component vector.
- `requested_backend` — Backend requested for the run.
- `selected_backend` — Reference/implementation backend selected during planning.
- `actual_backend` — Backend reported by the terminal run record.
- `terminal_status` — Canonical run status; PLANNED means no terminal record exists.
- `error_reason` — Recorded run/result error or a grounded report status reason.

## Preview

| run_key | run_id | instance_id | family | algorithm | algorithm_category | objective | requested_backend | selected_backend | actual_backend | terminal_status | error_reason |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 2600a48ab5f118e398fbd2c1a234547139908f7d6463cbc2eb1cc23f93ec00f9 | run_20261012T042830Z__branch-and-bound__minmax__C-lower-90fec4bb6118-801fe3006e__r00__65943c0b5504 | C-lower-90fec4bb6118-801fe3006e | c-lower | branch-and-bound | exact_reference | minmax | auto | branch-and-bound | branch-and-bound | INVALID | Terminal status INVALID; no additional solver error message was recorded. |
| cd4ede709ec52f333a39e333adab24c0bbcbf0d253f6deb1e44432f49c4724d5 | run_20261012T042830Z__brute-force__minmax__C-lower-90fec4bb6118-801fe3006e__r00__71feb2324191 | C-lower-90fec4bb6118-801fe3006e | c-lower | brute-force | diagnostic | minmax | auto | branch-and-bound | brute-force | FAILED | Terminal status FAILED; no additional solver error message was recorded. |
| 0b4c9c617d2c7077573ac5949365466236044a0b18e3fa20286e9b780b4b3d4c | run_20261012T042830Z__genetic__minmax__C-lower-90fec4bb6118-801fe3006e__r00__efd46495f85c | C-lower-90fec4bb6118-801fe3006e | c-lower | genetic | heuristic | minmax | auto | branch-and-bound | genetic | INVALID | Terminal status INVALID; no additional solver error message was recorded. |
| d262838e5be9ce400e1e0b65242ce748cef86d05b314fd6bdbc0ac3e2ef5fc22 | run_20261012T042831Z__greedy__minmax__C-lower-90fec4bb6118-801fe3006e__r00__1f0be2ec88ee | C-lower-90fec4bb6118-801fe3006e | c-lower | greedy | heuristic | minmax | auto | branch-and-bound | greedy | INVALID | Terminal status INVALID; no additional solver error message was recorded. |
| 6c1d7a2122c5171181cd367326295128ae43e777927c7b133bb264db71e4f6de | run_20261012T042831Z__ip-kont__minmax__C-lower-90fec4bb6118-801fe3006e__r00__f92c0d7f6ad2 | C-lower-90fec4bb6118-801fe3006e | c-lower | ip-kont | exact_candidate | minmax | auto | branch-and-bound |  | SKIPPED | {"message": "ip-kont is registered but the native KONT/COPT backend is unavailable", "reason_code": "BACKEND_UNAVAILABLE", "status": "SKIPPED"} |
| 6047fcdd14184a4224b125523da66e444a189bd49928c6e430e4050fb8cc559b | run_20261012T042831Z__local-search__minmax__C-lower-90fec4bb6118-801fe3006e__r00__43ff012000d1 | C-lower-90fec4bb6118-801fe3006e | c-lower | local-search | heuristic | minmax | auto | branch-and-bound | local-search | INVALID | Terminal status INVALID; no additional solver error message was recorded. |
| 2368c97066fca51035462e58ef38593761a738511b3411a5b23dd6f547ab208b | run_20261012T042831Z__lp-rounding__minmax__C-lower-90fec4bb6118-801fe3006e__r00__f6b5619205de | C-lower-90fec4bb6118-801fe3006e | c-lower | lp-rounding | heuristic | minmax | auto | branch-and-bound | lp-rounding | INVALID | Terminal status INVALID; no additional solver error message was recorded. |
| 58f95ac2f8809fe22430b3b22832a95d2bd9b97cf8e4bf9324d647dfc498ee6b | run_20261012T042831Z__nn__minmax__C-lower-90fec4bb6118-801fe3006e__r00__3af19fe04a08 | C-lower-90fec4bb6118-801fe3006e | c-lower | nn | heuristic | minmax | auto | branch-and-bound | nn | INVALID | Terminal status INVALID; no additional solver error message was recorded. |
| 0add9a72af46706071723b20d06457074ac9041413c3ce6675f1348dbc6ea92e | run_20261012T042832Z__primal-dual__minmax__C-lower-90fec4bb6118-801fe3006e__r00__84487952e28e | C-lower-90fec4bb6118-801fe3006e | c-lower | primal-dual | heuristic | minmax | auto | branch-and-bound | primal-dual | INVALID | Terminal status INVALID; no additional solver error message was recorded. |
| 51057de565c61c65a317d164e37f2587db850efd9967a8761cf0664d86e660b5 | run_20261012T042832Z__sa__minmax__C-lower-90fec4bb6118-801fe3006e__r00__6f5348f42586 | C-lower-90fec4bb6118-801fe3006e | c-lower | sa | heuristic | minmax | auto | branch-and-bound | sa | INVALID | Terminal status INVALID; no additional solver error message was recorded. |
| 92a04e83824cc815b349ce46c381f10cdd4cfe563f114df73754b4825d6686ab | run_20261012T042832Z__shifting__minmax__C-lower-90fec4bb6118-801fe3006e__r00__09abb453691f | C-lower-90fec4bb6118-801fe3006e | c-lower | shifting | heuristic | minmax | auto | branch-and-bound | shifting | INVALID | Terminal status INVALID; no additional solver error message was recorded. |
| 1d7680d3464a0965c50d5a0cde9e7c2382fac2d60c9482848acdf1781e89b85b | run_20261012T042833Z__branch-and-bound__minmaxsum__C-lower-90fec4bb6118-801fe3006e__r00__83d725c4a5f5 | C-lower-90fec4bb6118-801fe3006e | c-lower | branch-and-bound | exact_reference | minmaxsum | auto | branch-and-bound | branch-and-bound | TIME_LIMIT | Time limit reached; no additional solver message was recorded. |

Preview shows 12 of 173 rows; the delimited file contains the complete population.
