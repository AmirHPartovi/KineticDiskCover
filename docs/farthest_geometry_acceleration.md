# Farthest-Point Geometric Acceleration: Investigation and Decision

## Decision summary

Do not implement or promote a geometric accelerator in the current change.
The exact convex-hull theorem below does justify excluding non-extreme sites
from a farthest-point query at a fixed time. It does not, by itself, justify
excluding them throughout continuous motion, nor does it provide a correct
dynamic kinetic hull with ownership changes, degeneracies, and deterministic
support ties.

There is also an implementation-state issue that must be resolved before
benchmarking the proposed second-level accelerator: the checked-out changes
do not currently provide an integrated kinetic tournament engine. The class
named `KineticFarthestTournament` stores a vector and computes its winner by
scanning that vector. Its `next_event_time` examines every point pair. The
support-event query accepts `KINETIC_TOURNAMENT` but runs the same exhaustive
scan, while `KineticSolution::extend` rejects that engine. Therefore the
repository cannot yet supply the three comparable production paths assumed
by the proposed benchmark (exhaustive, kinetic tournament, geometric).

The correct sequence is to complete and validate the first-level tournament
engine, establish representative workloads and a reproducible benchmark,
then prototype a kinetic hull behind a non-default engine. A full dynamic
farthest-point Voronoi diagram is not recommended for this model.

## 1. Problem and exact fixed-time theorem

For a station \(q\) and its currently owned finite site set \(A\), the
required support maximizes squared Euclidean distance:

\[
  \max_{p\in A} \lVert p-q\rVert^2.
\]

Let \(P=\operatorname{conv}(A)\). A maximizer can be found among the extreme
points of \(P\). Every point \(p\in A\) is a convex combination of vertices
\(v_k\) of \(P\). If \(p\) is not itself an extreme coordinate, this
combination contains at least two distinct vertices with positive weights.
Strict convexity of squared distance gives

\[
  \lVert p-q\rVert^2
  < \sum_k \lambda_k\lVert v_k-q\rVert^2
  \le \max_k \lVert v_k-q\rVert^2.
\]

Thus no non-extreme site can be a farthest site; the maximum over \(A\) is
attained by a site whose coordinate is a vertex of \(P\). This argument uses
convexity of squared distance, not a linear-objective support query. The
strict inequality also shows that a distinct, non-extreme site cannot tie the
maximum. Coincident sites are an exception in the representation, not in the
geometry: identical coordinates have identical distances, so the hull
representation must retain the lowest point id at each represented extreme
coordinate (or otherwise preserve the repository's lowest-id tie rule).

This is a per-time theorem only. It says nothing about how to maintain the
extreme set as the sites move or ownership changes. In particular, a site
that is interior now may become extreme later, and may become the farthest
site. An implementation may omit it only while a maintained kinetic
certificate proves the relevant hull classification remains valid.

The repository's support tie rule is lowest point id on equal distance.
Any geometric candidate structure must preserve that rule, including
coincident sites, collinear sites, simultaneous certificate failures, and
ties at trajectory waypoints.

## 2. Alternatives

| Alternative | Correctness assumptions | Dynamic insertion/deletion | Motion breakpoints and event locality | Certificates and numerical risk | KDC workload assessment |
|---|---|---|---|---|---|
| **A. Exhaustive reference scan** | None beyond the existing piecewise-quadratic root solver; compare every currently owned candidate to the support. | Assignment changes are reflected directly in the owner vector; no persistent structure to update. | Each support query segments every candidate/support pair at both trajectories' waypoints. Events are global scans, not local. | \(O(n)\) comparisons per support query, with potentially many segment-pair equations. Uses existing solver and tolerances; simplest oracle. | Highest predictable work, but lowest correctness and integration risk. Keep as oracle. |
| **B. Kinetic tournament** | Every internal winner certificate must remain valid through the next root or trajectory breakpoint; simultaneous failures and ties must be repaired deterministically. | A real tree can update a leaf and its root path in \(O(\log n)\) comparisons; the current class is not such a tree and currently rescans. | Pair comparisons are between tournament sub-winners. Motion breakpoints invalidate certificates on affected paths; an event queue should localize repairs. | \(O(n)\) internal comparison certificates for a balanced tree, with quadratic roots per fixed motion-segment pair. The current solver conventions can be reused. | A natural first acceleration. It may still have many certificate events, but is simpler to validate than a kinetic hull and supports dynamic ownership locally. |
| **C. Kinetic convex hull** | Fixed-time farthest support lies among hull vertices. A kinetic hull must prove every maintained hull edge/vertex remains correct until its next orientation or motion event. Non-extreme sites cannot be permanently discarded. | Fully dynamic hull update bounds depend on data structure and degeneracy model. A point handover requires deletion in one station's set and insertion in another; each must repair the local hull and certificates. | With fixed linear motion per site segment, a triple orientation determinant is degree at most two. Root events and every participating motion breakpoint must be handled; an affected site can invalidate multiple certificates. | Orientation roots use the existing robust quadratic solver in exact arithmetic semantics, but floating tolerances, zero polynomials, collinearity intervals, duplicate positions, and simultaneous roots complicate topology. | The theorem provides a safe candidate reduction when the kinetic hull is correct. Hull maintenance is a substantial new correctness surface; hull size may still be \(n\), eliminating the benefit. |
| **D. Farthest-point Voronoi maintenance** | Must maintain the full farthest-site subdivision for the current moving site set and answer queries at the fixed station. A static diagram cannot be reused while sites move. | Dynamic site insertion/deletion changes the subdivision; handovers require updates in two different station-specific sets. | Diagram combinatorics can change on kinetic events, and event locality is not automatically small for moving sites. | More combinatorial predicates and topology changes than the hull candidate theorem alone requires. Degenerate co-circular/collinear configurations need explicit support. | Excessive complexity for one fixed query per station. No evidence here that maintaining the entire subdivision pays for itself. |
| **E. Tournament plus geometric filtering** | A filter may reject a point only if a certificate proves it cannot beat the tournament winner through the interval. A current hull vertex list is safe only while the kinetic hull certificates remain valid. | Tournament leaf updates and hull updates both have to be made atomically on a handover. | Could reduce candidates to hull vertices while retaining tournament comparisons, but motion and ownership events must update both structures in the same deterministic event transaction. | Combines certificate systems and their simultaneous-failure handling. Fewer distance comparisons are possible only if the hull is smaller and maintenance does not dominate. | Potentially useful after each component is independently exact and benchmarked. It is not the appropriate first geometric implementation. |

Complexity statements in this table are structural counts, not a claim of
wall-clock performance. In particular, no logarithmic farthest-query bound is
claimed for the repository's moving, dynamically owned point sets.

## 3. Why a static hull or neighbor filter is not sufficient

The station is fixed, but its eligible set is station-specific:

\[
  A_j(t)=\{i:\operatorname{owner}(i,t)=j\}.
\]

The points move continuously and may change owner at a handover. Thus a
static hull computed from all points, or from one station's initial
assignment, is not a valid filter for later support searches.

Using only hull vertices at the current time is also insufficient. The
excluded interior sites must remain represented as potential entrants into
the hull. As trajectories change, orientation certificates can fail and an
excluded point can become extreme before any current farthest-support
comparison involving that point is scheduled. A correct kinetic hull must
schedule and process those hull-combinatorics events, including trajectory
breakpoints.

Neither Delaunay edges nor ordinary nearest-neighbor adjacency provides a
completeness proof for a farthest query. The exact geometric statement
available here is about current convex-hull extreme sites; using that
statement over time requires a valid kinetic hull.

## 4. Proposed exact kinetic-hull invariants

If a hull prototype is pursued, it should be station-local, dynamic, and
optional. At each event time, for every station:

1. Its maintained site ids equal exactly the ids whose owner is that station.
2. Its hull contains every current extreme coordinate and preserves the
   lowest-id representative for coincident extreme coordinates.
3. Its ordered hull satisfies the documented orientation convention,
   including an explicit representation for empty, singleton, collinear,
   and duplicate-coordinate sets.
4. Every kinetic certificate is valid on its scheduled open time interval.
5. Every certificate's polynomial is formed only over an interval where all
   participating trajectories use fixed linear segments.
6. At a simultaneous event, all affected certificates are invalidated and
   the hull is repaired to a deterministic valid state before the next
   positive-duration interval.
7. Its station support equals the brute-force argmax by squared distance,
   with lowest point id on ties.
8. A handover removes the point from the source structure and inserts it
   into the receiver structure as one ownership transition; both resulting
   structures satisfy the invariants before support selection continues.

For three point trajectories that are linear on one common segment, the
orientation determinant is a polynomial of degree at most two. Its real
roots must be computed with the repository's quadratic solver, restricted to
that common segment, and re-created after any participating point changes
trajectory segment. Degree zero/identically zero predicates, tangencies,
near-zero discriminants, collinear intervals, and roots shared by multiple
certificates need explicit deterministic policies. A zero of an orientation
predicate is not by itself proof of a persistent hull-topology change.

These are requirements for a future implementation, not guarantees supplied
by the current code.

## 5. Current implementation and benchmark status

The working tree has an uncommitted `KineticFarthestTournament` API, but its
implementation is currently a vector-backed scan:

- `initialize`, `insert`, `erase`, `update_motion`, `process_until`, and
  `validate` recompute the winner across the full stored point vector.
- `next_event_time` examines all point pairs and their piecewise trajectory
  segments; it does not maintain internal tournament nodes or an event queue.
- `KineticCore` support-event queries that accept
  `KINETIC_TOURNAMENT` still execute `support_changes_impl`, the exhaustive
  all-points scan.
- `KineticSolution::extend` currently accepts only
  `REFERENCE_EXHAUSTIVE`.

Consequently, the current `KINETIC_TOURNAMENT` label is not evidence of a
kinetic tournament engine and cannot serve as a separate benchmark baseline.
It would be misleading to report it as one.

No geometric accelerator exists in this tree, so no valid measurements are
available for geometric updates, certificate failures, queue operations,
or geometric-engine equivalence. The repository's
`kinetic_interval_emission` benchmark measures interval-emission variants,
not the exhaustive/tournament/geometric comparison requested here. Its
results must not be presented as geometric-acceleration results. No runtime
or speedup is claimed in this report.

### Measured benchmark results

| Engine | Comparable production implementation in this tree? | Runtime / verification / event metrics |
|---|---|---|
| Exhaustive reference | Yes | Not measured in this investigation; no paired three-engine benchmark was run. |
| Kinetic tournament | No; the named helper rescans and is not integrated into `KineticSolution::extend`. | Not measurable as a distinct engine. |
| Geometric accelerator | No implementation. | Not measurable. |

This is an explicit no-result, not an estimate: collecting numbers from
different call paths or comparing a vector rescan with an integrated
extension would not be an apples-to-apples benchmark.

### Reproducible benchmark protocol for a future prototype

Once a real tournament engine is integrated, compare all three engines on
identical serialized instances, initial assignments, direction, handover
settings, solver budgets, build mode, hardware, and continuous-verification
settings. Record one raw row per instance and engine, including:

- instance id, dimensions, trajectory segment count, and deterministic seed;
- extension total runtime and continuous verification runtime;
- support-event time, point comparisons, segment-pair examinations, and
  polynomial solves;
- tournament certificate failures, geometric orientation-certificate
  failures, hull updates, and priority-queue pushes/pops/stale events;
- emitted interval count, peak value/time, total integral, and deterministic
  support/ownership event trace.

Warm-up policy, compiler/version/flags, repetitions, and whether timing is
median or distribution must be recorded. Validate event traces and interval
representations against the exhaustive oracle before comparing performance.
For every run, compare all support and handover events (including simultaneous
events and deterministic tie outcomes), ownership/support vectors at event
times and open-interval representatives, interval coefficients, continuous
verification, peak, and integral. Report per-instance measurements and
aggregates; do not infer a general speedup from synthetic instances alone.

## 6. Recommendation

1. Retain exhaustive scanning as the correctness oracle.
2. Complete the actual kinetic tournament and integrate it into extension
   before treating it as an established baseline.
3. Benchmark that implementation on representative KDC workloads.
4. If exhaustive pair scans remain dominant and the assigned-set hull is
   materially smaller than the set on those workloads, prototype a
   station-local kinetic convex hull behind an optional engine. Preserve
   non-extreme sites and all orientation/motion certificates.
5. Differential-test that prototype—including event traces, degeneracies,
   ownership handovers, and backward extension—against exhaustive behavior.
6. Consider a hull-plus-tournament hybrid only if measured hull maintenance
   cost and support comparison savings justify its extra state.

Do not build a full farthest-point Voronoi diagram for the current fixed
station queries absent workload evidence that the simpler exact hull
candidate filter is inadequate. Do not claim an asymptotic query improvement
until the complete dynamic/kinetic maintenance and degeneracy analysis
supports it.
