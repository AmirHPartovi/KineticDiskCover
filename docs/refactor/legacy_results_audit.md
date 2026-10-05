# Legacy results audit

## Repository tracking

The root `.gitignore` excludes `/results/`. No result files are tracked by
Git. The local working environment contains several historical archives;
they were examined read-only and left in place.

## Local archive inventory

| Location | Approximate size / count | Available provenance | Audit classification |
|---|---:|---|---|
| `results/experiments/2026-10-03_public_heuristics_ip/` | 2.4 GB; 8,369 files | Batch manifest records commit, dataset identity, configuration, backend request/selection/actual | Legacy experiment; useful outputs, but no immutable run IDs/checksum/artifact inventory; do not import as trusted new-schema data |
| `results/experiments/2026-10-05_020027_60234/` | 1.9 MB; 123 files | Top-level stage manifest records source, dataset fingerprint, commit, dirty state, statuses; nested batch manifest | Partial experiment; provenance is incomplete for canonical per-run import |
| `results/experiments/2026-10-05_021013_20420/` | 14 MB; 275 files | Top-level/batch manifests and generated tables/figures/reports | Partial/legacy experiment; not a complete immutable run package |
| `results/comparison/` and root `results/experiment_manifest.json` | small | C++ comparison/calibration manifests with backend and dataset fingerprint fields | Legacy shared-output data; schema and run lineage incomplete |
| `results/preflight/`, `results/logs/` | about 8 KB and 13 MB | Reports/logs | Operational evidence, not standalone scientific run records |

All inspected backend manifests distinguish requested, selected, and actual
backend fields; at least one historic request for `ip-kont` records
branch-and-bound as selected/actual. That is useful evidence, but individual
legacy records still require their own validation before scientific reuse.
No missing provenance should be reconstructed by guesswork.

## Migration policy

Do not move, rewrite, delete, or merge these outputs during implementation.
Future import tooling should copy only explicitly selected archives into
`results/legacy/`, preserve original bytes, compute import-time checksums,
mark `provenance_complete: false` where evidence is absent, and produce a
migration manifest. New aggregations must exclude legacy data unless a
validated import is explicitly requested.
