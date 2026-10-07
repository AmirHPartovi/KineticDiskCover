# Kinetic performance and correctness certification

## Executive result

The deterministic 200-seed campaign found no observable differences among
`REFERENCE_EXHAUSTIVE` with global handover evaluation,
`KINETIC_TOURNAMENT` with global handover evaluation, and
`KINETIC_TOURNAMENT` with local exact handover evaluation. All 600 emitted
engine rows passed differential checks and certified continuous verification.

There is no meaningful support-event speedup or measured break-even point.
Median extension speedups were close to 1x, interquartile ranges crossed 1x,
and support comparison/pairwise polynomial-solve counts were identical. Local
handover evaluation reduced whole-assignment fallback scans in the
handover-heavy family, but the aggregate runtime gain remained small and mixed.
No automatic engine selection is justified; the API default remains
`REFERENCE_EXHAUSTIVE`.

The measurements are exploratory, not representative release benchmarks:
they were recorded from a dirty worktree and a direct optimized compile rather
than a clean CMake build.

## Mathematical invariants

For each station, support is the assigned point maximizing squared distance.
At exact ties, the centralized tolerance policy and deterministic point-id
rule apply. Directional support resolves the first nonzero coefficient in the
local quadratic distance difference; a trajectory breakpoint uses its
adjacent motion segment in the requested direction. This is a directional
limit, not temporal sampling.

A support transition occurs at a root of a pairwise squared-distance
difference over a fixed pair of linear-motion segments. External handover
challenge roots compare the current source support with the current receiver
support. The existing handover rules remain in force: source support
strictness, directional receiver acceptance, the minimum source ownership
condition, and non-increasing objective. Ownership changes only after the
preceding interval is emitted. Simultaneous events use the repository's
central event-time policy.

All roots, comparisons, and event grouping use floating-point arithmetic and
the repository's tolerances; “exact” in this report means exact under those
existing numerical semantics, not exact arithmetic. Continuous verification
is an independent implementation-level check. Kinetic event correctness does
not prove global optimality of the static MinMax, MinSum, or MinMaxSum problem.

## Engines and implementation boundary

| Configuration | Event-engine option | Handover evaluation |
|---|---|---|
| `reference_global` | `REFERENCE_EXHAUSTIVE` | `REFERENCE_GLOBAL` |
| `tournament_global` | `KINETIC_TOURNAMENT` | `REFERENCE_GLOBAL` |
| `tournament_local` | `KINETIC_TOURNAMENT` | `LOCAL_EXACT` |

These rows invoke the public engine options; the benchmark does not label a
vector implementation or placeholder as a tournament baseline. Inspection of
the checked-out implementation shows that `support_changes_impl` still
enumerates every challenger and solves its pairwise segment equations for
both engine values. The engine argument is validated there, but does not
select a maintained tournament certificate queue. The tournament structure
is used for support queries, but its event-query surface is not integrated
into a certificate-driven support scheduler. Consequently, this campaign
certifies equivalence of these execution paths and handover policies; it does
not certify or measure a fully maintained tournament KDS.

For handovers evaluated immediately after a support-state change, local
evaluation falls back to the global reference check. Source eligibility at a
transition must retain the reference approach-side semantics; the differential
campaign exposed a spurious reverse transfer without this fallback. These
fallbacks are counted in `global_fallbacks`.

## Correctness methodology

`benchmarks/kinetic_certification.cpp` generates ten deterministic instance
families: support-event-heavy small cases, large \(n\)/small \(m\), moderate
\(n\)/larger \(m\), irrelevant waypoints, handover stress, near-zero time
scales, simultaneous motion, tangent/degenerate motion, long piecewise
trajectories, and sparse events.

The simultaneous family varies the crossing across deterministic seeds: roots
are placed exactly at the shared midpoint breakpoint, \(2\times10^{-9}\)
before or after it, or nearly simultaneously across station groups. The
tangent/degenerate family alternates an exact tangent equality with a
\(10^{-10}\) spatial perturbation. These are generated adversarial variants,
not a transform applied to every root discovered in every random instance.

Each case uses the same initial assignment, objective, bounds, and interval
emission policy for all three configurations. The comparison checks:

- ordered event traces, event times under the centralized simultaneity
  policy, event types, station/support ids, handover ids, and tie outcomes;
- interval boundaries, support vectors, ownership vectors, and quadratic
  coefficients;
- costs at each event and the adjacent representable floating-point times,
  peak and peak time, and total integral;
- certified continuous verification for every engine output.

The benchmark does not replace continuous verification with dense sampling.
Extension timing excludes verification; rows report `extension_ns`,
`verification_ns`, and `end_to_end_ns` separately. The speedup field is zero
unless the differential checks and verifier pass. On failure, the process
returns nonzero and reports the seed and family for reproduction.

The existing C++ tests additionally cover near-zero roots, exact and adjacent
trajectory-breakpoint directional states, tangent roots, second-order ties,
simultaneous support/handover events, forward and backward traversal, and
randomized handover comparisons.

## Randomized campaign and reproducibility

The raw per-engine, per-instance results are stored in
[`benchmarks/kinetic_certification_results.csv`](../benchmarks/kinetic_certification_results.csv).
The harness takes seed count, repetition count, and optional starting seed
index. The CI command runs `kdc-kinetic-certification 200 1`; the saved local
campaign uses 200 consecutive seeds starting at `20261007`, three repetitions
per engine and instance, and reports the median extension-time run for each
engine. There was no outlier removal.

Every row includes Git revision/dirty status, compiler, build type, reported
logical CPU count, seed, family, \(n\), \(m\), trajectory-segment total,
engine/handover configuration, timing breakdown, available work counters,
interval count, support/handover event counts, peak, integral, speedup, and
correctness status. It also stores an event-grouped trace with event times,
types, stations, support ids, and transferred points. Missing counters and
memory are recorded as `NA`, not estimated.

Local measurement environment:

- Base revision `31fa4196bd478f6e94c033cc0fd7afe73e9528b5`, dirty worktree
  (`git_dirty=true`).
- Apple clang 17.0.0, arm64 macOS 27.2, 8 reported logical CPUs.
- Direct optimized compile: `-O2 -DNDEBUG`; row build type `Release`. This is
  not the repository's full CMake Release build.
- Seeds `20261007` through `20261206`; three repeats per engine and seed.
- Synthetic benchmark only; a clean-runner and stored real-instance campaign
  remain necessary for representative performance claims.

## Raw and aggregate results

All 200 seeds (600 rows) reported `correctness_status=PASS`. Aggregate
per-instance speedups compare each tournament row with its corresponding
reference extension time:

| Configuration | Median extension speedup | p25 | p75 | Geometric mean | Faster than reference |
|---|---:|---:|---:|---:|---:|
| Tournament + global handover | 1.001x | 0.990x | 1.017x | 1.006x | 105 / 200 |
| Tournament + local handover | 1.005x | 0.995x | 1.025x | 1.010x | 122 / 200 |

These small differences are within workload/process variability and should not
be treated as proof of improvement. Median extension times across all rows
were 0.290 ms for reference/global, 0.286 ms for tournament/global, and
0.291 ms for tournament/local. Median verification times were 0.183, 0.183,
and 0.183 ms; verification is reported separately.

Per-family median speedups and interquartile ranges are below. Each family has
20 seeds; these are extension-time ratios to the matching reference instance.

| Family | Tournament/global median (p25–p75) | Tournament/local median (p25–p75) |
|---|---:|---:|
| Support-event-heavy | 1.003x (0.995–1.026) | 1.006x (0.998–1.009) |
| Large \(n\), small \(m\) | 1.001x (0.989–1.016) | 0.996x (0.989–1.015) |
| Moderate \(n\), larger \(m\) | 0.997x (0.983–1.014) | 1.004x (0.978–1.026) |
| Irrelevant waypoints | 1.010x (0.996–1.033) | 1.003x (0.995–1.023) |
| Handover stress | 1.002x (0.988–1.030) | 1.048x (1.040–1.067) |
| Near-zero scale | 1.006x (0.995–1.034) | 1.006x (0.997–1.055) |
| Simultaneous motion | 0.999x (0.989–1.017) | 1.002x (0.997–1.021) |
| Tangent/degenerate | 0.997x (0.991–1.027) | 1.002x (0.990–1.013) |
| Long piecewise paths | 1.005x (0.996–1.015) | 1.009x (0.997–1.017) |
| Sparse events | 0.991x (0.975–1.003) | 1.000x (0.984–1.007) |

In the handover-heavy family (20 seeds), median handovers were six per
solution. The local path's aggregate global-fallback point scans were 1,600,
versus 4,400 for the global evaluator. Its 1.048x median speedup is a
measurable benefit on this deliberately handover-heavy fixture, but does not
establish a broad speedup for typical workloads.

Pairwise point-vs-support comparisons and quadratic solve counts matched
between reference and tournament-selected engines. This confirms that the
tournament option did not reduce the dominant support-event candidate work
in this implementation. Some individual instances improved and some
regressed; no family exhibits evidence for a dependable size-based selection
threshold.

## Break-even, regressions, and memory

No empirical support-event break-even was found over the tested sweep:
\(n\) reached 160, \(m\) reached 8 in the scaled station-count family, and
trajectories reached 40 segments per point. Since point-vs-support and
pairwise polynomial work were unchanged, these results cannot support an
automatic-selection threshold. No `auto` mode is added.

Individual timings show regressions as well as wins. The mixed results and
very small overall median speedups do not meet the promotion criterion.
Tournament-node storage exists, but memory was not measured; all memory
fields are `NA`. Certificate creation/failure, support queue operations,
stale support events, tree updates, and handover queue pops are also not yet
instrumented and are reported as `NA`.

## CI and promotion decision

`.github/workflows/kinetic-certification.yml` runs on pushes, pull requests,
and manual dispatch. It builds the complete C++ test executable and
certification harness, runs the full C++ suite, then runs a seeded 200-case
three-engine differential campaign and uploads the CSV.

Locally, all 228 C++ test cases (37,229 assertions) passed after a direct
Clang build against the installed dependencies. The normal CMake-generated
build could not be regenerated in this checkout because its cached
FetchContent state did not provide the Eigen and Catch2 targets; CI exercises
the repository's standard clean CMake path.

The promotion gate is **not met**. Differential correctness and continuous
verification passed in the recorded campaign, but a fully certificate-driven
tournament event scheduler, clean repeatable runtime benefit, and memory
evidence are absent. Keep `REFERENCE_EXHAUSTIVE` as the default and keep
tournament selection explicit. Nothing in this report claims global
optimization optimality.
