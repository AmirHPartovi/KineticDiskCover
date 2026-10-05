# Experiment pipeline audit

## Current architecture

`configs/pipeline/{smoke,reference,full}.json` drives shared defaults.
`scripts/run_experiment.sh` parses the profile and CLI overrides, creates an
experiment directory, and records stages by calling
`scripts/experiment_pipeline.py`. The Python module resolves config, snapshots
or converts datasets, writes the experiment manifest, validates master
results, produces summaries, selects animation inputs, and finalizes status.
The shell invokes CMake, CTest, preflight, calibration, `run_batch.py`,
`build_tables.py`, `plot_comparisons.py`, and `animate_best.py`.

The three workflows are thin wrappers around the same runner and upload
separate experiment paths. The local profile and Action inputs are not yet
backed by one typed schema; CLI validation lives in Bash/Python/C++.

## Stage-model gaps against the requested contract

Current required stages are `init`, `dataset`, `build`, `tests`, `preflight`,
`backend_selection`, `benchmark`, `result_validation`, `tables`, `figures`,
`animations`, `final_report`, and `summary`. Calibration is called inside
`backend_selection`; run planning and aggregation are not separate stages.
The run-stage records do not uniformly include input/output artifact lists.
`research_summary` is also recorded in some flows outside the required list.
Skipped required work causes a partial outcome, but skip policy is coupled to
the shell CLI/config flags rather than a general stage contract.

The full CLI requested in the brief (`--dry-run`, stage bounds and run-plan
preview) is not implemented. Resume compares several config/dataset values
and resumes stage operations, but it does not reconcile a run matrix or
protect complete individual runs.

## Manifest and completion findings

- The Python experiment manifest is `experiment_manifest.json`; the batch
  runner and exact calibration can write additional manifests with the same
  filename in subtrees.
- `finalize` embeds solver/calibration manifests and sets COMPLETE when the
  required stage statuses are completed. It does not verify canonical
  per-run schemas, full run-matrix classification, aggregate lineage,
  artifact inventory, or experiment checksums.
- Benchmark integrity checks compare results to expected record counts and
  statuses, but the expected contract is inferred from the records/config and
  not persisted as `runs/plan.jsonl` before execution.
- Animation/report generation reads the batch master JSON, so aggregates
  currently act as the source for further processing.

## Resource and backend notes

Profile values provide reasonable defaults, but are supplemented by
`BatchRunConfig`, Python wrappers, Bash parsing, and workflow input defaults.
KONT/COPT detection/calibration is correctly separate from exact backend
execution in the existing backend model, though every per-run result does not
yet carry complete structured backend-detection and calibration lineage.
GitHub Actions allows AUTO and explicit backend choices; it does not provision
native KONT by default.
