# Dependency map

## Current code paths

### Domain and algorithms

```text
types / Instance
  -> DatasetReader
  -> CandidateSet / InstancePrecompute / StationarySolver
  -> IStaticSolver and registered static algorithms
  -> KineticCore / KineticSolution
  -> MinMaxSolver or MinSumSolver
  -> Verifier / SolutionSerializer
```

Static algorithms use `IStaticSolver`; IP-backed algorithms also use
`ILPSolver`. The registry constructs algorithm implementations. Kinetic
objective modules coordinate static solves, extension/combination, and
verification. `solution.cpp` and `kinetic.cpp` remain geometry-heavy and
cross-cut several of these responsibilities.

### Experiment execution

```text
src/main.cpp
  -> BatchRunner / BenchmarkRunner / AlgorithmComparator / preflight
BatchRunner
  -> DatasetReader, ExactReferenceSelector, solver registry,
     MinMaxSolver, MinSumSolver, TraceWriter, SolutionSerializer,
     thread pool, benchmark manifest writer
scripts/run_experiment.sh
  -> configs/pipeline/*.json via experiment_pipeline.py
  -> build / ctest / preflight / calibrate / run_batch.py
  -> validation / build_tables.py / plot_comparisons.py / animate_best.py
```

The batch runner owns both calculation scheduling and output serialization.
The shell runner and Python pipeline module both participate in state
management. Python wrappers also call the C++ executable as subprocesses.

### Reporting

```text
batch master_results.json
  -> experiment_pipeline.py integrity/summary
  -> build_tables.py
  -> plot_comparisons.py
  -> animate_best.py
```

Standalone plotting/animation tools also accept legacy/global default paths
and parse result/solution data independently. These are separate consumers
without an enforced shared canonical-result reader.

## Layer boundary assessment

- Domain/geometry does not directly import Python reporting or GitHub Actions.
- `main.cpp`, `BatchRunner`, benchmark modules, and comparison modules are
  application/infrastructure code but live in the same target and namespace
  as the mathematical core.
- JSON types and paths enter some public runner APIs; presentation formats
  (CSV/Markdown) are emitted from core C++ application modules.
- No confirmed cyclic header include graph was found in the reviewed headers.
  High fan-in and cross-layer coupling are present; add an automated include
  graph before any architecture move is treated as cycle remediation.
- `CandidateSet::precompute` stores a mutex-protected cache on the `Instance`,
  so this cache is instance-scoped rather than a process-global geometry cache.

## Target dependency direction

```text
domain and numerical policy
  -> geometry and immutable precomputation
  -> static/kinetic algorithms
  -> structured verification
  -> experiment planner and run executor
  -> canonical storage and serialization
  -> aggregate/report/animation adapters
  -> CLI and CI entry points
```

The target should keep algorithms independent of Markdown, plotting,
filesystem layout, workflow environment, and presentation code. Storage owns
path construction and artifact lifecycle; reporters consume validated
aggregates and cannot call solvers.
