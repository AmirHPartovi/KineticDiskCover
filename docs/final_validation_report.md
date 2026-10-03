# Final validation report

This report records the final validation pass on the benchmark/protocol work.
It distinguishes checked behavior from unverified or non-comparable results;
no speedup or mathematical exactness is inferred from timing alone.

## A. Files changed in this validation phase

- Added `include/kdc/profiling.hpp` and `src/profiling.cpp` for opt-in phase
  timers, atomic aggregate counters, and a JSON report.
- Instrumented candidate/precompute, geometry and coverage construction,
  trajectory positions, model and LP/ILP construction/solve, kinetic support
  and handover events, extension, combination, MinSum local improvement,
  verification, and serializers.
- Added `tests/test_profiling.cpp`; corrected the exact-backend-count assertion
  in `tests/test_batch_runner.cpp` so it checks counts after processing the
  complete result set.
- Fixed `calibrate` command dispatch in `src/main.cpp`; it had been nested
  under the `batch` command and could not be reached.
- Added dirty-source-tree metadata to experiment manifests through
  `CMakeLists.txt` and `src/benchmark_protocol.cpp`, with a schema assertion
  in `tests/test_benchmark.cpp`.
- Documented optional profiling in `README.md`.

## B. Bottlenecks found

The medium exact-reference run (50 trajectories, 25 stations) was dominated
by branch-and-bound static solving. Each initial static solve reached its
60-second per-static limit; kinetic extension and final continuous
verification then added substantial time. In MinMax, the observed total solve
time was 84.072 s; in MinSum it was 92.827 s. These are not global-budget
overruns: the configured global exact budget was 600 s, while the child static
limit was 60 s.

## C. Before/after measurements

The attempted baseline used clean committed `HEAD` (`560e9e4`) and the same
three input files. The baseline benchmark output has no algorithm/backend
identity and does not establish that the same static backend was run as in the
current benchmark. Therefore the wall-time values below are recorded but are
**not a valid before/after performance comparison**:

| Size | Objective | Baseline wall time | Current B&B wall time | Baseline/current status |
|---|---|---:|---:|---|
| Tiny (2, 2) | MinMax | 0.000290 s | 0.000030 s | backend not comparable |
| Tiny (2, 2) | MinSum | 0.000331 s | 0.000108 s | backend not comparable |
| Small (10, 6) | MinMax | 0.001359 s | 0.000453 s | backend not comparable |
| Small (10, 6) | MinSum | 0.001155 s | 0.001369 s | backend not comparable |
| Medium (50, 25) | MinMax | 0.680 s | 84.072 s | backend not comparable; current TIME_LIMIT |
| Medium (50, 25) | MinSum | 2.073 s | 92.827 s | backend not comparable; current TIME_LIMIT |

The measured process wall times were 2.76 s for baseline and 177.09 s for
current. They likewise must not be interpreted as a speedup/regression because
the selected backend cannot be confirmed as identical. No valid pre-change
phase profiler was available.

Current profile totals for the successful exact-reference benchmark were:

| Phase | Calls | Cumulative seconds |
|---|---:|---:|
| candidate_build (including cache lookups) | 661 | 0.001327 |
| trajectory_position | 243,401 | 0.005221 |
| distance_matrix | 54 | 0.000353 |
| coverage_matrix | 20 | 0.004814 |
| support_event_detection | 562 | 0.009167 |
| handover_detection | 45 | 0.008264 |
| kinetic_extension | 14 | 0.019028 |
| combination | 1 | 0.000004 |
| verification | 6 | 0.025795 |
| serialization | 1 | 0.002549 |
| model_build / LP-ILP build / LP-ILP solve | 0 | 0.000000 |
| local_improvement | 0 | 0.000000 |

Zero counts mean those phases were not invoked by this exact-reference profile;
they do not mean those operations are free. Phase totals are inclusive when
nested and are not additive wall-time components.

## D. Static-solve reduction

No reduction is established. The current exact profile used 1/6, 2/9, and
1/1 static solves for Tiny, Small, and Medium MinMax/MinSum respectively;
the legacy baseline reported 1/6, 2/7, and 2/7. The medium current counts are
low because the first exact static solve timed out, not because refinement
work was successfully reduced. The backends also differ or are not identified
in the baseline output.

## E. MinSum refinement semantics

`HEURISTIC_ADAPTIVE` skips the sampled lower-bound initialization, starts from
an initial feasible static solution, and refines adaptively under its
iteration/stagnation/tolerance conditions. The separate
`CERTIFIED_BOUND` path remains distinct; sampled estimates are not treated as
certified integral lower bounds. Tests cover skipped sampling, deterministic
refinement, stopping, and bound metadata.

## F. Exact calibration result

AUTO calibrated on the selected tiny instance for both objectives. Native
KONT/COPT was unavailable and both KONT requests were rejected by the runtime
capability probe. Branch-and-bound MinMax was feasible, continuously verified,
and proven optimal; branch-and-bound MinSum was feasible and continuously
verified but rejected because optimality was not proven. There were no paired
accepted runs, so the runtime statistics were unavailable (`-1`); AUTO used
the deterministic fallback rule rather than claiming a measured winner.

## G. Actual backend

The calibration manifest records
`built-in-branch-and-bound-fallback` as the actual backend when the unavailable
KONT request resolves to the built-in fallback. The successful representative
benchmark explicitly requested `branch-and-bound` and records that backend
under that name. Neither result is labeled KONT.

## H. Result and status semantics

The representative result records kept `bound_status` separate from
`optimality_status`. Tiny MinMax was `OPTIMAL`; small feasible results were
not promoted to kinetic optimality merely because their static subproblems
were exact. Medium rows were `TIME_LIMIT`, with a feasible incumbent and
continuous verification. Certified gaps appeared only with a certified lower
bound and feasible upper bound. Empirical exact ratios were absent unless the
exact reference was `OPTIMAL`.

## I. Timeout behavior

The medium branch-and-bound child reached its 60-second static limit. The
solver retained its feasible incumbent, completed kinetic extension and
continuous verification, and reported `TIME_LIMIT`/`timeout=true`; it did not
claim optimality. The extra extension time is visible in total solve time and
does not change the reason for the timeout.

## J. Cache architecture

Candidate identities and trajectory segment metadata are held in immutable
instance-scoped precompute data. The cache is attached to the `Instance`,
validated against its geometry, and guarded for reuse; there is no
process-global geometry cache. Query-time geometry remains time-specific.

## K. Verification architecture

Final feasibility uses `CERTIFIED_CONTINUOUS` verification. The verifier checks
piecewise-polynomial inequalities at interval boundaries, relevant roots, and
sufficient interior points; sampling verification remains explicitly
empirical. The continuous verifier runs separately from solving in the
benchmark metadata, and regression tests include violations between coarse
samples and trajectory breakpoints.

## L. Tests and results

- C++: `ctest --test-dir build --output-on-failure` passed after the profiling
  instrumentation and the batch-count assertion correction.
- Python: `python3 -m pytest -q tests/python` passed (30 tests).
- The suite covers candidate identity/coverage/squared-distance equivalence,
  static IP equivalence, kinetic event/handover behavior, deterministic seeds,
  timeout incumbent retention, status/gap rules, brute-force comparisons,
  continuous verification, degeneracy/collinearity, exact calibration,
  backend exclusivity/reuse, and result/manifest compatibility.
- The optional-profiler test checks phase names and emitted JSON.

The later dirty-tree manifest metadata assertion was added after that recorded
test pass. Final CMake configuration succeeded, but compilation failed when
Clang could not spawn a process (`posix_spawn failed: Resource temporarily
unavailable`). A serial build retry then failed because `make` could not
start. Consequently, the final CMake/manifest change and full suite were not
revalidated after that addition; earlier source changes had passed the
complete C++ and Python suites.

## M. Exact build command

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j2
```

## N. Exact test commands

```sh
ctest --test-dir build --output-on-failure
python3 -m pytest -q tests/python
```

## O. Exact calibration command

```sh
KDC_PROFILE_PHASES=/tmp/kdc-profile-calibration.json \
  build/kdc-solver calibrate \
  --dataset /tmp/kdc-thesis-validation/dataset \
  --output /tmp/kdc-thesis-validation/calibration \
  --exact-reference auto
```

## P. Exact representative benchmark command

```sh
KDC_PROFILE_PHASES=/tmp/kdc-thesis-validation/after-phases.json \
  build/kdc-solver benchmark \
  --dataset /tmp/kdc-thesis-validation/dataset \
  --output /tmp/kdc-thesis-validation/after \
  --mode both --profile exact-reference \
  --exact-reference branch-and-bound --seed 42 --repeats 1
```

The dataset contained `tiny.json` (2, 2), converted `small.mdc` (10, 6), and
converted `medium.mdc` (50, 25). A full `all-fast` attempt was blocked before
launch by the same host process-resource error; it is not reported as a
completed benchmark.

## Q. Remaining limitations

- No same-backend historical performance baseline was available; no speedup is
  claimed.
- The medium built-in branch-and-bound cases did not prove optimality within
  the per-static limit.
- KONT/COPT could not participate in AUTO calibration, so AUTO had no paired
  runtime comparison.
- Profile output is an aggregate per process; nested timings overlap. The
  exact-only run did not exercise LP/ILP or local-improvement phases.
- `BenchmarkResult` currently leaves serialization duration at zero in its
  per-run rows; aggregate serialization was measured by the optional profiler.
  Batch records have their own serialization accounting.
- CMake records commit plus a dirty-tree boolean, but the worktree was dirty
  during this pass. Reproducible thesis results should be rerun from a clean,
  committed source tree.
