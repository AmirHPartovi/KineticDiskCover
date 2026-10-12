# Bounds And Gaps

- **Purpose:** Keep lower/upper bounds, bound provenance, certified/heuristic/empirical gaps distinct.
- **Experiment:** `kdc-smoke20-all-registered-final-20261012T074200`
- **Source manifest/result:** `validated canonical run records`
- **Generation:** `scripts/report_experiment.py --stage tables`
- **Rows:** 660
- **File scope:** One row per all planned slots.
- **Sort order:** stable lexicographic order by the leading identity/group columns unless otherwise stated
- **Filters:** No status filter; only explicitly stored fields are reported.
- **Status counts:** COMPLETED=487, FAILED=51, INVALID=39, SKIPPED=60, TIME_LIMIT=23
- **Missing/null handling:** unavailable or inapplicable measurements remain blank/null; no unrelated metric is substituted.
- **Limitations:** No gap is reconstructed from unrelated objective/bound fields; null means unavailable.

## Derived metric formulas

No metrics are calculated for this table; values are copied from the validated plan, run record, manifest, or artifact inventory.

## Column definitions

- `run_key` — Stable logical run-plan slot identity.
- `run_id` — Immutable physical run identifier; null means the planned slot has not produced a run record.
- `instance_id` — Canonical selected-instance identifier.
- `algorithm` — Algorithm identifier from the immutable run plan.
- `objective` — Objective identifier; MinMaxSum remains a two-component vector.
- `terminal_status` — Canonical run status; PLANNED means no terminal record exists.
- `objective_value` — Scalar MinMax or MinSum value: MinMax is peak disk-area cost (area units); MinSum is integrated disk-area cost (area×time units). Null for MinMaxSum.
- `lower_bound` — Reported lower bound; not inferred from objective or upper-bound fields.
- `upper_bound` — Reported upper bound; not inferred from objective or lower-bound fields.
- `bound_status` — Solver-reported bound status.
- `bound_provenance` — Solver-reported provenance/type of the bound.
- `certified_gap` — Explicitly reported certified gap; never reconstructed from heuristic bounds.
- `heuristic_gap` — Explicitly reported heuristic gap; kept separate from certified gaps.
- `empirical_gap` — Explicitly reported empirical gap; kept separate from certified gaps.
- `empirical_ratio_to_exact` — Observed ratio to a valid proven exact reference; not a theoretical approximation ratio.

## Preview

| run_key | run_id | instance_id | algorithm | objective | terminal_status | objective_value | lower_bound | upper_bound | bound_status | bound_provenance | certified_gap | heuristic_gap | empirical_gap | empirical_ratio_to_exact |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 2600a48ab5f118e398fbd2c1a234547139908f7d6463cbc2eb1cc23f93ec00f9 | run_20261012T042830Z__branch-and-bound__minmax__C-lower-90fec4bb6118-801fe3006e__r00__65943c0b5504 | C-lower-90fec4bb6118-801fe3006e | branch-and-bound | minmax | INVALID | 0.0 | 0.0 |  | NONE |  |  | 0.0 |  |  |
| cd4ede709ec52f333a39e333adab24c0bbcbf0d253f6deb1e44432f49c4724d5 | run_20261012T042830Z__brute-force__minmax__C-lower-90fec4bb6118-801fe3006e__r00__71feb2324191 | C-lower-90fec4bb6118-801fe3006e | brute-force | minmax | FAILED | 0.0 | 0.0 |  | NONE |  |  | 0.0 |  |  |
| 0b4c9c617d2c7077573ac5949365466236044a0b18e3fa20286e9b780b4b3d4c | run_20261012T042830Z__genetic__minmax__C-lower-90fec4bb6118-801fe3006e__r00__efd46495f85c | C-lower-90fec4bb6118-801fe3006e | genetic | minmax | INVALID | 0.0 | 0.0 |  | NONE |  |  | 0.0 |  |  |
| d262838e5be9ce400e1e0b65242ce748cef86d05b314fd6bdbc0ac3e2ef5fc22 | run_20261012T042831Z__greedy__minmax__C-lower-90fec4bb6118-801fe3006e__r00__1f0be2ec88ee | C-lower-90fec4bb6118-801fe3006e | greedy | minmax | INVALID | 0.0 | 0.0 |  | NONE |  |  | 0.0 |  |  |
| 6c1d7a2122c5171181cd367326295128ae43e777927c7b133bb264db71e4f6de | run_20261012T042831Z__ip-kont__minmax__C-lower-90fec4bb6118-801fe3006e__r00__f92c0d7f6ad2 | C-lower-90fec4bb6118-801fe3006e | ip-kont | minmax | SKIPPED |  |  |  |  |  |  |  |  |  |
| 6047fcdd14184a4224b125523da66e444a189bd49928c6e430e4050fb8cc559b | run_20261012T042831Z__local-search__minmax__C-lower-90fec4bb6118-801fe3006e__r00__43ff012000d1 | C-lower-90fec4bb6118-801fe3006e | local-search | minmax | INVALID | 0.0 | 0.0 |  | NONE |  |  | 0.0 |  |  |
| 2368c97066fca51035462e58ef38593761a738511b3411a5b23dd6f547ab208b | run_20261012T042831Z__lp-rounding__minmax__C-lower-90fec4bb6118-801fe3006e__r00__f6b5619205de | C-lower-90fec4bb6118-801fe3006e | lp-rounding | minmax | INVALID | 0.0 | 0.0 |  | NONE |  |  | 0.0 |  |  |
| 58f95ac2f8809fe22430b3b22832a95d2bd9b97cf8e4bf9324d647dfc498ee6b | run_20261012T042831Z__nn__minmax__C-lower-90fec4bb6118-801fe3006e__r00__3af19fe04a08 | C-lower-90fec4bb6118-801fe3006e | nn | minmax | INVALID | 0.0 | 0.0 |  | NONE |  |  | 0.0 |  |  |
| 0add9a72af46706071723b20d06457074ac9041413c3ce6675f1348dbc6ea92e | run_20261012T042832Z__primal-dual__minmax__C-lower-90fec4bb6118-801fe3006e__r00__84487952e28e | C-lower-90fec4bb6118-801fe3006e | primal-dual | minmax | INVALID | 0.0 | 0.0 |  | NONE |  |  | 0.0 |  |  |
| 51057de565c61c65a317d164e37f2587db850efd9967a8761cf0664d86e660b5 | run_20261012T042832Z__sa__minmax__C-lower-90fec4bb6118-801fe3006e__r00__6f5348f42586 | C-lower-90fec4bb6118-801fe3006e | sa | minmax | INVALID | 0.0 | 0.0 |  | NONE |  |  | 0.0 |  |  |
| 92a04e83824cc815b349ce46c381f10cdd4cfe563f114df73754b4825d6686ab | run_20261012T042832Z__shifting__minmax__C-lower-90fec4bb6118-801fe3006e__r00__09abb453691f | C-lower-90fec4bb6118-801fe3006e | shifting | minmax | INVALID | 0.0 | 0.0 |  | NONE |  |  | 0.0 |  |  |
| 1d7680d3464a0965c50d5a0cde9e7c2382fac2d60c9482848acdf1781e89b85b | run_20261012T042833Z__branch-and-bound__minmaxsum__C-lower-90fec4bb6118-801fe3006e__r00__83d725c4a5f5 | C-lower-90fec4bb6118-801fe3006e | branch-and-bound | minmaxsum | TIME_LIMIT |  |  |  | NONE |  |  |  |  |  |

Preview shows 12 of 660 rows; the delimited file contains the complete population.
