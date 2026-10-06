# kdc-solver

`kdc-solver` is a C++17 project scaffold for kinetic discrepancy correction
models. Its optimization backend is KONT, which uses the COPT-compatible API.

## Reproducible experiment pipeline

Run one of three reproducible pipelines from any current working directory:

```bash
bash scripts/run_experiment.sh --pipeline smoke
bash scripts/run_experiment.sh --pipeline reference
bash scripts/run_experiment.sh --pipeline full
```

The profiles are defined in `configs/pipeline/` and share the same staged
runner: dataset snapshot, build, tests, preflight, exact-backend resolution,
benchmark, result validation, tables, figures, animations, and final report.
Each run has a unique output under `results/experiments/` with its resolved
configuration, stage statuses/timings, dataset and selection fingerprints,
logs, result integrity report, and generated artifacts. Workflow dispatches
are available separately as **KDC Smoke Validation**, **KDC Full Dataset
Reference Validation**, and **KDC Full Scientific Benchmark**. Use
`bash scripts/run_experiment.sh --help` for profile overrides and resume
options; resume requires matching dataset and benchmark configuration.

### Smoke validation

The smoke profile deterministically selects 10 valid, family-diverse instances
from the source dataset, preferring instances within `n <= 50` trajectories
and `m <= 25` stations. The manifest records dimensions, source file size and
SHA-256, rank, selection rationale, and any size-limit exception. A larger
instance is included only when there are too few eligible candidates to meet
the requested count; this is recorded rather than hidden. For development:

```bash
bash scripts/run_experiment.sh --pipeline smoke
```

Smoke uses all registered algorithms, both objectives, one repeat, two
workers, per-static-solve / heuristic / exact deadlines of 2 / 5 / 10 seconds,
and the debug verification profile. It generates verified animations for all
eligible algorithms and selected instances. Override the size bounds with
`--smoke-max-n` and `--smoke-max-m`; outputs are labeled as development
validation, not as the full scientific benchmark.

### Full-dataset reference validation

The reference profile runs one heuristic (`greedy` by default) and exactly one
resolved exact backend on every instance in `data/instances/public_instance_set/`,
for both objectives. AUTO selects either runtime-ready KONT/COPT or
branch-and-bound and records the choice and calibration evidence. Use
`--algorithms` to choose another approximation/heuristic.

### Full scientific benchmark

The full profile runs all registered algorithms across the complete dataset,
both objectives, and three repeats for stochastic algorithms by default.
Exact-reference results are produced by one resolved backend, never by
silently substituting an unreported solver. The CLI and workflow inputs allow
repeat, worker-count, and time-limit overrides.

To create a separate materialized smoke dataset for another tool:

```bash
python3 scripts/select_test_instances.py \
  --source data/instances/public_instance_set \
  --output /tmp/kdc-smoke10 --count 10 --max-n 50 --max-m 25
```

Exact-result animations are generated only for feasible, continuously
verified `OPTIMAL` solutions; feasible or timed-out exact results remain in
the reports but are not presented as optimal animations. Heuristic animations
also require feasibility and continuous verification. Raw integrity reports
retain timeout and failure counts. Empirical ratios are not theoretical
approximation guarantees.

## Prerequisites

* CMake 3.20 or newer.
* A compiler with C++17 support.
* KONT/COPT is optional. A usable vendor installation and valid runtime
  license are required for the explicit `ip-kont` algorithm. The built-in
  `branch-and-bound` backend is available without KONT/COPT.

## Build

```sh
mkdir build && cd build && cmake .. && make -j
```

If KONT/COPT is installed in a non-standard location, set the CMake
`KONT_ROOT` option or the `KONT_ROOT`, `COPT_HOME`, or `KONT_HOME` environment
variable:

```sh
cmake -S . -B build -DKONT_ROOT=/path/to/kont
cmake --build build --parallel
```

Check both compile-time discovery and runtime/license availability with:

```sh
build/kdc-solver backend-info
```

An installed SDK can pass the API compile check but still be unavailable at
runtime when its license or environment cannot initialize. `backend-info`
reports this separately. `--exact-reference auto` chooses only a runtime-ready
native backend; an explicit `--algorithm ip-kont` or
`--exact-reference ip-kont` fails with the diagnostic rather than switching
algorithms. Use `branch-and-bound` explicitly when native KONT/COPT is
unavailable. When KONT/COPT is not found, omit `KONT_ROOT`:

```sh
cmake -S . -B build
cmake --build build --parallel
```

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
written under `results/preflight/`; without a usable KONT backend, optional
native-IP checks report unavailable and pipeline sanity checks use
branch-and-bound or `MockILPSolver`.

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
The reference and full pipelines default to the 302-instance public dataset
in `data/instances/public_instance_set/`. They convert the repository's MDC
files into the solver's canonical JSON format before solving and publish
results, tables, figures, animations, and stage reports as workflow artifacts.

`--exact-reference auto` runtime-probes the KONT/COPT API, then calibrates
KONT/COPT and branch-and-bound as static solvers on the same deterministic
subset of at most three small dataset instances, with records labeled for
both MinMax and MinSum. A calibration run is eligible when it returns a
feasible result without timing out and has a finite runtime; kinetic
optimality/continuous-verification status is not used as evidence of static
solver performance. AUTO compares median static-solver runtime over paired
accepted runs, with ties selecting branch-and-bound. If no paired measurements
are available, it selects native KONT/COPT only when its runtime/license probe
passes, otherwise it selects branch-and-bound. An explicitly requested
`ip-kont` never silently changes algorithms: it fails with the backend
diagnostic when the native runtime/license is unavailable. The manifest names
the actual branch-and-bound algorithm `branch-and-bound`, not a KONT fallback.
The resulting `experiment_manifest.json` records the request,
actual/selected backends, calibration inputs and runs, rejection reasons,
runtime statistics, and selection rule. A later AUTO run reuses that decision
only when the dataset path and content fingerprint match. The calibration
command can be run separately with
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
MinSum extensions at a cumulative-integral crossing. MinMax refinement instead
keeps candidates on their common time domain and lets the pointwise lower
envelope decide whether they reduce the global peak; it never uses integral
ordering to accept or reject a MinMax candidate.

`kdc::MinMaxSolver` iteratively solves stationary IPs at the current peak time,
extends and continuously verifies the resulting kinetic candidates, then
combines them with the incumbent by pointwise minimum. Its trace records the
static-solver status, candidate/combined peaks, acceptance, and stop reason.
Continuous verification checks the returned kinetic representation and
feasibility; it does not establish global MinMax optimality.

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
