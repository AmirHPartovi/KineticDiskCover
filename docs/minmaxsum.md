# MinMaxSum objective

`minmaxsum` is a bi-objective kinetic optimization mode. It reports the pair
`(peak_cost, integral_cost)`, where `peak_cost` is the maximum instantaneous
cost over the horizon and `integral_cost` is the time integral of that cost.
There is deliberately no scalar `objective_value`: the two criteria remain
separate and auditable.

The mode runs the existing MinMax and MinSum solvers, then combines their
verified feasible solutions by taking their pointwise lower envelope. On each
interval where the source cost polynomials cross, the combined solution uses
the lower-cost source's assignment and support. The result is then checked
over the continuous horizon by the verifier.

Since the envelope's cost is no greater than either source at any time, the
joint result must satisfy both dominance checks:

* `peak_cost <= minmax_component_peak` and `peak_cost <= minsum_component_peak`
* `integral_cost <= minmax_component_integral` and
  `integral_cost <= minsum_component_integral`

A failed continuous verification or dominance check invalidates the joint
result. The full mathematical model and closure proof are in
[`minmaxsum_specification.md`](minmaxsum_specification.md).

## Status and certification

`OPTIMAL` is reported only when both component results certify their respective
global optima and the verified envelope satisfies the dominance invariants.
An exact zero component objective is independently certifiable because
disk-area costs are nonnegative. A verified envelope without both component
certificates is `FEASIBLE`; an exhausted shared time budget is `TIME_LIMIT`;
and a result with no usable feasible component or failed verification is
`FAILED`.

The default shared time budget is split evenly between MinMax and MinSum.
`--minmax-budget-fraction` and `--minsum-budget-fraction` set the respective
positive shares and must sum to one. MinSum can use its full cumulative
deadline, including unused time from the earlier MinMax phase.

Benchmark and batch outputs retain the vector costs, component execution and
optimality statuses, component objective diagnostics and certified gaps when
available, dominance flags, and source-run identifiers. For this mode,
scalar `objective_value` is null/blank rather than a misleading proxy.
Structured JSON stores the pair in `objective_vector` and the component
diagnostics in `joint`; CSV exports expose `objective_vector_peak_cost` and
`objective_vector_integral_cost`. These are additive schema-version-1 fields,
so historical MinMax and MinSum records remain valid and unchanged.

Managed-result validation requires finite peak and integral costs on feasible
joint results, continuous verification, consistent component-dominance flags,
and matching joint/run optimality statuses. The result pipeline checks the
component inequalities within the solver's configured numerical tolerance.
The smoke profile exercises all three modes on its selected small dataset;
the reference and full profiles retain their existing MinMax/MinSum defaults
and accept `--modes minmaxsum` or `--modes all`.

The reporting tools keep the two components separate: tables include a
dedicated `minmaxsum` vector table, comparison plots include peak-versus-
integral points, and animations show both peak cost and integral-per-horizon.
Because the vector has no scalar ordering, MinMaxSum animation selection uses
the fastest verified feasible run as a visualization tie-break, not as an
objective ranking.

The envelope formulation relies on cost-free switching between feasible
interval-local assignments. If switching costs, switching limits, or
cross-interval ownership constraints are introduced, closure must be
re-proved and this formulation may no longer be valid.
