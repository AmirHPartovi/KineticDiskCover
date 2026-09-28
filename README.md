# kdc-solver

`kdc-solver` is a C++17 project scaffold for kinetic discrepancy correction
models. Its optimization backend is KONT, which uses the COPT-compatible API.

## Prerequisites

* CMake 3.20 or newer.
* A compiler with C++17 support.
* KONT is optional. When available, set `KONT_ROOT` if it is not installed
  in a standard search path; otherwise the built-in exact fallback and
  `MockILPSolver` can be used.

## Build

```sh
mkdir build && cd build && cmake .. && make -j
```

If KONT is installed in a non-standard location:

```sh
cmake .. -DKONT_ROOT=/path/to/kont
make -j
```

When KONT is unavailable, omit `KONT_ROOT`; CMake builds with the built-in
fallback backend.

## Tests

```sh
ctest --output-on-failure
```

Catch2 tests cover the data model, dataset I/O, candidate disks, and coverage
matrix.

Run the complete pre-flight workflow to configure, build, test, check the
registered algorithms and pipelines, and write Markdown reports:

```sh
python3 scripts/preflight_check.py [--clean] [--kont-root /path/to/kont]
```

The CLI check can also be run directly with
`kdc-solver preflight [--output FILE]`. Its report and the workflow summary are
written under `results/preflight/`; without a usable KONT backend, checks use
`MockILPSolver` and report that fallback explicitly.

Run all registered static solvers against every JSON instance in both
objectives with:

```sh
python3 scripts/run_batch.py [--parallel --threads N] [--algorithms nn,greedy]
```

The wrapper requires a passing pre-flight report unless `--force` is supplied.
Each static/IP subsolve is limited to 10 seconds by default; override this
with `--time-limit SEC`. Algorithms that perform multiple static solves per
kinetic objective (notably MinSum) can therefore take longer than one timeout
in total. The configured per-subsolve timeout is recorded in each result.
Batch artifacts are written beneath `results/batch/`, including per-run
solutions, result metadata, convergence traces, master JSON/CSV, and a summary.

For a strict wall-clock cap on each individual
instance/algorithm/objective combination, use
`python3 scripts/run_batch_limited.py --instances DIR --output results/batch
--algorithms all --modes both --threads 4 --run-timeout 10`. Each combination
runs in its own process, timed-out combinations are recorded as failed, and
already completed results within the same limit are reused on restart.

Generate Markdown and CSV master, per-algorithm, per-instance, and per-family
tables from the batch master JSON with:

```sh
python3 -m pip install -r requirements.txt
python3 scripts/build_tables.py
```

The generated index, tables, and failed-run report are written under
`results/tables/`. Use `--format markdown` or `--format csv` to select a single
output format.

Create runtime, memory, quality, Pareto, and convergence charts (PNG and PDF)
from the batch results with:

```sh
python scripts/plot_comparisons.py
```

The script writes figures and `REPORT.md` under `results/figures/`. Use
`--sections A1,C2` to generate selected chart sections, `--dpi N` to choose
figure resolution, and `--fixed-m N` / `--fixed-n N` to set scaling-chart
subsets.

## Commands

```text
kdc-solver solve --instance FILE [--mode minmax|minsum] [--algorithm NAME]
                  [--output FILE] [--time-limit SEC] [--gap TARGET]
                  [--no-handovers] [--no-dedup] [--no-partial]
kdc-solver verify --instance FILE --solution FILE
kdc-solver benchmark --dataset DIR --output DIR --mode both|minmax|minsum
```

The `solve` command defaults to the `minmax` objective and `ip-kont` static
solver. Its optional output is a JSON `KineticSolution` that can be loaded with
`SolutionSerializer`. `--help` displays command usage.

## Dataset I/O

`kdc::DatasetReader` reads and writes JSON instances and reads the simple text
format (`n m`, followed by station coordinates and linear trajectory endpoints).
It validates trajectory time breaks and instance dimensions. Random datasets
can be generated reproducibly with an explicit seed.

`kdc::CandidateSet` creates station/point disks ordered by their initial
distance and constructs point-major CSR coverage matrices at a requested time.

`kdc::StationarySolver::solve_nn` builds a nearest-station assignment and
reports station radii, disk-area cost, and point-coverage feasibility at a
requested time.

`kdc::StationarySolver::solve_ip` builds a binary disk-cover model through an
`ILPSolver`, reconstructs station radii from the selected disks, and can return
the solver lower bound and status.

`kdc::KineticCore` provides stable quadratic roots, support-change and
handover event detection across piecewise-linear trajectories, second-furthest
assigned-support selection, and derivative-based resolution for equidistant
supports.

`kdc::KineticSolution` stores piecewise quadratic cost intervals and evaluates
costs, integrals, peak values/times, and structural consistency. It can extend
a stationary assignment across kinetic support and handover events, combine
two interval solutions under either objective, and truncate an extension at an
integral intersection.

`kdc::MinMaxSolver` iteratively solves stationary IPs at the current peak time,
extends and combines the resulting kinetic assignments, tracks a monotone
relative-gap trace, and can verify point coverage and interval costs.

`kdc::MinSumSolver` builds a sampled stationary lower-bound integral, refines
the sample with contribution-guided stationary IP solves, and combines
lower-integral kinetic assignments while tracking the relative-gap trace.

`kdc::StaticSolverRegistry` provides the `nn`, `greedy`, `lp-rounding`,
`primal-dual`, `local-search`, `sa`, `genetic`, and `shifting` heuristics and
`ip-kont`, `brute-force`, and `branch-and-bound` exact static solvers behind a
common interface. Select one for CLI solves with `--algorithm nn`,
`--algorithm greedy`, `--algorithm lp-rounding`, `--algorithm primal-dual`,
`--algorithm local-search`, `--algorithm sa`, `--algorithm genetic`,
`--algorithm shifting`, `--algorithm ip-kont`, `--algorithm brute-force`, or
`--algorithm branch-and-bound`; `ip-kont` remains the default. Brute force
and branch-and-bound are bounded by their configured limits and expose valid
lower bounds when interrupted.

`kdc::Verifier` reports coverage, support, assignment, pointwise-cost, and
integral-consistency checks separately, including the maximum observed
coverage violation.

`kdc::BenchmarkRunner` runs either or both objectives over JSON datasets and
writes timestamped JSON and CSV summaries under the requested output directory.
For example: `kdc-solver benchmark --dataset data --output results --mode both`.

`kdc-solver compare --algorithms greedy,ip-kont --dataset data --output results/comparison --mode static`
compares static solvers at configured sample times and writes JSON/CSV records
plus verification and statistical-analysis Markdown reports.

Use `--mode minmax` or `--mode minsum` to compare kinetic solutions instead.
These modes write `<mode>_results.json` and `<mode>_results.csv` alongside
verification and statistical-analysis reports in the output directory.

The benchmark command supports `--parallel --threads N` to process dataset
instances concurrently. Each parallel instance task owns an independent KONT
solver instance; omit `--parallel` to run sequentially.

`scripts/animate_best.py` selects the best feasible, verified solver result
for each instance and renders a scene-and-cost animation for the requested
objective. For example:

```sh
python scripts/animate_best.py --mode both
```

It writes one animation per instance/objective under `results/animations/`,
choosing MP4 when ffmpeg is available and otherwise falling back to GIF. Use
`--frames`, `--fps`, `--top-n`, and `--output` to control rendering.

`kdc::KontSolver` implements the ILP solver interface and uses the COPT-
compatible KONT C++ API when its required symbols are available. If an
installation only provides the C API or a stub, it reports the exact
branch-and-bound backend in its result message rather than silently treating
the stub as a working KONT solver. `kdc::DummyLPAdapter` solves the continuous
relaxation for small test models.

## Directory structure

Public headers are in `include/kdc`, implementations in `src`, and tests in
`tests`. Input data belongs in `data`; helper scripts are in `scripts`;
generated JSON, CSV, figures, and logs go in `results/json`, `results/csv`,
`results/figures`, and `results/logs`. Optional third-party sources belong in
`third_party`.
