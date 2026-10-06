# MinMaxSum scientific specification

## Scope and conclusion

This document audits the current kinetic disk-cover (KDC) representation and
specifies a first production `minmaxsum` mode. It does not implement that mode.

Under the feasibility semantics currently enforced by `Verifier`, reassignment
at a solution-interval boundary has no cost and no separate transition
constraint. Subject to the assumptions below, feasible solutions are therefore
closed under pointwise minimum. Combining a globally optimal MinMax solution
and a globally optimal MinSum solution yields a feasible solution attaining
both optimum values. The appropriate formulation is a joint objective vector
`(peak_cost, integral_cost)`, not an invented scalar tradeoff.

This conclusion depends on the present model. If legal handover events, dwell
times, switching penalties, or other cross-boundary restrictions are made part
of feasibility, closure must be reproved and the combination operation may
need to change.

## 1. Formal KDC solution and objective definitions

Let the finite horizon be `[0, T]`. There are fixed station locations
`s_j`, and moving points have positions `p_i(t)`. A kinetic solution `S` is a
finite partition of the horizon into intervals. On each interval it contains:

- a station support point `q_j` (or no support for an unused station);
- a point-to-station ownership map `owner(i)`;
- the polynomial cost coefficients for that same support configuration.

For station `j`, the represented disk radius on an interval is
`r_j(t) = ||p_{q_j}(t) - s_j||`; an unused station contributes zero. The
instantaneous represented cost is the sum of disk areas:

```math
C_S(t) = \pi \sum_{j:\,q_j\text{ exists}} \|p_{q_j}(t)-s_j\|^2
       = a t^2 + b t + c
```

on each interval where point trajectories are affine and the selected supports
are fixed. Feasibility requires every moving point to be covered by the
selected disks and every owned point to lie within its owner's disk. A support
must be a point owned by its station; an unused station cannot own points.
These are the continuous conditions checked by `verify_continuous`.

The two scalar objectives are:

```math
M(S) = \max_{t \in [0,T]} C_S(t)
```

and

```math
I(S) = \int_0^T C_S(t)\,dt.
```

`MinMax` means minimizing `M(S)`. The implementation's `peak_cost()` evaluates
each interval's endpoints and any interior vertex of a concave quadratic;
`peak_time()` applies the same candidates and reports one maximizing time.
`MinSum` means minimizing `I(S)`. `integral_on()` and `total_integral()`
integrate the stored quadratic coefficients analytically. `MinSum` is not
minimizing a sum of discrete sample costs: the solver uses static solves to
construct and refine kinetic solutions, then evaluates their analytic
integrals.

There is an endpoint convention worth preserving in the production
specification. `cost_at(t)` chooses the right-hand interval at an interior
breakpoint, while `peak_cost()` checks both closed endpoints of every interval.
An event can change supports and lower the represented area discontinuously.
In that case the largest closed-interval endpoint value can be a left-hand
limit that the right-continuous `cost_at` function does not attain. The code's
reported peak is therefore the supremum over the piecewise-polynomial
intervals; it equals the stated maximum whenever the represented cost is
continuous (or its supremum is attained). For a mathematically total
production definition with arbitrary handover jumps, specify
`M(S) = sup_{t in [0,T]} C_S(t)`; this agrees with the current `peak_cost()`
and preserves the intended worst-case-cost interpretation. The integral is
unaffected by isolated breakpoint values.

These are different objective functionals over the same representation. The
`ObjectiveType` argument to `combine` does not change how it selects intervals:
both current modes use the same pointwise lower-envelope construction.

## 2. `combine` semantics and implementation audit

For two inputs with a common full domain `[0,T]`, let `A_k` and `B_k` be their
active intervals over an elementary time segment. `KineticSolution::combine`
does the following:

1. Requires each input to be well-formed. It unions and sorts all interval
   endpoints from both solutions. Well-formedness requires positive-length,
   contiguous intervals within each input.
2. On each elementary segment between those endpoints, obtains one active
   interval from each input. The input interval polynomials are valid there;
   trajectory breakpoints, support changes, and handovers already partition
   the source solutions.
3. Solves the quadratic equation `C_A(t) - C_B(t) = 0`. Roots strictly inside
   the elementary segment are added as partitions. The quadratic solver handles
   degenerate linear/constant cases; roots and breakpoints are filtered or
   coalesced using floating-point tolerances.
4. Samples the midpoint of each root-free open subinterval. Since the
   difference polynomial cannot change sign inside it, this selects the
   polynomial with lower cost throughout that open subinterval.
5. Copies the selected source interval wholesale, changing only its time
   endpoints. Thus the selected support vector and owner map travel together;
   the operation does not synthesize a mixture of the two assignments.
6. Merges adjacent identical intervals. At shared boundaries, both source
   intervals were checked over their closed endpoints by continuous
   verification; the resulting representation chooses the right-hand interval
   at an interior boundary when queried with `cost_at`.

Consequently, for continuously feasible source solutions on the same full
horizon, the constructed right-continuous cost is the pointwise minimum on
every open subinterval and at shared boundaries, up to numerical
root/breakpoint tolerances:

```math
C_{\operatorname{combine}(A,B)}(t) = \min(C_A(t), C_B(t)).
```

The implementation does **not** independently prove source feasibility.
`combine` checks structural well-formedness, not coverage or assignment
validity. A production joint mode must require both inputs to have the same
full `[0,T]` domain and pass continuous verification before combination, and
must continuously verify the combined result afterward. The implementation can
also combine partially overlapping domains by copying whichever input is
active outside the overlap; that is not the desired contract for `minmaxsum`.

### Feasibility and switching audit

The copied interval's support/owner state is a source state that was feasible
throughout that interval. Restricting it to a subinterval preserves point
coverage, support ownership, and support validity. The continuous verifier
checks coverage and assignment/support validity on each interval, including
trajectory subsegments and comparison roots. It does not validate a transition
relation between the owner maps on adjacent intervals.

`KineticSolution::extend` and `KineticCore` contain optional handover-event
logic that transfers a support point when geometric conditions permit.
`MinSum` also has an integral-improving handover pass. Those are construction
policies for some generated candidates; they are not transition constraints
checked by `verify_continuous` or by `KineticSolution::combine`. The encoded
feasibility model therefore admits instantaneous, cost-free changes from one
interval's feasible assignment to the next. This is the precise reason
pointwise-envelope closure holds in the current implementation.

If this is not the intended scientific model, closure must not be relied upon:
add an explicit transition validator/model first, then prove that every
boundary switch selected by `combine` is a legal handover. In particular,
interval-local feasibility alone does not imply legality under a future
handover-only model.

## 3. Closure theorem

**Theorem (closure under pointwise minimum).** Suppose `A` and `B` are
continuously feasible solutions on the same complete horizon `[0,T]`, and:

1. feasibility at time `t` depends only on the active interval's supports,
   owners, and disk coverage at `t`;
2. changing the active feasible interval state has no switching cost,
   resource constraint, dwell-time requirement, or cross-boundary transition
   predicate;
3. the interval cost polynomials exactly represent the disk-area costs, and
   the combination partitions all source interval boundaries and all roots of
   their cost difference.

Then `combine(A,B)` is feasible. At each time it chooses one whole source
state, not an invalid hybrid, and that state is feasible at that time. Its cost
is the pointwise minimum, subject only to floating-point tolerance in the
implementation.

It follows for the two objective functionals that:

```math
M(\operatorname{combine}(A,B)) \leq \min(M(A),M(B))
```

and

```math
I(\operatorname{combine}(A,B)) \leq \min(I(A),I(B)).
```

Here `M` is the maximum when attained, and otherwise the supremum convention
above. The first inequality follows by taking the maximum/supremum of
`min(C_A(t), C_B(t)) <= C_A(t)` and likewise for `B`. The second follows by
integrating the same pointwise inequalities over `[0,T]`.

The current `combine` implementation realizes this theorem only to its
numerical tolerances, and only its structural preconditions are enforced by
`combine` itself. Full-horizon agreement, source verification, and result
verification are required at the caller.

## 4. Simultaneous-optimum theorem

Let `F` be the feasible set of full-horizon KDC solutions under the switching
semantics above. Assume the two optima are attained by feasible solutions
`S_M, S_I`:

```math
M(S_M) = M^* = \min_{S\in F} M(S), \qquad
I(S_I) = I^* = \min_{S\in F} I(S).
```

By closure, `S_J = combine(S_M,S_I)` is in `F`. Its peak and integral satisfy:

```math
M(S_J) \leq M^*, \qquad I(S_J) \leq I^*.
```

Since `M^*` and `I^*` are minima over all of `F`, neither inequality can be
strict. Thus:

```math
(M(S_J), I(S_J)) = (M^*, I^*).
```

So a globally MinMax-optimal and a globally MinSum-optimal solution can be
combined into one solution that simultaneously attains both optima. This
theorem requires global component optimality certificates, same-instance and
same-horizon feasible solutions, attained optima, exact objective
representations, valid combination, and the cost-free switching semantics.
Static exactness alone, a time-limited incumbent, or continuous verification
alone does not meet those assumptions.

## 5. Formulation choice

The scientifically appropriate production definition is **joint pointwise
envelope optimization**, reporting `(M(S), I(S))`. Under the closure theorem,
there is no unavoidable tradeoff between the two global optimum values. The
joint envelope is a constructive way to attain the pair; it does not introduce
a preference coefficient.

| Candidate formulation | Assessment for this repository |
|---|---|
| Raw weighted sum `alpha M + (1-alpha) I` | Reject. `M` has units of area while `I` has units of area-times-time. A raw sum makes the tradeoff depend on units and horizon, and the weight invents a user preference. |
| Normalized weighted sum `alpha M/M_ref + (1-alpha) I/I_ref` | Reject as the production definition. It repairs dimensional mismatch only by choosing reference scales and a tradeoff preference. It is unnecessary under closure and may select a non-component-optimal point if exact component optimization is unavailable. |
| Lexicographic MinMax then MinSum | It can attain the same pair under the theorem, but it encodes an ordering and is not the neutral joint objective. It is an alternative API policy, not the mathematical meaning of `minmaxsum`. |
| Epsilon-constraint `min I(S)` subject to `M(S) <= M* + epsilon` | With a proven `M*` and `epsilon = 0`, it can attain the joint pair. Positive epsilon deliberately relaxes MinMax and no longer means simultaneous optimization. It is an alternative algorithmic encoding, not required. |
| Joint pointwise envelope | Choose. It uses the actual two repository objectives, is invariant to arbitrary scalarization scales, and by the closure theorem attains both component minima when each input is globally optimal. |

If future model changes invalidate closure, these alternatives become genuine
tradeoff formulations and must be selected as a product/scientific decision;
this document does not preselect scalarization.

## 6. First implementation blueprint

Implement the first mode as the following bounded pipeline:

1. Solve MinMax and retain its full-horizon feasible solution, status,
   verification report, and stable source run ID.
2. Solve MinSum and retain the analogous result.
3. Require the same instance, objective cost convention, and complete `[0,T]`
   domain. Reject missing, malformed, or unverified component solutions.
4. Combine the two solutions using the pointwise envelope.
5. Independently run continuous verification on the combined solution and
   recompute both peak and analytic integral from it.
6. Check all four componentwise dominance invariants below. If any fails
   beyond a scale-aware numerical tolerance, mark the joint run invalid; do
   not emit a successful `minmaxsum` result.
7. Report the objective vector and provenance. Do not use a scalar
   `objective_value` as a proxy for both objectives.

No further optimization/refinement is scientifically required once exact
MinMax and MinSum optima are both certified and the combined solution verifies:
the theorem proves that neither component can be improved. With heuristic or
time-limited components, additional candidate solves can be useful, but they
are an explicitly heuristic search for improvements to the objective vector,
not a necessary part of the first implementation. An archive of verified
component candidates can be combined iteratively; each envelope must preserve
or improve both values and be reverified.

## 7. Optimality and component status semantics

Use the existing status vocabulary with joint-specific evidence:

- `OPTIMAL`: both component solutions are feasible and continuously verified;
  each component is independently proven globally optimal for its own
  full-horizon objective; the combination and all invariants pass; and the
  switching/closure assumptions apply. A static solver being exact is not
  itself a dynamic-objective certificate.
- `FEASIBLE`: a full-horizon joint solution exists and passes continuous
  verification and all dominance invariants, but at least one component lacks
  a global optimum certificate and the run did not hit its time limit.
- `TIME_LIMIT`: a required component or combination/verification phase hit the
  joint time budget before an optimality certificate was established. If a
  complete verified joint incumbent exists, retain and report it with this
  status; otherwise objective values and the joint solution are unavailable.
- `FAILED`: a component has no usable feasible solution without a time-limit
  explanation, the combination is malformed, verification fails, or an
  invariant fails beyond tolerance. Never convert a failed verification into
  a feasible result.

The current `MinMax` can establish dynamic MinMax optimality only when its
certified global lower bound matches its verified peak (within the declared
tolerance). The current `MinSum` result path does not currently expose a
certified dynamic integral gap: its sampled bound is heuristic, it resets
`certified_gap`, and therefore its ordinary result status cannot certify
global MinSum optimality. A `minmaxsum` run using current MinSum results must
normally be `FEASIBLE`, not `OPTIMAL`, unless a separate valid global MinSum
certificate is supplied. This is a prerequisite for truthful optimality
reporting, not a reason to relabel a heuristic bound as certified.

If a time-limited component still returns a valid feasible incumbent, it may
participate in a verified joint incumbent. Preserve `TIME_LIMIT` at the joint
level and preserve the component's incumbent optimality status separately.

## 8. Result schema

The joint result should contain at least:

| Field | Meaning |
|---|---|
| `peak_cost` | Recomputed `M(S_joint)`, nullable if no joint solution exists. |
| `integral_cost` | Recomputed `I(S_joint)`, nullable if no joint solution exists. |
| `minmax_component_status` | Execution/result availability for the MinMax component, e.g. completed, time-limited, or failed. |
| `minsum_component_status` | Execution/result availability for the MinSum component. |
| `minmax_component_optimality` | MinMax component's certified/uncertified optimality status. |
| `minsum_component_optimality` | MinSum component's certified/uncertified optimality status. |
| `joint_optimality_status` | Joint status with meanings defined above. |
| `dominates_minmax` | True iff the joint vector weakly dominates the MinMax component vector within tolerance. |
| `dominates_minsum` | True iff the joint vector weakly dominates the MinSum component vector within tolerance. |
| `minmax_source_run` | Stable identifier/path for the MinMax input result. |
| `minsum_source_run` | Stable identifier/path for the MinSum input result. |

The weak-dominance tests are, respectively,
`M_joint <= M_minmax` and `I_joint <= I_minmax`, and
`M_joint <= M_minsum` and `I_joint <= I_minsum`. Persist the component vectors
as well (at minimum `minmax_peak_cost`, `minmax_integral_cost`,
`minsum_peak_cost`, `minsum_integral_cost`) so these flags are auditable.
Statuses and objective values should be null/explicitly unavailable when the
corresponding component or joint solution does not exist. Do not populate a
single scalar objective value for this mode.

## 9. Required invariants

For verified component solutions and a verified joint solution, enforce:

```math
M_{\text{joint}} \leq M_{\text{minmax}}, \qquad
M_{\text{joint}} \leq M_{\text{minsum}},
```

```math
I_{\text{joint}} \leq I_{\text{minmax}}, \qquad
I_{\text{joint}} \leq I_{\text{minsum}}.
```

Use a scale-aware tolerance such as
`tol(x,y) = atol + rtol * max(1, |x|, |y|)` and make `atol`/`rtol` explicit in
the result/verification configuration. A failed inequality beyond tolerance
invalidates the joint run. The flags `dominates_minmax` and `dominates_minsum`
must be derived from the corresponding pair of inequalities, not solver
statuses.

## 10. Future extensions

If switching costs, handover legality, bounded numbers of simultaneous
transfers, minimum dwell times, assignment continuity, or other inter-interval
constraints are introduced:

- add those rules to the formal feasible set and the continuous/transition
  verifier;
- make `combine` validate each newly introduced switch, rather than merely
  copying a feasible source interval;
- re-evaluate closure and the simultaneous-optimum theorem;
- if closure fails, expose a real multiobjective tradeoff (for example a
  Pareto frontier or an explicitly user-selected lexicographic,
  epsilon-constraint, or normalized scalar objective) instead of claiming the
  joint-envelope theorem.

### Implementation references audited

The conclusions above follow from `src/solution.cpp`
(`compute_quadratic_coeffs_precomputed`, `extend`, `combine`,
`partial_extend`, `total_integral`, `peak_cost`, `peak_time`, and
`is_well_formed`), `src/kinetic.cpp` (support and optional handover event
detection), `src/minmax.cpp`, `src/minsum.cpp`, and `src/verify.cpp`
(`verify_continuous_coverage`, `verify_continuous_assignment`,
`verify_continuous_cost`, and `Verifier::verify_continuous`). Regression
coverage is in `tests/test_solution.cpp` and `tests/test_minmax.cpp`.
