# KDC Stage 12 - Integral Upper-Envelope Engine

Date: 2026-11-19
Repository: `/Users/amirpartovi/Desktop/Codes/KineticDiskCover`

## Added files

- `include/kdc/weighted_upper_envelope.hpp`
- `src/weighted_upper_envelope.cpp`
- `tests/test_weighted_upper_envelope.cpp`

No candidate-disk static solver, temporal MinSum, benchmark, or CLI code was
changed.

## API

`WeightedUpperEnvelope::solve(I, assigned_station, mode)` receives:

- interval `[interval_start,interval_end]`;
- point-to-station assignment;
- degree-two coefficients from `WeightedGeometry`;
- FAST_DOUBLE or VALIDATED mode.

It returns:

- direct-envelope integral if valid;
- D+ fallback integral if available;
- final UB;
- validity/status;
- failure reason;
- piece records.

## Direct envelope

For every station:

\[
q_i(t)=\max_{j\in S_i(\sigma_I)}d_{ij}(t).
\]

Empty stations contribute zero.

The implementation:

1. splits I at the common trajectory breakpoint partition;
2. constructs every pairwise quadratic difference;
3. isolates linear/quadratic roots with long-double discriminant analysis;
4. rejects near-coincident roots as indeterminate;
5. sorts only proven roots;
6. selects the active function by midpoint comparison;
7. integrates each active quadratic analytically;
8. sums station integrals and multiplies by pi.

The objective is the integral of the upper envelope, not the sum of separate
integrals for all d_ij values.

## D+ fallback

For FAST_DOUBLE mode, the fallback uses:

\[
UB=\pi |I|\sum_i
\max_{j\in S_i(\sigma_I)}D^+_{ij}(I).
\]

The D+ matrix is not converted into an actual Instance/time geometry.

It remains separate from the direct envelope and is used only when direct
root isolation fails.

## Failure semantics

- direct root indeterminate: direct UB invalid;
- fallback valid: fallback UB used;
- fallback invalid: UB remains infinity;
- validated mode: direct and fallback FAST results are not promoted to
  validated UB; UB remains infinity.

## Special cases covered

- identical polynomials;
- root at a boundary;
- repeated quadratic root;
- three-function equality;
- empty station;
- constant/affine/zero coefficients;
- trajectory-breakpoint pieces;
- simultaneous breakpoints;
- D+ fallback.

## Status fields

- FAST_COMPLETE;
- ROOT_INDETERMINATE;
- VALIDATED_UNAVAILABLE;
- INFEASIBLE;
- INVALID.

## Build and test

The existing CMake source/test glob includes the new files.

Result:

- configure/build passed;
- CTest 1/1 passed;
- full existing suite passed.
