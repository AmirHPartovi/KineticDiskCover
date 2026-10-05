# Result producer migration

This inventory covers the solver and reporting producers identified in
`src/`, `include/`, `scripts/`, and the experiment workflows. The canonical
storage API is `scripts/kdc_tools/storage.py`. The Python batch orchestrator
reserves one isolated run directory per solver invocation and derives
aggregates from finalized, checksum-validated run packages.

| Producer | Current output path | Current format | Current writer | Current source of truth | Shared or isolated | Overwrite risk | New storage destination | Migration status |
|---|---|---|---|---|---|---|---|---|
| C++ `BatchRunner` via `kdc-solver batch` | `<output>/runs/<instance>/<algorithm>/<objective>/`, plus master JSON/CSV/summary | JSON, CSV, Markdown, optional solution JSON/trace CSV | `src/batch_runner.cpp` | C++ in-memory records and per-run files | Depends on caller; default remains legacy `results/batch` | Refuses to overwrite existing batch artifacts; direct `ofstream` writers are still legacy | `results/experiments/<id>/runs/<run_id>/` through the Python orchestrator; C++ output is confined under that run | Isolated by orchestrator; standalone CLI remains a non-canonical compatibility producer |
| `scripts/run_batch.py` | Experiment batch output, optionally legacy caller `--output` | JSON, CSV, Markdown, manifest | Python preflight wrapper and immutable orchestrator | Validated canonical run packages | Experiment-scoped by default | Immutable run reservation; derived compatibility outputs may be regenerated | Experiment-scoped run directories; compatibility batch files generated after aggregation | Integrated |
| `scripts/run_batch_limited.py` | Experiment batch output | JSON, CSV, Markdown, manifests, trace CSV/JSONL | Python orchestrator plus isolated C++ subprocesses | Finalized, checksum-validated run packages | Isolated per run | Create-only canonical artifacts; aggregate exports are replaceable derived views | Reserved run directories and aggregate exports from validated run records | Integrated |
| Exact-backend calibration | Caller output, often experiment `calibration/` or `results/comparison/` | JSON manifest/report | `src/exact_reference_selector.cpp`, `src/benchmark_protocol.cpp` | Calibration decision and backend probe | Output-directory scoped | Manifest may be rewritten | Experiment-level immutable backend calibration/provenance | Remains experiment metadata; per-run decision copied into each run |
| `BenchmarkRunner` | Caller directory, commonly `results/` | JSON/CSV, manifest | `src/benchmark.cpp`, `src/benchmark_protocol.cpp` | In-memory benchmark records | Caller scoped | Direct writer can replace existing files | Adapt to the same run storage and aggregate API | Not yet migrated |
| `ComparativeRunner` / `AlgorithmComparator` | Caller directory, commonly `results/comparison/` | Comparison JSON/CSV and solutions | `src/comparative_runner.cpp`, `src/algorithm_comparison.cpp` | In-memory comparative records | Caller scoped | Direct writes to deterministic names | One run per algorithm/objective under the experiment; comparison becomes a derived aggregate/report | Not yet migrated |
| `SolutionSerializer` | Caller-provided path, commonly nested in batch/comparison output | JSON | `src/solution_serializer.cpp` | `KineticSolution` and `Instance` | Path scoped | Opens final destination directly | `runs/<run_id>/result/solution.json` | Used by isolated C++ invocation; direct writer atomicity remains |
| `TraceWriter` | Caller-provided path, commonly `trace.csv` | 7-column CSV | `src/trace.cpp` | Iteration trace vector | Path scoped | Opens final destination directly | Run-local trace under `runs/<run_id>/result/`; aggregate trace is never shared | Run-local via isolated C++ invocation; JSONL conversion remains |
| C++/Python preflight | Explicit report path or `results/preflight/` | Markdown and summary JSON | `src/main.cpp`, `scripts/preflight_check.py` | Probe results | Global default unless pipeline supplies explicit experiment path | Deterministic report replacement | `experiments/<id>/preflight/` as experiment evidence | Pipeline-scoped; standalone default remains legacy |
| `scripts/experiment_pipeline.py` | `experiment_manifest.json`, `reports/`, `dataset/`, `animations/` | JSON, Markdown, copied inputs | Python pipeline commands | Stage manifest and dataset snapshot | Experiment scoped | Manifests/reports intentionally atomically replaced on resume | Experiment root with immutable run packages and explicitly derived reports | Stage orchestration retained; needs canonical aggregation integration |
| `scripts/build_tables.py` | Caller output, historically `results/tables/` | CSV, Markdown | Python/pandas | Input JSON/CSV records | Derived output tree | Replaces deterministic output filenames | `experiments/<id>/tables/` from validated aggregate | Managed experiment inputs require a complete aggregate and verified export checksums |
| `scripts/plot_results.py` | Caller output, historically `results/figures/` | PNG, Markdown report | Python/matplotlib | Input result file | Derived output tree | Replaces deterministic output filenames | `experiments/<id>/figures/` from validated aggregate | Explicit path supported; lineage manifest pending |
| `scripts/plot_comparisons.py` | Caller output, historically `results/figures/` | PNG/PDF and report | Python/matplotlib | Input result file | Derived output tree | Replaces deterministic output filenames | `experiments/<id>/figures/` from validated aggregate | Managed experiment inputs require a complete aggregate and verified export checksums |
| `scripts/animate_best.py` | Caller output, historically `results/animations/` | GIF/MP4 and selected solution | Python/matplotlib | Batch rows plus per-run solution paths | Derived output tree | Replaces output with deterministic name | `experiments/<id>/animations/<objective>/<instance>__<run_id>.*` | Validates the complete aggregate before reading managed batch rows; output sidecar records source run/checksum |
| `scripts/animate_solution.py` | Caller output directory | GIF/MP4 | Python/matplotlib | Explicit instance and solution paths | Derived output tree | Replaces deterministic names | Run-linked animation under experiment with source `run_id` metadata | Not yet migrated |
| `scripts/select_test_instances.py` | Caller output directory and selection manifest | Canonical input JSON and JSON manifest | Python selector/materializer | Source dataset plus selection manifest | Dataset-scoped | Materialize removes prior selected files listed by manifest | Immutable experiment `input/` snapshots referenced by run `input_ref.json` | Selection remains; snapshots added at execution boundary |

## Storage API responsibility

Only `scripts/kdc_tools/storage.py` owns run ID allocation, run reservation,
safe run-local paths, lifecycle state transitions, immutable JSON/artifact
publication, checksum calculation, finalization, run validation, recovery,
and aggregation. Worker code may create solver-local files beneath its
reserved run root, but it may not construct aggregate or shared result paths.

The standalone C++ `batch`, `benchmark`, and comparison commands remain
callable for legacy/manual use. Batch now refuses to replace its existing
result files, but these direct C++ outputs are not canonical experiment
sources unless imported through an explicit adapter. Existing ignored
historical `results/` data is not rewritten or migrated.
