# Exact Reference Comparison

- **Purpose:** Compare candidate scalar objectives only against the one designated, valid, proven exact reference.
- **Experiment:** `kdc-smoke20-all-registered-final-20261012T074200`
- **Source manifest/result:** `designated reference run records, experiment manifest and canonical plan`
- **Generation:** `scripts/report_experiment.py --stage tables`
- **Rows:** 61
- **File scope:** One row per candidate rows with a matching proven exact reference.
- **Sort order:** stable lexicographic order by the leading identity/group columns unless otherwise stated
- **Filters:** Reference must be designated exact_reference, COMPLETED, feasible, verification-passed, CERTIFIED_CONTINUOUS and OPTIMAL; MinMaxSum excluded from scalar ratios.
- **Status counts:** COMPLETED=61
- **Missing/null handling:** unavailable or inapplicable measurements remain blank/null; no unrelated metric is substituted.
- **Limitations:** empirical_ratio_to_exact is descriptive and is not a theoretical approximation ratio. No valid reference means no comparison row.

## Derived metric formulas

empirical_ratio_to_exact=candidate objective_value/reference objective_value; computed only when both are finite, the reference objective is nonzero, and the designated reference passes the documented proof/verification filter.

## Column definitions

- `instance_id` — Canonical selected-instance identifier.
- `family` — Family label copied from the validated smoke20 selection manifest.
- `algorithm` — Algorithm identifier from the immutable run plan.
- `objective` — Objective identifier; MinMaxSum remains a two-component vector.
- `run_id` — Immutable physical run identifier; null means the planned slot has not produced a run record.
- `reference_run_id` — Recorded reference_run_id value; not inferred from another field.
- `selected_reference_backend` — One experiment-level selected exact-reference backend.
- `terminal_status` — Canonical run status; PLANNED means no terminal record exists.
- `reference_status` — Recorded reference_status value; not inferred from another field.
- `objective_value` — Scalar MinMax or MinSum value: MinMax is peak disk-area cost (area units); MinSum is integrated disk-area cost (area×time units). Null for MinMaxSum.
- `reference_objective_value` — Recorded reference_objective_value value; not inferred from another field.
- `empirical_ratio_to_exact` — Observed ratio to a valid proven exact reference; not a theoretical approximation ratio.
- `ratio_semantics` — Recorded ratio_semantics value; not inferred from another field.
- `verification_kind` — Verification kind; EMPIRICAL is distinct from continuous verification.

## Preview

| instance_id | family | algorithm | objective | run_id | reference_run_id | selected_reference_backend | terminal_status | reference_status | objective_value | reference_objective_value | empirical_ratio_to_exact | ratio_semantics | verification_kind |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| att48-8efa61a1d9ab-5196644df6 | att48 | genetic | minmax | run_20261012T041939Z__genetic__minmax__att48-8efa61a1d9ab-5196644df6__r00__f598af0598e6 | run_20261012T041938Z__branch-and-bound__minmax__att48-8efa61a1d9ab-5196644df6__r00__70fd050872bb | branch-and-bound | COMPLETED | OPTIMAL | 2455.7713594478446 | 2318.289942578307 | 1.0593029432360976 | descriptive empirical ratio to a valid proven optimum; not a theoretical approximation ratio | CERTIFIED_CONTINUOUS |
| att48-8efa61a1d9ab-5196644df6 | att48 | greedy | minmax | run_20261012T041939Z__greedy__minmax__att48-8efa61a1d9ab-5196644df6__r00__b436c081d879 | run_20261012T041938Z__branch-and-bound__minmax__att48-8efa61a1d9ab-5196644df6__r00__70fd050872bb | branch-and-bound | COMPLETED | OPTIMAL | 2675.3745457835635 | 2318.289942578307 | 1.1540293112811082 | descriptive empirical ratio to a valid proven optimum; not a theoretical approximation ratio | CERTIFIED_CONTINUOUS |
| att48-8efa61a1d9ab-5196644df6 | att48 | local-search | minmax | run_20261012T041939Z__local-search__minmax__att48-8efa61a1d9ab-5196644df6__r00__fe1900be63f5 | run_20261012T041938Z__branch-and-bound__minmax__att48-8efa61a1d9ab-5196644df6__r00__70fd050872bb | branch-and-bound | COMPLETED | OPTIMAL | 2455.7713594478446 | 2318.289942578307 | 1.0593029432360976 | descriptive empirical ratio to a valid proven optimum; not a theoretical approximation ratio | CERTIFIED_CONTINUOUS |
| att48-8efa61a1d9ab-5196644df6 | att48 | lp-rounding | minmax | run_20261012T041939Z__lp-rounding__minmax__att48-8efa61a1d9ab-5196644df6__r00__eed4d316c8c6 | run_20261012T041938Z__branch-and-bound__minmax__att48-8efa61a1d9ab-5196644df6__r00__70fd050872bb | branch-and-bound | COMPLETED | OPTIMAL | 2318.289942578307 | 2318.289942578307 | 1.0 | descriptive empirical ratio to a valid proven optimum; not a theoretical approximation ratio | CERTIFIED_CONTINUOUS |
| att48-8efa61a1d9ab-5196644df6 | att48 | nn | minmax | run_20261012T041939Z__nn__minmax__att48-8efa61a1d9ab-5196644df6__r00__85c1528a18e7 | run_20261012T041938Z__branch-and-bound__minmax__att48-8efa61a1d9ab-5196644df6__r00__70fd050872bb | branch-and-bound | COMPLETED | OPTIMAL | 2604.437182235978 | 2318.289942578307 | 1.1234303071424403 | descriptive empirical ratio to a valid proven optimum; not a theoretical approximation ratio | CERTIFIED_CONTINUOUS |
| att48-8efa61a1d9ab-5196644df6 | att48 | primal-dual | minmax | run_20261012T041939Z__primal-dual__minmax__att48-8efa61a1d9ab-5196644df6__r00__7101e87ef183 | run_20261012T041938Z__branch-and-bound__minmax__att48-8efa61a1d9ab-5196644df6__r00__70fd050872bb | branch-and-bound | COMPLETED | OPTIMAL | 2465.808651486608 | 2318.289942578307 | 1.0636325535468771 | descriptive empirical ratio to a valid proven optimum; not a theoretical approximation ratio | CERTIFIED_CONTINUOUS |
| att48-8efa61a1d9ab-5196644df6 | att48 | shifting | minmax | run_20261012T041939Z__shifting__minmax__att48-8efa61a1d9ab-5196644df6__r00__7379967a13ba | run_20261012T041938Z__branch-and-bound__minmax__att48-8efa61a1d9ab-5196644df6__r00__70fd050872bb | branch-and-bound | COMPLETED | OPTIMAL | 2604.437182235978 | 2318.289942578307 | 1.1234303071424403 | descriptive empirical ratio to a valid proven optimum; not a theoretical approximation ratio | CERTIFIED_CONTINUOUS |
| euro-night-0000030-e7069adc74e0-164a859873 | euro-night | brute-force | minmax | run_20261012T042002Z__brute-force__minmax__euro-night-0000030-e7069adc74e0-164a85__r00__83e9acf750c6 | run_20261012T042002Z__branch-and-bound__minmax__euro-night-0000030-e7069adc74e0-164a85__r00__c4d033ab6bec | branch-and-bound | COMPLETED | OPTIMAL | 387.866526064915 | 387.866526064915 | 1.0 | descriptive empirical ratio to a valid proven optimum; not a theoretical approximation ratio | CERTIFIED_CONTINUOUS |
| euro-night-0000030-e7069adc74e0-164a859873 | euro-night | genetic | minmax | run_20261012T042002Z__genetic__minmax__euro-night-0000030-e7069adc74e0-164a85__r00__6b905b128889 | run_20261012T042002Z__branch-and-bound__minmax__euro-night-0000030-e7069adc74e0-164a85__r00__c4d033ab6bec | branch-and-bound | COMPLETED | OPTIMAL | 387.866526064915 | 387.866526064915 | 1.0 | descriptive empirical ratio to a valid proven optimum; not a theoretical approximation ratio | CERTIFIED_CONTINUOUS |
| euro-night-0000030-e7069adc74e0-164a859873 | euro-night | greedy | minmax | run_20261012T042002Z__greedy__minmax__euro-night-0000030-e7069adc74e0-164a85__r00__d313fad51bc7 | run_20261012T042002Z__branch-and-bound__minmax__euro-night-0000030-e7069adc74e0-164a85__r00__c4d033ab6bec | branch-and-bound | COMPLETED | OPTIMAL | 387.866526064915 | 387.866526064915 | 1.0 | descriptive empirical ratio to a valid proven optimum; not a theoretical approximation ratio | CERTIFIED_CONTINUOUS |
| euro-night-0000030-e7069adc74e0-164a859873 | euro-night | local-search | minmax | run_20261012T042003Z__local-search__minmax__euro-night-0000030-e7069adc74e0-164a85__r00__dce98987c6d5 | run_20261012T042002Z__branch-and-bound__minmax__euro-night-0000030-e7069adc74e0-164a85__r00__c4d033ab6bec | branch-and-bound | COMPLETED | OPTIMAL | 387.866526064915 | 387.866526064915 | 1.0 | descriptive empirical ratio to a valid proven optimum; not a theoretical approximation ratio | CERTIFIED_CONTINUOUS |
| euro-night-0000030-e7069adc74e0-164a859873 | euro-night | lp-rounding | minmax | run_20261012T042003Z__lp-rounding__minmax__euro-night-0000030-e7069adc74e0-164a85__r00__0795c8ba2b33 | run_20261012T042002Z__branch-and-bound__minmax__euro-night-0000030-e7069adc74e0-164a85__r00__c4d033ab6bec | branch-and-bound | COMPLETED | OPTIMAL | 387.866526064915 | 387.866526064915 | 1.0 | descriptive empirical ratio to a valid proven optimum; not a theoretical approximation ratio | CERTIFIED_CONTINUOUS |

Preview shows 12 of 61 rows; the delimited file contains the complete population.
