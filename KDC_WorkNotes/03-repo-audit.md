# KDC Stage 03 - Local Repository Architecture Audit

Date: 2026-11-19
Scope: read-only audit of the working tree
Repository: `/Users/amirpartovi/Desktop/Codes/KineticDiskCover`

## Git baseline

- Branch: `main`
- HEAD: `9d0bb0a415b8e3bb5a58bb1621b10db7768b9273`
- Status before audit: clean
- Unstaged diff: none
- Staged diff: none
- Untracked files: none
- No project file was modified during this audit.

## Architecture map

### Entry points

1. `src/main.cpp:430-552` creates a `KontSolver`, creates the selected
   `IStaticSolver` through `StaticSolverRegistry`, and dispatches objective:
   - `MIN_MAX` -> `MinMaxSolver::solve`
   - `MIN_MAX_SUM` -> `MinMaxSumSolver::solve`
   - otherwise -> `MinSumSolver::solve`
2. `src/benchmark.cpp:524-821` resolves an exact backend, creates an
   `IStaticSolver`, creates a global `SolverBudget`, and dispatches objective.
   For `MIN_SUM` it calls `MinSumSolver::solve(instance, *static_solver, ...)`
   at `src/benchmark.cpp:746-750`.
3. `src/batch_runner.cpp:1080-1152` creates the registered static solver and
   dispatches `MIN_SUM` to `MinSumSolver::solve` at lines 1143-1152.
4. `src/algorithm_comparison.cpp:430-540` also creates registered static
   solvers and dispatches `MIN_SUM` to `MinSumSolver::solve` at lines 527-529.
5. `src/comparative_runner.cpp:154-157` separately runs MinMax and MinSum.
6. `src/main.cpp:835-839` routes `benchmark` to `BenchmarkRunner::run_all`.

### Static solver layer

`IStaticSolver` is the static-at-a-single-time contract.

- API: `include/kdc/istatic_solver.hpp:12-28`
  - `solve(instance,time)`
  - `solve_with_budget(instance,time,budget,static_time_limit)`
  - capability/status methods
- `StaticSolution` fields: supporting points, radius, cost, feasibility,
  lower/upper bounds, certified gap, optimality, exactness, time, solver name,
  and assigned points.
- Responsibility: solve only one static geometry snapshot.
- Caller: `KineticSolution::extend` and the MinSum/MinMax refinement loops,
  through `IStaticSolver::solve_with_budget`.

The static solver registry:

- API: `include/kdc/static_solver_registry.hpp:12-19`
- Implementation: `src/static_solver_registry.cpp:38-122`
- It stores factories keyed by name and exposes `create` and `list`.
- Built-ins: `nn`, `ip-kont`, `brute-force`, `branch-and-bound`, `greedy`,
  `lp-rounding`, `primal-dual`, `local-search`, `sa`, `genetic`, `shifting`.
- Registering or selecting a static solver name does **not** change the
  temporal interval algorithm. It changes only the static assignment/radius
  returned at each midpoint or initial time.

### Geometry and stationary assignment

`CandidateSet`:

- API: `include/kdc/candidate.hpp:12-65`
- Candidate disks are `(station_id,supporting_point)`.
- Instance precompute stores stations, trajectories, and affine trajectory
  segments: `include/kdc/candidate.hpp:16-40`.
- `src/candidate.cpp:94-200` precomputes segment metadata and candidate disks.
- `StaticGeometry` stores actual point positions and
  `station_point_distance_squared`.
- `CandidateSet::build_coverage` creates point coverage of candidate disks.

`StationarySolver`:

- API: `include/kdc/stationary.hpp:12-27`
- `solve_nn` and `solve_ip` operate at an actual supplied `time`.
- `assign_points_to_disks` normalizes a feasible ownership partition:
  `src/stationary.cpp:11-137`.
- `solve_ip` builds Eigen cost/constraint matrices at actual `D(t)` geometry:
  `src/stationary.cpp:269-360`.
- It does not accept D-(I), D+(I), or arbitrary W as a separate public API.

`ILPSolver`/`ILPResult`:

- API: `include/kdc/solver_interface.hpp:10-48`
- Input: cost vector c, sparse coverage matrix A, rhs b, integer variable
  indexes, time limit, and gap target.
- Output: status, assignment x, objective, lower_bound, gap, timing, backend,
  and license/provenance flags.
- There is no thesis Oracle, dual-certificate, or D-/D+ type in this interface.

### Exact static solvers

`IPStaticSolver`:

- API: `include/kdc/algorithms/ip_static_solver.hpp:7-28`
- Implementation: `src/algorithms/ip_static_solver.cpp:10-86`
- Wraps `StationarySolver::solve_ip`.
- Maps `ILPResult::Status::OPTIMAL/FEASIBLE/TIME_LIMIT/INFEASIBLE/ERROR`
  into static result statuses.
- It is an exact static solver only when the backend supplies a valid exact
  result/bound; native runtime failure can be a fallback.

`BranchAndBoundSolver`:

- API: `include/kdc/algorithms/branch_and_bound_solver.hpp:8-58`
- Implementation: `src/algorithms/branch_and_bound_solver.cpp:1-220`
  and subsequent branch search implementation.
- Operates on actual distances at `time`, current radii, and remaining points.
- Uses geometric/frontier lower bounds and optional LP strengthening.
- Node limit/time limit yield a feasible incumbent, but do not prove
  global kinetic optimality.

`BruteForceSolver`:

- Implementation: `src/algorithms/brute_force_solver.cpp:1-180`
- Enumerates m^n assignments at one time.
- `OPTIMAL` is issued only after full enumeration within budget.
- Assignment-count cap or time limit prevents exact completion.

`KontSolver`:

- API: `include/kdc/kont_solver.hpp:11-55`
- Native/fallback behavior: `src/kont_solver.cpp:900-980`
- It can delegate to COPT/KONT or built-in fallback.
- Backend status and fallback provenance are separate fields.

### Temporal kinetic layer

`KineticSolution`:

- API: `include/kdc/solution.hpp:13-54`
- Stores piecewise quadratic `SolutionInterval` records:
  start/end, support, assigned owners, and a/b/c coefficients.
- `KineticSolution::extend` builds exact-event/hand-over partitions.
- `KineticSolution::combine` adds the common refinement and lower-envelope
  choice: `src/solution.cpp:1030-1160`.
- For `MIN_SUM`, `combine` retains the candidate with lower interval objective
  on open subintervals; it does not implement D-, D+, P/Q, rollback, or
  weighted Oracle calls.

`MinSumSolver`:

- API: `include/kdc/minsum.hpp:12-108`
- Main temporal solver: `src/minsum.cpp:501-884`
- Flow:
  1. static solve at t=0
  2. extend kinetic solution over [0,T]
  3. optionally sample static pointwise bounds
  4. choose the incumbent interval with largest integral or heuristic LB
  5. solve static midpoint
  6. extend forward/backward
  7. combine and locally improve
  8. sample-continuous prefilter and final continuous verification
- It is heuristic/adaptive for `HEURISTIC_ADAPTIVE`.
- `CERTIFIED_BOUND` uses sampled/trapezoidal estimates but does not certify
  them by itself.
- It always sets a separate nonnegative integral LB of zero for the final
  MinSum result: `src/minsum.cpp:580-607,812-851`.

### Bounds and status semantics

`BoundStatus` and `OptimalityStatus`:

- API: `include/kdc/result_status.hpp:5-67`
- `BoundStatus`: `NONE`, `HEURISTIC`, `CERTIFIED`.
- `OptimalityStatus`: `OPTIMAL`, `FEASIBLE`, `TIME_LIMIT`, `INFEASIBLE`,
  `FAILED`.
- `VerificationKind`: `NONE`, `EMPIRICAL`, `CERTIFIED_CONTINUOUS`.

Static status helper:

- `set_static_result_status`: `include/kdc/istatic_solver.hpp:32-91`
- It downgrades CERTIFIED if the LB is not finite/nonnegative.
- It sets `upper_bound=cost` only for feasible solutions.
- It constructs a static certified gap only for exact+CERTIFIED.
- Exact `OPTIMAL` can be downgraded to `FEASIBLE` if the certified gap is not
  within tolerance.
- It does not carry method, arithmetic mode, provenance, or leaf partition
  validity beyond the existing fields.

Gap semantics:

- Static `certified_gap` is `(UB-LB)/max(1,|LB|)`:
  `include/kdc/istatic_solver.hpp:74-91`.
- General `gap` in kinetic solvers is solver-specific relative gap.
- MinSum always resets `certified_gap`, even when using exact static solvers:
  `src/minsum.cpp:812-851`.
- MinSum `gap` is relative to `lower_bound_integral`, while that integral may
  be a heuristic/trapezoidal estimate or zero:
  `src/minsum.cpp:326-375,622-681,812-851`.
- The implementation currently does not merge thesis statuses
  NUMERICALLY_BOUNDED, SAMPLED, or INCOMPLETE.

### Verifier and serialization

`Verifier`:

- API: `include/kdc/verify.hpp:12-61`
- It has finite-sample and continuous paths.
- Continuous verification is implemented:
  `src/verify.cpp:1032-1110`.
- It checks interval dimensions, coverage, support/assignment consistency,
  cost coefficients, and analytic vs direct integral.
- Finite-sample `verify_empirical` remains empirical:
  `src/verify.cpp:927-1029`.
- VerificationKind is not an objective optimality proof.

`SolutionSerializer`:

- API/implementation: `include/kdc/solution_serializer.hpp:7-20`,
  `src/solution_serializer.cpp:1-220`.
- Saves/loads interval support, assigned owners, quadratic coefficients,
  objective, peak, and integral summary.
- It validates continuity, dimensions, support ranges, owner ranges, and
  summary consistency.
- It does not serialize leaf LB/UB provenance, arithmetic mode, D-/D+,
  Oracle calls, P/Q, rollback history, or stopping reason.

`TraceWriter`:

- API: `include/kdc/trace.hpp:5-36`.
- Iterations include objective, LB, gap, wall time, static solver status,
  static exactness, candidate acceptance, and stop reason.
- It is not a complete thesis leaf/partition history.

## Textual call graph

```text
CLI/main or BatchRunner/BenchmarkRunner/AlgorithmComparator
  -> StaticSolverRegistry::create(name, ILPSolver*)
  -> IStaticSolver::solve_with_budget(instance, time, budget, limit)
       -> StationarySolver or native static algorithm
       -> StaticSolution

BenchmarkRunner::run_single/MIN_SUM
  -> StaticSolverRegistry::create(selected_backend,...)
  -> MinSumSolver::solve(instance, IStaticSolver, Config, SolverBudget)
       -> IStaticSolver::solve_with_budget(t=0)
       -> KineticSolution::extend(initial assignment, [0,T])
       -> [optional] IStaticSolver::solve_with_budget(midpoint)
       -> KineticSolution::extend(forward/backward)
       -> KineticSolution::combine
       -> Verifier::verify_continuous
       -> MinSumSolver::Result
  -> BenchmarkResult/JSON/CSV

BatchRunner::run_single/MIN_SUM
  -> StaticSolverRegistry::create(algorithm_name, ILPSolver*)
  -> MinSumSolver::solve(...)
  -> BatchRunRecord/solution/trace

AlgorithmComparator/MIN_SUM
  -> StaticSolverRegistry::create(...)
  -> MinSumSolver::solve(...)
  -> KineticComparisonResult

MIN_SUM registration fact
  -> static name only selects IStaticSolver
  -> temporal interval algorithm is selected solely by ObjectiveType::MIN_SUM
  -> registration of a static name alone does not invoke MinSumSolver
```

## Static/temporal responsibility matrix

| Component | Responsibility | Actual geometry/input | Reusable | Missing for thesis |
|---|---|---|---|---|
| `IStaticSolver` | Static assignment at one time | D(t) geometry | Budget, static result, assigned ownership | Weighted W API, Oracle separation, provenance/arithmetic fields |
| `StaticSolverRegistry` | Named static solver factories | Factory only | Solver selection and built-ins | Temporal solver registration, W/Oracle factories |
| `CandidateSet` | Candidate disks and D(t) coverage | Actual `D(t)` and trajectory positions | Coverage matrix, segment precompute | D-(I)/D+(I) matrices and arbitrary W |
| `StationarySolver` | Static ownership/IP construction | Actual time | Coverage and ownership normalization | Direct weighted subproblem solver |
| `ILPSolver/ILPResult` | MILP/LP solve interface | c,A,b | Native/fallback status | Dual certificate/verified LB/certificate provenance |
| `IPStaticSolver` | Native KONT static solve | Actual D(t) | Exact backend bridge | Thesis Oracle.Exact certificate distinction |
| `BranchAndBoundSolver` | Static exact/search | Actual D(t) | Incumbent/frontier LP lower bound | D-/D+ weighted subproblems and thesis leaf bounds |
| `BruteForceSolver` | Static exact enumeration | Actual D(t) | Complete small-instance reference | Not an interval engine |
| `KontSolver` | KONT/COPT/fallback adapter | MIP inputs | Backend diagnostics/fallback | Independent dual/certificate checking |
| `KineticSolution` | Temporal interval representation | Actual D(t), trajectory segments | Quadratic coefficients, event extension, combination | P/Q partition, D-/D+, Phi, leaf history |
| `MinSumSolver` | Temporal incumbent refinement | Static D(t) calls around a kinetic solution | Continuous verification and integral recombination | Thesis branch-and-bound leaf engine, exact LB/UB certificates |
| `Verifier` | Continuous/empirical feasibility | Actual geometry | Continuous polynomial/root checks | Proof of global MinSum optimality |
| `SolutionSerializer` | Kinetic solution JSON | Actual solution | Round-trip interval/owner schema | Leaf bounds, provenance, arithmetic, stopping reason |

## Existing tests and coverage

- `tests/test_minsum.cpp:55-231`
  - adaptive/sampled policies
  - stagnation/tolerance/time budget
  - exact-static solver does not certify kinetic optimum
  - continuous solution and moving instance
  - handover ownership
- `tests/test_branch_and_bound.cpp:15-117`
  - parity, lower-bound validity, optional LP bound, node/time limits,
    coverage, registry metadata.
- `tests/test_brute_force.cpp:16-131`
  - parity, cap/time limit, exact cost, coverage, radius/cost.
- `tests/test_stationary.cpp:64-170`
  - nearest-neighbor and IP small models.
- `tests/test_candidate.cpp` and `test_kinetic.cpp`
  - candidate geometry, event model, event emission, handovers.
- `tests/test_verify.cpp:43-119`
  - empirical and continuous verification and breakpoint handling.
- `tests/test_result_status.cpp:98-254`
  - enum round trips, static provenance, infeasible/timeouts, heuristic
    kinetic status, exact child result not optimality.
- `tests/test_solution_serializer.cpp:27-164`
  - MinMax/MinSum round trips, ownership, malformed/mismatched/invalid JSON.
- `tests/test_benchmark.cpp:32-177`
  - benchmark persistence and CLI workflow.
- `tests/test_batch_runner.cpp:47-262`
  - one/multiple instance and objective execution, overwrite protection,
    parallel consistency, missing directory/error handling.

Coverage is strong for static solvers, current kinetic event engine,
continuous feasibility, and result serialization. It does not cover a general
weighted W subproblem, independent Oracle semantics, D-/D+, cone/Phi/Lipschitz
bounds, thesis P/Q leaf management, rollback/history, or exact overall
MinSum interval certification.

## Existing/incomplete/missing capabilities

| Capability | Status | Evidence |
|---|---|---|
| Fixed-center, moving-point, single-coverage Model A data | Existing | `include/kdc/types.hpp`, `src/candidate.cpp:94-200` |
| Static D(t) weighted set-cover formulation | Existing | `src/stationary.cpp:269-360` |
| Kinetic interval quadratic representation | Existing | `src/solution.cpp:164-231,1030-1160` |
| Static exact backends IP/B&B/Brute Force | Existing but static-only | headers and implementations cited above |
| Continuous feasible program verifier | Existing | `src/verify.cpp:1032-1110` |
| Finite UB from complete kinetic program | Existing for incumbent paths | `src/minsum.cpp:501-884` |
| Static bound/status enum | Existing but incomplete | `include/kdc/result_status.hpp`, `include/kdc/istatic_solver.hpp` |
| MinSum adaptive incumbent refinement | Existing heuristic | `src/minsum.cpp:501-884` |
| Independent LB=0 for nonnegative integral cost | Existing | `src/minsum.cpp:580-607,812-851` |
| General weighted matrix W API | Missing | no W/Oracle abstraction found |
| D(t), D-(I), D+(I) separated inputs | Missing | D-/D+ not present |
| Oracle.Exact/Heuristic/Relaxation API | Missing | no thesis Oracle class/interface found |
| Independently checked dual/LB certificate | Incomplete | LP can report lower_bound, but no independent certificate object |
| Lipschitz and cone/Phi interval bounds | Missing | no current implementation/API found |
| d-/d+ endpoint bounds | Missing | no current implementation/API found |
| Upper-envelope integral branch-and-bound | Missing | only MinSum incumbent combination |
| P live partition, priority, Q queue | Missing | no thesis partition/P/Q structures |
| Splitting/rollback/aggregation/history | Missing/partial | interval splitting exists for combination, not thesis leaf engine |
| Full thesis stopping/status schema | Missing | status enums lack INCOMPLETE/SAMPLED/NUMERICALLY_BOUNDED thesis fields |
| Leaf arithmetic/provenance model | Missing | SolutionInterval has no LB/UB/method/arithmetic/provenance |
| Global exact MinSum optimality proof | Missing | tests explicitly assert exact static solver does not certify kinetic optimum |

## MIN_SUM benchmark answer

`BenchmarkRunner::run_single` first resolves a static backend and creates an
`IStaticSolver` by `StaticSolverRegistry::create`:
`src/benchmark.cpp:524-562`.

It then dispatches `ObjectiveType::MIN_SUM` to:
`src/benchmark.cpp:738-750`

```cpp
MinSumSolver::solve(instance, *static_solver, solver_config, budget)
```

Therefore:

- registering `branch-and-bound`, `brute-force`, or another static name only
  supplies the static solver used inside the refinement.
- it does not cause `BenchmarkRunner` to run a separate thesis temporal
  branch-and-bound partition engine.
- `ObjectiveType::MIN_SUM` is what invokes `MinSumSolver`.
- the temporal interval engine is always the existing
  `KineticSolution::extend/combine` plus adaptive/sampled MinSum refinement.

## Files likely to change in later stages

Likely new files may be needed for a separate thesis execution path; exact
names are deferred until an approved stage contract.

Potential existing files:

- `include/kdc/minsum.hpp`
- `src/minsum.cpp`
- `include/kdc/istatic_solver.hpp`
- `include/kdc/solver_interface.hpp`
- `include/kdc/stationary.hpp`
- `src/stationary.cpp`
- `include/kdc/candidate.hpp`
- `src/candidate.cpp`
- `include/kdc/algorithms/branch_and_bound_solver.hpp`
- `src/algorithms/branch_and_bound_solver.cpp`
- `include/kdc/algorithms/ip_static_solver.hpp`
- `src/algorithms/ip_static_solver.cpp`
- `include/kdc/kont_solver.hpp`
- `src/kont_solver.cpp`
- `include/kdc/result_status.hpp`
- `include/kdc/trace.hpp`
- `src/trace.cpp`
- `include/kdc/solution.hpp`
- `src/solution.cpp`
- `include/kdc/verify.hpp`
- `src/verify.cpp`
- `include/kdc/solution_serializer.hpp`
- `src/solution_serializer.cpp`
- `src/benchmark.cpp`
- `src/batch_runner.cpp`
- `src/algorithm_comparison.cpp`
- `src/main.cpp`
- `CMakeLists.txt`
- `tests/test_minsum.cpp`
- `tests/test_branch_and_bound.cpp`
- `tests/test_brute_force.cpp`
- `tests/test_stationary.cpp`
- `tests/test_verify.cpp`
- `tests/test_result_status.cpp`
- `tests/test_solution_serializer.cpp`
- `docs/schemas/result.schema.json`
- `docs/schemas/verification.schema.json`
- `docs/schemas/run_record.schema.json`
