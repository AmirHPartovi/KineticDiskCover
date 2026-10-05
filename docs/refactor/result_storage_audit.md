# Result storage audit

## Current paths and authorities

| Producer / command | Current default or path | Current authority |
|---|---|---|
| `kdc-solver batch` / `BatchRunConfig` | `results/batch` | `BatchRunner` writes per-run files plus master JSON/CSV and summary; downstream Python consumers read master JSON |
| `kdc-solver benchmark` | `results` | `BenchmarkRunner` writes benchmark outputs and manifest under caller-selected directory |
| compare APIs | `results/comparison` | `AlgorithmComparator` / `ComparativeRunner` write comparison JSON/CSV, reports, and manifests |
| C++ preflight | `results/preflight/preflight_report.md` | CLI and `SanityCheckReport` writer |
| Python preflight | `results/preflight/` | `preflight_check.py` writes report and summary |
| logging | `results/logs/kdc.log` | global default from `include/kdc/logging.hpp` |
| Python tables/plots/animation | `results/tables`, `results/figures`, `results/animations` and legacy `results/json` | each standalone script writes its own output tree |
| `run_experiment.sh` | unique `results/experiments/<id>` by default | experiment runner owns stage tree; batch master remains the input to validation/reporters |
| `run_batch_limited.py` | `results/batch` by default | wrapper clears prior owned output, runs child processes in temporary trees, merges/copies files, then writes aggregate files |

## Collision and overwrite points

- `BatchRunner::run` calls `clear_previous_batch_output`, which removes
  `runs/`, `master_results.json`, `master_results.csv`, and `batch_summary.md`
  inside the selected output directory before execution. This is destructive
  for direct reuse and can remove complete runs if the directory is reused.
- `run_batch_limited.py` also clears those output names, writes per-run records
  to deterministic paths, and uses `copytree(..., dirs_exist_ok=True)` when
  merging job outputs.
- `run_batch.py` rewrites master JSON, CSV metadata, and batch manifest/
  summary after C++ execution. These writes are in-place and not atomic.
- C++ `DatasetReader::write_json`, `SolutionSerializer::save_json`,
  `TraceWriter::write_csv`, benchmark and comparison writers open final paths
  directly; they do not enforce create-only semantics or atomic publication.
- `benchmark_protocol::write_experiment_manifest` similarly writes a
  caller-specified destination directly.
- `experiment_pipeline.py` has a temp-file-and-replace JSON helper for some
  manifests, but replacement is intentional and there is no artifact-level
  immutable/no-overwrite integration.
- Plot and animation scripts save deterministic filenames under caller
  output directories and may replace an earlier artifact.
- Dataset selection `materialize` removes prior selected materialized files
  listed by an existing manifest before regenerating the same output tree.
- `run_experiment.sh` creates a unique directory by default and refuses an
  existing explicit output path unless resuming. This protects new top-level
  experiments, but does not make nested run files immutable; resume calls the
  batch path whose C++ runner clears its owned outputs.

## Local historical result data

`results/` is wholly ignored by Git (`.gitignore`); `git ls-files results`
returns no tracked files. The inspected machine contains approximately 2.4 GB
under `results/experiments/`, 13 MB under `results/logs/`, plus legacy
`results/comparison`, root calibration/preflight files, and multiple
experiment directories. The 2026-10-03 experiment alone is about 2.4 GB and
contains 8,369 files. Two 2026-10-05 experiment directories contain partial
and/or generated reports and outputs.

These archives have not been moved, rewritten, or deleted. Their manifests
record some commit, dataset and backend provenance, but they lack the future
per-run schema, immutable run IDs, expected-run plan, artifact checksums, and
uniform schema validation. Treat them as legacy/partially trusted until
individually validated; do not silently merge them into new aggregates.

## Post-audit foundation

`scripts/kdc_tools/storage.py` now provides an atomic create-only JSON
publisher, exclusive run-directory creation, path containment checks, and
SHA-256 calculation. Existing producers listed above do not call it yet, and
the C++ batch runner still clears its legacy output tree. The helper is a
foundation, not a claim that the existing result flow is already immutable.
