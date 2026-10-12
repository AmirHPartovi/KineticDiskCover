# Completeness and Artifact Inventory

- **Purpose:** Report planned versus terminal run coverage and exact observed artifact counts by experiment directory.
- **Experiment:** `kdc-smoke20-all-registered-final-20261012T074200`
- **Source manifest/result:** `active experiment manifest, canonical run plan/records and artifact directory inventory`
- **Generation:** `scripts/report_experiment.py --stage final`
- **Rows:** 10
- **File scope:** One row per artifact directory scope.
- **Sort order:** declared experiment artifact directory order
- **Filters:** Counts include all currently present files in each named scope; no files from another experiment are read.
- **Status counts:** not applicable or status column absent
- **Missing/null handling:** unavailable or inapplicable measurements remain blank/null; no unrelated metric is substituted.
- **Limitations:** A present artifact is not by itself evidence of a valid solver result; inspect canonical run and visualization manifests.

## Derived metric formulas

artifact_count is the recursive number of files present in the scope when this final report is generated. Run planned/terminal counts come from the immutable run plan and validated terminal records.

## Column definitions

- `artifact_scope` — Experiment-relative directory or artifact grouping.
- `artifact_status` — Observed presence/status of artifacts in this scope.
- `planned_count` — Number of logical slots in the immutable run plan for the group.
- `terminal_count` — Number of planned slots with a canonical terminal run record.
- `artifact_count` — Count of files in the indicated experiment artifact scope.
- `generated_snapshots` — Count of successfully generated solution snapshot image artifacts.
- `generated_animations` — Count of successfully generated per-solution animations.
- `generated_comparison_sheets` — Count of Prompt 6 per-instance/objective comparison sheets.
- `aggregate_complete` — Whether the canonical aggregate explicitly reports completeness.
- `reason` — Grounded reason for an unavailable output or excluded row.

## Preview

| artifact_scope | artifact_status | planned_count | terminal_count | artifact_count | generated_snapshots | generated_animations | generated_comparison_sheets | aggregate_complete | reason |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| runs | PRESENT | 660 | 660 | 18386 |  |  |  | False | Run status counts: {"COMPLETED": 487, "FAILED": 51, "INVALID": 39, "SKIPPED": 60, "TIME_LIMIT": 23} |
| aggregates | PRESENT |  |  | 5 |  |  |  | False |  |
| batch | PRESENT |  |  | 5 |  |  |  | False |  |
| tables | PRESENT |  |  | 27 |  |  |  | False |  |
| figures | PRESENT |  |  | 22 |  |  |  | False |  |
| snapshots | PRESENT |  |  | 2068 | 502 |  | 60 | False |  |
| animations | PRESENT |  |  | 1004 |  | 502 |  | False |  |
| certification | PRESENT |  |  | 3 |  |  |  | False |  |
| reports | PRESENT |  |  | 10 |  |  |  | False |  |
| logs | PRESENT |  |  | 13 |  |  |  | False |  |
