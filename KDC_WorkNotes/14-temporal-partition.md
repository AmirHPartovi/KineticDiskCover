# KDC Stage 14 - Temporal Partition Structures

Date: 2026-11-19
Repository: `/Users/amirpartovi/Desktop/Codes/KineticDiskCover`

## Added files

- `include/kdc/temporal_partition.hpp`
- `src/temporal_partition.cpp`
- `tests/test_temporal_partition.cpp`

No global branch-and-bound loop or splitting/evaluation policy was added.

## P/Q model

P is a map of all live leaves in the current partition.

Q is a set of leaf IDs that are OPEN and eligible for later structural
splitting.

The invariant is enforced by `TemporalPartition::validate()` and the static
`TemporalPartition::validate_partition(...)`.

## IntervalRecord fields

`IntervalRecordV1` includes:

- stable leaf ID;
- parent/left/right IDs;
- start, end, midpoint, delta, and depth;
- independent LB and UB records;
- LB/UB validity, method, arithmetic, and provenance;
- UB piecewise-radius program;
- Dminus/Dplus matrices;
- Lipschitz upper constant;
- status OPEN/FROZEN/FATHOMED;
- freeze reason;
- Oracle call summary and certificate IDs;
- precompute/bound/Oracle timings.

## Structural operations

`split_leaf`:

- rejects non-OPEN leaves;
- requires an interior split point;
- replaces the parent in P with left/right leaves;
- adds left/right to Q;
- removes the parent from Q;
- stores the old parent in history;
- records parent/left/right links.

`freeze_leaf` removes a leaf from Q and keeps it in P.

`fathom_leaf` requires finite valid LB=UB and removes the leaf from Q.

## Boundary ownership

Shared endpoints are allowed.

The boundary ownership policy is deterministic:

- `TemporalBoundaryOwnershipV1::LOWER_INTERVAL`.

Adjacent leaves share a boundary but never overlap in open interiors.

## Invariants enforced

- P is not empty;
- P covers [domain_start,domain_end];
- leaf lengths are positive and finite;
- adjacent boundaries are consistent;
- interiors are pairwise disjoint;
- every Q member is in P;
- every Q member is OPEN;
- FROZEN/FATHOMED leaves are removed from Q;
- FATHOMED LB=UB is finite and valid;
- empty P, invalid length, and inconsistent boundaries are rejected.

OPEN/FROZEN leaves remain in P and are never omitted from the partition.

## Tests

Tests cover:

- initial P/Q construction;
- structural split;
- split endpoint rejection;
- freeze;
- fathom equal LB/UB;
- fathom unequal LB/UB rejection;
- empty P;
- invalid leaf length;
- gaps and overlaps;
- Q subset and OPEN invariant.

## Build and test

The existing CMake source/test glob includes the new files.

Build and CTest passed.
