# kdc-solver

`kdc-solver` is a C++17 project scaffold for kinetic discrepancy correction
models. Its optimization backend is KONT, which uses the COPT-compatible API.

## Reproducible experiment pipeline

Run one of three reproducible pipelines from any current working directory.
Set `KDC_REPO` to the absolute path of your clone:

```bash
KDC_REPO=/path/to/KineticDiskCover
bash "$KDC_REPO/scripts/local_validation.sh" experiment smoke
bash "$KDC_REPO/scripts/local_validation.sh" experiment reference
bash "$KDC_REPO/scripts/local_validation.sh" experiment full
```

The profiles are defined in `configs/pipeline/` and share the same staged
runner: dataset snapshot, build, tests, preflight, exact-backend resolution,
benchmark, result validation, tables, figures, animations, and final report.
Each run has a unique output under `results/experiments/` with its resolved
configuration, stage statuses/timings, dataset and selection fingerprints,
logs, result integrity report, and generated artifacts. The experiment
pipeline runs CMake configure/build, CTest, Python tests, preflight, and
benchmark/reporting stages locally. Use
`bash "$KDC_REPO/scripts/run_experiment.sh" --help` for profile overrides
and resume options; resume requires matching dataset and benchmark
configuration.
See [`docs/local_validation.md`](docs/local_validation.md) for the complete
local command reference and dependency notes.

### Smoke validation

The smoke profile deterministically selects exactly 20 distinct, valid,
family-diverse instances from the source dataset, subject to `n <= 50`
trajectories and `m <= 25` stations. It selects across eligible families
before balancing additional picks; if fewer than 20 candidates satisfy the
limits, selection fails with the shortage and rejection reasons rather than
silently relaxing the bounds. The manifest records source and canonical
SHA-256 hashes, dimensions, family coverage, selection rank, and rationale.
Each new smoke experiment also writes its canonical 20 inputs and a
`selection_report.md` under its experiment-specific `dataset/` directory.
For development:

```bash
bash "$KDC_REPO/scripts/local_validation.sh" experiment smoke
```

Smoke uses all registered algorithms, all three objectives, one repeat, two
workers, per-static-solve / heuristic / exact deadlines of 2 / 5 / 10 seconds,
and the debug verification profile. Animation is configured for all eligible
algorithms; outputs are labeled as development validation, not as the full
scientific benchmark. Selection limits are configured in
`configs/pipeline/smoke.json`.

### Full-dataset reference validation

The reference profile runs one heuristic (`greedy` by default) and exactly one
resolved exact backend on every instance in `data/instances/public_instance_set/`,
for both original objectives by default; MinMaxSum is available via
`--modes minmaxsum` or `--modes all`. AUTO selects either runtime-ready KONT/COPT or
branch-and-bound and records the choice and calibration evidence. Use
`--algorithms` to choose another approximation/heuristic.

### Full scientific benchmark

The full profile runs all registered algorithms across the complete dataset,
both original objectives, and three repeats for stochastic algorithms by
default. `--modes minmaxsum` or `--modes all` adds the joint vector objective.
Exact-reference results are produced by one resolved backend, never by
silently substituting an unreported solver. The CLI allows repeat,
worker-count, and time-limit overrides.

To create a separate materialized smoke dataset for another tool:

```bash
python3 scripts/select_test_instances.py \
  --source data/instances/public_instance_set \
  --output /tmp/kdc-smoke20 --count 20 --max-n 50 --max-m 25 --seed 0
```

The tracked `data/test_sets/smoke10.json` remains preserved as legacy
selection evidence and is not accepted as smoke20 input.

Exact-result animations are generated only for feasible, continuously
verified `OPTIMAL` solutions; feasible or timed-out exact results remain in
the reports but are not presented as optimal animations. Heuristic animations
also require feasibility and continuous verification. Raw integrity reports
retain timeout and failure counts. Empirical ratios are not theoretical
approximation guarantees.

## Prerequisites

* CMake 3.20 or newer.
* A compiler with C++17 support.
* Python 3 and the packages in `requirements.txt` for Python tests and reports.
* KONT/COPT is optional. A usable vendor installation and valid runtime
  license are required for the explicit `ip-kont` algorithm. The built-in
  `branch-and-bound` backend is available without KONT/COPT.

## Build and tests

```sh
KDC_REPO=/path/to/KineticDiskCover
bash "$KDC_REPO/scripts/local_validation.sh" configure-build
bash "$KDC_REPO/scripts/local_validation.sh" ctest
bash "$KDC_REPO/scripts/local_validation.sh" python-tests
bash "$KDC_REPO/scripts/local_validation.sh" certification
```

These commands resolve repository and build paths from the script location.
The certification CSV is saved under `build/local-validation/`; its command
accepts optional seed count, repetitions, and start-index arguments.
Dependencies fetched by CMake may need network access on a fresh, uncached
build. This project does not claim a fully offline build.

If KONT/COPT is installed in a non-standard location, set `KONT_ROOT`,
`COPT_HOME`, or `KONT_HOME` before configuring:

```sh
KDC_REPO=/path/to/KineticDiskCover
KONT_ROOT=/path/to/kont \
  bash "$KDC_REPO/scripts/local_validation.sh" configure-build
```

Check both compile-time discovery and runtime/license availability with:

```sh
"$KDC_REPO/build/kdc-solver" backend-info
```

An installed SDK can pass the API compile check but still be unavailable at
runtime when its license or environment cannot initialize. `backend-info`
reports this separately. `--exact-reference auto` chooses only a runtime-ready
native backend; an explicit `--algorithm ip-kont` or
`--exact-reference ip-kont` fails with the diagnostic rather than switching
algorithms. Use `branch-and-bound` explicitly when native KONT/COPT is
unavailable. When KONT/COPT is not found, omit those environment variables:

```sh
KDC_REPO=/path/to/KineticDiskCover
env -u KONT_ROOT -u COPT_HOME -u KONT_HOME \
  bash "$KDC_REPO/scripts/local_validation.sh" configure-build
```

Catch2 tests cover the C++ implementation. Run preflight independently when
needed to check the registered algorithms and pipeline configuration:

```sh
bash "$KDC_REPO/build/kdc-solver" preflight \
  --output "$KDC_REPO/build/preflight_report.md"
```

The CLI check can also be run directly with
`kdc-solver preflight --output FILE`. It requires an explicit report path;
without a usable KONT backend,
optional native-IP checks report unavailable and pipeline sanity checks use
branch-and-bound or `MockILPSolver`.

Run the FAST profile (the eight non-exact algorithms plus one exact reference)
against every JSON instance in both objectives with:

```sh
python3 scripts/run_batch.py [--parallel --threads N] [--algorithms all] \
    [--profile fast --seed 42 --repeats 1] \
    [--exact-reference auto|ip-kont|branch-and-bound] --force
```

For a managed end-to-end experiment with a scoped preflight stage, prefer
`bash scripts/run_experiment.sh`. The standalone compatibility wrapper checks
a caller-supplied `--preflight-report`; `--force` bypasses that gate but does
not mark preflight as passed.

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

For optional phase-level profiling, set `KDC_PROFILE_PHASES` when running a
solver command. Managed batch execution routes it to a run-local
`runs/<run-id>/execution/profile_phases.json`; direct solver commands should
set it to an explicit experiment-scoped filename.
The JSON report includes call counts and cumulative seconds for candidate
construction, trajectory positions, geometry/coverage matrices, model and
LP/ILP construction/solve, kinetic events/extensions, combination, local
improvement, verification, and serialization. Profiling is disabled by
default; reported phase times are inclusive where operations are nested and
must not be summed as disjoint wall time. `candidate_build` includes scoped
precompute-cache lookups as well as cold builds.

The wrapper requires a caller-supplied passing `--preflight-report` unless
`--force` is supplied.
Parallel runs use all available CPU cores by default. Each static/IP subsolve
is limited to 60 seconds by default; override this with `--time-limit SEC`.
Each instance/algorithm/objective run also has a cooperative global deadline:
30 seconds for fast algorithms and 600 seconds for exact algorithms by default.
Override these with `--fast-time-limit SEC` and `--exact-time-limit SEC`.
Nested static solves are capped by both their per-solve limit and the remaining
global budget. On expiration, the solver returns its best complete feasible
kinetic solution when one is available and records `time_limited`; it does not
claim optimality because of a timeout.
Standalone batch artifacts are written beneath a unique managed
`results/experiments/<experiment-id>/` directory, including immutable run
records, compatibility master JSON/CSV, and a summary.
The reference and full pipelines default to the 302-instance public dataset
in `data/instances/public_instance_set/`. They convert the repository's MDC
files into the solver's canonical JSON format before solving and write
results, tables, figures, animations, and stage reports to the local
experiment directory.

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
designate exactly one exact reference backend. The `all-registered` policy
retains every algorithm in the static C++ registry, including both
`ip-kont` and `branch-and-bound` as distinct candidates; only the calibrated
backend is designated as the reference. The policy also includes
`brute-force` as a validation diagnostic, bounded by its assignment and
time budgets. Registered backends unavailable on the local machine remain in
the run plan and are recorded as skipped with a structured reason.
`run_batch_limited.py` performs calibration once at the experiment level and
passes its selected backend into each isolated per-run process.

To plan every registered algorithm for each instance and objective without
executing solver slots, use `--algorithms all-registered --modes all
--plan-only`. To execute each individual instance/algorithm/objective
combination in a separate process, use
`python3 scripts/run_batch_limited.py --instances DIR
--algorithms all-registered --modes all`. It creates a unique managed experiment under
`results/experiments/` unless `--output` names a new experiment or its
`batch/` compatibility-export directory. Use `--modes all` to include
MinMaxSum. It defaults to using all available CPU cores and forwards the cooperative global and
per-static-solve limits to the solver. An optional `--safety-timeout SEC`
provides an external hard subprocess timeout; any such interruption is recorded
as wrapper metadata and does not manufacture a solver optimality or feasibility
status. Faster, semi-exact, and exact algorithms run in successive phases;
canonical run records are create-only, and retries receive new run IDs.

The experiment pipeline uses the canonical reporting layer below. The older
`build_tables.py` and `plot_comparisons.py` commands remain compatibility tools
for explicit legacy inputs; use `report_experiment.py` for experiment reports
because it joins the immutable run plan to validated canonical run records and
never reads a shared historical results directory.

```sh
python3 scripts/report_experiment.py \
  --experiment results/experiments/EXPERIMENT_ID --stage all
python3 scripts/report_experiment.py \
  --experiment results/experiments/EXPERIMENT_ID --stage audit
```

The report creates complete plan-slot coverage, per-run results, runtime and
quality summaries, MinMaxSum component/dominance rows, feasibility and
verification counts, unsuccessful slots, per-instance and per-family
comparisons, exact-reference comparisons, separate bound/gap fields, and
completeness/artifact inventory under `tables/`. Tables retain nulls and
unsuccessful rows; runtime quantiles use only completed runs with an explicit
`solve_time_sec`. Figures are emitted as PNG and PDF only when the required
validated observations exist. Missing-data reasons are listed in
`figures/REPORT.md`, not represented by empty charts. The final
`reports/experiment_report.md` links the tables and companions, generated
figures, Prompt 6 visualizations, manifests, and integrity reports.

Every `.csv` and `.tsv` anywhere under an experiment has an adjacent Markdown
companion with the source population, command, row count, filters, column
definitions, null treatment, status counts, formulas where applicable, and a
bounded preview. The recursive `--stage audit` command verifies that these
companions exist and match the actual delimited-file row counts.

For smoke20, the `all-solutions` animation policy writes a three-time snapshot
for every feasible run with a valid serialized solution, per-solution MP4
animations (GIF when FFmpeg is unavailable), and one HTML comparison sheet for
each selected instance/objective. Snapshot times are `τ = 0, 0.5, 1`, converted
to actual simulation times with the instance's `T_end`. The
`reports/visualization_manifest.json` indexes source-solution checksums, run
status, verification and optimality labels, rendering parameters, and artifact
checksums. The visualization audit leaves failed, skipped, time-limited, and
solution-less slots visible in comparison sheets rather than treating them as
successful renders.

## Commands

```text
kdc-solver solve --instance FILE [--mode minmax|minsum|minmaxsum] [--algorithm NAME]
                  [--output FILE] [--time-limit SEC] [--gap TARGET]
                  [--verify-each-iteration] [--no-verify]
                  [--no-handovers] [--no-dedup] [--no-partial]
kdc-solver verify --instance FILE --solution FILE
kdc-solver benchmark --dataset DIR --output DIR --mode both|minmax|minsum|minmaxsum \
                    [--profile fast|exact-reference|debug]
                    [--algorithms all-fast|all-comparison|all-registered]
                    [--seed N] [--repeats N]
                    [--exact-reference auto|ip-kont|branch-and-bound]
```

The `solve` command defaults to the `minmax` objective and `ip-kont` static
solver. Its optional output is a JSON `KineticSolution` that can be loaded with
`SolutionSerializer`. `--help` displays command usage.

`minmaxsum` runs both component solvers and combines their verified solutions
using the pointwise lower envelope. It reports `peak_cost` and `integral_cost`
as a vector, not as a scalar objective; batch and benchmark records preserve
the component statuses, dominance checks, and source identifiers. See
[`docs/minmaxsum.md`](docs/minmaxsum.md) for result and certification semantics.
The development smoke pipeline runs all three modes and produces a dedicated
peak-versus-integral vector report and plot for MinMaxSum.

## Experiment result layout

New experiments default to unique directories under
`results/experiments/<experiment-id>/`. `experiment_manifest.json` records
pipeline provenance and stage state; `experiment.json` is the storage
envelope. The canonical result for each attempt is
`runs/<run-id>/run.json`; `aggregates/` contains derived canonical exports and
integrity metadata, while `batch/` contains compatibility exports. Dataset
selection evidence and canonical inputs live under `dataset/` and `input/`.
Calibration, preflight, logs, tables, figures, animations, and reports are
experiment-scoped and created only when used. The root `artifacts.json` indexes
experiment files by relative path, SHA-256, and size (excluding the index
itself). Existing experiment directories are never reused unless `--resume`
validates their configuration and immutable run plan.

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
For example: `kdc-solver benchmark --dataset data/instances --output
results/experiments/EXPERIMENT_ID/benchmark --mode both`.

`kdc-solver compare --algorithms greedy,ip-kont --dataset data/instances
--output results/experiments/EXPERIMENT_ID/comparison --mode static`
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
python scripts/animate_best.py \
  --batch results/experiments/EXPERIMENT_ID/batch \
  --instances results/experiments/EXPERIMENT_ID/dataset \
  --output results/experiments/EXPERIMENT_ID/animations \
  --mode both
```

It writes one animation per instance/objective under the selected experiment,
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
`tests`. Input data belongs in `data`; helper scripts are in `scripts`.
Generated experiment artifacts are isolated in
`results/experiments/<experiment-id>/`; see
[`docs/refactor/immutable_run_validation.md`](docs/refactor/immutable_run_validation.md)
for the canonical run layout. Optional third-party sources belong in
`third_party`.
