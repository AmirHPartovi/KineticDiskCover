# Performance and memory audit

## Evidence already available

The project has opt-in phase profiling (`KDC_PROFILE_PHASES`) instrumented for
candidate/precompute work, geometry and coverage matrices, trajectory
positions, model/LP/ILP work, kinetic events/extensions, combination,
verification, and serialization. `docs/final_validation_report.md` contains
historic measurements, but explicitly says its baseline used an unverified
backend and is not a valid before/after comparison. No new speedup claim is
made by this audit.

## Candidate areas for measurement

1. **Candidate and coverage construction:** `candidate.cpp` builds disk
   candidates, distance data, and coverage structures. Precomputation is
   cached on each `Instance` behind a mutex; cache validity and warm/cold
   workload should be included in future benchmarks.
2. **Trajectory evaluation and event work:** `kinetic.cpp` and `solution.cpp`
   repeatedly evaluate trajectories and search support/handover/event
   boundaries. Existing profiler counters can establish whether these are
   material on the current workload.
3. **Continuous verification:** `verify.cpp` is 1,079 lines and contains
   polynomial roots, interval cuts, and multiple consistency checks. Measure
   it separately from solver time before changing it.
4. **MinSum candidate refinement/combination:** `minsum.cpp` (884 lines) and
   `solution.cpp` (873 lines) handle repeated static solves, interval
   combination, and objective integration.
5. **Batch result retention:** `BatchRunner` accumulates records before
   master JSON/CSV/summary output; Python tools generally load whole JSON
   arrays into memory or a DataFrame. A full run matrix can grow with
   instances × algorithms × objectives × repeats.
6. **Visualization:** `plot_comparisons.py` creates many PNG/PDF pairs, and
   animation tools retain/render frames. The current profile can produce a
   large artifact set; artifact volume should be measured rather than
   suppressed or deleted.

## Safe optimization protocol

Record workload fingerprint, code commit, backend, CPU/toolchain, cold/warm
cache state, profiler phase counts and wall time before changing hot paths.
Use identical deterministic instances and configuration for comparisons.
Do not add process-global caches: the current precompute cache is instance-
scoped, and future cache work must retain explicit ownership/invalidation.
Use streaming JSONL aggregation as a storage scalability improvement, with
validation and lineage tests proving no records are lost.
