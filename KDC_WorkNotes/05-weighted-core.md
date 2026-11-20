# KDC Stage 05 - Independent Weighted Static Core

Date: 2026-11-19
Repository: `/Users/amirpartovi/Desktop/Codes/KineticDiskCover`

## PDF evidence rechecked

- Chapter 5, printed page 115 / PDF index 131:
  - Equation (2.5):
    \(g(W)=\min_{\sigma}\pi\sum_i\max_{j\in S_i(\sigma)}w_{ij}\),
    \(\max\emptyset=0\).
- Chapter 5, printed page 115-116 / PDF index 131-132:
  - Assignment/MILP formulation Equation (4.5).
  - For every point j:
    \(\sum_i x_{ij}=1\).
  - \(q_i\ge w_{ij}x_{ij}\).
  - \(x_{ij}\in\{0,1\}\), \(q_i\ge0\).
- The weighted matrix W is independent of D(t), and D-(I)/D+(I) must not
  be converted into Instance/time objects.

## Git baseline

- Branch: `main`
- HEAD: `9d0bb0a415b8e3bb5a58bb1621b10db7768b9273`
- Initial status: clean.
- No pre-existing modified, staged, or untracked files.

## Files added

- `include/kdc/weighted_static.hpp`
- `src/weighted_static.cpp`
- `tests/test_weighted_static.cpp`

No existing source, temporal, benchmark, CMake, documentation, or schema file
was modified.

## API

### `WeightMatrix`

Responsibilities:

- independent row-major matrix with station count m and point count n;
- rows are stations; columns are points;
- stores nonnegative finite weights;
- validates dimensions and row-major data;
- no Instance, geometry, trajectory, time, or candidate-disk dependency.

Constructors and factories:

- `WeightMatrix(station_count, point_count)`
- `WeightMatrix(station_count, point_count, row_major_weights)`
- `WeightMatrix::from_rows(station_rows)`

The PDF contract defines ordered station/point indexes. This API uses:

- station index range `[0,m)`;
- point index range `[0,n)`;
- row-major index `station*n+point`.

### `WeightedStaticObjective`

- `validate_assignment(matrix, assignment)`
- `evaluate_assignment(matrix, assignment)`

`evaluate_assignment` recomputes the PDF objective exactly:

1. validate assignment size and station indices;
2. set each station maximum to zero;
3. find the maximum weight assigned to each station;
4. sum station maxima;
5. multiply by pi.

Empty station groups contribute zero.

## Boundary semantics

The PDF does not state m=0/n=0 explicitly. The implemented engineering
decision is:

- m=0,n=0: objective 0;
- m>0,n=0: objective 0;
- m=0,n>0: infeasible; no assignment exists.

This is distinct from an empty-point problem and is not inferred as
equivalent to the infeasible points/no-station case.

Empty stations with n>0 are permitted; their radius/maximum weight is zero.

## Objective evidence

The implementation formula in
`src/weighted_static.cpp:130-162` recomputes:

\[
\pi\sum_i \max_{j:\sigma(j)=i} w_{ij},
\]

with `station_max` initialized to zero. It therefore follows the PDF formula
and does not reduce to the existing `Instance` or candidate-disk geometry.

## Brute-force reference

The brute-force implementation is in
`tests/test_weighted_static.cpp:15-67` only.

It recursively enumerates every assignment in lexicographic station-index
order. It:

- does not appear in `src/`;
- is not registered as a static/FAST solver;
- is not in `BenchmarkRunner`;
- is testing-only.

Complexity:

- assignments: \(m^n\);
- cost evaluation: \(O(mn)\);
- total: \(O(mn\,m^n)\);
- extra assignment memory: \(O(n)\);
- recursion stack: \(O(n)\).

The brute-force code is restricted to tests and expected only for small sizes.

## Tie-breaking

Assignment tie-breaking is independent of the objective value:

- enumeration is lexicographic;
- point 0 is the first mixed-radix position;
- stations iterate ascending;
- the first minimum assignment is retained;
- objective value remains `pi * sum(station_max)`.

No tie-break changes the optimum.

## Test coverage

`tests/test_weighted_static.cpp` includes:

- m=1,n=1;
- m=0,n=0;
- m>0,n=0;
- m=0,n>0 infeasible;
- zero weights;
- repeated weight rows;
- multiple equal-cost optima;
- lexicographic deterministic tie;
- mismatched row sizes;
- negative, NaN, and infinite weights;
- wrong assignment length;
- negative/out-of-range station indexes;
- brute-force/reference direct objective comparisons.

## Build and test results

Build command:

```bash
bash scripts/local_validation.sh configure-build
bash scripts/local_validation.sh ctest
```

Result:

- configure/build passed;
- CTest: 1/1 test target passed;
- full suite: 238 test cases, 37338 assertions;
- weighted static tests passed.

## Compatibility

- Existing `MinSumSolver` was not modified.
- Existing `BranchAndBoundSolver` was not modified.
- `IStaticSolver` was not modified.
- `BenchmarkRunner`, `BatchRunner`, CLI temporal dispatch, and solution
  serialization were not modified.
- No dependency or CMake target was added manually; CMake's existing source
  and test globs include the new files.

## Acceptance

- PASS: independent W matrix without Instance/geometry.
- PASS: dimension and weight validation.
- PASS: assignment cost recomputation.
- PASS: brute-force reference is test-only.
- PASS: empty-domain decision is explicit.
- PASS: tie-breaking separated from objective.
- PASS: targeted tests and existing suite pass.
