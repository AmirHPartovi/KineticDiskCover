# Bound and optimality semantics

Result fields distinguish the feasible objective value (`upper_bound`), the
reported lower bound (`lower_bound`), the provenance of that bound
(`bound_status`), whether the algorithm is an exact method (`exact_solver`),
and its termination state (`optimality_status`). A `certified_gap` is present
only for an exact solver that returned a feasible solution and a certified
lower bound. A timeout always has `TIME_LIMIT` status, even when an incumbent
exists; it is not an optimality proof.

## Static lower bounds

The geometric bound used by nearest-neighbor, greedy, local-search, simulated
annealing, genetic, shifting, and the built-in exact methods is

`LB = pi * max_i min_j ||p_i - s_j||^2`.

For any fixed point `p_i`, every feasible cover assigns it to a disk centered
at some station `s_j`. The covering disk's radius is at least
`||p_i - s_j||`, and hence the sum of all disk areas is at least
`pi * min_j ||p_i - s_j||^2`. Taking the maximum over points preserves that
lower-bound relation. This is a valid, potentially weak certificate for the
static disk-covering objective.

| Solver | Bound treatment | Justification |
|---|---|---|
| Nearest-neighbor | `CERTIFIED` | Uses the universally valid zero lower bound; its cost is nonnegative. |
| Greedy | `CERTIFIED` | Uses the geometric bound above. |
| Primal-dual | `HEURISTIC` | Its epsilon-adjusted dual updates are not exposed with a verified dual-feasibility certificate; no proof is inferred from the solver capability flag. |
| Local search | `CERTIFIED` | Uses the geometric bound above. |
| Simulated annealing | `CERTIFIED` | Uses the geometric bound above; stochasticity affects the incumbent, not this bound. |
| Genetic | `CERTIFIED` | Uses the geometric bound above; stochasticity affects the incumbent, not this bound. |
| LP rounding | `CERTIFIED` only when the LP backend reports `OPTIMAL` | The optimal LP-relaxation objective is a lower bound for the integer covering problem. Otherwise the LP value is labeled `HEURISTIC`; rounding/repair never proves optimality. |
| Shifting | `CERTIFIED` | Uses the geometric bound above; the shifting construction affects the incumbent, not this bound. |
| IP-KONT | `CERTIFIED` when the ILP backend supplies its valid best bound | The bound is the backend's global branch-and-bound best bound. A feasible incumbent or a time/gap stop does not by itself imply optimality. The fallback backend reports zero when it has no stronger bound. |
| Branch-and-bound | `CERTIFIED` | Uses its global frontier bound, derived from the accumulated assigned cost and per-point minimum remaining incremental costs. Optional LP strengthening is included only after an optimal LP solve. |
| Brute force | `CERTIFIED` | Full enumeration proves the optimum. On interrupted enumeration, the incumbent (if any) is feasible but is not an optimality proof; the retained lower bound zero follows from nonnegative disk-area costs. |

Exact solvers may report `OPTIMAL` only when termination proves it. A timeout
remains `TIME_LIMIT`, whether or not an incumbent has been found. Non-exact
solvers report at most `FEASIBLE`, even if a bound happens to equal the
incumbent; no heuristic method is upgraded to exact by comparing those values.

## Kinetic objectives

For MinMax, any certified static lower bound at a single time is a certified
lower bound on the kinetic peak objective. The solver carries only such bounds
into `certified_lower_bound`; heuristic progress estimates stay in the legacy
heuristic field.

MinMax minimizes the maximum instantaneous area over time. Candidate selection
therefore uses pointwise lower-envelope combination and peak comparisons; it
does not use integrated area to discard candidates. A certified continuous
verification establishes that the returned piecewise solution is feasible and
that its cost representation is consistent, but it is not a proof of global
MinMax optimality. A certified optimality gap is reported only when a feasible
kinetic upper bound and an independently certified lower bound are both
available.

For MinSum, `HEURISTIC_ADAPTIVE` performs no initial bound-sampling phase. At
each refinement it chooses the incumbent solution interval with the largest
integral contribution and solves statically at that interval's midpoint.
Refinements must pass sampled coverage/support checks before replacing the
incumbent. Stagnation patience and a scaled improvement tolerance prevent
repeated non-improving work. `CERTIFIED_BOUND` retains a separate sampled-bound-guided
path, but trapezoidal integration of sampled pointwise bounds does not prove
anything about the optimum between samples; that integral remains heuristic.
Both paths retain zero as an independent certified integral lower bound
because disk-area cost is nonnegative at every time. The solver does not
report a certified gap for MinSum: its kinetic refinement is not a global
optimality proof, and the sampled estimate is not a continuous-time
certificate.

Kinetic results additionally require a feasible incumbent before an upper
bound or certified gap is recorded. Continuous verification and the separate
peak-consistency check concern the returned solution; neither establishes
global optimality.

## Compatibility

Legacy `lower_bound` and `gap` fields remain available. Older benchmark JSON
without provenance is loaded with `bound_status: NONE`; certification is not
inferred from an old `lower_bound`. Legacy optimality status is treated as
`FEASIBLE` unless the record explicitly contains the new status fields.
