# Implementation audit

**Scope:** current branch `amirhosseinpartovi0-kinetic-disk-cover`. This audit
was first written before the cooperative deadline implementation; the timeout
findings and requirement matrix below are updated to describe the current code.

## Execution flow

- `main.cpp` routes `batch` to `BatchRunner`, `benchmark` to
  `BenchmarkRunner`, and `compare` to `AlgorithmComparator`. The batch and
  benchmark commands both parse `--exact-reference`, but the benchmark runner
  does not use that setting to choose a backend.
- `BatchRunner::run` discovers and sorts JSON paths, selects algorithms,
  groups work into fast / heuristic / exact phases, and creates a fresh
  `IStaticSolver` per run. In parallel mode it also creates a per-task
  `KontSolver`. `run_single` calls MinMax or MinSum, optionally serializes the
  solution and trace, then records JSON/CSV results.
- `BenchmarkRunner::run_all` loads top-level JSON files, iterates objectives
  and repeats, and invokes `run_single`. It always constructs `KontSolver`;
  its `exact_reference` config field is not consulted for selection.
- `ComparativeRunner::run` runs MinMax and then MinSum using one supplied
  `IStaticSolver`, and writes a combined result. `AlgorithmComparator` instead
  compares static registry solvers against a static reference, or invokes the
  kinetic solvers once per configured algorithm.
- Both kinetic solvers call the `IStaticSolver` interface for stationary
  assignments. `IPStaticSolver` builds an IP through `StationarySolver`;
  `StationarySolver` builds candidates and coverage through `CandidateSet`
  before calling the `ILPSolver`. Kinetic extensions use
  `KineticSolution::extend` and `KineticCore` event routines, then combine
  intervals. The final verifier runs from MinMax/MinSum and can also run again
  in `BenchmarkRunner`.
- `run_batch.py` forwards options to the C++ batch command. The limited wrapper
  launches one solver process per instance/algorithm/objective and merges the
  per-run outputs.

## Findings

### Timeouts and iteration limits

`SolverBudget` uses a `steady_clock` deadline, supports cooperative
checkpoints/cancellation, and reports remaining seconds. MinMax and MinSum
share one budget for each run; batch creates one per instance × algorithm ×
objective. Their default limits are 30 seconds for heuristic solvers and 600
seconds for exact solvers, with CLI overrides `--fast-time-limit` and
`--exact-time-limit`. The independent per-static/IP default remains 60 seconds.

`IStaticSolver::solve_with_budget` caps each child solve at the smaller of its
configured static limit and remaining global time. Budget checkpoints are
propagated through candidate/coverage construction, stationary IP model
construction, static-solver refinement loops, kinetic extension/combine,
handover/event work, local improvement, and verification. Static backends
receive the clamped limit through `set_time_limit` or their nested ILP call;
an already-running third-party backend can only stop cooperatively according
to that backend's own time-limit behavior. No process or thread is forcibly
terminated. After a complete kinetic incumbent exists, timeout preserves it,
returns normally, and marks `time_limited`; no optimality claim follows from
expiration. Before initial extension produces a complete incumbent, a timeout
can still propagate as an error.

Both Python batch wrappers forward the three solver limits. The limited
wrapper no longer imposes a separate OS-process timeout, so it cannot kill a
solver that fails to cooperate.

### Lower bounds and certification

Results now carry explicit `BoundStatus` and `OptimalityStatus` values,
feasibility, exact-solver provenance, upper/lower bounds, and an optional
certified gap. Certification is assigned per solver rather than inferred from
`provides_lower_bound()`. The nearest-station geometric bound used by nearest
neighbor, greedy, local search, SA, genetic, and shifting is certified.
Primal-dual's epsilon-updated dual value is marked heuristic. LP-rounding's
relaxation bound is certified only when the LP backend reports optimality.
IP-KONT and branch-and-bound carry backend/search bounds; brute force is
optimal only after full enumeration. See `docs/bound_semantics.md` for the
individual mathematical justifications and conservative downgrades.

For MinMax, only a static bound whose explicit status is `CERTIFIED` is
propagated as a certified kinetic peak bound. Heuristic progress estimates
remain separately labeled.

MinSum defaults to incumbent-driven adaptive refinement and does not perform
the sampled lower-bound initialization. It selects the current interval with
the largest estimated integral contribution and solves at its midpoint,
stopping on the configured gap target, deadline, iteration cap, stagnation
patience, or negligible improvement. Candidate refinements must pass sampled
coverage/support checks before replacing the incumbent; this is not a
continuous-time proof. Its separate `CERTIFIED_BOUND` policy
retains the sampled/trapezoidal estimate only as a heuristic: pointwise
samples do not certify the integral between times. Both policies carry the
nonnegative-cost bound zero as the independent certified integral lower
bound, and neither emits a certified gap because the kinetic solver does not
prove global optimality. Timeout never establishes optimality.

### Verification and continuous feasibility

`Verifier::verify` now delegates to `verify_continuous`; the requested sample
count is retained only for source compatibility. The continuous path partitions
solution intervals at every trajectory breakpoint. On each affine piece it
forms the polynomial boundary equation for
`distance(point, station) <= distance(support, station) + tolerance`, isolates
its real roots, then checks each root and one point in every resulting open
interval. It applies the same check to assigned points, validates piecewise
quadratic cost coefficients over each full interval, and compares the stored
integral with an analytic integral split at trajectory breakpoints.
`verify_empirical` retains the old finite-sample diagnostic and labels its
report `EMPIRICAL`; it is not certification. The continuous root calculations
use floating-point arithmetic and the configured tolerance, so this is an
analytic piecewise-model check rather than an exact-arithmetic proof.

MinMax and MinSum default to final verification enabled and per-iteration
verification disabled. `--verify-each-iteration` enables explicit checks at
accepted iterations; `--no-verify` disables final verification. MinSum still
requires continuous verification before accepting a refinement; its sampling
filter is only a heuristic prefilter. Solver results report verification kind
and time separately, and batch/benchmark records expose `solve_time_sec`
beside end-to-end `wall_time_sec`.

### Repeated geometry and performance

`Instance` now owns a synchronized, lifetime-scoped `InstancePrecompute`.
It stores candidate identities in the original `(station_id,
supporting_point)` order, station positions, initial point positions, and
trajectory-segment metadata. The cache is tied to the instance object rather
than process-global state; its source snapshot is checked before reuse, so
mutating public instance geometry invalidates the precompute. Its lifetime and
memory use are bounded by the owning instance.

Each static query builds one transient `StaticGeometry`: trajectory positions
are evaluated once and station-point squared distances are computed once.
Coverage derives each candidate radius squared from that same table and emits
its CSR rows in one geometry pass, preserving the previous distance-space
`1e-9` threshold via its equivalent squared-distance tolerance. Static NN, IP,
LP-rounding, primal-dual, and branch-and-bound LP construction consume this
geometry instead of recomputing candidate radii or point distances.

Kinetic event routines consume the same instance-scoped segment metadata,
including cached duration, start position, velocity, and affine origin.
Support-event construction uses those segment velocities and evaluates each
shared segment start once instead of deriving velocity from trajectory
positions at both ends. `find_next_event` reuses that precompute across
stations; handover searches determine the second assigned support and support
events once per source station rather than once per station pair. Integral
handover refinement uses the grouped source-station search. No event-result
cache is used, avoiding incomplete cache keys or unbounded event storage.

`KineticSolution::extend` reuses one query geometry for initial assignment
ownership, uses precomputed affine segments for probe positions, derivatives,
and cost coefficients, and finds the next trajectory breakpoint through one
sorted instance-wide breakpoint list. There is no profiler-backed benchmark
in the test suite establishing the relative costs of remaining kinetic
operations.

### Exact reference and algorithm selection

`ExactReferenceSelector::resolve` writes requested/actual backend strings to
`experiment_manifest.json`, but `auto` is not an actual calibration in the
current build. Without `KDC_HAS_COPT_CPP_API`, it selects branch-and-bound
directly. With that macro, it attempts runtime comparisons, but the helper
does not validate optimality/feasibility, times MinMax for `ip-kont` and MinSum
for branch-and-bound, averages successful calls rather than comparing the
same workload with a robust statistic, and suppresses exceptions. The
verification predicate intended for calibration is unused. Runtime
availability for `ip-kont` is checked via `KontSolver::name()`, which always
returns `"KONT"` and does not establish that the native backend is usable.

`BatchRunner` resolves once using the configured dataset/output paths, but
default algorithm selection can resolve again through hard-coded
`data/instances` and `results/batch` paths. The decision is logged but is not
used to choose the algorithm in `run_single`; explicit `ip-kont` can still
execute through the `KontSolver` fallback while the manifest calls it
`ip-kont`. Custom algorithm lists are not constrained to exactly one exact
solver. The C++ default profile is eight fast heuristics plus one exact
reference and ignores `brute-force`; explicit custom lists may select multiple
exact solvers.

The Python limited wrapper chooses `auto` by checking whether
`build/kdc-solver` exists, which is not a backend-availability check; it
usually selects `ip-kont` whenever the executable exists. Its per-run manifest
is in a temporary directory and is not copied to the final output. These
behaviors make backend selection and experiment-level reproducibility
inconsistent across entry points.

### Results, reproducibility, and timing

Batch JSON declares certified/heuristic lower-bound fields, but
`BatchRunner::run_single` does not populate them; batch CSV omits them.
`BenchmarkRunner` propagates both fields for MinMax, but not for MinSum, and
benchmark CSV also omits them. `ComparativeRunner` and kinetic
`AlgorithmComparator` retain only an undifferentiated lower bound and gap.
Result records do not include exact backend provenance/calibration evidence.

Static `solve_time_sec` values exist, and benchmark records expose `ip_time_sec`
as time accumulated around calls to the wrapped `ILPSolver`. Wall time includes
solver work and, in the runner, verification; it may also include solution
serialization. CPU time uses process-wide `std::clock()`, so measurements from
parallel work items are not isolated per task and can include concurrent CPU
consumption. Kinetic `total_time_sec` excludes measured verification work;
batch and benchmark records retain wider end-to-end wall time separately from
solver and verification time.

Randomized static solvers use fixed default seed 42; random instance generation
accepts an explicit seed. These choices make same-config solver calls
repeatable, but CLI/wrappers do not expose or record the algorithm seeds.
Benchmark repeats reuse the same configs, and output records contain no seed,
solver configuration snapshot, compiler/backend version, or calibration
sample manifest.

## Test coverage

Existing tests cover solver feasibility/quality on small fixtures, some
static lower-bound inequalities, fixed-seed determinism for randomized
algorithms, candidate ordering/coverage and one large candidate-build timing
threshold, verifier behavior, B&B/brute-force static limits, batch/benchmark
outputs, and sequential-versus-parallel consistency.

There are no tests for exact-reference selection or calibration, manifest
content/retention, per-experiment backend reuse, real-time global deadline
overshoot in third-party backends, MinMax/MinSum iteration caps or verification cadence, cache
collisions/eviction/concurrency, certified-versus-heuristic metadata across
all serializers, or continuous coverage between verifier samples. Existing
kinetic bound tests check `lower_bound <= objective` on fixtures and monotone
reported gaps; they do not establish a general bound certificate. Verifier
tests include a between-sample coverage violation and trajectory-breakpoint
handling.

## Later phases and requirement matrix

| Requirement | Status | Evidence / gap |
|---|---|---|
| Per-subsolve timeouts | IMPLEMENTED | Child static solve limit is clamped to remaining global budget; native backends receive the limit. |
| Cooperative global kinetic deadline | IMPLEMENTED | Shared budget is checked throughout expensive kinetic operations and solver loops; in-flight external backends remain cooperative. |
| Configurable iteration caps | IMPLEMENTED | Both kinetic solvers default to 64 and use the cap. |
| Separate heuristic and certified bounds | IMPLEMENTED | Explicit bound provenance is carried by static, kinetic, batch, and benchmark results. |
| Certified MinSum lower bound and gap | IMPLEMENTED | The certified integral lower bound is zero; sampled trapezoid estimates remain heuristic. |
| Candidate/coverage reuse | PARTIAL | Caches exist, but keys are lossy, maps unbounded, and values copied. |
| Geometry and kinetic-event reuse | MISSING | Segment, position, distance, and event computations are repeated. |
| Continuous feasibility verification | IMPLEMENTED | `verify_continuous` partitions at trajectory breakpoints and polynomial contact roots; `verify_empirical` is explicitly non-certifying. Root isolation uses floating-point arithmetic and configured tolerances. |
| Verification checkpoints and separate timing | IMPLEMENTED | Final and optional iteration checks use continuous verification; verification kind/time and solver time are separate in kinetic, batch, benchmark, and comparison records. |
| Exact-reference auto calibration | MISSING | No trustworthy same-workload calibration/verification/median selection; no calibration report. |
| Exactly one exact backend per experiment | PARTIAL | Default C++ profile adds one, but custom selections and Python wrapper behavior are not consistently constrained. |
| Requested vs actual backend and persistent decision | PARTIAL | Manifest fields exist, but solver selection may not consume that decision and wrapper manifests are discarded. |
| Complete scientific result schema | PARTIAL | Bound and optimality metadata is carried by batch and benchmark records; comparative output and some legacy CSV surfaces remain incomplete. |
| Reproducible seed/config provenance | PARTIAL | Fixed defaults and seeded data generation exist; run seeds/config/backend provenance are not exposed or recorded. |
| Separate solve and verification timing | IMPLEMENTED | Kinetic results expose solver and verification time; batch/benchmark records retain end-to-end wall time as well. |

Suggested implementation order (not performed in this audit):

1. Make exact-reference selection one per experiment, consume the resolved
   backend in all runners, and persist the actual backend and calibration
   evidence; test unavailable/fallback and custom-list behavior.
2. Define proof semantics for static bounds and a sound MinSum integral
   certificate; only compute/report certified gaps from proven bounds.
3. Propagate a shared deadline through startup, static solvers, refinement,
   geometry work, and verification; report timeout state and separate timings.
4. Replace process-global string-key caches with bounded, collision-safe
   per-instance reusable geometry/event data; test cache equivalence and
   concurrency.
5. Complete and version the result schema across JSON/CSV/comparative outputs;
   expose and record seeds/configuration and add focused regression tests.
