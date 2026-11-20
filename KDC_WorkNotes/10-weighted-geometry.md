# KDC Stage 10 - Weighted Geometry Module

Date: 2026-11-19
Repository: `/Users/amirpartovi/Desktop/Codes/KineticDiskCover`

## Added files

- `include/kdc/weighted_geometry.hpp`
- `src/weighted_geometry.cpp`
- `tests/test_weighted_geometry.cpp`

No temporal MinSum, static solver, benchmark, CLI, or output file was changed.

## API

`WeightedGeometry` snapshots and validates:

- point count n;
- station count m;
- fixed station positions and station identifiers;
- piecewise-linear trajectories;
- [0,T] domains;
- finite breakpoints and waypoints;
- strict trajectory breakpoint ordering.

It exposes:

- `common_partition()`;
- `station_ids()`;
- `d_at(time)`;
- `bounds_on(interval_start, interval_end)`;
- quadratic coefficients for a trajectory/station segment.

The API uses separate result wrappers:

- `DAtTimeMatrixV1`;
- `DMinusMatrixV1`;
- `DPlusMatrixV1`;
- `IntervalDistanceBoundsV1`.

Each wrapper contains a `WeightMatrix` explicitly. It does not convert
D-/D+ into an actual geometric Instance/time object.

## Common partition

The common partition is:

- union of 0, T_end, and every trajectory breakpoint;
- sorted;
- exact duplicate breakpoints merged;
- no breakpoint smoothing or tolerance repair.

Repeated breakpoints within one trajectory are rejected because the trajectory
constructor requires strictly increasing breakpoints.

## Quadratic coefficients

For a trajectory segment, the implementation computes:

\[
d_{ij}(t)=a_{ij}t^2+b_{ij}t+c_{ij}
\]

with:

- \(a=|v|^2\);
- \(b=2v\cdot(p_{\text{start}}-c_i-vt_{\text{start}})\);
- \(c=|p_{\text{start}}-c_i-vt_{\text{start}}|^2\).

## D(t)

`d_at(time)` evaluates the exact-time squared distances for every
(station,point) pair and constructs a `DAtTimeMatrixV1`.

## D-(I) and D+(I)

`bounds_on(a,b)` splits I at the common partition, evaluates each
station/point coefficient on every subinterval, and computes:

- endpoints;
- vertex for positive quadratic minimum;
- vertex for negative quadratic maximum.

For squared-distance polynomials, \(a=|v|^2\ge0\), so:

- minimum may use the vertex;
- maximum uses endpoints.

The result carries:

- FAST_DOUBLE arithmetic mode;
- FAST_FLOAT_ONLY validity;
- no certified/validated claim.

## Validation

The module rejects:

- negative/invalid n or m;
- trajectory/point count mismatch;
- station/center count mismatch;
- non-finite center, breakpoint, or waypoint;
- T_end mismatch;
- trajectory beginning not at 0;
- non-strict breakpoint sequence;
- interval outside [0,T];
- invalid coefficient/segment dimensions;
- non-finite computed coefficients/bounds.

Duplicate centers remain separate station entries and station IDs are
preserved.

## Tests

`tests/test_weighted_geometry.cpp` covers:

- D(t) stationary/moving;
- sample-time containment;
- vertex inside/outside;
- zero velocity;
- simultaneous breakpoints;
- breakpoint at beginning/end;
- exact one-segment interval;
- duplicate centers and station IDs;
- invalid dimensions and breakpoints.

## Build and test

The existing CMake source/test glob includes the new files automatically.

Result:

- configure/build passed;
- CTest 1/1 passed;
- full existing suite passed.
