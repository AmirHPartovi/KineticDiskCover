# KDC Stage 11 - Chapter 5 Lipschitz Lower Bound

Date: 2026-11-19
Repository: `/Users/amirpartovi/Desktop/Codes/KineticDiskCover`

## Added files

- `include/kdc/weighted_lipschitz.hpp`
- `src/weighted_lipschitz.cpp`
- `tests/test_weighted_lipschitz.cpp`

No temporal MinSum, verifier, benchmark, or static solver file was modified.

## Thesis source

Chapter 5, Section 5.5:

- `Rij(I)=max_{t in I} ||p_j(t)-c_i||`, Eq. (7.5);
- \(L_{ij}(I)=2R_{ij}(I)||v_j||\);
- \(L_\sigma(I)=\pi\sum_i\max_jL_{ij}(I)\), Eq. (8.5);
- \(\Lambda_I=\pi\sum_i\max_{j}L_{ij}(I)\), Eq. (9.5);
- \(f(t)\ge f(t_c)-\Lambda_I|t-t_c|\), Eq. (11.5);
- Phi piecewise definition, Eq. (12.5);
- Lipschitz integral lower bound, Eq. (13.5).

Printed PDF pages: 121-124.

## API

`WeightedLipschitzBound` provides:

- `pair_upper(point,station,I)`:
  - outward/upper R;
  - velocity norm upper;
  - pair \(L=2RV\).
- `velocity_norm_upper(point,I)`:
  - rejects intervals crossing a trajectory breakpoint;
  - uses the segment velocity corresponding to I.
- `lambda_upper(I)`:
  - \(\pi\sum_i\max_jL_{ij}\).
- `phi_fast(u,K,Delta)`;
- `local_lower(I, midpoint_lower)`.

## Midpoint lower-bound contract

`ValidatedMidpointLowerBoundV1` is required.

It accepts only:

- EXACT_STATIC;
- DUAL_CERTIFICATE;
- RELAXATION_CERTIFICATE.

HEURISTIC and UNKNOWN throw.

This prevents a heuristic static assignment cost from becoming \(u\).

## Rij and velocity enclosure

For each pair:

- sample the two interval endpoints;
- compute the squared distance with outward operations;
- take the larger endpoint squared value;
- square-root with upward rounding.

For a fixed affine point trajectory, squared distance is convex over the
segment, so endpoint maxima are sufficient. The vertex is used only for
minimum calculations, not maximum distance.

The velocity is:

\[
v_j=(p_j(b)-p_j(a))/(b-a)
\]

on a segment entirely containing I. The norm is stored upward.

## Lambda formula

For station i:

\[
\max_j L_{ij}
\]

then:

\[
\Lambda_I=\pi\sum_i\max_jL_{ij}.
\]

Empty point sets contribute zero for that station.

## Phi implementation

For K=0 or Delta=0 or u=0:

- result is 0.

For u>=K*Delta/2:

\[
\Delta u-K\Delta^2/4.
\]

For 0<=u<K*Delta/2:

\[
u^2/K.
\]

The implementation evaluates both branch expressions with downward/upward
arithmetic and uses the lower candidate near the boundary.

## Arithmetic and validity

The implementation is currently `FAST_FLOAT_ONLY`.

It uses:

- `nextafter` outward operations;
- directed-up/down arithmetic helpers;
- upward pi and velocity enclosures;
- no exact rational backend;
- no validated MPFR interval backend.

It does not claim:

- global certification;
- exact interval arithmetic;
- exact root/integral proof;
- `CERTIFIED` lower bound.

The numerical result is a FAST_FLOAT diagnostic. It can become certified
only after the validated MPFR/interval backend is integrated.

## Tests

Added:

- K=0;
- u=0;
- Delta=0;
- high/low Phi branches;
- branch boundary;
- direct symmetric integral examples;
- R and velocity upper bounds;
- Lambda calculation;
- zero velocity;
- crossing breakpoint rejection;
- heuristic midpoint rejection;
- exact/dual midpoint acceptance.

## Build and test

The existing CMake source/test glob includes the new files.

Build and CTest passed.
