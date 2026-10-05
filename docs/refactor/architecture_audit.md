# Architecture audit

**Audit basis:** the current `amirhosseinpartovi0-kinetic-disk-cover` worktree,
including its existing uncommitted pipeline and backend-provenance changes.
The audit findings describe the code before the schema/storage foundation
described below was added. No solver behavior was changed. Historical files
under the ignored `results/` directory were inspected read-only.

## Post-audit foundation

The first implementation slice now adds Draft 2020-12 schemas under
`docs/schemas/`, strict JSON/schema helpers in `scripts/kdc_tools/schemas.py`,
and create-only run-directory/artifact primitives in
`scripts/kdc_tools/storage.py`. These are not yet wired into existing batch,
benchmark, experiment-manifest, or reporting producers; the storage risks
below remain current until those integrations are completed.

## Current structure

- `include/kdc/` exposes domain types and algorithms alongside solver
  interfaces, CLI-facing benchmark/batch runners, serialization, profiling,
  and infrastructure APIs. Only the individual algorithms have a nested
  subdirectory.
- `src/` is similarly mostly flat. `src/algorithms/` contains the registered
  static solvers. `main.cpp` is the CLI composition root; `batch_runner.cpp`,
  `benchmark.cpp`, `algorithm_comparison.cpp`, and `comparative_runner.cpp`
  each contain orchestration and/or output code.
- Kinetic geometry, interval construction, event detection, and verification
  are primarily in `candidate.cpp`, `kinetic.cpp`, `solution.cpp`, and
  `verify.cpp`. MinMax and MinSum have separate solver modules.
- `scripts/` contains 11 Python entry points and one Bash pipeline runner.
  Dataset selection/conversion, execution wrappers, stage orchestration,
  tables, plots, and animations are not yet split into shared packages.
- `configs/pipeline/` contains the three JSON pipeline profiles. `.github/
  workflows/` contains the corresponding three dispatch workflows.
- `tests/` has a flat C++ unit/integration suite and `tests/python/`; C++
  fixtures are largely generated in code, while Python tests use temporary
  files and small inline datasets.
- `data/` contains 853 tracked files across generated families, the public
  instance set, and smoke manifests. `results/` is ignored by Git and contains
  local result data rather than repository fixtures.

## Dependency direction

The intended runtime path is:

```text
Instance/types
  -> candidate and stationary geometry
  -> static solver registry / static algorithms
  -> MinMax or MinSum kinetic optimization
  -> verification and solution serialization
  -> benchmark/batch orchestration
  -> Python aggregation, tables, figures, and animations
```

`main.cpp` composes most layers directly. `BatchRunner` reaches into dataset
I/O, exact-backend selection, both objective solvers, static registry,
candidate precomputation, trace writing, solution serialization, profiling,
threading, and result/report output. The intended domain-to-presentation
boundary is therefore not enforced by module structure.

The headers inspected do not show a confirmed cyclic include chain. They do
show broad cross-layer interfaces: for example, `istatic_solver.hpp` includes
stationary assignment APIs, and benchmark/batch headers expose JSON and
configuration details. Treat this as a dependency-direction problem rather
than asserting a compile-time include cycle without a dedicated graph check.

## Oversized responsibilities

Largest implementation files by current line count include:

| File | Lines | Audit observation |
|---|---:|---|
| `src/batch_runner.cpp` | 1,265 | Selection, scheduling, solving, validation, serialization, CSV/summary writing |
| `scripts/experiment_pipeline.py` | 1,246 | Config resolution, dataset snapshot, stage state, validation, reporting, animation policy |
| `scripts/plot_comparisons.py` | 1,207 | Data loading, many plot families, report generation, CLI |
| `src/verify.cpp` | 1,079 | Several distinct verification and polynomial/event checks |
| `src/kont_solver.cpp` | 1,006 | Native runtime loading, native adapter calls, fallback, status translation |
| `src/benchmark.cpp` | 964 | Benchmark execution, result parsing, serialization, output |
| `src/minsum.cpp` | 884 | Objective solver and refinement strategies |
| `src/solution.cpp` | 873 | Kinetic extension, ownership, combination, integration, interval utilities |
| `src/main.cpp` | 820 | CLI dispatch, option parsing, execution setup, report output |
| `src/kinetic.cpp` | 752 | Trajectory geometry, support/handover event detection |
| `scripts/run_experiment.sh` | 602 | Pipeline config parsing and end-to-end stage orchestration |

These are candidates for responsibility extraction, not automatic file-splitting
targets; future changes should follow call boundaries and regression tests.

## Main audit findings

1. There are multiple execution surfaces (`benchmark`, `batch`, `compare`,
   limited batch wrapper, and the experiment pipeline) with overlapping
   configuration, result and backend responsibilities.
2. The pipeline's experiment directory is isolated by default, but that
   property is not enforced by the lower-level C++/Python APIs.
3. Batch output records, experiment manifests, comparison output and solution
   files use different shapes and version markers. A canonical schema
   registry now exists for new experiment documents, but current producers
   and compatibility readers are not yet integrated with it.
4. Run identity is mostly a deterministic filesystem tuple
   (instance/algorithm/objective/repeat), not a unique execution ID. A
   repeated physical execution can therefore target the same output path.
5. The current experiment manifest, C++ batch manifest, and backend
   calibration manifest coexist. Their relationship is copied/merged by the
   shell/Python layer instead of represented by one canonical manifest model.
6. Dataset snapshotting exists, but per-instance source/canonical hashes and
   stable instance identities are not uniformly carried from selection to
   every run.
7. The result pipeline's checks are stage/artifact-oriented; it does not yet
   create and validate a complete expected run matrix before execution.
8. Reports and animations consume master batch arrays, not a validated
   canonical per-run store with lineage.
9. Domain algorithms are already separated from plotting and Actions, but
   experiment infrastructure is mixed into core source files and shared
   output defaults remain available.
10. Existing objective, bound, backend, ownership, and verification semantics
    are documented and tested in several places. Refactoring these semantics
    before establishing schemas and stronger regression fixtures would be
    unnecessarily risky.

## Audit limits

This audit inventories all requested top-level areas and traces the main
execution/output surfaces. It does not claim a formal whole-program call
graph, exhaustive numerical profile, or content-level validation of every
tracked instance. Those require dedicated tooling and are included as planned
follow-up validation where relevant.
