# Chapter 6 Optimizations

## Status

PASS for the scoped standalone weighted preprocessing, warm-start, and cache utilities. No benchmark or remote operation was run. No reduction was connected to a live solver or temporal loop.

## Baseline

- Repository: `/Users/amirpartovi/Desktop/Codes/KineticDiskCover`
- Branch: `main` (`upstream/main`)
- Before this stage: no staged changes and no tracked diff; prior-stage temporal files and docs were untracked and preserved.
- New stage files: `docs/thesis_implementation/chapter6_optimizations.md`, `include/kdc/weighted_optimizations.hpp`, `src/weighted_optimizations.cpp`, `tests/test_weighted_optimizations.cpp`.
- Build outputs are under the ignored `build/` directory.

## Source review

- Chapter 6 source: `~/Desktop/Texts/MScThesisPersian/Chapters/06_Static_Implementation.tex`.
- Weighted API: `include/kdc/weighted_static.hpp`, `src/weighted_static.cpp`.
- Solver and build context: `include/kdc/weighted_oracle.hpp`, `CMakeLists.txt`, `scripts/local_validation.sh`, `README.md`.
- Existing theorem map and tests were read; they were not edited.

## Decisions

- Created an independent proof-obligation table that separates `D(t)`, `D^-(I)`, and `D^+(I)`, records value-versus-assignment preservation, and limits each rule to the exact input on which it is checked.
- Added opt-in flags, defaulting off, for squared-radius caps, exact equal-row symmetry groups, the objective lower cut, station-row dominance, and point-column dominance. The matrix and original-index maps are retained; reduced matrices do not alter source instance or station serialization.
- Did not implement incumbent-based variable fixing because the available path has no independently validated same-matrix incumbent-cost certificate.
- Added a structurally validated assignment factory and a warm-start rebuild that checks identity/order, revalidates the target assignment, recomputes station maxima and cost, and has no lower-bound field.
- Added an in-memory cache whose exact key includes matrix dimensions and binary64 contents, instance and matrix/time scope, objective/model, backend/version, arithmetic mode, solver and feature configuration, and sorted named parameters. Cached certificate IDs require independent revalidation; cache hits do not alter recorded validity.
- The existing solver API has no MIP-start or general solve-cache integration point, so these remain standalone helpers. No speedup is claimed.

## Validation

- Strict standalone syntax compilation of weighted sources: PASS.
- Focused weighted tests: PASS, 21 test cases and 2,108 assertions.
- Repository CTest suite: PASS, 336 test cases and 39,965 assertions.
- Small exhaustive reference checks cover all 2x2 matrices over `{0,1,2}` and binary 2x3/3x2 matrices for station/point dominance and their composition.
- The full suite initially exposed one incorrectly specified cache test; the test was corrected to assert the intended missing-evidence-ID rejection, then the full suite passed.
- No benchmark/large-instance run was performed.

## Risks and acceptance

- The utilities are matrix-level and do not prove continuous dominance over a time interval. Temporal application requires the separately documented whole-interval proof.
- `FAST_FLOAT` assignment costs and cache evidence are not validated-arithmetic certificates. The cache's certificate verifier is an interface; its correctness belongs to the caller's independent verifier.
- The older untracked theorem map still describes general reduction/cache replay support as not implemented. It was not edited under the pre-existing-change protection rule; this new document supersedes that entry for the scoped standalone helpers and preserves the limitation that they are not integrated.
- Acceptance: **PASS** for the requested proof table, feature-gated matrix helpers, warm-start validation, exact contextual cache key/provenance, and focused tests. **Not claimed**: live solver integration, validated numerical certificates, benchmark speedups, or continuous-time reductions.
