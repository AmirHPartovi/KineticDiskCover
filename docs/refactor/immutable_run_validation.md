# Immutable run validation

## Canonical layout

New experiments are isolated under `results/experiments/<experiment-id>/`.
The pipeline's `experiment_manifest.json` records configuration, provenance,
reproduction commands, and stage states. `experiment.json` is the validated
storage envelope; it is not a second run-record store. `runs/<run_id>/run.json`
is the single canonical result record for each physical attempt. The layout
includes only output directories created by active stages:

```text
experiment/
  experiment_manifest.json
  experiment.json
  artifacts.json
  dataset/
    selection_manifest.json
  input/
  runs/
    plan.jsonl
    <run_id>/
      request.json
      input_ref.json
      input/instance.json
      execution/
      result/result.json
      verification/verification.json
      run.json
      artifacts.json
  aggregates/
  batch/
  calibration/
  preflight/
  tables/
  figures/
  animations/
  reports/
  logs/
```

`dataset/selection_manifest.json` is present when selection evidence applies;
`input/` contains immutable experiment-scoped input snapshots, while each
run's `input/instance.json` remains the checksum-validated execution input.
Calibration and preflight evidence are recorded only when those stages run.
`artifacts.json` indexes experiment files by safe relative path, SHA-256,
size, and media type; it excludes itself and is refreshed after pipeline
finalization. Per-run `artifacts.json` remains create-only and immutable.

`scripts/kdc_tools/storage.py` is the authority for run IDs, safe paths,
create-only writes, lifecycle transitions, finalization, checksum validation,
recovery, and aggregation. A failed or interrupted attempt retains its run
directory and evidence; a retry receives a new `run_id` and references the
earlier attempt with `rerun_of`.

## Finalization and integrity

A run is reportable only after its terminal `run.json`, result, verification,
backend/provenance metadata, and artifact manifest are present and valid.
`validate_run_directory()` checks the run/status agreement, request and input
documents, canonical input hash, required result documents, and every
registered artifact's path, checksum, and size. The immutable `artifacts.json`
includes the finalized run record and execution evidence but excludes itself
and mutable lifecycle status.

`aggregate_experiment()` reads the plan and validates every run package before
building canonical JSON/JSONL/CSV data, a run index, and the legacy-compatible
`batch/master_results.*` views. The aggregate manifest lists planned/missing,
invalid, failed, retried, and completed attempts. Completion requires each
planned logical run to have a latest valid `COMPLETED` attempt and no missing,
unexpected, or invalid run packages.

Each derived export is checksummed in `aggregates/aggregate_manifest.json`.
Reporting tools call `validate_results_input()` for managed experiments before
reading their batch export; that check requires a complete aggregate, the
current immutable plan digest, and matching checksums and sizes for all listed
exports. `experiment_pipeline.py validate-integrity` additionally validates
the canonical run packages and their artifact checksums.

Standalone table, plot, and animation commands require explicit output paths;
they do not write to shared `results/tables`, `results/figures`, or
`results/animations` defaults. `run_batch.py` checks only a caller-supplied
preflight report; `--force` bypasses the gate without recording preflight as
passed.

## Recovery and retry rules

On resume, the runner requires the existing plan to match the requested
configuration, then calls `recover_experiment()`. Stale `PLANNED`/`RUNNING`
reservations are finalized as recovered failures when possible or marked
`ABANDONED`; evidence is retained. An active process is not retried
concurrently. A retry of a failed logical run reserves a fresh package with a
`rerun_of` link. A completed experiment cannot accept new attempts.

The legacy C++ batch command refuses to overwrite existing batch artifacts.
Standalone C++ benchmark/comparison interfaces remain compatibility tools;
their output is not considered canonical experiment data.

## Validation

The Python tests cover schema rejection, create-only writes, lifecycle
transitions, checksums, concurrent reservations, aggregation gaps, reporting
export integrity, and isolated parallel solver workers. The broader project
validation sequence is:

```sh
bash scripts/local_validation.sh python-tests
bash scripts/local_validation.sh configure-build
bash scripts/local_validation.sh ctest
bash scripts/local_validation.sh experiment smoke
```

Historical ignored `results/` archives are not automatically migrated,
rewritten, or deleted.
