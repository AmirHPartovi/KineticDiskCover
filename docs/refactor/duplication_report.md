# Duplication report

## Duplicated responsibilities

| Concern | Current locations | Consequence / direction |
|---|---|---|
| Experiment execution | `src/benchmark.cpp`, `src/batch_runner.cpp`, `src/comparative_runner.cpp`, `src/algorithm_comparison.cpp`, `scripts/run_batch.py`, `scripts/run_batch_limited.py` | Different execution policies and record shapes; converge on one planned run interface while preserving purpose-specific adapters |
| Manifest production | `src/benchmark_protocol.cpp`, benchmark/batch/comparison runners, `scripts/experiment_pipeline.py`, both Python batch scripts | Several manifests per experiment and no shared schema/ownership |
| Config defaults | `configs/pipeline/*.json`, `run_experiment.sh`, Python parsers, C++ config structs/CLI defaults, workflow `inputs` | Defaults and overrides can diverge; normalize once and record requested plus resolved config |
| Dataset parsing and path resolution | `src/io.cpp`, `scripts/prepare_real_instances.py`, `scripts/select_test_instances.py`, `experiment_pipeline.py`, both batch wrappers | Conversion, canonical loading, manifest resolution and metadata extraction overlap |
| Result interpretation | C++ record serializers, Python annotation/validation, table/plot/animation parsers | A field's semantics can drift across consumers |
| Batch result writing | `BatchRunner::save_master`, `BenchmarkRunner` writers, comparison writers, `run_batch.py`, `run_batch_limited.py` | JSON/CSV/summary formats evolve independently |
| Family classification | selector, table builder, plot comparison and report code | Filename heuristics may produce inconsistent dataset-family grouping |
| Path sanitization | `safe_name` in limited wrapper; `_safe_filename` in animation/plot tools; C++ `sanitize_path_component` | Different collision and traversal behavior |
| Reports and figures | `plot_results.py`, `plot_comparisons.py`, `build_tables.py`, experiment pipeline summaries | Both legacy and newer report paths remain supported without one shared validated input model |
| Animation handling | `animate_solution.py`, `animate_best.py`, exact animation paths in pipeline | Two entry points independently load instance/solution formats and save derived output |

## Configuration split

The three JSON profiles provide a useful current source for pipeline values,
but their policy is not the sole configuration authority: shell defaults,
Python argument defaults, C++ `BatchRunConfig`/`BenchmarkConfig`, and
workflow-dispatch inputs continue to supply behavior. Workflow values are
forwarded as CLI overrides. The normalized precedence and requested-versus-
resolved difference are not yet represented consistently.

## Refactoring rule

Extract shared semantics behind focused APIs (configuration, input catalog,
schemas, storage, aggregate readers) rather than introducing a general-purpose
utility module. Preserve standalone legacy commands as adapters until their
outputs can be generated from canonical runs.
