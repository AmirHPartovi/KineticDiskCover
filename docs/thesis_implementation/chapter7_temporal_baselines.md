# Chapter 7 temporal baselines

The temporal methods are selected independently of the legacy static solver
registry. Their results use either the versioned temporal interval result or
`temporal_baseline_result_v1`; neither schema reinterprets the legacy static
result fields.

| Method | Implemented behavior | Output semantics and assurance |
|---|---|---|
| `b-grid` | Runs the interval bound machinery and always selects the longest eligible leaf. Equal lengths are ordered by smaller start time, then smaller stable leaf ID (Chapter 7 §7.3.2, Table 7.6). | Uses the temporal interval result schema. Its status and bounds retain the assurance actually produced by the interval solver; the strategy name alone does not imply certification. |
| `b-nn` | Computes each point's nearest station over every common linear-trajectory segment, inserts pairwise squared-distance equality roots, and selects the smaller station ID at ties (Chapter 7 §7.3.2). The assigned-point distance envelopes define the radius program. | `temporal_baseline_result_v1`; the integrated cost is an experimental `FAST_FLOAT_ONLY` MinSum program cost, not an official certified UB or LB. |
| `b-static-hold` | Solves the static instance at time zero when an exact Oracle is available, otherwise uses a feasible heuristic assignment; it holds that assignment while integrating each station's distance envelope (Chapter 7 §7.3.2). | `OPTIMAL_AT_ZERO` is used only when the exact Oracle supplies a certified optimality result. Otherwise the result is a candidate. The temporal cost remains `FAST_FLOAT_ONLY` and is not an official bound. |
| `b-minmax` | Solves MinMax on each `D^+(I)` matrix, constructs fixed squared radii from that assignment, and sums the resulting interval program costs over a gap-free common partition (Chapter 7 §7.3.3). | MinMax termination applies only to the auxiliary MinMax subproblem. The reported cost is a `FAST_FLOAT_ONLY` feasible-program candidate for MinSum; no MinSum optimum or lower bound is claimed. |

`COMPLETE` for a baseline means that its output program was structurally
constructed over the horizon; it does not mean continuously certified. The
baseline schemas preserve their method, cost role, numeric validity, static
solve or subproblem provenance, and program pieces. Markdown and CSV summaries
use method-specific columns so auxiliary MinMax values are not presented as
MinSum bounds.
