# Chapter 7 E6 ablation infrastructure

## Exact factor convention

Chapter 7, `Chapters/07_experimental_protocol.tex`, §E6 and
Table `factorial-configs`, explicitly defines the three bits in the order
`w,c,r`:

| Code | `w` warm start | `c` solve cache | `r` value-preserving reductions |
|---|---:|---:|---:|
| R000 | off | off | off |
| R100 | on | off | off |
| R010 | off | on | off |
| R001 | off | off | on |
| R110 | on | on | off |
| R101 | on | off | on |
| R011 | off | on | on |
| R111 | on | on | on |

No bit-order ambiguity was found. R111 is the protocol's shared B-ours/E1
configuration; this module defines configuration and pairing only and does
not execute or reuse an experiment.

The E6 `r` factor enables exactly station-row dominance and point-column
aggregation. The separate C0-C4 strengthening ladder is not folded into `r`.
Any other preprocessing setting, including the q cap, equal-row ordering,
objective cut, solver configuration, and duplicate-center symmetry handling,
must be identical across the eight arms and is represented by
`fixed_feature_configuration`. `apply_e6_configuration_v1` changes only the
two E6 reductions; it leaves the other options unchanged.

## Time accounting

Chapter 7 subsection `subsec:time-memory`, Eq. `loop-time-identity` and
Eq. `time-identity`, defines exclusive accounting:

```text
t_loop = t_pre + t_bound + t_oracle + t_other_loop
t_wall = t_loop + t_verify + t_post
```

`AblationTimingRecorderV1` measures the outer loop and wall spans with a
monotonic clock. The three named in-loop phases are separately timed;
`t_other_loop` is the measured loop remainder, not an estimate. Verification
is timed outside the loop; `t_post` accounts for the remaining wall span.
`t_reduce` is recorded as a subset of `t_pre` and is not added a second time.
Warm-start preparation and cache lookup/hit work belong inside `t_oracle`, as
E6's H6a/H6b endpoints are `t_oracle`. The recorder enforces both identities
in integer nanoseconds before exposing seconds.

The paired report includes `t_pre`, `t_bound`, `t_oracle`, `t_reduce`,
`t_loop`, `t_verify`, and `t_wall`. Row/column/event counts are retained as
separate diagnostic counters; they are never converted into a speed claim.

## Pairing and output semantics

`AblationPairContextV1` records the identical non-factor manifest fields
required to compare a pair: sample and input digest, replicate seed, source
revision and executable digest, machine/compiler/solver identity, model and
arithmetic mode, budgets, solver configuration, fixed feature configuration,
and sorted named parameters. The canonical run manifest adds the E6 code.
`compare_paired_ablation_runs_v1` rejects unequal pair contexts, duplicate
configurations, invalid timing identities, or output evidence that fails
validation. Consequently only paired runs with the same environment, input,
budget, and every non-factor setting can produce timing deltas. The helper
returns paired time differences only; it performs no statistical analysis
and claims no speedup.

`validate_e6_block_v1` additionally requires all eight distinct configurations
to share one valid pair context and to pass the same output-semantic checks
before the block can be treated as a complete factorial unit.

All configurations use the same `finalize_temporal_result_v2` status and
bound policy:

- Warm starts are only proposals. A used assignment must be revalidated on
  the target input; its feasibility can support a UB, never an LB.
- A cache hit must revalidate its payload and any claimed bound certificates.
  The cache key and stored validity are not upgraded by E6.
- Applied reductions must carry value-preservation evidence and a reconstructed
  assignment validated against the original input. The reduced matrix may
  select a different optimal assignment; identity equality is not required.
- Any valid UB additionally requires an assignment validated on the original
  input. The shared V2 finalizer remains responsible for LB/UB validity,
  assurance level, status, and stop-reason semantics in every arm.

Therefore output *meaning* is invariant, not necessarily its numeric contents:
under equal finite budgets a warm start or cache may find a tighter UB or
reach a different stop reason; valid reductions may return another
value-equivalent assignment. Such differences are valid only when the common
evidence checks pass. The infrastructure does not equate two time-limited
brackets or claim that all arms must produce identical LB/UB values.

## Scope

This change provides the eight named configurations, canonical paired
manifests, timing collection, semantic guards, and paired-delta gate. It does
not wire the factors into the temporal solver or Oracle, create datasets,
run E6, estimate speedups, or run large experiments.
