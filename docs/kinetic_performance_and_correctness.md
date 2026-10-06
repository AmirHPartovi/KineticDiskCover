# Kinetic Handover Performance and Correctness

## 1. Scope and implementation status

This change optimizes the area-admissibility work performed after an exact
handover candidate has been detected. It does not replace support-event root
prediction, alter receiver acceptance or derivative tie-breaking, change
MinMax/MinSum/MinMaxSum semantics, or establish global optimality.

The checked-out `KineticFarthestTournament` class is not a maintained
certificate tournament: it rescans a point vector, and its event-query
surface is not integrated into `KineticSolution::extend`. Accordingly, this
report compares the existing exhaustive kinetic extension with the same
extension using global-reference versus local-exact handover evaluation. It
does not report tournament or geometric-engine measurements.

## 2. Reference algorithm

For a possible transfer of point \(p\) from station \(A\) to \(B\), the
original `make_nonincreasing_handover` scanned every owned point and built
before/after radii and support ids for every station. It then compared the
whole objective:

\[
  C_{\mathrm{after}} \le C_{\mathrm{before}} +
  10^{-9}\max(1, |C_{\mathrm{before}}|, |C_{\mathrm{after}}|).
\]

The point's owner is the only assignment changed by this hypothetical
transfer. Therefore every other station \(C\notin\{A,B\}\) has the same
assigned set and the same exact radius before and after. The global reference
path is retained as `HandoverEvaluation::REFERENCE_GLOBAL` for differential
tests and benchmarks.

## 3. Local exact algorithm

The optimized path groups points by owner once per handover query. For a
candidate \(p:A\to B\), it computes only:

- before-support/radius for \(A\) and \(B\);
- after-support/radius for \(A\setminus\{p\}\) and \(B\cup\{p\}\).

The affected station supports are selected using the same directional
velocity tie-breakers and floating tolerance as the reference routine. The
receiver's inserted candidate is considered in ascending point-id order,
matching the original all-points scan. This is a side-effect-free
hypothetical evaluation: owner vectors and any persistent support state are
not mutated.

Let \(L_{\mathrm{before}}\) and \(L_{\mathrm{after}}\) be the sum of the two
affected squared radii. If \(L_{\mathrm{after}}\le L_{\mathrm{before}}\), the
global objective cannot increase: all other stations contribute the same
nonnegative term to both sides. The original non-increase predicate
therefore necessarily accepts, including its nonnegative tolerance. If the
local affected sum increases, the optimized implementation falls back to the
retained global routine. This preserves the original global scale-dependent
tolerance decision exactly in the only case where that shared unchanged
objective could affect acceptance. The fallback is counted in diagnostics.

The local common case inspects points owned by \(A\) and \(B\); it does not
scan unrelated owners or allocate before/after arrays for all stations.
Point grouping costs one linear pass per top-level handover query. Support
event searches that produce candidate events remain the exhaustive reference
search and are not pruned.

## 4. Invariants and event ordering

- Ownership changes only for the candidate point; all other owner groups
  remain unchanged during hypothetical evaluation.
- Each station's radius is the maximum squared distance in its current
  owner group, with the implementation's direction-sensitive tie handling.
- The source candidate must still be the before-support of \(A\), and both
  affected after-stations must have a support, as in the reference routine.
- The existing receiver-disk derivative condition and whole-objective
  non-increase condition remain in force.
- A rejected candidate leaves permanent ownership/support state untouched.
- The extension loop selects the earliest computed boundary and applies a
  handover only at that boundary. Support selection is recomputed after the
  ownership change; a simultaneous support root/active-support waypoint is
  therefore handled on the next iteration using the post-handover owner
  state.
- `event_times_simultaneous` centralizes test/diagnostic time comparison:
  \[
    |t_1-t_2|\le 10^{-9}
      +64\epsilon_{\mathrm{machine}}\max(1,|t_1|,|t_2|).
  \]
  Root prediction and the repository's existing event-selection semantics
  are otherwise unchanged. Existing extension boundary tolerances continue
  to govern interval construction.

Kinetic events are still predicted by piecewise-quadratic root solving across
the union of relevant trajectory breakpoints. The local handover path does
not replace event prediction with sampling.

## 5. Differential certification

`HandoverEvaluation::REFERENCE_GLOBAL` executes the original global
before/after recomputation; `LOCAL_EXACT` uses the station-local path with
the positive-delta global fallback. Tests compare validity and complete
handover event fields in both time directions across fixed and deterministic
random instances, including differing point/station sizes and piecewise
motion.

Extension-level differential tests run the same reference exhaustive event
engine and initial assignment with each handover evaluation mode. They compare
interval boundaries, support vectors, ownership vectors, quadratic
coefficients, peak, peak time, integral, and require continuous verification
for both solutions. The benchmark additionally compares deterministic event
traces, including event type, station/support ids, handover ids, and tie
outcomes. Objective agreement alone is not considered sufficient.

The test corpus also exercises breakpoint support events, tangent/equal
distance cases, simultaneous support changes, support-id ties, forward and
backward extension, handover-at-start behavior, and continuous coverage.
No kinetic hull/geometric engine is claimed or compared.

## 6. Degeneracies

The local support selector retains the prior per-station comparison tolerance
and directional derivative tie-breaker. Equal distance and equal
tie-breaker retains the first point in ascending id order. An accepted local
non-increase requires no global resummation; a positive local increase uses
the original full scan, preserving its aggregate tolerance behavior.

Trajectory waypoints remain the responsibility of the existing piecewise
event detector. A handover exactly coincident with another predicted event
is processed at the common extension boundary; ownership is updated only
after constructing the preceding interval, and subsequent supports are
recomputed from the new ownership.

## 7. Benchmark methodology

The CMake `BUILD_BENCHMARK` suite now includes
`kdc-kinetic-handover-benchmark`, using the repository's existing benchmark
target pattern and build metadata. It emits CSV to standard output and takes
instance-count and repetition-count arguments. For each deterministic seed,
both handover evaluators receive the same initial assignment, event engine,
solver setup, and verification. Rows include seed/configuration, build
revision/dirty state, platform and logical CPU count, runtime, event and
handover diagnostics, interval counts, verification runtime, peak, integral,
speedups, and a required correctness status. Unsupported tournament,
geometric, and certificate-queue metrics are explicitly emitted as `NA`.

Five deterministic workload families are covered: small, general,
large-\(n\) small-\(m\), moderate-\(n\) large-\(m\), and handover-heavy.
Repetitions are sorted by
extension wall time and the middle observation is reported. Correctness is
required for every repetition; a mismatch returns failure rather than
emitting a successful row. The generated suite is a reproducible synthetic
microbenchmark, not a substitute for the repository's stored real-instance
experiment runs.

Run with:

```sh
cmake -S . -B build -DBUILD_BENCHMARK=ON
cmake --build build --target kdc-kinetic-handover-benchmark
./build/kdc-kinetic-handover-benchmark 10 3 > handover-results.csv
```

The reported extension speedup is the ratio of reference-global to
local-exact extension runtime. Event-detection and interval-construction
ratios are reported separately. Verification overhead is reported as
optimized verification runtime divided by reference verification runtime.
No global solver optimality claim follows from a faster or verified kinetic
extension; MinMax lower-bound/optimality semantics are unchanged.

## 8. Measured results

The persisted CSV `benchmarks/kinetic_handover_results.csv` records five
deterministic families (three repetitions each; median extension runtime),
measured on Release/macOS with 8 logical CPUs. Every repetition passed
solution/event-trace comparison and continuous verification. The recorded
revision was `78cd913` with a dirty worktree. The benchmark executable was
manually linked against the available Release project libraries because
CMake regeneration/build subprocesses were blocked by host process-resource
exhaustion; the resulting executable ran successfully and produced the CSV.
This is a reproducible measurement from the recorded binary and inputs, but
not a clean CMake rebuild of the current dirty worktree.

| Workload | \(n,m\) | Reference → local extension (ms) | Speedup | Handover checks | Handover-phase speedup | Intervals ref → local | Correct |
|---|---:|---:|---:|---:|---:|---:|:---:|
| Small | 8, 2 | 0.0630 → 0.0618 | 1.0195x | 0 | 1.2049x* | 9 → 9 | PASS |
| General | 60, 6 | 73.945 → 72.709 | 1.0170x | 0 | 1.0163x* | 109 → 109 | PASS |
| Large \(n\), small \(m\) | 120, 2 | 7.800 → 7.674 | 1.0165x | 0 | 1.0232x* | 48 → 48 | PASS |
| Moderate \(n\), large \(m\) | 36, 12 | 58.366 → 58.454 | 0.9985x | 0 | 0.9981x* | 64 → 64 | PASS |
| Handover-heavy | 20, 2 | 0.1010 → 0.0958 | 1.0553x | 19 | 1.1270x | 7 → 7 | PASS |

`*` No handover was evaluated in these families; these phase ratios are
microsecond-scale timing observations, not evidence of a handover benefit.
The local implementation inspects 440 station-local support points across
19 handover checks in the handover-heavy workload, accepts 11 locally, and
uses no global fallbacks. Support-comparison and certificate counts matched
between evaluators on every row. Peak cost and integral agreed exactly in the
recorded output; verification overhead ratios ranged from 0.9256x to 1.0500x.
The measured gains are modest, one workload has a small regression, and
interval counts are unchanged. These results do not demonstrate a universal
runtime improvement; they support retaining the reference mode and revisiting
default selection with broader real-instance measurements.

## 9. Known limitations and default recommendation

- The support-event search remains exhaustive and can dominate total runtime,
  limiting the end-to-end gain from a handover-only optimization.
- Positive affected-radius deltas deliberately fall back to the global
  reference evaluator to preserve its global tolerance semantics.
- A diagnostic grouped-point construction is linear in point count. Very
  small or no-handover instances may not benefit; benchmark results should
  guide whether the default should be local or reference for those workloads.
- `KINETIC_TOURNAMENT` is not a maintained, integrated kinetic certificate
  engine in the checked-out implementation. It is not a valid tournament
  benchmark baseline.
- No orientation certificates, kinetic hull, or farthest-point Voronoi
  structure are implemented here.

Keep exhaustive support-event detection and global handover evaluation
available as references. The local path is exact by the two-station locality
argument and is differentially checked. Current synthetic measurements show
small extension gains on four families, one slight regression, and a larger
gain on the handover-heavy family; they do not establish representative
real-world performance. This work does not change solver optimality
certification.
