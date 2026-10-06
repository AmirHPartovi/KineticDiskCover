# MinMax correctness audit and fix

## Defect and reproduction

`KineticSolution::partial_extend(...)` in `src/solution.cpp` used the cumulative
integral difference to decide whether to keep a candidate on the MinMax path.
That comparison is not a valid MinMax criterion: MinMax minimizes the maximum
instantaneous area, not the area integrated over time. The candidate could be
discarded before `combine(...)` had a chance to construct the pointwise
minimum.

The regression in `tests/test_solution.cpp` uses these piecewise-constant
quadratic intervals:

| Time | Current cost | Candidate cost | Pointwise minimum |
|---|---:|---:|---:|
| `[0, 0.8]` | 5 | 30 | 5 |
| `[0.8, 1]` | 20 | 10 | 10 |

The current integral is 8, while the candidate integral is 26. Its own peak is
30 (worse than the incumbent peak 20), but the pointwise minimum has peak 10.
The old cumulative-integral test rejected the candidate (`26 > 8`); correct
MinMax combination accepts the useful late interval and reduces the peak to
10.

## Changes

- `partial_extend(MIN_MAX)` now only intersects the candidate with the shared
  time domain; it makes no integral-based accept/reject decision. The old
  cumulative-integral branch was removed rather than left as unreachable
  fallback code. `MIN_SUM` retains its existing full-candidate behavior.
- `combine(...)` remains the pointwise lower envelope: it partitions at common
  interval boundaries and quadratic difference roots, then selects the lower
  polynomial on each root-free interval. The existing one- and two-crossing,
  identical-polynomial, no-crossing, and touching-root tests exercise this
  behavior.
- MinMax now stops on the static no-improvement condition only when the
  returned assignment is an exact, proven optimum. A heuristic static
  incumbent is allowed through candidate construction and combination; lack
  of peak improvement then terminates as heuristic stagnation, not as an
  optimality claim. The configured gap target is applied only to a certified
  lower bound.
- Every generated MinMax candidate is checked for a well-formed full
  `[0, T_end]` domain and continuously verified before combination. The
  combined peak is required not to increase beyond a scale-aware tolerance.
- MinMax results now carry initial/final peak times and values, separate
  heuristic gap and bound provenance, and an independent peak-consistency
  result. `Verifier::check_peak_consistency(...)` reconstructs extrema from
  interval endpoints and concave quadratic vertices. This validates the
  reported objective against the returned representation; it does not prove
  global optimality.
- MinMax traces now include static solve status/bounds, candidate and combined
  peaks, improvement, acceptance, certified/heuristic gaps, and explicit stop
  reasons. The reader remains compatible with the previous seven-column trace
  format.
- Batch and benchmark result JSON expose MinMax peak time, initial peak,
  heuristic gap, and peak consistency. Solution serialization also validates
  the independent MinMax peak check.

## End-to-end regression

A deterministic one-point instance moves from station A to station B. The
initial assignment has peak `100*pi`; an exact static solve at the worst-case
time selects B. Extending that candidate backward and combining by pointwise
minimum lowers the global peak. The test asserts a continuously verified,
well-formed result, an exact static solve at the peak, and a final peak strictly
below the initial one.

## Smoke experiment comparison

The pre-existing `algorithm-smoke-20261005-complete` results were inspected
read-only and left unchanged. A new immutable local experiment was generated
under `results/experiments/minmax-correctness-20261006-run03` (30 MinMax runs;
10 selected instances; branch-and-bound, greedy, and nearest-neighbor). Its
aggregate manifest reports 30/30 valid runs, no missing/invalid/failed runs.
All three representative instances below were continuously verified and
passed the independent peak check:

| Instance | Algorithm | Historical peak | New peak | New initial peak | New status |
|---|---|---:|---:|---:|---|
| att48 | branch-and-bound | 7,825.602 | 2,318.290 | 7,825.602 | OPTIMAL |
| att48 | greedy | 9,665.224 | 2,675.375 | 9,665.224 | FEASIBLE |
| att48 | nn | 7,388.436 | 2,604.437 | 7,388.436 | FEASIBLE |
| berlin52 | branch-and-bound | 17,390.778 | 4,698.534 | 17,390.778 | FEASIBLE |
| berlin52 | greedy | 16,693.036 | 5,079.121 | 16,693.036 | FEASIBLE |
| berlin52 | nn | 16,543.415 | 4,698.534 | 16,543.415 | FEASIBLE |
| eil51 | branch-and-bound | 26,956.090 | 3,204.819 | 26,956.090 | FEASIBLE |
| eil51 | greedy | 26,956.090 | 3,217.423 | 26,956.090 | FEASIBLE |
| eil51 | nn | 51,710.110 | 3,375.938 | 51,710.110 | FEASIBLE |

These are diagnostic old/new run outputs, not claims that historical values
were optimal. The new experiment includes manifests, result and solution
artifacts, CSV/JSONL traces, verification records, aggregate exports, tables,
figures, and selected animations. Historical archives were not overwritten.

## Validation and limitations

Passed:

- Release configure and full build: `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release`
  and `cmake --build build --parallel 4`
- `ctest --test-dir build --output-on-failure` and the complete `kdc-tests`
  Catch2 suite
- Focused MinMax/solution/trace/bound-provenance regressions
- `python3 -m pytest -q tests/python` (83 passed)
- `git diff --check`
- The final smoke pipeline at
  `results/experiments/minmax-correctness-20261006-run03`: 30/30 completed and
  verified MinMax runs, with peak consistency passing for all records; the
  aggregate manifest reports no missing or invalid runs. Tables, figures, and
  representative animations were generated.

The smoke comparison is not an exhaustive proof over all kinetic policies.
Solver optimality remains governed by the reported bound and termination
status. The host's native KONT license check failed, so the exact-reference
smoke path used the built-in fallback; records retain the actual backend and
optimality status. Candidate verification adds per-iteration work in exchange
for checking every inserted candidate continuously.
