# Documentation audit

## Existing documentation

- `README.md` documents solver build/test commands, backend discovery,
  batch execution, time limits, profiling, outputs, and scientific bound
  semantics. Its current first section describes the three pipelines, but
  later legacy sections still describe `results/batch/`, `results/preflight/`,
  a generic manual Actions pipeline, and direct FAST usage as if they were
  the principal experiment interface.
- `docs/bound_semantics.md` is a useful conservative explanation of lower
  bounds, MinMax/MinSum semantics, timeouts, and legacy records. It explicitly
  distinguishes sampled estimates from certificates.
- `docs/implementation_audit.md` records execution and solver semantics, but
  is a snapshot from earlier phases and does not cover the latest immutable
  storage/pipeline requirements.
- `docs/final_validation_report.md` documents profiling and backend limits;
  it explicitly declines an invalid baseline/current speedup claim.

## Gaps and maintenance risks

There is no current architecture, schema, storage-layout, reproducibility,
pipeline-stage, or backend-provenance guide as a navigable documentation set.
README material mixes current experiment commands with legacy low-level
commands and path defaults. Existing scientific semantics are valuable and
should be preserved; stale operational claims should be revised only after
the new storage/CLI behavior is implemented and tested.

The requested new refactor documents should link to implementation and
validation evidence rather than duplicate long prose. Documentation updates
should follow the relevant implementation phase to avoid claiming functionality
that has not shipped.
