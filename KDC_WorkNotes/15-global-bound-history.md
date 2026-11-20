# KDC Stage 15 - Global Bound History

Date: 2026-11-19
Repository: `/Users/amirpartovi/Desktop/Codes/KineticDiskCover`

## Added files

- `include/kdc/global_bound_history.hpp`
- `src/global_bound_history.cpp`
- `tests/test_global_bound_history.cpp`

No global temporal branch-and-bound loop was added.

## Current partition bounds

`LB_cur` sums valid leaf LB values:

- invalid leaf LB contributes zero;
- LB_cur is not valid if any leaf LB is invalid;
- conservative/EXACT arithmetic modes use directed summation.

`UB_cur`:

- sums valid finite leaf UB values;
- becomes infinity if any leaf UB is invalid or absent;
- uses upward-directed conservative summation.

FAST_DOUBLE is not accepted as a certificate for global history.

## LB/UB history

LB history records:

- value;
- partition version;
- certificate IDs;
- provenance;
- sequence.

LB_best is the maximum valid recorded LB.

UB history records:

- value;
- partition version;
- certificate IDs;
- provenance;
- complete incumbent snapshot.

UB_best is the minimum valid recorded UB.

LB and UB best values may come from different partitions.

## Incumbent snapshots

A UB snapshot is accepted only when:

- all leaves have valid finite UB;
- every leaf has a piecewise-radius program;
- assignment/program pieces exist;
- the snapshot is complete.

The snapshot records:

- partition version;
- instance hash;
- solver/config/numeric-backend identity;
- verifier state;
- certificate IDs;
- every leaf UB;
- global UB;
- leaf assignment;
- time pieces and station-radius pieces.

## Certificate invalidation

`invalidate_certificate(id)` removes matching LB/UB history entries and
recomputes:

- LB_best;
- UB_best.

The snapshot is retained only if its UB entry remains certificate-valid.

## Serialization

History snapshots can be serialized to JSON and deserialized back.

Read-back restores:

- LB/UB history;
- snapshot identity;
- snapshot leaves;
- assignment/pieces;
- partition version;
- certificate IDs.

## Tests

The test suite covers:

- trivial LB and infinite UB;
- monotone valid LB;
- monotone valid UB;
- LB/UB best from different partitions;
- invalidation/recompute;
- no UB snapshot for incomplete UB;
- JSON serialization/read-back.

## Build and test

Build and CTest passed.
