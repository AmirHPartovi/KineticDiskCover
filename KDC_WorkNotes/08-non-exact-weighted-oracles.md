# KDC Stage 08 - Non-Exact Weighted Oracles

Date: 2026-11-19
Repository: `/Users/amirpartovi/Desktop/Codes/KineticDiskCover`

## Implemented files

- `include/kdc/weighted_oracle.hpp`
- `src/weighted_oracle.cpp`
- `tests/test_weighted_oracle.cpp`

No temporal MinSum or benchmark code was changed.

## Part A: deterministic heuristic

Implemented:

- `GreedyWeightedHeuristicOracle`
- factory: `WeightedOracleFactory::greedy_heuristic()`

Deterministic rule:

1. For every point j, select the station i with minimum w_ij.
2. Resolve ties by lowest station index.
3. Return one complete assignment.
4. Recompute cost only with `WeightedStaticObjective`.

The result is UB-only:

- no lower bound field is generated;
- no relaxation or dual value is inferred;
- a heuristic cost never becomes LB.

Boundary behavior:

- m=0,n>0: infeasible;
- n=0: feasible zero-cost empty assignment;
- all weights zero: station 0 for every point, cost 0.

## Part B: LP relaxation

Implemented:

- `WeightedLPRelaxationOracle`
- factory: `WeightedLPRelaxationOracle` through
  `WeightedOracleFactory::lp_relaxation(...)`

The LP model is the binary MILP with x binaries relaxed to continuous:

- exact cover:
  \(\sum_i x_{ij}=1\);
- link:
  \(q_i-w_{ij}x_{ij}\ge0\);
- bounds:
  \(x_{ij}\in[0,1]\), \(q_i\ge0\).

Weight normalization preserves the objective:

- \(\max w\) is used to keep normalized q variables inside the existing
  ILP backend's [0,1] variable bounds;
- there is no large-M coefficient;
- objective cost remains
  \(\pi \max w\sum_i q'_i\).

The backend LP result is retained only as:

- diagnostic primal objective;
- termination;
- backend provenance/time.

It is never copied into:

- independent_validated_lower_bound;
- lower_bound_validity=VALID.

If no independently valid dual certificate exists, the official relaxation
LB is invalid/absent.

## Exact LP dual derived

For minimization:

\[
\max\sum_j y_j
\]

subject to:

\[
y_j-w_{ij}z_{ij}\le0\quad\forall i,j,
\]

\[
\sum_j z_{ij}\le\pi\quad\forall i,
\]

\[
z_{ij}\ge0,
\]

and y_j is free.

The implementation validates:

- y and z vector dimensions;
- y finite;
- z finite and nonnegative;
- every x residual
  \(y_j-w_{ij}z_{ij}\);
- every station q residual
  \(\sum_j z_{ij}-\pi\);
- directed lower sum of y.

The residual check uses long-double accumulation and a downward-rounded
conversion for the lower dual objective. Zero lower objectives remain exactly
zero rather than becoming negative.

The LP result alone is never promoted to a certificate.

## Certificate behavior

`validate_weighted_lp_dual` returns:

- valid=true only when every dual constraint passes;
- certificate.present=true;
- certificate.independently_validated=true;
- directed lower bound;
- conservative residual;
- arithmetic mode CONSERVATIVE_INTERVAL.

If dimensions or signs are invalid, or residuals fail, it returns:

- valid=false;
- no directed LB;
- no valid certificate.

If the backend provides only a primal objective or no dual vectors, the
relaxation result keeps:

- diagnostic_primal_objective optional;
- lower_bound_validity=INVALID;
- independently_validated_lower_bound absent.

## Tests

Added or extended `tests/test_weighted_oracle.cpp` for:

- greedy assignment feasibility;
- heuristic cost >= brute-force optimum;
- equal/zero-weight heuristics;
- valid small dual examples;
- negative z and wrong-size z rejection;
- zero-weight dual degeneracy;
- relaxation diagnostic primal objective;
- absent official LB when no dual is available;
- invalid/unverified relaxation certificates.

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

## Scope

No temporal MinSum or BenchmarkRunner code was modified.
