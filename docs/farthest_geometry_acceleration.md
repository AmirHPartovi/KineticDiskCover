# Farthest-Point Geometric Acceleration: Research Gate

## Decision

**ABANDON_GEOMETRIC_ACCELERATION** for the current implementation cycle.

Do not implement or promote a kinetic hull or farthest-point Voronoi structure.
This is a scope decision based on the absence of a certified tournament event
engine, no measured support-event benefit from the current engine option, and
the substantially larger correctness surface a kinetic hull would add. It is
not a claim that kinetic hulls are never useful.

The fixed-time convex-hull theorem is valid and could eventually make a
station-local hull a candidate filter. It does not justify temporal pruning,
and the current benchmark does not establish that there is a correct,
integrated tournament against which a hull could be compared. In particular,
the passing Prompt 4 differential rows certify equivalence of the current
public execution paths; they do **not** certify a certificate-driven
tournament KDS. This gate therefore does not satisfy Prompt 5's prerequisite
for a hull experiment. The appropriate action is to fix and certify the
first-level event engine before reopening geometric acceleration.

## Fixed-time theorem and assumptions

For one station at fixed position \(q\) and its current finite assigned set
\(A\), a farthest point maximizing
\[
  \max_{p\in A}\|p-q\|^2
\]
can be chosen among the extreme points of \(\operatorname{conv}(A)\).
Squared Euclidean distance is strictly convex: if \(p\) is not an extreme
coordinate and is a convex combination of at least two distinct hull
vertices \(v_k\), then
\[
  \|p-q\|^2
  < \sum_k \lambda_k\|v_k-q\|^2
  \le \max_k\|v_k-q\|^2.
\]
Thus a non-extreme coordinate cannot be strictly farthest. Coincident point
ids have the same distance, so a hull representation must retain the lowest
point id for every represented extreme coordinate to preserve the solver's
tie rule. Collinear sets have only their endpoint coordinates as extremes
(with the same duplicate-coordinate rule).

This theorem assumes Euclidean distance and a fixed station and time. It
applies separately to each station's currently owned set
\[
  A_j(t)=\{i:\operatorname{owner}(i,t)=j\}.
\]
It is not a temporal pruning theorem. A currently interior point can later
become extreme, and ownership can move a point between station sets. No
current-time hull, global hull, nearest-neighbor set, radius cutoff, ordinary
Voronoi adjacency, or Delaunay adjacency can safely exclude a point over a
future interval without kinetic certificates.

## What a correct kinetic hull would require

No hull structure or hull-based farthest query is implemented in this
repository. The following are requirements for a possible future prototype,
not implemented guarantees.

### Certificate definitions

On a common interval where three trajectories are each linear, an orientation
predicate for points \(a,b,c\) is
\[
  \operatorname{orient}_{abc}(t)=
  (p_b(t)-p_a(t))_x(p_c(t)-p_a(t))_y
  -(p_b(t)-p_a(t))_y(p_c(t)-p_a(t))_x.
\]
Each coordinate difference is affine in time, so this determinant is a
polynomial of degree at most two. Its roots can signal a change in the
orientation relation used by a hull certificate. Every such certificate
would have to be scheduled only up to the earliest participating trajectory
breakpoint or certificate failure, then rebuilt using the adjacent motion
segments. The root itself does not necessarily change the hull topology.

Triple-orientation tests for current hull neighbors alone are insufficient to
prove all excluded points remain interior. A correct kinetic hull needs a
complete certificate scheme for its hull representation and for points that
may become hull vertices, including the relevant bridge/visibility or
replacement conditions. Such a scheme and its proof are not present here.

### Required state transitions and degeneracies

A future station-local implementation must support atomic ownership
transitions (erase from source hull, insert into receiver hull), and must
restore valid certificates before advancing beyond a simultaneous event
batch. It also needs explicit deterministic behavior for:

- empty, singleton, two-point, and collinear sets;
- coincident coordinates and repeated positions, retaining the lowest-id
  representative where coordinates coincide;
- identically-zero orientation polynomials and intervals of collinearity;
- tangent/repeated roots, simultaneous certificate failures, and a
  trajectory breakpoint coinciding with an orientation root.

The current numerical policy uses floating-point coefficients, centralized
scale-aware comparison tolerances, and deterministic point-id ties; it is not
exact arithmetic. Any hull predicate would need a documented tolerance policy
compatible with support comparisons, root isolation, and simultaneous-event
grouping. The hull's farthest-support query would have to equal the
brute-force support at every critical time and directional state.

## Current implementation and complexity

`KineticFarthestTournament` currently builds a balanced binary winner tree
over sorted point ids and stores locally reduced winner/runner-up values at
each node. Because the pairwise tolerance comparison is not transitive, those
grouped values cannot define the authoritative support. The public winner and
runner-up are obtained with a canonical ascending-id fold over the assignment;
the cached nodes do not provide a production query-work reduction. It is not
yet a certificate-driven kinetic tournament scheduler:

- `initialize`, `insert`, `erase`, `update_motion`, and `process_until`
  rebuild the tree. Rebuild sorts the stored ids and recomputes nodes, so
  updates are \(O(n\log n)\) in the number of assigned points, rather than a
  local \(O(\log n)\) tree update.
- `next_event_time` examines pairs of assigned points and their piecewise
  trajectory segments; its work is pairwise, not a maintained local
  certificate queue.
- `KineticSolution::extend` resolves owner supports by scanning assigned
  points and uses `support_changes_impl` for support-event prediction.
  That detector enumerates every challenger and solves pairwise polynomial
  equations. The `KINETIC_TOURNAMENT` selection does not replace this
  exhaustive support-event scan with maintained tournament certificates.

Consequently, the current structure is a winner tree with rebuild-based
maintenance, but it is not an integrated kinetic tournament KDS. The current
support-event work remains exhaustive in the number of challengers and
trajectory segment pairs. The reference detector remains the correctness
oracle.

A kinetic hull could reduce the number of sites participating in a
fixed-time farthest query from \(n\) to hull size \(h\) **only after** complete
kinetic hull maintenance proves the candidate set is valid. Hull maintenance,
certificate count, event queue costs, dynamic ownership update costs, and
memory have no implemented complexity bound or measured value here. In
particular, no logarithmic kinetic-query or update claim is made.

## Differential and benchmark evidence

Prompt 4 recorded 200 deterministic seeds across ten synthetic families,
three repetitions, and three public configurations (600 rows). All saved
rows were `PASS`, including continuous verification and comparisons of
ordered event traces, interval boundaries/state/coefficient data, event-time
and adjacent-representable-time costs, peak, and integral. The run was
compiled directly with Apple clang 17 using `-O2 -DNDEBUG` from a dirty
worktree at base revision `31fa4196bd478f6e94c033cc0fd7afe73e9528b5`.
These results are exploratory synthetic measurements, not clean-runner
release benchmarks.

| Configuration | What was actually exercised | Median speedup vs reference | p25–p75 | Interpretation |
|---|---|---:|---:|---|
| `REFERENCE_EXHAUSTIVE` + global handover | Exhaustive support-event search and reference handover evaluation | 1.000x baseline | — | Correctness oracle |
| `KINETIC_TOURNAMENT` + global handover | Public option with same exhaustive support-event candidate search; tournament winner structure also used | 1.001x | 0.990–1.017x | Not evidence for a certificate-driven tournament |
| `KINETIC_TOURNAMENT` + local exact handover | Same support-event path, local handover evaluation with reference fallback | 1.005x | 0.995–1.025x | Handover-local path only; no hull comparison |
| Kinetic hull | Not implemented | Not measured | Not measured | No correctness, runtime, or memory result |
| Tournament + hull | Not implemented | Not measured | Not measured | No correctness, runtime, or memory result |

Across the 200 per-instance ratios, tournament/global's geometric mean
speedup was 1.006x, and tournament/local's was 1.010x; the quartile ranges
cross 1x. Median extension times were 0.290 ms (reference/global), 0.286 ms
(tournament/global), and 0.291 ms (tournament/local). Point-vs-support
comparison and quadratic-solve counts were unchanged between the reference
and tournament-selected engine options. No support-event break-even was
demonstrated. In the deliberately handover-heavy family, local handover
evaluation reduced recorded global-fallback point scans from 4,400 to 1,600
across 20 seeds, but this does not imply a geometric benefit.

There is no measured hull memory consumption, orientation-certificate count,
hull update/query cost, or hull differential result because the structure was
not implemented. Tournament-node memory was also not measured in the
campaign. These unknowns are not estimates and must not be described as
measured performance.

## When geometry may help or hurt

Geometry may help only if real workloads have small station-local hulls for
most intervals and the cost of discovering, maintaining, and repairing hull
certificates is smaller than the support work they remove. The strongest
candidate workloads would have many assigned points, few hull vertices, and
relatively few hull/ownership events. This is a hypothesis, not an observed
result in this repository.

Geometry may hurt when many points are extreme, trajectories frequently
change segments, orientation certificates fail often, or handovers force
frequent delete/insert repairs. Collinearity, duplicates, and simultaneous
orientation events increase both implementation complexity and repair work.
If hull size approaches \(n\), a hull may add certificate and storage
overhead without reducing support candidates. A tournament-plus-hull hybrid
would maintain two interacting structures and is unjustified before either
the tournament scheduler or a standalone hull is certified.

A full farthest-point Voronoi diagram is even less justified: the station is
a fixed query point and the required result is only the maximum-distance
site. Maintaining the entire moving-site subdivision adds topology and
dynamic update complexity without evidence of a benefit over a correct
station-local hull.

## Research decision and reopening criteria

Selected option: **ABANDON_GEOMETRIC_ACCELERATION** for now. Retain the
exhaustive reference as oracle and do not add a hull or FVD engine in this
change. This is the evidence-based choice because the prerequisite
certificate-driven tournament has not been demonstrated, the tested engine
options retain the same exhaustive support-event work, and there are no
representative hull-size, runtime, or memory measurements.

Reopen this decision only after:

1. tournament support certificates actually drive support-event scheduling;
2. that engine passes deterministic, randomized, adversarial, forward,
   backward, simultaneous-event, and continuous-verifier comparisons against
   the exhaustive reference;
3. representative workloads report per-station hull-size distributions and
   support-event work, with clean/reproducible timing and memory;
4. evidence shows a meaningful remaining support-query bottleneck that a
   hull could reduce.

If reopened, first prototype a station-local kinetic hull behind an optional
engine. Compare exhaustive, the certified tournament, hull, and a combined
path only if each individual structure is independently correct. Do not
promote a hull based only on the fixed-time theorem, static hull measurements,
or sampled future positions.
