# Local generated-state cleanup

**Date:** 2026-10-12
**Scope:** Local working tree only. No build or experiment was started after
cleanup.

## Pre-cleanup inventory

Before deletion, `git status --short` showed no changes under `results/` or
`benchmarks/`; the workflow/documentation changes already present from the
previous task were left untouched. `git ls-files results` listed 8,817
tracked files. All 8,817 files on disk under `results/` were tracked generated
outputs, including the historical experiment, batch, comparison, figure,
animation, log, preflight, selection, and table trees and the root
`results/experiment_manifest.json`. The directory occupied approximately
284 MB. Its top-level tracked-file counts were:

| Path | Files before cleanup | Classification |
|---|---:|---|
| `results/animations/` | 3 | Generated GIFs and `.gitkeep` |
| `results/batch/` | 1,385 | Batch records, exports, traces, and summaries |
| `results/comparison/` | 5 | Generated comparison and calibration artifacts |
| `results/experiments/` | 7,288 | Historical, partial, smoke, reference, and benchmark runs |
| `results/figures/` | 96 | Generated PNG/PDF figures and report |
| `results/local-validation/` | 0 files | Empty generated-run directories |
| `results/logs/` | 4 | Generated logs |
| `results/preflight/` | 3 | Generated preflight reports and `.gitkeep` |
| `results/selection/` | 0 files | Empty generated selection directories |
| `results/tables/` | 32 | Generated table exports and `.gitkeep` |
| `results/experiment_manifest.json` | 1 | Generated root manifest |

The suffix inventory included 4,988 JSON, 1,429 CSV, 599 log, 426 Markdown,
318 PNG, 318 PDF, 282 JSONL, 270 text, 183 GIF, and 4 `.gitkeep` files. The
two benchmark result CSVs, `benchmarks/kinetic_certification_results.csv`
(about 1 MB) and `benchmarks/kinetic_handover_results.csv` (about 3.3 KB),
were tracked and timestamped 2026-10-12. Their headers and contents are
generated campaign measurements; the adjacent benchmark `.cpp` files are
implementation sources and were not removed.

The existing `build/` occupied approximately 165 MB and contained CMake
configuration/generated files, fetched dependency sources and build trees,
compiled solver/test/benchmark targets, CTest output, and local validation
output. Python caches present before cleanup were `.pytest_cache/`,
`tests/python/.pytest_cache/`, and `tests/python/__pycache__/`. No repository
virtual environment or `cmake-build-*` directory was found.

## Removed and preserved

Removed all non-`.gitkeep` files below `results/` and the now-empty generated
run/selection directories. The four tracked placeholders
`results/{animations,figures,preflight,tables}/.gitkeep` remain. This deletes
8,813 tracked historical result files from the local working tree; Git reports
those local deletions because the files were tracked. Nothing was staged, and
this local cleanup does not remove files from any remote repository.

Removed the two named generated benchmark result CSVs, the complete local
`build/` tree, the observed Python cache directories, and any repository-local
`profile_phases.json` if present. The benchmark implementation sources remain:
`benchmarks/kinetic_certification.cpp`,
`benchmarks/kinetic_handover.cpp`, and
`benchmarks/kinetic_interval_emission.cpp`.

Preserved `src/` (40 files), `include/` (45), `tests/` (48 source/test files),
`docs/schemas/` (12), `scripts/` (16), `CMakeLists.txt`, `requirements.txt`,
pipeline configs, all of `data/`, and source/math/verifier documentation.
The data tree had 854 files before cleanup and still has 854 afterward (853
Git-tracked input files plus the pre-existing ignored
`data/instances/.DS_Store`). Representative pre/post SHA-256 values match:

| Preserved input | SHA-256 |
|---|---|
| `data/test_sets/smoke10.json` | `254611872890e801e6871d938b2ff0a6ca393a9af99f8d17321688880bb0c1db` |
| `data/instances/public_instance_set/att48.mdc` | `8efa61a1d9ab8fa13553ad56c76096e4c1a178a27ce03d87ca080f6c8d58a91f` |
| `data/instances/public_instance_set/berlin52.mdc` | `3939d9e3cc19729916df1e7e8bfb198827657566a94afdd70ba5aa630f47af7d` |

## `.gitignore`

Added ignore rules for the repository-root `results/`, generated CMake/CTest
files and directories, profiling outputs, common Python caches, and the three
narrowly named benchmark result CSVs. Benchmark source files are not ignored.
Build directories and Python caches are local generated state; the two
benchmark CSVs and historical results were tracked before cleanup, so ignore
rules do not hide their deletions or untrack them.

## Post-cleanup verification

* `results/` contains exactly four `.gitkeep` files; no historical result
  records, experiments, figures, animations, logs, tables, or generated
  manifests remain.
* `build/` is absent; no `cmake-build-*`, Python cache, virtual-environment,
  or profiling-output paths remain in the repository.
* Both generated benchmark CSVs are absent and all three benchmark `.cpp`
  implementations remain.
* `src/`, `include/`, `tests/`, `docs/schemas/`, `scripts/`, pipeline
  configuration, and source datasets remain present. The input file count and
  representative hashes are unchanged.
* `git check-ignore --no-index -v` confirmed that future `results/`,
  benchmark-result CSV, `build/CMakeCache.txt`, and `profile_phases.json`
  paths are covered by the intended rules. `git diff --check` exited 0.
* `git status --short` reports the expected tracked deletions: 8,813 files
  under `results/` and the two benchmark CSVs, in addition to unrelated
  pending changes from earlier tasks. The cleanup report and `.gitignore`
  update are local changes. No build or new smoke experiment was run.
