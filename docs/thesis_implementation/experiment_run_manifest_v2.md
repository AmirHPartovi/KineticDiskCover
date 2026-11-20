# Experiment run manifest v2

Each newly finalized run has an immutable snapshot in
`runs/<run_id>/manifest_snapshots/`. The v1 `run.json` and its meanings remain
unchanged; existing v1 runs without a v2 snapshot are still readable.

The v2 schema records the canonical input and checksum, generator and registry
versions, algorithm/model regime, backend roles, numeric mode, budgets and
tolerances, build/compiler/library identity, local Git commit and dirty status,
the fully resolved configuration, and checksummed result, solution, trace, and
verifier artifacts. Unknown string information is explicitly recorded as `UNRESOLVED`; unknown
lifecycle states are `null`. The manifest does not infer a value from a solver
status.

`designed`, `generated`, `validated`, and `used_in_run` are independent booleans.
No lifecycle state implies another. Callers with registry metadata can provide
it through `manifest_context` to `finalize_run`; absent metadata remains
`UNRESOLVED`.

The manifest SHA-256 is calculated over deterministic UTF-8 JSON (sorted keys,
LF line ending) with the `manifest_sha256` member omitted. Snapshot files use
both the fresh UUID and digest prefix in their names. Writes are create-only;
revisions must create a new UUID rather than replace an earlier snapshot.
Referenced files are re-hashed during validation. Git provenance uses only
local `git rev-parse` and `git status` commands; no remote is contacted.

Build, PRNG, registry, backend-role, budget, or tolerance information that is
not available to the current run pipeline is deliberately `UNRESOLVED`. It
must be supplied by the caller before a run can claim that provenance as
known.
