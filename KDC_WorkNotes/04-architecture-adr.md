# KDC Stage 04 - Model A Architecture ADR

Date: 2026-11-19
Status: Proposed
Scope: staged implementation design for thesis-aligned Model A MinSum
Repository: `/Users/amirpartovi/Desktop/Codes/KineticDiskCover`

## Context

The local repository already implements:

- static disk-cover solvers;
- a piecewise kinetic solution representation;
- `MinSumSolver` adaptive refinement;
- continuous feasibility and integral verification;
- benchmark, batch, CLI, and result serialization paths.

The thesis source additionally requires a separate weighted static problem,
three Oracle families, interval matrices D(t)/D-(I)/D+(I), interval bound
certificates, a temporal interval branch-and-bound, and status/provenance
semantics that the current repository does not contain.

This ADR is an implementation plan only. It does not change code or outputs.

## Direct thesis constraints

The following come directly from the thesis specification extracted in
`02-thesis-spec.md`.

1. Model A minimizes
   \(J=\int_0^T\pi\sum_i r_i(t)^2dt\).
2. Centers are fixed and point trajectories are piecewise linear.
3. Coverage is required at every instant.
4. Assignments may change over time.
5. No handover cost, radius continuity, rate bound, or station-capacity
   constraint belongs to Model A.
6. `D(t)`, `D-(I)`, and `D+(I)` are three different inputs.
7. `D-(I)` and `D+(I)` are weighted bound matrices, not geometric
   configurations of an actual time.
8. Oracle.Heuristic returns a feasible UB candidate, never an LB.
9. Oracle.Relaxation provides an LB only with an independently valid
   lower-bound certificate.
10. Oracle.Exact requires exact proof, not merely an optimal backend status.
11. Every finite global UB requires a complete feasible program over [0,T].
12. Every LB/UB needs validity, method, arithmetic mode, and provenance.
13. A missing or invalid local certificate replaces that leaf in the current
    partition with LB=0 and UB=+infinity.
14. CERTIFIED, NUMERICALLY_BOUNDED, HEURISTIC, SAMPLED, and INCOMPLETE remain
    separate.
15. Continuous verification proves feasibility/consistency, not objective
    optimality.

## Proposed engineering decisions

The items below are **proposed engineering decisions**. They are not claims
that a specific implementation detail appears in the thesis.

- [PED-1] Put the thesis implementation behind a new objective execution
  mode, named provisionally `MODEL_A_BRANCH_AND_BOUND`, rather than modifying
  `MinSumSolver`.
- [PED-2] Preserve `MinSumSolver` unchanged as the existing adaptive path.
- [PED-3] Preserve `BranchAndBoundSolver` as a geometric static solver.
- [PED-4] Consider static search reuse only after a compatibility harness
  proves the existing search can consume the pure weighted matrix API.
- [PED-5] Use a separate weighted W API with no `Instance`, `time`,
  `CandidateSet`, or station-point geometry.
- [PED-6] Do not add weighted behavior to `IStaticSolver`.
- [PED-7] Use a versioned internal/public data schema.
- [PED-8] Keep legacy result fields and JSON keys; add new fields under
  versioned schema objects.
- [PED-9] Introduce a leaf status object rather than reusing `BoundStatus`
  alone.
- [PED-10] Use explicit budget checkpoints and cooperative cancellation.
- [PED-11] Use conservative double intervals initially, and permit exact
  arithmetic as a separate later implementation, not as a substitute that
  silently changes result semantics.
- [PED-12] Preserve existing benchmark data and output paths; add a new
  execution mode and versioned result fields rather than replacing legacy
  fields.

## Architectural separation

```text
Geometry and trajectories
  -> IntervalDistanceBoundBuilder
  -> D(t), D-(I), D+(I)

D-(I), D+(I), or D(t)
  -> WeightedStaticProblem
  -> WeightedOracleExact / Heuristic / Relaxation

Weighted Oracle results
  -> CertificateValidator / arithmetic binding
  -> ValidatedLB / ValidatedUB / Assignment

IntervalDistanceBoundBuilder
  + Validated static bound results
  -> IntervalBoundEvaluator
  -> IntervalLB, IntervalUB, complete feasible program

IntervalBoundEvaluator
  -> TemporalLeaf
  -> TemporalPartition P
  -> TemporalBranchAndBound

TemporalBranchAndBound
  -> complete incumbent and global LB/UB
  -> ModelAResult

ModelAResult / kinetic program
  -> IndependentContinuousVerifier
  -> VerificationReport

Benchmark/CLI/Serializer
  -> versioned JSON/CSV/trace
```

`WeightedStaticProblem` is the only dependency of `TemporalBranchAndBound`
for weighted static work. `TemporalBranchAndBound` does not depend on
`IStaticSolver`, `CandidateSet`, `StationarySolver`, or actual geometry.

## Proposed package layout

The names below are **proposed engineering decisions**. Exact names require
later approval.

### 1. Weighted static problem and Oracles

New files:

- `include/kdc/model_a/weighted_static.hpp`
- `src/model_a/weighted_static.cpp`
- `include/kdc/model_a/weighted_oracle.hpp`
- `src/model_a/weighted_oracle.cpp`

#### Responsibility

`WeightedStaticProblem` owns:

- nonnegative matrix `W` of size m x n;
- matrix dimension/version metadata;
- the objective
  `g(W)=pi*min_sigma sum_i max_{j: sigma(j)=i} w_ij`;
- assignment and q-variable conventions.

It must not contain an `Instance`, trajectory, station, time, or coverage
matrix derived from geometry.

`WeightedOracle` owns no geometry or temporal state.

#### Proposed API

```cpp
struct WeightedStaticProblemV1 {
  std::uint32_t schema_version{1};
  Eigen::MatrixXd weights;
  MatrixOwnership ownership;
};

struct WeightedExactResult {
  bool exact_proof{false};
  Assignment assignment;
  ValidatedBound lower_bound;
  ValidatedBound upper_bound;
  Provenance provenance;
};

struct WeightedHeuristicResult {
  Assignment assignment;
  ValidatedBound upper_bound;
  Provenance provenance;
};

struct WeightedRelaxationResult {
  bool dual_feasible{false};
  ValidatedBound lower_bound;
  RelaxationCertificate certificate;
  Provenance provenance;
};
```

The exact result includes lower and upper bounds only if independently valid.
The heuristic result has an UB but no LB.
The relaxation result has an LB only if certificate validation passes.

#### Dependencies

- Eigen for matrix representation.
- existing ILP interface may be adapted through an internal backend adapter.
- no dependency on `Instance`, `CandidateSet`, `IStaticSolver`,
  `StationarySolver`, `KineticSolution`, or time.

#### Data ownership

The caller owns the matrix. `WeightedStaticProblem` may copy or reference it
only behind a documented lifetime. Oracle results own their assignment,
certificates, provenance, and computed bounds.

#### Errors

- invalid dimensions;
- negative/nonfinite weights;
- infeasible problem;
- missing/corrupt certificate;
- timeout;
- backend initialization failure;
- arithmetic/certificate invalidation.

#### Budget/cancellation

- one optional `SolverBudget` shared across Oracle calls;
- cooperative checkpoints in preprocessing, solve, certificate validation,
  and assignment expansion;
- timeout does not change a historical valid certificate;
- timeout invalidates the current leaf result and replaces it in P by
  LB=0/UB=+infinity.

#### Contract tests

- W-only API can run with no `Instance`.
- Chapter-5 strict-LP-gap matrix.
- exact enumeration parity.
- heuristic assignment cost >= exact optimum.
- relaxation-only result cannot produce a heuristic cost as LB.
- invalid dual certificate downgrades to no LB.
- mutation/copy isolation.

### 2. Weighted search compatibility adapter

New files:

- `include/kdc/model_a/weighted_search_adapter.hpp`
- `src/model_a/weighted_search_adapter.cpp`

#### Responsibility

This adapter demonstrates whether existing `BranchAndBoundSolver` search
logic can be reused for weighted matrices.

It must first implement a compatibility harness that separates:

- geometric static cost computation;
- weighted assignment search;
- bound frontier logic;
- incumbent reconstruction.

#### Proposed engineering decision

Do not reuse `BranchAndBoundSolver` until tests prove:

- no `time` argument is required;
- no `CandidateSet` is consulted;
- no geometry-to-W conversion occurs;
- frontier bounds remain valid for arbitrary W;
- preprocessing does not alter g(W);
- timeout yields FEASIBLE/TIME_LIMIT rather than OPTIMAL.

Until equivalence is proven, the existing `BranchAndBoundSolver` remains the
geometric static solver and is not a Weighted Oracle implementation.

### 3. Geometry and interval bound builder

New files:

- `include/kdc/model_a/geometry_bounds.hpp`
- `src/model_a/geometry_bounds.cpp`

#### Responsibility

Consumes actual `Instance` and a time interval I.

Produces:

- D(t) or the interval distance matrices;
- D-(I);
- D+(I);
- interval split breakpoints;
- conservative arithmetic intervals for d_ij(t).

It must not call `WeightedStaticProblem` or use candidate disks for D-/D+.

#### Proposed API

```cpp
struct IntervalDistanceMatricesV1 {
  std::uint32_t schema_version{1};
  TimeInterval interval;
  Eigen::MatrixXd lower;
  Eigen::MatrixXd upper;
  Provenance provenance;
};

IntervalDistanceMatricesV1 build_interval_distance_bounds(
    const Instance& instance,
    const TimeInterval& interval,
    const ArithmeticConfig& arithmetic,
    SolverBudget* budget);
```

#### Validation

Before conversion to D-/D+, independently check:

- componentwise inclusions for all t in I;
- endpoint and breakpoint inclusion;
- outward rounding;
- no NaN/Inf;
- no use of midpoint sampling as a bound.

This class may reuse existing trajectory segment metadata, but it must not
reuse `CandidateSet` disk-candidate interpretation for D-/D+.

### 4. Arithmetic and certificate layer

New files:

- `include/kdc/model_a/arithmetic.hpp`
- `include/kdc/model_a/certificate.hpp`
- `src/model_a/arithmetic.cpp`
- `src/model_a/certificate.cpp`

#### Responsibilities

- represent exact, interval, and floating-point arithmetic modes;
- conservatively round comparisons;
- independently validate Oracle and relaxation certificates;
- provide validity tokens for LB/UB;
- downgrade invalid or unsupported certificates.

#### Proposed API

```cpp
enum class ArithmeticModeV1 {
  DOUBLE,
  CONSERVATIVE_INTERVAL,
  EXACT_RATIONAL
};

struct ValidatedBoundV1 {
  double lower;
  double upper;
  BoundValidity validity;
  BoundMethod method;
  ArithmeticModeV1 arithmetic;
  Provenance provenance;
};

struct CertificateCheckV1 {
  bool valid;
  std::string reason;
  Provenance checker_provenance;
};

CertificateCheckV1 validate_lower_bound(
    const WeightedRelaxationResult& result,
    const WeightedStaticProblemV1& problem);
```

#### Validity semantics

- DOUBLE certificate can only issue non-certified numeric status.
- CONSERVATIVE_INTERVAL may issue NUMERICALLY_BOUNDED if the inclusion is
  proven.
- EXACT_RATIONAL may support CERTIFIED only after exact proof and complete
  feasible-program checks.
- An LP primal objective is never a LB without a checked dual/certificate.
- A heuristic assignment cost is never a LB.

### 5. Interval evaluator and lower/upper-bound engine

New files:

- `include/kdc/model_a/interval_evaluator.hpp`
- `src/model_a/interval_evaluator.cpp`

#### Responsibility

Evaluates one temporal interval I:

- obtains D-(I), D+(I), and optional D(t_mid);
- constructs weighted static subproblems;
- calls the three Oracles;
- constructs interval LB/UB with method and provenance;
- computes an integral bound contribution for a leaf;
- validates complete-program coverage for UB.

#### Proposed API

```cpp
struct IntervalBoundV1 {
  double lb_integral;
  double ub_integral;
  LeafBoundStatus status;
  BoundMethod lb_method;
  BoundMethod ub_method;
  ArithmeticModeV1 arithmetic;
  Provenance provenance;
  std::vector<OracleCallRecord> oracle_calls;
};

IntervalBoundV1 evaluate_interval(
    const Instance& instance,
    const TimeInterval& interval,
    WeightedOracle& oracle,
    const ArithmeticConfig& arithmetic,
    SolverBudget* budget);
```

#### Data ownership

The evaluator owns no persistent global incumbent or partition. It receives
and validates I and returns immutable leaf-bound records.

#### Invalid leaf behavior

If a local computation/certificate is invalid:

- `lb_integral=0`;
- `ub_integral=+infinity`;
- status `INCOMPLETE`;
- provenance records the failure.

Historical valid certificates stay in history but are not merged into P unless
revalidated against the current matrix and partition.

### 6. Temporal interval branch-and-bound

New files:

- `include/kdc/model_a/model_a_partition.hpp`
- `include/kdc/model_a/model_a_branch_and_bound.hpp`
- `src/model_a/model_a_partition.cpp`
- `src/model_a/model_a_branch_and_bound.cpp`

This layer is separate from `MinSumSolver`.

#### Responsibility

- owns P and Q;
- splits live intervals;
- aggregates leaf bounds;
- maintains incumbent complete programs;
- rolls back invalid leaves;
- tracks history separately;
- applies stopping conditions;
- emits final LB/UB/status.

#### Proposed P/Q types

```cpp
struct ModelALiveLeafV1 {
  TimeInterval interval;
  IntervalBoundV1 bounds;
  unsigned depth;
  LeafStatus status;
  Provenance provenance;
};

class ModelAPartitionV1 {
 public:
  bool covers_full_domain() const;
  bool disjoint_open_intervals() const;
  double lower_bound_integral() const;
  double upper_bound_integral() const;
  void split_leaf(const LeafId&, double);
  void rollback(const SnapshotId&);
};
```

Priority and queue ordering are **proposed engineering decisions**:

- largest integral uncertainty first;
- deterministic leaf id tie-break;
- stale Q entries rejected after partition revision;
- no priority rule is taken as a thesis claim.

P/Q must not depend on existing `IStaticSolver` or geometric static solver
interfaces.

#### Temporal B&B dependencies

Allowed dependencies:

- `WeightedStaticProblem`;
- `WeightedOracle`;
- `IntervalDistanceBoundBuilder`;
- `IntervalBoundEvaluator`;
- `ArithmeticCertificate`;
- `IndependentContinuousVerifier`;
- `SolverBudget`.

Forbidden dependencies:

- `IStaticSolver`;
- `CandidateSet`;
- `StationarySolver`;
- existing `BranchAndBoundSolver` in the temporal engine;
- `MinSumSolver`;
- existing geometric static solver registry.

The existing geometric `BranchAndBoundSolver` may be reused only after the
separate compatibility adapter proves all weighted-search requirements.

#### Stopping behavior

- gap absolute/relative;
- oracle cap;
- interval/leaf cap;
- timeout;
- minimum interval length;
- verification failure;
- oracle failure;
- inconsistent partition.

These are thesis-defined conditions; their exact stop reason serialization is
part of the API.

### 7. Independent continuous verifier

New files:

- `include/kdc/model_a/model_a_continuous_verifier.hpp`
- `src/model_a/model_a_continuous_verifier.cpp`

This verifier must not reuse `MinSumSolver` or adapt a finite-sample
heuristic into a continuous certificate.

#### Responsibility

Given a complete Model A program:

- verify coverage at every continuous piece;
- verify assigned ownership/support;
- verify radius/assignment consistency;
- verify quadratic cost representation;
- verify analytic integral;
- verify that endpoints and open pieces are all covered.

`VerificationKind` remains separate from objective optimality.

### 8. Integration/output layer

Potential changes to existing files:

- `src/main.cpp`
- `src/benchmark.cpp`
- `src/batch_runner.cpp`
- `src/algorithm_comparison.cpp`
- `docs/schemas/result.schema.json`
- `docs/schemas/verification.schema.json`
- `docs/schemas/run_record.schema.json`
- `include/kdc/trace.hpp`
- `src/trace.cpp`

#### Proposed engineering decision

Add:

- `objective`/`execution_mode = model_a_thesis`;
- versioned `model_a` result object;
- separate `bound_status`, `verification`, `stop_reason`, and provenance;
- no implicit mapping of old fields to new status semantics.

Legacy fields remain:

- `lower_bound`;
- `upper_bound`;
- `gap`;
- `optimality_status`;
- `verified`;
- `verification_kind`;

New result fields are versioned and preserve legacy values only for compatibility.

## Dependency graph

```text
model_a/weighted_static
  <- model_a/weighted_oracle
  <- model_a/certificate
  <- model_a/weighted_search_adapter(optional)

trajectory/Instance
  -> model_a/geometry_bounds
  -> D-/D+/D(t)

D-/D+/D(t)
  -> model_a/weighted_static
  -> model_a/weighted_oracle

model_a/weighted_oracle
  -> model_a/certificate
  -> model_a/interval_evaluator

model_a/geometry_bounds
  -> model_a/interval_evaluator

model_a/interval_evaluator
  -> model_a/model_a_partition
  -> model_a/model_a_branch_and_bound

model_a/model_a_partition
  -> model_a/model_a_branch_and_bound

model_a/model_a_branch_and_bound
  -> model_a/model_a_continuous_verifier
  -> model_a/model_a_result

model_a/model_a_result
  -> existing benchmark/batch/CLI adapters
  -> versioned JSON/CSV/trace schemas
```

Hard forbidden edge:

```text
model_a/model_a_branch_and_bound
  -X- IStaticSolver
  -X- CandidateSet
  -X- StationarySolver
  -X- existing geometric BranchAndBoundSolver
  -X- MinSumSolver
```

The Weighted Oracle also has no edge to any of those classes.

## Textual sequence diagram

```text
CLI/Benchmark
  -> ModelAExecutor: solve(Instance, Config, SolverBudget)
  -> InitialPartitionBuilder: P0 from trajectory breakpoints
  -> PartitionLoop:
       select live leaf I
       -> GeometryBounds: build D(t), D-(I), D+(I)
       <- validated matrices
       -> IntervalEvaluator: evaluate I
          -> WeightedProblem: construct g(D-(I))/g(D+(I))
          -> Oracle.Heuristic: complete assignment candidate
          <- UB candidate
          -> Oracle.Relaxation: dual/LP result
          -> CertificateValidator: independently validate LB
          <- validated LB or invalid
          -> Oracle.Exact where required
          <- exact assignment/bounds or timeout
       <- leaf LB/UB/status/provenance
       -> Partition: replace leaf or split/rollback
  -> when finite complete incumbent exists:
       ContinuousVerifier: verify entire program
       <- passed/failed
  -> aggregate live leaf LB/UB
  <- ModelAResultV1
  -> Legacy+versioned output adapters
```

No certificate is issued by Oracle.Heuristic.
No LP primal objective is accepted as LB without validation.
No finite UB is accepted unless the complete program is verified.

## API contracts

### Weighted static API

Inputs:

- immutable schema v1 W matrix;
- Oracle mode;
- time limit;
- gap target;
- matrix snapshot checksum;
- arithmetic/certificate configuration.

Outputs:

- exact result, heuristic result, or relaxation result;
- assignment where applicable;
- valid bounds only;
- provenance;
- status;
- solve timing.

Ownership:

- no Instance/time/geometry ownership;
- matrix remains input-owned or is copied under the problem object.

Errors:

- invalid matrix;
- infeasible;
- timeout;
- backend error;
- certificate failure.

### Interval geometry API

Inputs:

- Instance;
- [a,b];
- arithmetic configuration;
- budget.

Outputs:

- D-, D+;
- no fake Instance/time object;
- endpoint/breakpoint evidence;
- provenance.

Errors:

- invalid trajectory;
- unbounded root/equation;
- invalid outward rounding;
- timeout.

### Temporal B&B API

Inputs:

- Instance;
- initial partition;
- Weighted Oracle implementation;
- interval evaluator;
- arithmetic config;
- budgets and caps.

Outputs:

- LB and UB;
- complete UB program or None;
- status;
- stop reason;
- history revision;
- provenance.

Errors:

- invalid partition;
- missing certificate;
- verifier failure;
- inconsistent aggregate;
- cancellation.

### Continuous verifier API

Inputs:

- Instance;
- complete candidate program;
- tolerance policy;
- budget.

Outputs:

- continuous coverage/support/cost/integral status;
- no global optimality claim.

## Staged implementation roadmap

### Stage 1 - Weighted matrix foundation

Create the W-only problem and input validation.

Files:

- `include/kdc/model_a/weighted_static.hpp`
- `src/model_a/weighted_static.cpp`

Tests:

- negative/nonfinite matrix rejection;
- strict-LP-gap known example;
- known assignment optimum;
- no geometry/Instance dependency.

Acceptance:

- Weighted problem compiles and solves with no `Instance` or time.

### Stage 2 - Oracle interfaces and certificate validation

Create Exact/Heuristic/Relaxation interfaces and separate result types.

Files:

- `include/kdc/model_a/weighted_oracle.hpp`
- `include/kdc/model_a/certificate.hpp`

Tests:

- heuristic never returns LB;
- primal LP objective not accepted as LB;
- invalid dual rejected;
- exact result requires exact proof.

Acceptance:

- each Oracle result has enforced validity semantics.

### Stage 3 - Weighted backends

Implement Brute-Force Oracle first. Demonstrate Branch-and-Bound reuse only
through a separate compatibility adapter.

Tests:

- exhaustive parity;
- timeout/cap behavior;
- warm start cannot certify;
- cache key invalidation.

Acceptance:

- exact oracle is exact only after full proof.

### Stage 4 - Interval geometry bounds

Create D(t)/D-/D+ separation and componentwise bound checks.

Tests:

- all t in I satisfy lower/upper;
- tangent/flat/monotone cases;
- breakpoint endpoints;
- outward rounding.

Acceptance:

- no artificial Instance/time object is created for D-/D+.

### Stage 5 - Arithmetic/certificate binding

Add arithmetic modes and certificate provenance.

Tests:

- double, interval, exact modes remain distinct;
- invalid certificate downgrades leaf;
- historical revalidation.

Acceptance:

- no CERTIFIED or NUMERICALLY_BOUNDED result without preconditions.

### Stage 6 - Interval evaluator

Create interval-only LB/UB engine.

Tests:

- complete UB program;
- oracle failure sets 0/+infinity;
- D-/D+ weighted subproblem calls;
- integral leaf aggregation.

Acceptance:

- no finite UB from partial interval coverage.

### Stage 7 - Temporal B&B

Implement P/Q partition, split, rollback, history, and stopping.

Tests:

- P covers [0,T];
- open leaves disjoint;
- split union exact;
- rollback clears stale bounds;
- timeout and caps do not claim optimality.

Acceptance:

- temporal B&B is independent of `IStaticSolver`.

### Stage 8 - Independent continuous verifier

Implement full continuous verification.

Tests:

- violations between samples;
- trajectory breakpoints;
- tangent roots;
- integral inconsistency;
- complete program.

Acceptance:

- continuous verification is separate from global LB/optimality.

### Stage 9 - Versioned integration

Add an explicit Model A mode to CLI/benchmark/batch/schemas.

Tests:

- legacy commands preserve current output;
- new schema validates;
- old fields are not silently remapped;
- provenance/status round trips.

Acceptance:

- existing MinSum and benchmark behavior remain compatible.

### Stage 10 - Differential and scale validation

Run differential tests against the existing MinSum adaptive path and known
small analytical cases.

Tests:

- same objective no worse than or equal to adaptive UB where analytically
  applicable;
- exact small cases;
- fair benchmark budgets;
- manifest/checksum reproducibility.

Acceptance:

- no result is promoted beyond the certificate preconditions.

## Compatibility and migration rules

1. Existing `MinSumSolver` remains the adaptive method.
2. Existing static solvers remain static.
3. Existing JSON fields remain accepted and unchanged in meaning.
4. New Model A fields use `schema_version=1`.
5. Missing legacy provenance is loaded as NONE/unknown, not inferred.
6. Legacy `OPTIMAL` is not upgraded to thesis CERTIFIED automatically.
7. C++ implementation and output code may diverge only behind an explicit
   execution mode.
8. No source file or schema is changed in this stage.
