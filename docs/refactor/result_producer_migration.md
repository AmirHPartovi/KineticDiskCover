# Result producer migration

This inventory covers the solver and reporting producers identified in
`src/`, `include/`, `scripts/`, and the experiment workflows. The canonical
storage API is `scripts/kdc_tools/storage.py`. The Python batch orchestrator
reserves one isolated run directory per solver invocation and derives
aggregates from finalized, checksum-validated run packages.

| Producer | Current output path | Current format | Current writer | Current source of truth | Shared or isolated | Overwrite risk | New storage destination | Migration status |
|---|---|---|---|---|---|---|---|---|
| C++ `BatchRunner` via `kdc-solver batch` | Required explicit `--output DIR` | JSON, CSV, Markdown, optional solution JSON/trace CSV | `src/batch_runner.cpp` | C++ in-memory records and per-run files | Caller scoped | Refuses to overwrite existing batch artifacts; direct `ofstream` writers are still legacy | `results/experiments/<id>/runs/<run_id>/` through the Python orchestrator; C++ output is confined under that run | Isolated by orchestrator; standalone CLI remains a non-canonical compatibility producer |
| `scripts/run_batch.py` | Experiment batch output, optionally legacy caller `--output` | JSON, CSV, Markdown, manifest | Python preflight wrapper and immutable orchestrator | Validated canonical run packages | Experiment-scoped by default | Immutable run reservation; derived compatibility outputs may be regenerated | Experiment-scoped run directories; compatibility batch files generated after aggregation | Integrated |
| `scripts/run_batch_limited.py` | Experiment batch output | JSON, CSV, Markdown, manifests, trace CSV/JSONL | Python orchestrator plus isolated C++ subprocesses | Finalized, checksum-validated run packages | Isolated per run | Create-only canonical artifacts; aggregate exports are replaceable derived views | Reserved run directories and aggregate exports from validated run records | Integrated |
| Exact-backend calibration | Caller output, often experiment `calibration/` or `results/comparison/` | JSON manifest/report | `src/exact_reference_selector.cpp`, `src/benchmark_protocol.cpp` | Calibration decision and backend probe | Output-directory scoped | Manifest may be rewritten | Experiment-level immutable backend calibration/provenance | Remains experiment metadata; per-run decision copied into each run |
| `BenchmarkRunner` | Caller directory, commonly `results/` | JSON/CSV, manifest | `src/benchmark.cpp`, `src/benchmark_protocol.cpp` | In-memory benchmark records | Caller scoped | Direct writer can replace existing files | Adapt to the same run storage and aggregate API | Not yet migrated |
| `ComparativeRunner` / `AlgorithmComparator` | Caller directory, commonly `results/comparison/` | Comparison JSON/CSV and solutions | `src/comparative_runner.cpp`, `src/algorithm_comparison.cpp` | In-memory comparative records | Caller scoped | Direct writes to deterministic names | One run per algorithm/objective under the experiment; comparison becomes a derived aggregate/report | Not yet migrated |
| `SolutionSerializer` | Caller-provided path, commonly nested in batch/comparison output | JSON | `src/solution_serializer.cpp` | `KineticSolution` and `Instance` | Path scoped | Opens final destination directly | `runs/<run_id>/result/solution.json` | Used by isolated C++ invocation; direct writer atomicity remains |
| `TraceWriter` | Caller-provided path, commonly `trace.csv` | 7-column CSV | `src/trace.cpp` | Iteration trace vector | Path scoped | Opens final destination directly | Run-local trace under `runs/<run_id>/result/`; aggregate trace is never shared | Run-local via isolated C++ invocation; JSONL conversion remains |
| C++/Python preflight | Required explicit output path | Markdown and summary | `src/main.cpp`, `scripts/preflight_check.py` | Probe results | Caller scoped; pipeline uses `<experiment>/preflight/` | Deterministic report replacement at the supplied path | `experiments/<id>/preflight/` as experiment evidence | Global default removed |
| `scripts/experiment_pipeline.py` | `experiment_manifest.json`, `reports/`, `dataset/`, `animations/`, `artifacts.json` | JSON, Markdown, copied inputs | Python pipeline commands | Pipeline stage manifest plus canonical run store | Experiment scoped | Derived reports/manifests are refreshed on validated resume | Experiment root with immutable run packages, aggregate views, and artifact checksums | Integrated with canonical storage and artifact indexing |
| `scripts/build_tables.py` | Required explicit output directory | CSV, Markdown | Python/pandas | Input JSON/CSV records | Derived experiment output | Replaces derived deterministic names in the supplied directory | `experiments/<id>/tables/` from validated aggregate | Explicit input/output required |
| `scripts/plot_results.py` | Required explicit output directory | PNG, Markdown report | Python/matplotlib | Input result file | Derived experiment output | Replaces derived deterministic names in the supplied directory | `experiments/<id>/figures/` from validated aggregate | Explicit input/output required |
| `scripts/plot_comparisons.py` | Required explicit output directory | PNG/PDF and report | Python/matplotlib | Input result file | Derived experiment output | Replaces derived deterministic names in the supplied directory | `experiments/<id>/figures/` from validated aggregate | Explicit input/output required |
| `scripts/animate_best.py` | Required explicit output directory | GIF/MP4 and selected solution | Python/matplotlib | Batch rows plus per-run solution paths | Derived experiment output | Replaces derived deterministic names in the supplied directory | `experiments/<id>/animations/<objective>/<instance>__<run_id>.*` | Explicit batch and output paths; managed aggregate validation retained |
| `scripts/animate_solution.py` | Required explicit output directory | GIF/MP4 | Python/matplotlib | Explicit instance and solution paths | Derived experiment output | Replaces derived deterministic names in the supplied directory | Run-linked animation under experiment with source `run_id` metadata | Explicit output required |
| `scripts/select_test_instances.py` | Caller output directory and selection manifest | Canonical input JSON and JSON manifest | Python selector/materializer | Source dataset plus selection manifest | Dataset-scoped | Materialize removes prior selected files listed by manifest | Immutable experiment `input/` snapshots referenced by run `input_ref.json` | Selection remains; snapshots added at execution boundary |

## Storage API responsibility

Only `scripts/kdc_tools/storage.py` owns run ID allocation, run reservation,
safe run-local paths, lifecycle state transitions, immutable JSON/artifact
publication, checksum calculation, finalization, run validation, recovery,
and aggregation. The experiment-level `artifacts.json` is a derived inventory;
the per-run `run.json` records remain the sole canonical attempt results.
Worker code may create solver-local files beneath its
reserved run root, but it may not construct aggregate or shared result paths.

The standalone C++ `batch`, `benchmark`, and comparison commands remain
callable for legacy/manual use. Batch now refuses to replace its existing
result files and requires an explicit output path, but these direct C++ outputs are not canonical experiment
sources unless imported through an explicit adapter. Existing ignored
historical `results/` data is not rewritten or migrated.
