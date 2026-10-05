# Refactor plan

**Status:** audit complete; schema and storage foundations implemented;
producer integration remains incremental. This plan is based on
`architecture_audit.md` and the companion audit documents in this directory.
Existing uncommitted pipeline/backend changes are preserved and are part of
the audit baseline; they will not be reset, staged, or committed implicitly.

## Guiding decisions

1. Use one physical execution attempt per immutable run directory. Every
   rerun gets a new `run_id` and a `rerun_of` reference when applicable.
2. Use per-run canonical records as source of truth. JSONL indexes and
   `master_results.*` are validated, derived compatibility exports.
3. Make experiment creation exclusive. Never reuse or replace a completed
   experiment/run; `--force` applies only to documented validation bypasses.
4. Keep the three pipeline profiles as configuration on one execution engine.
   Preserve established solver/objective behavior and legacy commands as
   adapters until their output can be derived from canonical runs.
5. Keep historical ignored result archives untouched. Legacy import must be
   explicit and must never invent provenance.
6. Prefer narrow, test-backed changes over broad file moves. Move C++ modules
   only after interfaces and behavioral invariants are locked down.

## Phased implementation

### Phase B — schemas and canonical result model

- Add versioned experiment, dataset, run, result, verification, trace,
  artifact, and pipeline schemas.
- Define explicit status/backend/verification/optional-number semantics.
- Add Python schema validation and strict JSON writing (`allow_nan=False`);
  reject malformed or non-finite canonical records.
- Add positive, negative, compatibility and serialization tests.
- Keep existing output readers available while schemas are introduced.

**Progress:** version-1 experiment, dataset-instance, run-plan-entry,
run-record, verification, artifact-manifest, and trace-event schemas are
present, with strict Python JSON helpers and schema validation. They are not
yet used by existing producers; pipeline/config schemas, broader record
compatibility fixtures, and end-to-end producer migration remain.

**Exit criteria:** every newly produced canonical model validates; intentionally
corrupt schema, NaN/Infinity, and inconsistent status/backend fixtures fail
with useful diagnostics.

### Phase C — immutable IDs and storage API

- Add a focused `scripts/kdc_tools` package for safe IDs, paths, state
  transitions, atomic writes and artifact checksums.
- Persist a unique `run_id`, canonical `run_key`, request, input reference,
  execution evidence, canonical result, verification and artifact manifest.
- Create run directories atomically and fail on collisions; record reruns as
  separate runs linked by `rerun_of`.
- Remove destructive output clearing from new-run flows; keep any legacy
  cleanup only behind an explicitly named administrative/migration tool.

**Progress:** an initial Python storage module now creates run directories and
JSON artifacts exclusively, publishes JSON atomically, validates path
containment, computes SHA-256, and tests state-transition rules. It is not yet
integrated with experiment creation or the C++/Python batch producers, and
state history persistence remains to be implemented.

**Exit criteria:** collision/rerun/concurrency tests prove no completed result
can be overwritten and invalid state transitions fail.

### Phase D — dataset identity and input snapshots

- Normalize source parsing and canonicalization through one dataset API.
- Snapshot exact canonical inputs at experiment scope; record stable instance
  ID, source/canonical hash, family, dimensions and format versions.
- Add checksum/path-containment validation and optional self-contained run
  input packages.

### Phases E–F — config, plan and execution

- Resolve defaults → profile → environment → CLI in one normalized config
  model, preserving both requested and resolved values.
- Add expected `runs/plan.jsonl` before execution, using explicit algorithm
  capability metadata and stable derived seeds.
- Implement dry-run and stage-bound commands; make resume reconcile valid
  completed runs and add new run IDs for retries.
- Adapt C++ batch execution to emit structured per-run records/evidence;
  maintain old master result formats as generated exports.

### Phases G–J — aggregate validation and derived artifacts

- Discover and schema/checksum-validate all planned run directories.
- Produce JSONL index/aggregate, compatibility JSON/CSV, and lineage manifest;
  classify missing/duplicate/invalid/unexpected runs explicitly.
- Require expected-matrix completeness for normal reporting. Add an explicit
  incomplete diagnostic mode rather than silently filtering failures.
- Convert tables, figures, and animations to pure consumers of validated
  aggregates. Register every output and identify source run IDs.

### Phases K–N — C++ architecture and measured optimization

- Extract application orchestration/serialization from domain algorithms
  into target-based CMake libraries without changing public behavior.
- Introduce explicit algorithm/backend capability records and structured
  verification results.
- Centralize numeric tolerances only alongside adversarial regression tests.
- Profile representative workloads before optimizing geometry, kinetic
  events, verifier, memory, or serialization; report comparable metrics only.

### Phases O–R — tests, docs, CI and legacy compatibility

- Add storage, failure-recovery, schema, concurrency, pipeline and
  scientific-invariant tests; reorganize tests only where it improves
  navigation.
- Update README and add architecture/protocol/schema/artifact/backend/
  reproducibility guides as behavior becomes available.
- Keep three thin Actions workflows on the shared engine; verify artifact
  uploads include full experiment packages and make backend policy explicit.
- Preserve current legacy result locations. Add a separate copy-only legacy
  importer with provenance-completeness flags; do not run it automatically.

### Phase S — final validation

- Build Release and Debug, run CTest and Python tests, and run complete smoke
  with schema, matrix, artifact, checksum and report verification.
- Run the full-dataset reference pipeline when resources/backend environment
  permit; record native-backend availability unambiguously.
- Treat the full scientific pipeline as complete only when all planned runs
  and required derived artifacts validate. Do not claim a full benchmark
  validation from smoke or configuration-only checks.
- Inspect `git status`, full diff, source/data integrity, scientific behavior,
  and ignored historical results before any commit or migration.

## Immediate next implementation slice

Start with Phase B's schema contracts and Python validation API, followed by
Phase C's create-only run storage. Do not route current C++ batch output into
the new format until those contracts have targeted tests. At the first
integration boundary, preserve existing result exports and compare deterministic
reference instances, backend fields, MinMax/MinSum outcomes, verification
statuses, and time-limit classifications.
