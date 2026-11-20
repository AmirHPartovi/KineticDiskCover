# KDC Stage 07 - Exact Weighted Oracle

Date: 2026-11-19
Repository: `/Users/amirpartovi/Desktop/Codes/KineticDiskCover`

## Implemented files

- `include/kdc/weighted_oracle.hpp`
- `src/weighted_oracle.cpp`
- `tests/test_weighted_oracle.cpp`

Existing weighted core files remain unchanged:

- `include/kdc/weighted_static.hpp`
- `src/weighted_static.cpp`

No temporal MinSum, benchmark, batch, CLI, or static candidate-disk code was
changed.

## Chapter 5 model

The implementation constructs:

\[
\min \pi\sum_i q_i
\]

subject to:

\[
\sum_i x_{ij}=1,\qquad
q_i\ge w_{ij}x_{ij},\qquad
x_{ij}\in\{0,1\},\qquad q_i\ge0.
\]

There is no invented large-M coefficient.

## Independent model construction

`ILPWeightedExactOracle` in `src/weighted_oracle.cpp` constructs a fresh
MILP from `WeightMatrix` only.

Variables:

- `x[i,j]` for every station/point pair;
- `q[i]` for every station.

Ordering:

- x variables are station-major: `i*n+j`;
- q variables follow the x block at `m*n+i`.

Constraints:

- lower point-cover row: `sum_i x[i,j] >= 1`;
- upper exact-cover row: `sum_i -x[i,j] >= -1`;
- direct link: `q[i]-w[i,j]x[i,j] >= 0`.

This pair of coverage constraints enforces exactly one assignment for each
point with binary x variables.

## Existing ILP adapter behavior

The existing native ILP interface adds all matrix rows as
\(Ax \ge rhs\).

The existing backend adapter also represents every variable with lower bound
0 and upper bound 1. To support arbitrary nonnegative weights without a
large-M coefficient, the weighted oracle normalizes weights by
\(\max w_{ij}\):

\[
q'_i \ge (w_{ij}/\max w)x_{ij},
\]

and uses objective cost
\(\pi\max w\sum_iq'_i\).

This preserves the original objective because
\(q_i=\max w\,q'_i\).

All-zero matrices bypass the backend and return a direct assignment:

- every point assigned to station 0;
- recomputed cost 0;
- no solver incumbent/best bound is fabricated.

## Boundary behavior

- m=0,n>0: INFEASIBLE, no assignment, no primal cost.
- m>0,n=0: OPTIMAL direct result, feasible empty assignment, cost 0.
- m=0,n=0: OPTIMAL direct result through the empty-point path.
- all weights zero: OPTIMAL direct result, lexicographic station 0 assignment.

## Assignment extraction

For a backend result with `x` data, extraction:

1. validates the solution vector size;
2. validates each x value as 0 or 1 within 1e-7;
3. requires exactly one station per point;
4. sets the assignment owner;
5. recomputes cost with `WeightedStaticObjective`;
6. rejects inconsistent/missing assignments.

The model objective is not used as the incumbent cost.

## Status separation

The exact Oracle result keeps separate:

- `solver_reports_optimal`;
- `optimality_claim_possible`;
- `optimality_certified`;
- backend primal objective;
- recomputed feasible primal cost;
- solver best bound;
- solver best-bound validity;
- independently validated lower bound;
- solver gap;
- termination status;
- arithmetic mode;
- provenance;
- solver message.

Numeric backend `OPTIMAL` only sets `solver_reports_optimal`.
It does not set `optimality_certified`.

Termination values include:

- OPTIMAL;
- FEASIBLE;
- TIME_LIMIT;
- INTERRUPTED;
- INFEASIBLE;
- UNBOUNDED;
- NUMERIC_FAILURE;
- ERROR;
- NOT_IMPLEMENTED.

## Backend lower-bound status

Finite nonnegative `ILPResult.lower_bound` is stored as:

- `solver_best_bound`;
- `REPORTED_UNVALIDATED`.

It is not moved to:

- independently_validated_lower_bound;
- CERTIFIED status.

Conservative rounding or exact/interval arithmetic is not performed in this
stage.

## Tests

`tests/test_weighted_oracle.cpp` includes a configured MILP mock and tests:

- m=1,n=1 brute-force agreement;
- m=2,n=2 brute-force agreement;
- all-zero weights;
- empty points;
- m=0,n>0 infeasible;
- time limit with incumbent;
- time limit without incumbent;
- interrupted status;
- infeasible status;
- numeric failure;
- generic backend error;
- direct assignment-cost/model-objective agreement;
- solver gap and backend objective separation.

The configured mock validates the model variable layout and returns controlled
ILP status/incumbent data. It is test-only.

## Build and test

Commands:

```bash
bash scripts/local_validation.sh configure-build
bash scripts/local_validation.sh ctest
```

Result:

- configure/build passed;
- CTest 1/1 passed;
- full suite passed.

The local KONT runtime is license-unavailable, so the native KONT runtime was
not used as proof in this stage. Contract behavior was tested with the
configured ILP mock.
