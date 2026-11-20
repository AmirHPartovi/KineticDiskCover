# KDC Stage 16 - Temporal Split Policy

Date: 2026-11-19
Repository: `/Users/amirpartovi/Desktop/Codes/KineticDiskCover`

## Added files

- `include/kdc/temporal_split_policy.hpp`
- `src/temporal_split_policy.cpp`
- `tests/test_temporal_split_policy.cpp`

No main branch-and-bound loop or production main-loop splitting was added.

## Priority

For leaf I:

\[
priority(I)=UB_I-LB_I.
\]

Final deterministic tie key:

1. smaller depth;
2. larger delta;
3. smaller start;
4. smaller leaf ID.

The ID is the final reproducibility tie-breaker.

## Split

For I=[a,b]:

- \(t_c=(a+b)/2\);
- \(I_L=[a,t_c]\);
- \(I_R=[t_c,b]\).

Guards:

- Delta >= 2*Delta_min;
- tc strictly inside (a,b);
- tc is not a shared trajectory breakpoint.

Guard freeze reasons:

- MIN_INTERVAL;
- MIDPOINT_COLLAPSE;
- SHARED_BREAKPOINT.

## Transaction

Before split:

- parent record is retained unchanged;
- left and right IntervalEvaluator calls complete independently;
- inherited assignment comes from parent UB piecewise program;
- failure of one child UB freezes the parent and prevents P change.

After successful evaluation:

- children bounds and pieces are installed;
- P/Q state is structurally validated;
- parent is replaced in P and moved to history/FROZEN;
- children are inserted into P and eligible children into Q.

Failure reasons:

- ORACLE_FAIL;
- VERIFY_FAIL;
- PARTITION_INVALID;
- MIN_INTERVAL;
- MIDPOINT_COLLAPSE;
- SHARED_BREAKPOINT.

Missing LB is not itself a split failure: a valid UB allows splitting and the
child LB contribution becomes zero with a non-certificate trivial status.

## Fathoming

No classical LB-versus-global-incumbent fathoming is implemented.

FATHOMED remains available only through the temporal partition validator,
which requires finite valid LB=UB.

## Tests

Tests cover:

- priority tie key;
- minimum-length guard;
- shared-breakpoint guard;
- successful split P/Q update;
- structural rollback;
- missing child UB/ORACLE_FAIL;
- missing child LB/no block;
- reconciliation/VERIFY_FAIL.

## Build and test

Build and CTest passed.
