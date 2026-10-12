# Local migration audit

**Audit date:** 2026-10-12
**Repository:** `AmirHPartovi/KineticDiskCover`
**Scope:** Read-only source/output audit and local validation at the time of
Prompt 0. This report records that audit baseline; later implementation
changes are noted below.

## 1. Baseline and evidence

Initial local Git checks:

| Check | Observed result |
|---|---|
| `git status --short --branch` | `## main...upstream/main` (clean) |
| `git rev-parse HEAD` | `f28b4e3866a51575ea47dddb3362694d84c6de25` |
| `git log -1 --oneline` | `f28b4e3 Merge pull request #13 from AmirHPartovi/amirhpartovi-directional-event-state` |
| `git diff --check` | Exit 0; no whitespace errors |

`git ls-files` contains 9,866 paths: 8,817 under `results/`, 853 under
`data/`, 84 under `src/` and `include/`, 48 under `tests/`, 33 under
`docs/`, 15 under `scripts/`, 4 workflow files, and 3 pipeline configs
(remaining paths are root/build/benchmark metadata). The filesystem currently
contains exactly the same 8,817 files under `results/` as Git tracks there.
Thus the checked-in result tree is historical data, not an ignored scratch
output tree.

Local output inventory: `results/` occupies about 284 MB and contains 1,429
CSV files, 4,988 JSON files, 282 JSONL files, 599 logs, 426 Markdown files,
318 PDFs, 318 PNGs, 183 GIFs, 270 text files, and 4 extensionless files.
Of the CSV/TSV files under `results/`, 1,312 do not have a same-stem Markdown
file. `build/` occupies about 163 MB and contains 8,252 files, including the
configured CMake tree, fetched dependency sources, libraries, object files,
generated headers, and executables. No CSV/TSV file exists under `build/`;
there are JSON calibration inputs and logs under `build/results/`.

The facts below come from the working source and filesystem. The user-provided
note that remote certification failed at CTest on 2026-10-09 is historical
context only; it was not independently checked and is not treated as a local
failure.

## 2. Actual execution graph and entry points

Primary local entry point is `bash scripts/run_experiment.sh`; default pipeline
is `full`. The current graph is:

1. **Resolve profile and inputs.** The shell runner loads one of
   `configs/pipeline/{smoke,reference,full}.json` through
   `scripts/experiment_pipeline.py config`. It applies explicit CLI overrides,
   validates profile/objective/backend choices, resolves paths relative to the
   repository, and fingerprints the source and optional selection manifest.
2. **Select and canonicalize data.** For `smoke10`, the shell runner accepts a
   selection manifest/materialized selection or calls
   `scripts/select_test_instances.py`. That selector reads canonical JSON or
   converts `.mdc` through `scripts/prepare_real_instances.py`; it validates
   coordinates, trajectories, and dimensions, then writes selected canonical
   JSON plus a manifest/checksums. The pipeline snapshots/copies or converts
   those files to `<experiment>/dataset/` and records the canonical dataset
   fingerprint and file list in `experiment_manifest.json`. The source dataset
   is read, not rewritten.
3. **Create experiment and run build checks.** The shell runner creates a
   unique directory under `results/experiments/` unless `--output` is given;
   an existing explicit output is refused except during resume. It creates
   `dataset/`, `preflight/`, `calibration/`, `batch/`, `tables/`, `figures/`,
   `animations/`, `logs/`, and `reports/`. Unless preflight is skipped, it
   configures/builds with CMake, runs CTest, and invokes
   `build/kdc-solver preflight --output <experiment>/preflight/...`.
4. **Calibrate exact reference.** `build/kdc-solver calibrate` writes the
   backend decision to `<experiment>/calibration/experiment_manifest.json`.
   `ExactReferenceSelector` resolves explicit `ip-kont`/`branch-and-bound` or
   `auto`; auto fingerprints the dataset, reuses a compatible calibration
   manifest, otherwise probes native KONT/COPT, benchmarks eligible
   calibration instances for both scalar objectives, and chooses the faster
   paired median when both backends have accepted measurements. Ties select
   branch-and-bound; if native KONT/COPT is unavailable, branch-and-bound is
   selected. This is a static-solver performance choice, not kinetic
   optimality certification. Calibration examines at most three instances
   capped at eight moving points and four stations, with a 20-second
   calibration-solver limit.
5. **Plan and execute solver runs.** `scripts/run_batch.py` checks the fixed
   preflight report for its standalone compatibility entry point, then calls
   `scripts/run_batch_limited.py`. The experiment runner bypasses that global
   preflight gate by passing `--force` after its own preflight stage. The
   limited runner selects one exact backend, snapshots each canonical input,
   hashes the logical run identity, writes `runs/plan.jsonl`, reserves unique
   physical run directories, and launches one `kdc-solver batch` process per
   run. It captures subprocess logs/exit status, parses the solver row,
   copies declared solution/trace files into the run package, validates and
   finalizes canonical run records, and retains failures/time limits as
   terminal run states. Traces are also converted from CSV to JSONL.
6. **Validate and aggregate.** The storage module validates schemas, result
   vector invariants, input and artifact checksums, run status, and run
   directory identity. It produces `aggregates/results.{jsonl,json,csv}`,
   `runs/index.jsonl`, an aggregate manifest, and legacy-compatible
   `batch/master_results.{json,csv}` plus `batch_summary.md`. The experiment
   pipeline writes result integrity counts/reports, including status and
   exact-backend provenance, and rejects incomplete canonical aggregates.
7. **Derive reports and visuals.** `scripts/build_tables.py` reads the
   validated batch export and produces tables; `scripts/plot_comparisons.py`
   produces figures and `figures/REPORT.md`; pipeline animation handling
   invokes `scripts/animate_best.py` against the same batch and canonical
   dataset. The pipeline checks figure/table/animation references, records
   stages and final status, and writes `reports/final_experiment_report.md`
   and summary output. Animation eligibility gates exact-reference solutions
   on feasibility, continuous verification, and `OPTIMAL`.

Other entry points include `build/kdc-solver solve|verify|benchmark|compare|
preflight|backend-info|calibrate|batch`, direct Python reporters and
`build/kdc-kinetic-certification`. `scripts/run_batch_limited.py` is the
current immutable-run executor; `kdc-solver batch` and other legacy C++
entry points remain available with their own output rules.

## 3. Algorithms and objective/result semantics

The actual static registry in `src/static_solver_registry.cpp` registers these
11 identifiers. Capability claims below are confirmed in the corresponding
`include/kdc/algorithms/*_solver.hpp` implementations, not inferred from the
registry names.

| Identifier | Classification | Observed capability / caveat |
|---|---|---|
| `nn` | Heuristic | `is_exact() == false`; no lower-bound capability |
| `ip-kont` | Exact/reference-capable | Exact static IP solver; requires native KONT/COPT backend and runtime/license availability |
| `brute-force` | Exact/reference-capable, primarily diagnostic | Exhaustive assignment search with a default assignment cap; excluded from the normal benchmark list and the immutable Python batch selection |
| `branch-and-bound` | Exact/reference-capable | Exact static search with configured node/time limits and optional LP lower bound; may return a time-limited result |
| `greedy` | Heuristic | Non-exact set-cover method; supplies a lower-bound value |
| `lp-rounding` | Heuristic | Non-exact; uses LP support and supplies lower-bound information |
| `primal-dual` | Heuristic | Non-exact; supplies lower-bound information |
| `local-search` | Heuristic | Non-exact randomized/local-improvement method |
| `sa` | Heuristic | Non-exact simulated annealing |
| `genetic` | Heuristic | Non-exact genetic search |
| `shifting` | Heuristic | Non-exact shifting strategy; may use an IP solver for cells |

No registered static algorithm is a validation-only or diagnostic-only
identifier. Verification (`kdc-solver verify`/`Verifier`), preflight,
calibration, and kinetic differential certification are separate tools.
“Provides a lower bound” is not equivalent to “exact”; `is_exact()` and
result status/provenance distinguish the exact-capable methods.

Selection aliases differ by entry point:

* C++ `BatchRunner` recognizes `all`, `all-registered`, `all-fast`, and
  `all-comparison`. `all`/`all-registered` select registered algorithms but
  retain only the selected exact-reference backend; this can include
  `brute-force`. `all-fast` and `all-comparison` currently both resolve to
  the default eight heuristics (`nn`, `greedy`, `primal-dual`, `local-search`,
  `sa`, `genetic`, `lp-rounding`, `shifting`) plus the selected exact backend.
* The immutable Python wrapper treats `all`, `all-fast`, and
  `all-comparison` as the same set of eight heuristics plus exact backend,
  explicitly discards `brute-force`, and removes the non-selected exact
  backend. It does not recognize `all-registered`; passing that token to this
  wrapper is rejected as an unknown algorithm.

The objectives are not interchangeable scalar summaries:

* **MinMax** minimizes peak disk-area cost over the time horizon. Result
  metadata separates feasible `upper_bound`, certified lower bound/status,
  heuristic lower bound/gap, optional `certified_gap`, `exact_solver`, and
  `optimality_status`. An exact-capable static solver does not itself imply
  kinetic `OPTIMAL`: that status requires a feasible exact result with a
  certified zero gap; time limits remain `TIME_LIMIT`.
* **MinSum** minimizes the time integral. It reports total integral,
  `lower_bound_integral`/heuristic information, a nonnegative certified
  baseline lower bound, upper bound, bound status, and verification. The
  integral is not placed in MinMax’s scalar field semantics. The code clears
  `certified_gap` at finalization; consequently ordinary feasible runs do not
  get an optimality claim merely from an exact static solver. Component
  statuses are retained when used for MinMaxSum.
* **MinMaxSum** constructs a jointly verified envelope from available MinMax
  and MinSum component solutions. Its objective is the vector
  `(peak_cost, integral_cost)`, with no scalar `objective_value`; scalar
  lower/upper-bound and certified-gap semantics do not apply (batch records
  use `BoundStatus::NONE`). It records component statuses/optimality,
  component objective vectors/source-run IDs, `joint_optimality_status`, and
  `dominates_minmax`/`dominates_minsum`/`dominance_invariants_ok`. The verifier
  checks the envelope’s peak and integral against each available component
  pair within tolerance. Joint `OPTIMAL` requires both components to be
  optimal; if only one component is usable, the vector can still be feasible
  but is not thereby joint-optimal.

`Verifier::verify_empirical` labels sampled checks `EMPIRICAL`;
`verify_continuous` labels its continuous checks `CERTIFIED_CONTINUOUS`.
The continuous report checks horizon coverage, supporting points,
assignment/cost consistency, and integral consistency; `all_ok()` requires
coverage, supporting-point, cost, integral, and assignment checks to pass.
`optimality_status` (`OPTIMAL`, `FEASIBLE`, `TIME_LIMIT`, `INFEASIBLE`,
`FAILED`), `bound_status` (`NONE`, `HEURISTIC`, `CERTIFIED`), and
`verification_kind` (`none`, `empirical`, `certified_continuous`) are
separate fields. Canonical run records additionally encode run lifecycle
status, requested/selected/actual backend, backend details, commit/platform,
verification document and artifact digests. Result and run schemas explicitly
require a null MinMaxSum scalar and its objective vector/joint metadata.

## 4. Smoke selection policy and `smoke10` dependencies

The checked-in profile is `configs/pipeline/smoke.json`: dataset profile
`smoke10`, count 10, max `n=50`, max `m=25`, all algorithms, all three
objectives, one repeat, seed 42, two threads, 5/10/2-second fast/exact/static
limits, verification, traces/solutions, tables/figures, and all-mode
animations. The workflow defaults the same selection limits.

`data/test_sets/smoke10.json` is a checked-in manifest selecting 10 instances
across 10 families. Each is within `n<=50,m<=25`; the current selected sizes
range from `n=1` to `n=13`, all with `m=25`. The manifest records source
fingerprints, per-file SHA-256, selection rank/reason, and limits.
`select_test_instances.py` validates candidates, sorts by `(n,m,n*m,file
size,relative path)`, takes the smallest in-limit instance per family first,
then fills by size; oversized family representatives/remaining valid
candidates are fallbacks only when there are too few in-limit candidates.
Materialized JSON has checksums and is revalidated against manifest metadata.

`smoke10` is currently a cross-cutting special case: profile JSON and shell
pipeline compatibility checks; explicit CLI choices in `run_batch.py` and
`run_batch_limited.py`; selection-manifest enforcement and execution defaults
in `select_test_instances.py`; conversion/fingerprinting and smoke labels in
`experiment_pipeline.py`; report/table/figure labeling; workflow limits; and
tests. `run_experiment.sh` requires `--pipeline smoke` to pair with
`--dataset-profile smoke10`; reference/full require `full`. The generic
selector accepts a count and creates a manifest named `smoke<count>`, but
there is no built-in `smoke20` profile or corresponding profile configuration
in the runners.

There is documentation drift: `docs/refactor/immutable_run_validation.md`
shows `--pipeline smoke10`, but the actual shell parser accepts pipeline
names `smoke|reference|full`; `smoke10` is the dataset profile, not a pipeline
name.

## 5. Storage layout, generated outputs, and overwrite behavior

**Managed pipeline experiment.** Default path is a unique
`results/experiments/kdc-<profile>-<timestamp>_<pid>/`; explicit existing
output is refused unless resuming. Canonical run records are under
`runs/<run_id>/` with request/input reference/input snapshot, execution
status/command/logs/timing, `result/result.json`, optional
`result/solution.json` and `result/trace.csv`/`.jsonl`, verification, run
record, and artifact manifest. Aggregate JSONL/JSON/CSV, index, and
`batch/master_results.*` are derived exports, not the canonical run record.
`run_batch_limited.py` prevents reuse of the fixed legacy `results/batch`
path, plans before launch, creates unique attempt IDs, writes canonical
artifacts create-only, and uses atomic replacement for derived aggregates.
Schemas, SHA-256 digests, sizes, input identity, path containment, state
transitions, and MinMaxSum invariants are actively validated here.

**Legacy/shared outputs.** All currently checked-in `results/` files are Git
tracked and `.gitignore` does not ignore `results/` (the “Generated reports
and solver results” section has no ignore patterns). Historical examples
include `results/batch/master_results.{json,csv}`, per-instance solver
results/traces, `results/experiments/` run packages/aggregates, fixed
`results/figures/`, `results/tables/`, `results/animations/`,
`results/preflight/`, `results/comparison/`, and `results/logs/`.
`results/experiments/` accounts for 7,288 files in the current inventory.
Other existing result subtrees include `results/local-validation/` and
`results/selection/`.
Historical results must therefore be preserved during migration.

**Paths outside an active experiment.** Active pipeline calls pass explicit
experiment paths for data, preflight, calibration, batch, tables, figures,
animations, and reports. Remaining shared defaults/dependencies include:

* C++ logging defaults to `results/logs/kdc.log` (`include/kdc/logging.hpp`);
  concurrent subprocesses can target that same global log even while the
  runner also captures per-run stdout/stderr.
* `kdc-solver batch` defaults to `results/batch`, and C++ preflight defaults
  to `results/preflight/preflight_report.md`. `scripts/run_batch.py` reads
  that fixed preflight path even when a caller chooses another dataset.
* Standalone `build_tables.py` defaults to `results/batch/master_results.json`
  and `results/tables`; `plot_comparisons.py` defaults to the legacy batch
  JSON and `results/figures`; `plot_results.py` defaults to
  `results/json/benchmark.json` and `results/figures`; `animate_solution.py`
  defaults to `results/json` and `results/figures`; `animate_best.py`
  defaults to `results/batch`, `data/instances`, and `results/animations`.
* C++ `benchmark` writes to its supplied output, including
  `csv/benchmark.csv`; `compare` writes comparison CSV/JSON; batch writes
  master JSON/CSV and summary. `prepare_real_instances.py` writes to its
  explicit output and overwrites matching destination files.

Managed new experiments use unique directories and canonical create-only
records, but deterministic standalone plot/table/animation names and C++
legacy JSON/CSV/manifests can be replaced on repeated runs. The C++ batch
runner refuses a batch output already containing its run/master artifacts;
that guard does not cover all standalone writers. Reusing a standalone
`--output` path is therefore not generally an immutable-run workflow.

The dedicated table generator does pair CSV and Markdown for master,
MinMaxSum, per-algorithm, per-instance, per-family, and failed-run outputs
when invoked with its default `both` format. This is not a repository-wide
guarantee: raw master/aggregate CSVs and solver `trace.csv` files have no
same-stem Markdown requirement or companion, and the current result inventory
confirms many unmatched CSVs.

## 6. Visualization and reporting coverage

* `build_tables.py` has a separate MinMaxSum table with peak/integral columns,
  component metrics, dominance flags, status, and verified flag. It states
  that MinMaxSum is a vector and is not scalar-ranked. Its other per-algorithm
  and per-instance pivot tables use scalar objective columns; the dedicated
  joint table is the correct current MinMaxSum view.
* `plot_comparisons.py` retains joint rows and has a MinMaxSum peak-vs-integral
  vector scatter and a report section with separate median peak/integral.
  Many other charts remain scalar-oriented (objective/gap/Pareto/heatmap
  plots). The report’s general per-algorithm medians are grouped over all
  objective rows, so they should not be interpreted as a vector-quality
  ranking; only its explicit vector section is appropriate for MinMaxSum.
* `plot_results.py` normalizes only `minmax` and `minsum` in `_mode`; it drops
  `minmaxsum` rows and its objective/gap plots are scalar-only.
* `animate_best.py` can load/animate `minmaxsum` records and avoids scalar
  objective ranking, but currently chooses the joint candidate by shortest
  wall time and algorithm name, not by a documented vector/Pareto policy.
  `animate_solution.py` exposes only `minmax`, `minsum`, or `both`; it cannot
  request MinMaxSum.
* Pipeline configs `reference` and `full` request `both` scalar objectives;
  only smoke config requests `all` objectives. The runners accept `all`, so
  joint runs are possible, but the default full/reference pipeline does not
  include MinMaxSum.
* Figure outputs are deterministic PNG/PDF pairs below the requested output
  directory; `plot_comparisons.py` writes its `REPORT.md` there.
  `animate_best.py` writes deterministic GIF/MP4 names under the requested
  output directory, with an algorithm subdirectory under `all-algorithms`.
  `animate_solution.py` writes to its chosen output directory and chooses
  MP4/GIF according to the available writer. The experiment pipeline chooses
  `<experiment>/figures` and `<experiment>/animations`.

**Reporting status visibility:** the canonical aggregate includes terminal
run status, failed/time-limit counts, invalid/missing/unexpected runs, and
completeness; the legacy master export retains the status fields, and the
integrity report counts status per objective and checks declared solution and
trace paths. Failed, skipped, and time-limited runs are therefore visible in
raw aggregate/integrity data. `build_tables.py` classifies every
not-feasible-and-verified row into its `failed_runs` view but emits only
instance/algorithm/objective/error text there, not the lifecycle status, so
failure versus skip versus timeout is not distinguished in that derived table.
Comparative plots intentionally use feasible, verified rows; their report
directs readers to raw integrity output for failures/timeouts.

## 7. Workflow dependencies and local validation baseline

At the time of this audit, four tracked workflow files existed:

| Workflow | Trigger / purpose | GitHub-hosted dependencies |
|---|---|---|
| `.github/workflows/kinetic-certification.yml` | push, pull request, manual certification | `actions/checkout`, Ubuntu runner, CMake dependency fetches, upload-artifact |
| `.github/workflows/kdc_smoke_validation.yml` | manual smoke | checkout, setup-python, apt/ffmpeg, pip requirements, upload-artifact, Actions summary |
| `.github/workflows/kdc_reference_validation.yml` | manual full-data reference | same hosted Python/ffmpeg/pip/artifact/summary services |
| `.github/workflows/kdc_full_benchmark.yml` | manual full benchmark | same hosted Python/ffmpeg/pip/artifact/summary services |

The certification workflow was the only push/PR-triggered job; all four used
GitHub Actions machinery. `CMakeLists.txt` also uses `FetchContent` for Eigen,
nlohmann-json, Catch2, and spdlog from GitLab/GitHub on a fresh uncached
configure. Python reports depend on `requirements.txt`; animation encoding
may use ffmpeg. Neither the audit nor the tests invoked GitHub workflows or
remote services.

**Test discovery/commands.** `CMakeLists.txt` globs `tests/*.cpp` into one
`kdc-tests` executable and registers a single CTest named `all`; it also
defines `kdc-kinetic-certification` when `BUILD_BENCHMARK=ON`. Python tests
are in `tests/python/` and are not registered with CTest.

Executed locally from the repository root:

| Command | Result |
|---|---|
| `ctest --test-dir build --output-on-failure` | Exit 0; CTest target `all` passed (1.42 s) |
| `python3 -m pytest -q tests/python` | Exit 0; 93 passed (24.32 s). Deprecation/FutureWarnings from matplotlib/pyparsing and pandas were emitted. |
| `./build/kdc-kinetic-certification 200 1` | Exit 0; stdout was suppressed to avoid flooding the terminal. |

The pre-existing `build/kdc-solver`, `build/kdc-tests`,
`build/kdc-kinetic-certification`, interval benchmark, and handover benchmark
executables were present. The CMake cache has a KONT header/library path, but
this audit did not execute a backend runtime/license probe; current
`auto`-selection is determined by the runtime probe and calibration described
above, not by presence in the cache alone. No local test failure was
reproduced. After all three test commands, `git status --short --branch`
remained clean.

## 8. Staged implementation plan

The following was the migration sequence proposed by Prompt 0; implementation
status after Prompt 1 is recorded in section 10.

| Stage | Depends on | Work and acceptance criteria |
|---|---|---|
| 1. Local-only execution boundary | None | Remove/disable all four GitHub workflow files and their Actions/artifact-summary dependencies. Decide how CMake dependencies and Python/ffmpeg tools are made available without workflow/network execution. Acceptance: no push/PR/manual Actions path remains; documented local setup/build works with the chosen dependency source. |
| 2. Generalize profiles and dataset selection | Stage 1 | Separate profile names from selection counts/limits; remove hard-coded `smoke10` comparisons where generalization is intended, while preserving manifest identity, source hashes, canonical snapshots, and deterministic policy. Correct stale `--pipeline smoke10` documentation. Acceptance: smoke10 compatibility tests pass; a second count/profile (including 20 if desired) runs end-to-end without hard-coded branches or source mutation. |
| 3. Close global output paths | Stage 1 | Thread experiment/output roots into C++ logging, direct batch/preflight, and standalone reporter defaults; preserve the legacy paths as explicit compatibility options. Keep existing tracked history unchanged. Acceptance: a test run in a temporary experiment root writes no new files to global `results/{logs,preflight,batch,json,figures,tables,animations}`; repeated runs cannot silently overwrite canonical artifacts. |
| 4. Make canonical records the sole reporting input | Stage 3 | Keep the current schema/checksum/create-only storage layer; integrate/verify all producer and legacy C++ paths against it, preserving requested/selected/actual backend, result status, verification, paths, and vector semantics. Promote lifecycle status into failed/skipped/time-limit tables and ensure aggregate completeness is explicit. Acceptance: corrupt/missing artifacts fail validation; failed, skipped, timed-out, invalid, and missing runs are separately represented; resumed runs do not overwrite prior attempts. |
| 5. Complete objective-aware visuals | Stage 4 | Add MinMaxSum support to `plot_results.py` and `animate_solution.py`; define a non-scalar joint animation-selection policy; gate scalar charts/rankings to MinMax/MinSum and use the vector for joint reporting. Align full/reference objective profiles with intended scientific coverage. Acceptance: fixtures with only each objective produce clear figures/reports/animations; no joint result is silently filtered or scalarized; all outputs have verified paths and table CSV companions where required. |
| 6. Local certification and migration acceptance | Stages 1-5 | Run documented CMake configure/build, CTest, Python suite, and seeded kinetic differential certification on local data; preserve exact exit codes and outputs in the experiment record. Acceptance: all pass locally, integrity checks validate all expected run records/artifacts, generated outputs stay in the selected experiment, and historical tracked inputs/results remain byte-identical. |

## 9. Limits and assumptions

* This report describes the checked-out source at commit
  `f28b4e3866a51575ea47dddb3362694d84c6de25`; current local results may have
  been generated by other revisions and are not treated as validation of this
  commit’s scientific outputs.
* Output counts, file sizes, and same-stem Markdown pairing are observations
  of the current working tree. “No CSV under `build/`” refers to this local
  build directory, not every possible CMake configuration.
* Native KONT/COPT runtime/license availability and actual current auto
  backend choice remain undetermined because no backend probe/calibration was
  run during this read-only audit.
* The proposed second smoke count is an acceptance example, not a decision
  that the project must adopt `smoke20`.

## 10. Prompt 1 implementation update

After this audit, all four workflow definitions listed in section 7 were
removed from the local working tree. `scripts/local_validation.sh` now
provides root-relative local build, CTest, Python test, certification, and
experiment-profile commands. The experiment runner's required `tests` stage
now runs both CTest and `tests/python`; a failed command stops dependent
stages and is recorded with its nonzero exit code. `README.md`,
`docs/local_validation.md`, and the certification discussion in
`docs/kinetic_performance_and_correctness.md` document local commands. CMake's
existing `FetchContent` downloads remain, so a fresh uncached configuration
is not guaranteed to work offline.
