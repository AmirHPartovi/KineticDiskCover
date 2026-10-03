# kdc-solver

`kdc-solver` is a C++17 project scaffold for kinetic discrepancy correction
models. Its optimization backend is KONT, which uses the COPT-compatible API.

## Reproducible experiment pipeline

Run the end-to-end research workflow from any current working directory with:

```bash
bash scripts/run_experiment.sh
```

The orchestrator snapshots or converts the dataset, runs preflight and the FAST
benchmark, validates raw records, generates tables and PNG/PDF figures, then
uses the selected exact backend for separate animation solves. Each run is
stored under `results/experiments/<EXPERIMENT_ID>/` with stage logs, a complete
manifest, integrity checks, and reports. Use `bash scripts/run_experiment.sh --help`
for dataset, algorithm, objective, time-limit, backend, animation,
skip, and resume options. Resume requires the same dataset fingerprint and
benchmark configuration.

FAST keeps the existing low-serialization profile. Exact animations are made
only when the exact-reference result is feasible, continuously verified, and
marked `OPTIMAL`; feasible or timed-out results remain reported but are not
animated as exact. Figure summaries may filter to feasible, verified records;
the raw integrity report retains timeout and failure counts. Empirical ratios
are not theoretical approximation guarantees.

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

Run the FAST profile (the eight non-exact algorithms plus one exact reference)
against every JSON instance in both objectives with:

```sh
python3 scripts/run_batch.py [--parallel --threads N] [--algorithms all] \
    [--profile fast --seed 42 --repeats 1] \
    [--exact-reference auto|ip-kont|branch-and-bound]
```

The supported profiles are `fast`, `exact-reference`, and `debug`.
`--algorithms all-fast` selects the eight fast algorithms plus the selected
exact reference; `--algorithms all-comparison` selects the full comparison
set plus that same single exact reference. `--seed` and `--repeats` apply to
heuristics; exact reference runs execute once per instance and objective.
Solver seeds are derived deterministically from the base seed, instance,
algorithm, objective, and repeat index. The result files and
`experiment_manifest.json` record this policy, environment, configuration,
time limits, and exact-reference calibration decision.

Each exact result is reused for all heuristic comparisons for the same
instance and objective. `empirical_ratio_to_exact` is populated only when that
exact run is feasible and proven optimal; it is an empirical comparison, not a
theoretical approximation ratio. `ratio_to_incumbent` compares feasible
results with the best feasible result in the experiment. Solve, verification,
and serialization times are recorded separately; `solve_time_sec` is the
primary runtime metric.

For optional phase-level profiling, set `KDC_PROFILE_PHASES` to an output
filename (or `1` to write `profile_phases.json`) when running a solver command.
The JSON report includes call counts and cumulative seconds for candidate
construction, trajectory positions, geometry/coverage matrices, model and
LP/ILP construction/solve, kinetic events/extensions, combination, local
improvement, verification, and serialization. Profiling is disabled by
default; reported phase times are inclusive where operations are nested and
must not be summed as disjoint wall time. `candidate_build` includes scoped
precompute-cache lookups as well as cold builds.

The wrapper requires a passing pre-flight report unless `--force` is supplied.
Parallel runs use all available CPU cores by default. Each static/IP subsolve
is limited to 60 seconds by default; override this with `--time-limit SEC`.
Each instance/algorithm/objective run also has a cooperative global deadline:
30 seconds for fast algorithms and 600 seconds for exact algorithms by default.
Override these with `--fast-time-limit SEC` and `--exact-time-limit SEC`.
Nested static solves are capped by both their per-solve limit and the remaining
global budget. On expiration, the solver returns its best complete feasible
kinetic solution when one is available and records `time_limited`; it does not
claim optimality because of a timeout.
Batch artifacts are written beneath `results/batch/`, including per-run
solutions, result metadata, convergence traces, master JSON/CSV, and a summary.
The manual GitHub Actions pipeline defaults to the 302-instance public dataset
in `data/instances/public_instance_set/`. It converts the repository's MDC
files into the solver's canonical JSON format before solving and publishes
batch results, tables, figures, and animations as workflow artifacts.

`--exact-reference auto` runtime-probes the KONT/COPT API, then calibrates
KONT/COPT and branch-and-bound on the same deterministic subset of at most
three small dataset instances for both MinMax and MinSum. A calibration run is
eligible only when its result is feasible, continuously verified, and proven
optimal. AUTO compares median static-solver runtime only across paired
accepted runs for the same instance and objective; ties, unavailable
KONT/COPT, or insufficient paired runs select branch-and-bound. If the native
KONT/COPT runtime is unavailable, the
manifest names the actual backend `built-in-branch-and-bound-fallback`; it is
never reported as KONT. The resulting `experiment_manifest.json` records the
request, actual/selected backends, calibration inputs and runs, rejection
reasons, runtime statistics, and selection rule. A later AUTO run reuses that
decision only when the dataset path and content fingerprint match. The
calibration command can be run separately with
`kdc-solver calibrate --dataset DIR --output DIR
[--exact-reference auto|ip-kont|branch-and-bound]`. Strict benchmark profiles
include exactly one exact backend; explicitly selecting both is rejected.
`run_batch_limited.py` performs calibration once at the experiment level and
passes its selected backend into each isolated per-run process.

To run each individual
instance/algorithm/objective combination in a separate process, use
`python3 scripts/run_batch_limited.py --instances DIR --output results/batch
--algorithms all --modes both`. It defaults to
using all available CPU cores and forwards the cooperative global and
per-static-solve limits to the solver. An optional `--safety-timeout SEC`
provides an external hard subprocess timeout; any such interruption is recorded
as wrapper metadata and does not manufacture a solver optimality or feasibility
status. Faster, semi-exact, and exact algorithms run in successive phases;
previous batch-owned result files are cleared before each run.

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
                  [--verify-each-iteration] [--no-verify]
                  [--no-handovers] [--no-dedup] [--no-partial]
kdc-solver verify --instance FILE --solution FILE
kdc-solver benchmark --dataset DIR --output DIR --mode both|minmax|minsum \
                    [--profile fast|exact-reference|debug]
                    [--algorithms all-fast|all-comparison]
                    [--seed N] [--repeats N]
                    [--exact-reference auto|ip-kont|branch-and-bound]
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
supports. Static assignments include an explicit point-to-station ownership
map. Handover events transfer the source's owned support only when it enters
the receiving station's current disk; each kinetic interval serializes the
resulting ownership map alongside its supporting points.

`kdc::KineticSolution` stores piecewise quadratic cost intervals and evaluates
costs, integrals, peak values/times, and structural consistency. It can extend
a stationary assignment across kinetic support and handover events, combine
two interval solutions using their pointwise lower envelope, and truncate
non-MinSum extensions at a cumulative-integral crossing.

`kdc::MinMaxSolver` iteratively solves stationary IPs at the current peak time,
extends and combines the resulting kinetic assignments, tracks a monotone
relative-gap trace, and can verify point coverage and interval costs.

`kdc::MinSumSolver` defaults to incumbent-driven adaptive refinement: it
starts with one feasible stationary solve and selects the current solution
interval with the largest estimated integral contribution for refinement.
Stop conditions include the global deadline, iteration cap, stagnation
patience, and negligible objective improvement. The optional `sampled`
(`CERTIFIED_BOUND`) policy performs sampled static solves to guide refinement,
but their trapezoidal integral is heuristic, not a certified continuous-time
lower bound. MinSum combines candidates by splitting intervals at roots where
their quadratic instantaneous area costs cross, then selecting the cheaper
candidate at each time; candidate generation remains heuristic and does not
certify global optimality. Select the policy with
`--minsum-refinement-policy adaptive|sampled` for solve, batch, or benchmark
commands.

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
coverage violation. `verify()` and solver final verification use the
piecewise-linear continuous-time path: trajectory breakpoints partition the
time domain, polynomial contact roots partition each affine piece, and
inequalities are checked at all resulting boundaries and interval interiors.
`verify_empirical()` is an explicitly finite-sample diagnostic and must not be
interpreted as a proof of continuous feasibility. Solver output reports the
verification kind and verification wall time separately from solve time.
The continuous checks use floating-point polynomial root isolation and the
configured geometric tolerance; they are not an exact-arithmetic certificate.
Benchmark and batch records retain end-to-end `wall_time_sec` and expose
`solve_time_sec` separately, so post-solve verification does not inflate the
solver timing.
Verification after solve is enabled by default; `--no-verify` disables that
final report, while MinSum still requires continuous verification before
accepting a refinement. `--verify-each-iteration` requests explicit
verification of every accepted iteration.

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
