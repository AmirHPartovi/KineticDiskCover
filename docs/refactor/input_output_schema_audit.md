# Input/output schema audit

## Inputs

- Source data includes MDC and JSON across generated families and the public
  benchmark set. `scripts/prepare_real_instances.py` converts MDC to the
  solver's canonical JSON. `DatasetReader` validates/loads canonical inputs.
- `scripts/select_test_instances.py` also converts source payloads while
  selecting; it validates dimensions and emits a selection manifest with
  per-source checksum, size, selection reason/rank, and smoke bounds.
- The experiment runner snapshots/converts selected input files under its
  `dataset/` directory and records a dataset fingerprint. Input identity is
  still primarily path/fingerprint metadata; stable canonical `instance_id`,
  source and canonical hashes per instance, format/canonicalization version
  are not consistently recorded through to each run.

## Outputs and current versioning

| Output | Format | Current version/validation |
|---|---|---|
| Batch master result | JSON array and CSV | C++ per-record `schema_version` is 3 in the current worktree; no formal JSON Schema |
| Benchmark result | JSON/CSV | Result records carry schema version 2; separate reader supports legacy fields |
| Static/kinetic comparison result | JSON/CSV | Comparison serializers use version 2 and a distinct record shape |
| Solution | JSON interval representation | Validated structurally and mathematically on load; no schema version |
| Trace | CSV (`iter,t_max,objective,lower_bound,gap,wall_time,num_ip_solves`) | Header/count/value validation; not JSONL event trace |
| Experiment/batch/calibration manifests | JSON | Different producers and shapes; no common versioned schema contract |
| Table/plot/animation reports | Markdown, CSV, PNG/PDF, GIF/MP4 | Derived from master arrays/solutions; no unified artifact lineage/checksum manifest |

At the audit baseline, no `docs/schemas/` or JSON Schema validator was present.
The first implementation slice now adds version-1 Draft 2020-12 schemas under
`docs/schemas/` and Python strict JSON/schema utilities in
`scripts/kdc_tools/`. Existing C++ and Python result producers have not yet
been migrated: their JSON paths may still emit/accept values under their
previous policies, so canonical strict-JSON behavior is not yet enforced
end-to-end. C++ serializers perform finite-value checks in some domain paths
and conditionally emit fields, but there is not yet one integrated result
serialization policy.

## Semantic distinctions

Existing C++ result models already distinguish bound status, optimality
status, feasibility, timeout, verification kind, and backend provenance in
several paths. The Python consumers have compatibility logic and Boolean
coercion, but the independent models have not been reconciled into one
schema. Some legacy `verified` fields do not state whether the check was
empirical or continuous; consumers must inspect companion fields where
available.

## Trace and artifact lineage

The current trace records objective iteration metrics only. It does not
encode individual support changes, handovers, ownership transitions, or
solution interval boundaries as structured events. Per-run CSV/solution
paths occur in aggregate records, but there is no canonical per-run
`artifacts.json`, SHA-256 inventory, or run-to-animation linkage.
