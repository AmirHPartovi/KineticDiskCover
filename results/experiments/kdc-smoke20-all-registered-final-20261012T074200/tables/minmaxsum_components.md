# Minmaxsum Components

- **Purpose:** Report MinMaxSum peak/integral vector components and the repository-recorded component/dominance data.
- **Experiment:** `kdc-smoke20-all-registered-final-20261012T074200`
- **Source manifest/result:** `validated canonical run records and run plan`
- **Generation:** `scripts/report_experiment.py --stage tables`
- **Rows:** 220
- **File scope:** One row per planned MinMaxSum slots.
- **Sort order:** stable lexicographic order by the leading identity/group columns unless otherwise stated
- **Filters:** No status filter; missing/unavailable vector values remain null.
- **Status counts:** COMPLETED=165, FAILED=25, SKIPPED=20, TIME_LIMIT=10
- **Missing/null handling:** unavailable or inapplicable measurements remain blank/null; no unrelated metric is substituted.
- **Limitations:** No weighted/scalar score is computed. Dominance flags are reported from the validated record.

## Derived metric formulas

No metrics are calculated for this table; values are copied from the validated plan, run record, manifest, or artifact inventory.

## Column definitions

- `run_key` — Stable logical run-plan slot identity.
- `run_id` — Immutable physical run identifier; null means the planned slot has not produced a run record.
- `instance_id` — Canonical selected-instance identifier.
- `family` — Family label copied from the validated smoke20 selection manifest.
- `algorithm` — Algorithm identifier from the immutable run plan.
- `algorithm_category` — Algorithm role from the resolved run configuration (for example heuristic, exact_reference, diagnostic).
- `terminal_status` — Canonical run status; PLANNED means no terminal record exists.
- `feasible` — Recorded feasibility value; blank when no result record exists.
- `verification_kind` — Verification kind; EMPIRICAL is distinct from continuous verification.
- `verification_passed` — Recorded verification pass flag.
- `optimality_status` — Solver-reported proof/incumbent status; not inferred from run status.
- `peak_cost` — Peak instantaneous disk-area cost over the horizon (area units); MinMaxSum vector component.
- `integral_cost` — Time integral of disk-area cost (area×time units); MinMaxSum vector component.
- `objective_vector_status` — Availability/validation status of the two-component objective vector.
- `dominates_minmax` — Recorded dominance relation against the MinMax component result.
- `dominates_minsum` — Recorded dominance relation against the MinSum component result.
- `dominance_invariants_ok` — Recorded check of MinMaxSum dominance invariants.
- `minmax_component_peak` — Recorded minmax_component_peak value; not inferred from another field.
- `minmax_component_integral` — Recorded minmax_component_integral value; not inferred from another field.
- `minsum_component_peak` — Recorded minsum_component_peak value; not inferred from another field.
- `minsum_component_integral` — Recorded minsum_component_integral value; not inferred from another field.
- `error_reason` — Recorded run/result error or a grounded report status reason.

## Preview

| run_key | run_id | instance_id | family | algorithm | algorithm_category | terminal_status | feasible | verification_kind | verification_passed | optimality_status | peak_cost | integral_cost | objective_vector_status | dominates_minmax | dominates_minsum | dominance_invariants_ok | minmax_component_peak | minmax_component_integral | minsum_component_peak | minsum_component_integral | error_reason |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 1d7680d3464a0965c50d5a0cde9e7c2382fac2d60c9482848acdf1781e89b85b | run_20261012T042833Z__branch-and-bound__minmaxsum__C-lower-90fec4bb6118-801fe3006e__r00__83d725c4a5f5 | C-lower-90fec4bb6118-801fe3006e | c-lower | branch-and-bound | exact_reference | TIME_LIMIT | False | NONE | False | TIME_LIMIT |  |  | MISSING_OR_INVALID | False | False | False |  |  |  |  | Time limit reached; no additional solver message was recorded. |
| e1419db6f8498d73c1e6bfc8c4895b0a156cf46754287a3da8abf1a7d1b1d322 | run_20261012T042833Z__brute-force__minmaxsum__C-lower-90fec4bb6118-801fe3006e__r00__be1ac0e3d500 | C-lower-90fec4bb6118-801fe3006e | c-lower | brute-force | diagnostic | FAILED | False | NONE | False | FAILED |  |  | MISSING_OR_INVALID | False | False | False |  |  |  |  | Terminal status FAILED; no additional solver error message was recorded. |
| 926428ddc71e30086f2fbfbca075fc4ebd30d0ee5853b2e1781c533f28a10255 | run_20261012T042833Z__genetic__minmaxsum__C-lower-90fec4bb6118-801fe3006e__r00__9169f05d0da0 | C-lower-90fec4bb6118-801fe3006e | c-lower | genetic | heuristic | FAILED | False | NONE | False | FAILED |  |  | MISSING_OR_INVALID | False | False | False |  |  |  |  | Terminal status FAILED; no additional solver error message was recorded. |
| 57300f54494100f3ba05055391aad3686c23a1769b06565cf3dd412181d0ed19 | run_20261012T042834Z__greedy__minmaxsum__C-lower-90fec4bb6118-801fe3006e__r00__daeec48d5142 | C-lower-90fec4bb6118-801fe3006e | c-lower | greedy | heuristic | FAILED | False | NONE | False | FAILED |  |  | MISSING_OR_INVALID | False | False | False |  |  |  |  | Terminal status FAILED; no additional solver error message was recorded. |
| c5233e31632085f3b8a4af7277a07e3adb36db66c6b51e2df8bcb00955263fd6 | run_20261012T042834Z__ip-kont__minmaxsum__C-lower-90fec4bb6118-801fe3006e__r00__708059cf4a7a | C-lower-90fec4bb6118-801fe3006e | c-lower | ip-kont | exact_candidate | SKIPPED | False | NONE | False | FAILED |  |  | MISSING_OR_INVALID | False | False | False |  |  |  |  | {"message": "ip-kont is registered but the native KONT/COPT backend is unavailable", "reason_code": "BACKEND_UNAVAILABLE", "status": "SKIPPED"} |
| dd52f72f11c1cfa21d498926c484c86d31219d0d2bed6a003c42558236a6f2df | run_20261012T042834Z__local-search__minmaxsum__C-lower-90fec4bb6118-801fe3006e__r00__3dfdf800e691 | C-lower-90fec4bb6118-801fe3006e | c-lower | local-search | heuristic | FAILED | False | NONE | False | FAILED |  |  | MISSING_OR_INVALID | False | False | False |  |  |  |  | Terminal status FAILED; no additional solver error message was recorded. |
| bb90536f8f1d464bffdbaf9236f5810e9a722eb4a9c7a3137dd77529b6f131d3 | run_20261012T041921Z__lp-rounding__minmaxsum__C-lower-90fec4bb6118-801fe3006e__r00__4697d065cb6d | C-lower-90fec4bb6118-801fe3006e | c-lower | lp-rounding | heuristic | COMPLETED | True | CERTIFIED_CONTINUOUS | True | FEASIBLE | 2174.865874979543 | 1256.964570496638 | COMPLETE | False | True | True |  |  | 2174.865874979543 | 1256.964570496638 |  |
| 852efb5747a5eab17044432cdbdcc84fd9c31a8b2521a3c0773b6d34d9973496 | run_20261012T042835Z__nn__minmaxsum__C-lower-90fec4bb6118-801fe3006e__r00__11e0489738ae | C-lower-90fec4bb6118-801fe3006e | c-lower | nn | heuristic | FAILED | False | NONE | False | FAILED |  |  | MISSING_OR_INVALID | False | False | False |  |  |  |  | Terminal status FAILED; no additional solver error message was recorded. |
| 66aaee1665464def7170166b2c56788f618f6929a13003164d409a4016ac2f76 | run_20261012T042836Z__primal-dual__minmaxsum__C-lower-90fec4bb6118-801fe3006e__r00__79cca8cd2c13 | C-lower-90fec4bb6118-801fe3006e | c-lower | primal-dual | heuristic | FAILED | False | NONE | False | FAILED |  |  | MISSING_OR_INVALID | False | False | False |  |  |  |  | Terminal status FAILED; no additional solver error message was recorded. |
| 88ffeded5ce6d6dfecc5892433e4b163bd2de39fa3c1b9b4b036f081fc2ab8fc | run_20261012T042836Z__sa__minmaxsum__C-lower-90fec4bb6118-801fe3006e__r00__1c4dcb37aa99 | C-lower-90fec4bb6118-801fe3006e | c-lower | sa | heuristic | FAILED | False | NONE | False | FAILED |  |  | MISSING_OR_INVALID | False | False | False |  |  |  |  | Terminal status FAILED; no additional solver error message was recorded. |
| 8ab356202175190adac2b24e078b3420158b937d8cd5d4d75d040aecb31e184b | run_20261012T041924Z__shifting__minmaxsum__C-lower-90fec4bb6118-801fe3006e__r00__6fc4dca6c984 | C-lower-90fec4bb6118-801fe3006e | c-lower | shifting | heuristic | COMPLETED | True | CERTIFIED_CONTINUOUS | True | FEASIBLE | 1865.2813162740713 | 1267.20467456831 | COMPLETE | False | True | True |  |  | 1865.2813162740713 | 1267.20467456831 |  |
| 6586282cfd4edde2e995d2eb8c71c39cedf7b44792869c04ea9e67e2ec549c8c | run_20261012T042840Z__branch-and-bound__minmaxsum__C-right-8bf4b5770668-e0f2e6a705__r00__9902d8b772f2 | C-right-8bf4b5770668-e0f2e6a705 | c-right | branch-and-bound | exact_reference | TIME_LIMIT | False | NONE | False | TIME_LIMIT |  |  | MISSING_OR_INVALID | False | False | False |  |  |  |  | Time limit reached; no additional solver message was recorded. |

Preview shows 12 of 220 rows; the delimited file contains the complete population.
