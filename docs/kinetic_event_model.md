# Kinetic Event Model and Reference Baseline

This document specifies the existing kinetic disk-cover model and the
exhaustive event path used as the correctness baseline for future acceleration.
The implementation intentionally makes no Voronoi/KDS assumptions.

## 1. Mathematical model

There are fixed stations \(q_j\), indexed by station id \(j\), and moving
points \(p_i(t)\), indexed by point id \(i\), over the common time domain
\([0,T]\). A point trajectory is piecewise linear. On one trajectory segment,
\[
p_i(t)=u_i t+v_i,
\]
so its squared distance to station \(j\) is the quadratic
\[
d_{ij}^2(t)=\lVert p_i(t)-q_j\rVert^2
= (u_i\cdot u_i)t^2
+ 2u_i\cdot(v_i-q_j)t+\lVert v_i-q_j\rVert^2.
\]

At any time, an assignment gives each point exactly one owner station.
For a fixed assignment, the support of station \(j\) is the farthest
currently assigned point,
\[
s_j(t)=\arg\max_{i:\operatorname{owner}(i)=j}d_{ij}^2(t),
\]
with ties resolved by the current deterministic point ordering: extension
scans point ids in ascending order and replaces the support only on a strict
distance increase, so the lowest id wins an exact distance tie. An empty
station has support \(-1\) and contributes zero radius. Its radius is
\[
r_j^2(t)=d_{s_j(t),j}^2(t),
\]
and the objective represented by a solution interval is
\[
C(t)=\pi\sum_j r_j^2(t).
\]

The objective is a single quadratic on an interval only while all three
conditions hold:

1. Each active support trajectory stays on one linear-motion segment, so its
   distance-to-station function has fixed quadratic coefficients.
2. The supporting point at every station stays unchanged, so the set of
   quadratics contributing to the sum stays unchanged.
3. Ownership stays unchanged, so the assigned-point sets—and hence the
   support candidates—stay unchanged.

`compute_quadratic_coeffs_precomputed` sums the quadratic coefficients of the
supporting points, multiplied by \(\pi\), using the midpoint of the interval to
select each support trajectory's motion segment. Consequently, an interval
must not span an active support's trajectory waypoint or a support/ownership
change.

## 2. Existing operation semantics

### Kinetic extension

`KineticSolution::extend` validates the supplied feasible assignment and its
ownership/coverage at the requested start. It then advances in the requested
direction. A short directional probe selects the farthest assigned point for
each station; equal distances retain the first point id in ascending order.
At a boundary it searches for:

- the next waypoint of any currently active support trajectory;
- the earliest owned candidate/support equality whose directional derivative
  says the candidate is becoming farther; and
- when enabled, the earliest valid non-increasing handover.

The next boundary is the earliest of these and the requested end. A handover
may also be applied immediately at the extension start when the existing
source-second-furthest and receiver-containment tests allow it. A source point
is only transferred when the complete before/after sum of squared station
radii does not increase, within the existing numeric tolerance.

The support-event search itself can span future trajectory waypoints; it
segments its equations internally. The emitted interval partition in the
default mode contains only active-support waypoints and selected
support/ownership events. A waypoint of an irrelevant point remains available
to the event search but is not by itself a `SolutionInterval` boundary.

`KineticIntervalEmission::REFERENCE_ALL_TRAJECTORY_BREAKPOINTS` is a retained
emission-only comparison mode. It adds every trajectory waypoint to the
extension loop boundary list but calls the same exhaustive support and handover
searches. The default
`KineticIntervalEmission::EXACT_RELEVANT_BOUNDARIES` chooses boundaries only
from active support waypoints and selected support/ownership events. The modes
can therefore be differentially compared without pruning points or removing
event certificates.

### Exhaustive support-event queries

`KineticCore::find_support_changes` reports equality-root candidates between
the current support and each other point. It does not itself assert that every
root is a true support transition: a tangent equality may be returned, and
extension subsequently applies ownership and directional-derivative tests.
`find_next_event` selects the earliest returned equality candidate among
stations; callers requiring an actual assignment-compatible transition must
apply the same transition tests as extension.

### Handovers and second-furthest points

`second_furthest_assigned` sorts an explicit station assignment by descending
squared distance, then ascending point id; it returns the second entry, or
\(-1\) if fewer than two points are assigned. Handover searches require valid
source/receiver supports and at least two source-owned points. The candidate
point must be the source support, become contained in the receiver disk in the
requested direction, and pass a whole-assignment before/after radius check.
`find_handovers` evaluates one source/receiver pair;
`find_handovers_from` checks each active receiving station in ascending order.
`find_next_handover` scans source and receiver station ids in ascending order
and keeps the first event at an equal event time.

### Combination and partial extension

`KineticSolution::combine` takes the common refinement of both input interval
partitions, solves the difference of the two interval quadratics, partitions
at interior real roots, and selects the lower-cost solution at each open
subinterval midpoint. The selected interval retains that solution's support
and ownership vectors. `partial_extend` leaves the existing objective policy
unchanged: MinMax and MinMaxSum candidates are retained over their shared
domain for later combination; MinSum likewise passes its candidate through.
Neither operation is altered by the reference event-engine selection.

## 3. Temporal event taxonomy

Three kinds of times must be kept distinct.

### A. Motion/representation breakpoints

A trajectory waypoint changes the linear formula used to represent a point's
motion. Such a waypoint is an internal segmentation point whenever an event
certificate compares that trajectory. It is a necessary solution boundary
when the point is an active support, because the objective polynomial changes
there. A waypoint belonging only to an irrelevant point is not automatically
a solution boundary.

### B. Combinatorial kinetic events

A support-equality root or handover equality is a candidate event in the
underlying kinetic structure. It may or may not change the actual combinatorial
state: for example, equality can be tangent, the candidate may belong to a
different station, or the event may not pass the directional or handover
validity tests.

### C. Solution representation breakpoints

A time is represented as a `SolutionInterval` boundary when an active support
changes motion segment, a station's selected support changes, or ownership
changes. In each resulting interval the ownership vector, supporting-point
vector, and defining quadratic coefficients remain fixed. A candidate root
that does not pass transition checks is not emitted as a solution boundary.

Thus event detection is allowed—and required—to segment at more times than the
final solution representation. Removing an irrelevant waypoint from the
solution partition must never remove that waypoint from a pairwise certificate
search.

## 4. Explicit exhaustive reference engine

The current path is explicitly named
`KineticEventEngine::REFERENCE_EXHAUSTIVE`. It is the default for
`KineticSolution::extend` and all public support/handover event searches.

For station \(j\), current support \(s\), and every other point \(i\), the
reference search forms the ordered union of the query endpoints and all
interior trajectory breakpoints of \(s\) and \(i\). On every positive-duration
piece it obtains each trajectory's linear segment, constructs
\[
d_{ij}^2(t)-d_{sj}^2(t),
\]
and solves the resulting quadratic exactly using the repository's quadratic
root routine. It retains roots in the relevant piece and directional query
interval, clamps tolerated endpoint roots to the piece, and orders results by
directional time and then candidate id. The existing time tolerance and
root-solving tolerances remain part of this reference behavior.
`find_next_event` scans station ids in ascending order, so equal-time station
events retain the lowest station id.

The algorithm performs no sampling, spatial pruning, or Voronoi-adjacency
filtering. Every point is compared against the current support, including
points that are later rejected by ownership/transition checks. Its cost is
therefore suitable as a transparent baseline, not as the eventual
performance-oriented data structure.

## 5. Correctness invariants

With the current ownership and tie rules:

- If no relevant support/ownership certificate changes and no active support
  trajectory changes its linear segment, the supporting-point and ownership
  vectors are unchanged.
- Every emitted extension interval has positive duration, finite quadratic
  coefficients, constant ownership, and a constant supporting-point vector.
- Intervals cover the requested positive-duration extension contiguously and
  in time order.
- At a support-equality event, adjacent objective polynomials agree at the
  event up to floating-point tolerance because the competing squared
  distances are equal and trajectories are continuous.
- At an active-support waypoint, adjacent objective polynomials agree at the
  boundary because the piecewise-linear trajectory is position-continuous.
- Ownership can change at a handover and the represented cost may decrease
  discontinuously. The appropriate compatibility condition there is valid
  ownership/coverage and a non-increasing before/after radius objective, not
  equality of the two objective values.

The extension tests exercise strict-interior and waypoint support changes,
tangencies, simultaneous changes, point-id ties, irrelevant waypoints,
coincident support/handover events, no-event and zero-duration requests, and
forward/backward traversal. They also check interval contiguity, support and
ownership vectors, polynomial continuity where the mathematical event is
continuous, and deterministic event-trace ordering.

## 6. Profiling and deterministic traces

Pass an optional `KineticEventDiagnostics*` to an event query or extension to
collect:

- station support-event searches;
- point-versus-support comparisons;
- trajectory-segment pair examinations;
- quadratic equations solved and real roots found;
- candidate roots rejected by time, ownership, or transition tests;
- selected support events and handover-event checks;
- generated solution intervals; and
- total extension, support-event detection, and handover detection
  nanoseconds.

No diagnostic object is allocated implicitly. When the pointer is null,
timers do not read the clock, counters are not updated, and event traces are
not built. Timings use `std::chrono::steady_clock`. Support-detection time
includes the exhaustive pairwise scans nested in handover detection, so its
time can overlap the total handover-detection time; compare the values as
inclusive phase measurements rather than summing them.

The optional structured trace contains event time/type, station, old/new
support for support changes, source/receiver station and post-handover support
ids for handovers, affected point, and a textual description of the
deterministic tie rule. It contains no pointer or thread identifiers.
Extension sorts trace entries deterministically by time and stable ids. The
existing `KDC_PROFILE_PHASES` facility remains available for broader solver
phase profiling; the event diagnostics are local to the caller and can be
used without enabling global profiling.

## 7. Exact interval-emission benchmark

`kdc-kinetic-interval-benchmark [count]` writes one CSV record per seeded
instance. It compares the retained all-waypoint emission mode with exact
relevant-boundary emission while using the same exhaustive event engine and
checks both outputs with the continuous verifier. The CSV includes raw
trajectory waypoint entries, interval counts, total extension time,
event-detection time, interval-construction time, and interval reduction.

The deterministic benchmark instances contain 50 points and 7 stations.
Each station has a fixed support point at radius 10; the remaining points
stay within radius 1.5 and have 16 piecewise-linear segments. Thus their
waypoints are deliberately irrelevant to the represented objective while
remaining present in exhaustive event searches. In a three-instance run,
each reference emitted 16 intervals and each exact mode emitted 1 (93.75%
fewer). Measured total extension time ranged from about 13.0--14.0 ms for
reference emission and 1.42--1.45 ms for exact emission on this run. This is a
benchmark-specific measurement, not a general speedup claim.

## 8. Why the reference path has no spatial pruning

The reference baseline must independently establish whether an event was
missed. A spatial structure, including a Voronoi-neighbor assumption, could
discard a point whose distance certificate becomes active after motion or a
trajectory-segment change. Such pruning would make differential testing
circular: the optimized engine and its supposed oracle could share the same
unproved omission. The exhaustive comparison is deliberately simple and
complete, and is retained for future comparisons even when it is slower.

## Future KDS Acceleration

Future optimization will replace repeated global scans with maintained
kinetic certificates and a prioritized event queue. It must preserve the
same ownership, tie, direction, breakpoint, and handover semantics and retain
`REFERENCE_EXHAUSTIVE` for differential verification on deterministic and
randomized instances. No Voronoi/KDS optimization is introduced by this
baseline task.
