# KDC Stage 13 - Local Interval Evaluator

Date: 2026-11-19
Repository: `/Users/amirpartovi/Desktop/Codes/KineticDiskCover`

## Added files

- `include/kdc/interval_evaluator.hpp`
- `src/interval_evaluator.cpp`
- `tests/test_interval_evaluator.cpp`

No global branch-and-bound loop was added.

## Responsibility

`IntervalEvaluator` combines the existing weighted components for one interval:

1. Verify I is inside one common-partition segment.
2. Build D-(I), D+(I), midpoint, and Delta.
3. Evaluate D- lower-bound paths.
4. Compute a Lipschitz diagnostic from a validated midpoint lower bound.
5. Use a direct or fallback upper bound.
6. Store assignment/pieces and reconcile LB/UB.

## Lower bounds

Official Dminus LB is accepted only from:

- Exact Oracle result with an independently valid LB certificate;
- Relaxation Oracle result with a valid dual certificate.

The following never create an official LB:

- heuristic cost;
- solver `OPTIMAL`;
- solver incumbent cost;
- zero MIP gap;
- unvalidated solver best bound.

If no official nontrivial LB exists, the evaluator sets:

- value=0;
- method=TRIVIAL_NONNEGATIVE;
- trivial=true;
- Oracle success=false.

Lipschitz value is retained as a FAST_DOUBLE diagnostic. It is not used as
the official LB in FAST_DOUBLE mode because the validated arithmetic backend
is not integrated.

## Upper bounds

The assignment is:

- inherited assignment if valid, otherwise
- a heuristic on D+.

The UB engine first uses the direct upper-envelope integral.
If direct root isolation fails, it uses the D+ fallback:

\[
\pi\,\Delta\sum_i
\max_{j\in S_i(\sigma_I)}D^+_{ij}(I).
\]

The final UB is the smaller valid of direct and fallback.

Validated mode is unavailable in the current implementation. It does not
promote FAST direct/fallback values to validated UB.

## Consistency and reconciliation

The evaluator records:

- LB;
- UB;
- assignment;
- envelope pieces;
- method/provenance;
- arithmetic mode;
- status;
- failure reason.

If finite LB > UB, status is `RECONCILIATION_FAILURE`.

If no finite UB exists, status is incomplete unless validated mode is explicitly
unavailable.

## Tests

`tests/test_interval_evaluator.cpp` covers:

- stationary trivial LB and zero UB;
- moving interval reference integral;
- reconciliation contradiction;
- validated mode unavailable.

## Build and test

The existing CMake source/test glob includes the new files.

Build and CTest passed.
