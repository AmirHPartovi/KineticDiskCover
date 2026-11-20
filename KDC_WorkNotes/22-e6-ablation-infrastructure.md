# E6 Ablation Infrastructure

## Status

PASS for the scoped ablation infrastructure. No large experiment, dataset run, benchmark, remote operation, or statistical analysis was executed.

## Baseline

- Repository: `/Users/amirpartovi/Desktop/Codes/KineticDiskCover`
- Branch: `main` (`upstream/main`)
- Before this stage: no staged changes and no tracked diff; prior-stage files were untracked and preserved.
- New files in this stage: `include/kdc/ablation.hpp`, `src/ablation.cpp`, `tests/test_ablation.cpp`, and `docs/thesis_implementation/chapter7_ablation_infrastructure.md`.
- Existing prior-stage files were read only; no existing source, test, or documentation file was edited.

## Thesis decisions

- Chapter 7 `07_experimental_protocol.tex`, E6 Table `factorial-configs`, explicitly specifies bit order `w,c,r`; no ambiguity was found.
- `R100` means warm start, `R010` cache, and `R001` value-preserving reductions. The reduction factor toggles only station-row dominance and point-column aggregation. The separate C0-C4 options and duplicate-center symmetry setting remain fixed across the E6 arms.
- `t_loop` follows the protocol identity exactly: `t_pre + t_bound + t_oracle + t_other_loop`. `t_wall` follows `t_loop + t_verify + t_post`. Warm-start/cache work is accounted within `t_oracle`; `t_reduce` is a separately reported subset of `t_pre`.
- Paired comparisons require an identical canonical non-factor manifest: input and sample identity, seed, source/binary identity, machine/compiler/solver, model/arithmetic, budgets, fixed features, and sorted parameters. The E6 configuration is the only allowed difference.
- Complete-block validation requires exactly one semantically valid run for each of the eight configurations under one shared non-factor manifest.
- Every run is finalized through the existing V2 result policy. The semantic guard rejects unvalidated warm starts, cache payload/certificates, reductions, or original-input assignments. Numeric LB/UB, assignment, or stopping outcomes need not be identical between time-limited arms; their validity/status meanings remain governed by the same checks.

## Validation

- Strict compiler check with `-Wall -Wextra -Wpedantic -Wshadow -Wconversion -Werror`: PASS.
- Focused E6 tests: PASS, 9 cases and 143 assertions.
- Full registered CTest suite: PASS.
- Tests cover bit order, all eight configs, reduction-only mapping, manifest mismatch rejection, exact time identities, semantic gating, and paired timing deltas. Count-only changes do not affect reported timing deltas.
- No benchmark or large experiment ran.

## Acceptance and risks

- Acceptance: **PASS** for configuration/mapping, timing, paired manifest gate, output-semantic validation, tests, and source-backed bit/time conventions.
- The infrastructure is not wired into the temporal solver/Oracle and does not execute E6. Future instrumentation must place warm-start/cache work inside the Oracle timing scope and produce the required run evidence/manifest.
- Variable/event reductions are separate counters and are not treated as speedups. No timing improvement is claimed.

Checkpoint: `/Users/amirpartovi/Desktop/KDC_WorkNotes/22-e6-ablation-infrastructure.md`
