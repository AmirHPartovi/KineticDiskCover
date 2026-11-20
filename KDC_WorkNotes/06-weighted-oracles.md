# KDC Stage 06 - Weighted Oracle Contracts

Date: 2026-11-19
Repository: `/Users/amirpartovi/Desktop/Codes/KineticDiskCover`

## Scope

Added independent Oracle contracts on top of the weighted static core:

- Exact contract
- Heuristic contract
- Relaxation contract
- shared provenance, arithmetic, validity, termination, budget, and adapter
  definitions
- unavailable factories
- ILP exact-result adapter
- heuristic assignment adapter
- relaxation-certificate adapter
- contract tests

No temporal solver or `BenchmarkRunner` code was changed.

## PDF/call contract

The Oracle roles follow Chapter 5:

- Exact: exact g(W) and an optimal assignment, with an independently valid
  exact proof requirement.
- Heuristic: a feasible assignment and recomputed cost; never an LB.
- Relaxation: an LB only with a valid certificate; LP primal objective alone
  is not a certified LB.

## Files added

- `include/kdc/weighted_oracle.hpp`
- `src/weighted_oracle.cpp`
- `tests/test_weighted_oracle.cpp`

Existing weighted core files remain:

- `include/kdc/weighted_static.hpp`
- `src/weighted_static.cpp`
- `tests/test_weighted_static.cpp`

## API separation

`WeightedExactOracleResultV1`

- optional assignment/incumbent
- incumbent feasibility
- recomputed feasible primal cost
- backend-solver reported optimal flag
- whether a backend-level optimality claim is possible
- whether independent optimality is certified
- solver best bound
- solver best-bound validity
- independently validated LB
- independent LB validity
- termination status
- arithmetic mode
- provenance

`WeightedHeuristicOracleResultV1`

- optional assignment/incumbent
- incumbent feasibility
- recomputed feasible primal cost
- termination
- arithmetic
- provenance

No lower bound or optimality certificate field exists in the heuristic result.

`WeightedRelaxationOracleResultV1`

- independently validated LB
- LB validity
- relaxation certificate
- termination
- arithmetic
- provenance

No assignment or primal objective field exists in the relaxation result.

## Existing ILPResult deficiency

`include/kdc/solver_interface.hpp:10-48` provides:

- primal objective
- solver-reported lower_bound
- status
- gap
- solve timing
- backend/version/fallback/licensing metadata

It does not provide:

- dual variable/certificate data;
- certificate feasibility check;
- independent certificate checksum/provenance;
- independent lower-bound validity.

Therefore the ILP exact-result adapter:

- exposes `lower_bound` as `REPORTED_UNVALIDATED`;
- does not move it to independently validated LB;
- sets `optimality_certified=false`;
- may set `optimality_claim_possible=true` only when the backend reports
  optimality, the incumbent is feasible, and primal/best-bound values agree;
- does not infer exactness from `solver_reports_optimal`.

## Certificate adapter semantics

`adapt_weighted_relaxation_certificate` accepts an LB only if:

- a certificate is present;
- the certificate is independently_validated;
- the certificate method is nonempty;
- the LB is finite and nonnegative.

An absent certificate or LP-primal-only result yields:

- no independent LB;
- `lower_bound_validity=INVALID`.

## Budget behavior

All Oracle interfaces accept existing `SolverBudget*`.

An expired or cancelled budget throws `SolverBudgetExpired` through
`SolverBudget::checkpoint`, preserving existing repository semantics.

The unavailable factories:

- return `NOT_IMPLEMENTED`;
- perform budget checkpoint first;
- do not claim assignment, cost, LB, or optimality.

## Factory behavior

`WeightedOracleFactory` provides:

- `unavailable_exact`;
- `unavailable_heuristic`;
- `unavailable_relaxation`.

These are contract placeholders. Backend implementation is deliberately not
presented as complete in this stage.

## Test coverage

`tests/test_weighted_oracle.cpp` covers:

- exact status versus independent optimality certification;
- recomputed feasible primal cost;
- reported bound validity versus independent LB validity;
- invalid exact assignment;
- heuristic no-LB semantics;
- absent/unverified relaxation certificates;
- valid independently checked relaxation certificate;
- invalid infinite relaxation LB;
- unavailable factory behavior;
- expired/cancelled budget behavior.

## Build and test

Commands:

```bash
bash scripts/local_validation.sh configure-build
bash scripts/local_validation.sh ctest
```

Result:

- configure/build passed;
- CTest 1/1 passed;
- full existing test suite passed.

## Compatibility

No existing result schema or field semantics were changed.

No temporal code, static solvers, `IStaticSolver`, `MinSumSolver`,
`BenchmarkRunner`, batch runner, or CLI behavior was modified.
