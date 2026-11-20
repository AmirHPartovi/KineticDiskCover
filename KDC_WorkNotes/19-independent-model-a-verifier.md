# Stage 19: Independent Model A snapshot verifier

## Scope and baseline

Implemented a file-based verifier for serialized Model A upper-bound snapshots. The verifier reloads the input instance and bound history, checks provenance and the stored program, and separates numerical checks from validated certification.

At stage start, the repository was on `main` with no tracked or staged diff. Temporal-engine files from earlier stages, including the verifier files being completed in this stage, were untracked. No GitHub/remote operations, staging, or commits were performed.

## Files read

- `include/kdc/model_a_snapshot_verifier.hpp`
- `src/model_a_snapshot_verifier.cpp`
- `tests/test_model_a_snapshot_verifier.cpp`
- `include/kdc/global_bound_history.hpp`
- `src/global_bound_history.cpp`
- `include/kdc/temporal_partition.hpp`
- `src/temporal_thesis_solver.cpp`
- `include/kdc/weighted_upper_envelope.hpp`
- `src/weighted_upper_envelope.cpp`
- `src/interval_evaluator.cpp`
- `CMakeLists.txt`
- `18-temporal-stopping-v2.md`

## Files added/changed

- `include/kdc/model_a_snapshot_verifier.hpp`
- `src/model_a_snapshot_verifier.cpp`
- `tests/test_model_a_snapshot_verifier.cpp`

The verifier API reloads both files instead of trusting evaluator caches. It validates the instance dimensions and trajectory representation, recomputes the instance hash, checks the selected snapshot's provenance/certificate references, validates full partition/program coverage and the lower-interval boundary convention, and independently checks assignments, radii, continuous coverage, empty stations, and cost against the stored UB. The first failure includes an interval and station/point identifiers when applicable.

Adversarial tests cover near-boundary ambiguity, repeated tangency, equal functions, duplicate trajectories, an envelope equality near an endpoint, zero distance/velocity, simultaneous breakpoints, shared-leaf boundaries, empty stations, insufficient UB, validated-mode refusal, and instance-hash mismatch.

## Decisions and assurance

- The V1 snapshot schema stores station radii per time piece, not arbitrary polynomial radius functions. The verifier interprets each stored vector as constant radii on that piece; overlapping station-sparse pieces compose by componentwise maximum, matching the schema's producer behavior.
- `FAST_FLOAT` reports `NUMERIC_PASS` only after numerical checks; its `long double` intermediates and one-ULP outward adjustments are not validated arithmetic or a certificate. Ambiguous signs/root ordering produce `INDETERMINATE`, never `PASS`.
- `VALIDATED` performs structural and provenance checks, then returns `NOT_CERTIFIED` because no validated arithmetic backend is available. `VALIDATED_PASS` is not issued.
- Hashing follows the temporal solver's existing canonical FNV-1a 64-bit instance identity. It checks consistency, not cryptographic collision resistance.
- Inspection found a coupled producer defect outside this verifier change: `WeightedUpperEnvelope::integrate_station_segment`'s single-assigned-point path stores the endpoint distance as a constant radius, rather than the maximum over the segment. For a point moving toward its station, that can produce an uncovered snapshot; this verifier rejects such a program. The producer was not changed in this verifier-only stage.

## Validation and acceptance

- `cmake --build build --target kdc-tests -j2`: PASS.
- `ctest --test-dir build --output-on-failure`: PASS, all tests.
- No trailing whitespace found in the three stage files.
- Acceptance: **PASS** for independent artifact reload, structural/program checks, numerical verification, conservative refusal on ambiguity, and non-issuance of certification without a validated backend.
- Limitation: this verifier cannot upgrade the V1 radius schema or FAST_FLOAT results to a continuous certificate. It only verifies the program represented in the snapshot; unsupported validated arithmetic remains an explicit blocker.

## Final Git state

The branch remains `main`; the stage files remain untracked along with prior-stage untracked temporal-engine files. No tracked/staged changes were introduced.
