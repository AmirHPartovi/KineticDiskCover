# Stage 09 — Numeric Foundation Final Closeout

**Verdict: `PARTIAL_BLOCKED`**

The MPFR arithmetic layer and the PROMPT 09-FIX-1/2 corrections are present,
and the complete configured project build succeeded before the final test-only
edit. The last full Catch2 run failed one assertion; after correcting that
expectation, the binary was stale and no new full test run occurred. Therefore
the current source has no passing fresh full-suite/CTest acceptance result.
This verdict does not claim temporal-solver validation, `CERTIFIED`, or
`E0 PASS`.

## Repository state

- Repository: `/Users/amirpartovi/Desktop/Codes/KineticDiskCover`
- Branch: `main`
- HEAD: `d30b93704894461131b85d2f6b1cbc652a5879ad`
- Staged paths: none.
- Unstaged tracked paths:
  - `docs/thesis_implementation/numeric_arithmetic_adr.md`
  - `docs/thesis_implementation/theorem_to_code_map.md`
  - `include/kdc/numeric.hpp`
  - `results/comparison/experiment_manifest.json`
  - `results/logs/kdc.log`
  - `src/numeric.cpp`
  - `tests/test_numeric.cpp`
- Untracked paths:
  - `results/comparison/calibration-inputs/0_kdc-kinetic-comparison-846679563875.json`
  - `results/comparison/calibration-inputs/0_kdc-kinetic-comparison-846684138333.json`

The two `results` tracked files and two untracked calibration JSON files
appeared during the earlier full test execution. They were not changed or
deleted during this closeout. Their current SHA-256 values are recorded here
to identify the preserved files:

| Path | SHA-256 |
|---|---|
| `results/comparison/experiment_manifest.json` | `9e4e4c95ad73b3e71c1609fbafd87fd94d32cb9f03cc95517f43a28d7cc6d7c3` |
| `results/logs/kdc.log` | `6d18cf78b4478b575eb7a318438ef9c47e9796fe4e669d4cb488a24b4a0a638d` |
| `results/comparison/calibration-inputs/0_kdc-kinetic-comparison-846679563875.json` | `7385adebcd425074ab91ae59394b9e9f547d491ae75f7cf643cdcae016b06bf0` |
| `results/comparison/calibration-inputs/0_kdc-kinetic-comparison-846684138333.json` | `a823b372fedd0889d3c60931e3ec51cc7902d6f26e0419f0bc52dbb30b033d65` |

`git diff --check` passes. No branch, staging, commit, or remote operation was
performed. This report is new and does not overwrite the earlier closeout at
`/Users/amirpartovi/Desktop/KDC_WorkNotes/09-numeric-foundation-closeout-20261120.md`.

## Stage 09 closeout change history

Current worktree paths above are the exact outstanding tracked/untracked paths
relative to HEAD. The Stage 09 closeout sequence in this worktree includes:

- The Stage 09 MPFR foundation in the repository baseline: `CMakeLists.txt`,
  `include/kdc/numeric.hpp`, `src/numeric.cpp`,
  `tests/test_numeric.cpp`, the numeric ADR, and theorem-to-code map.
- FIX-1: the distinct identically-zero root-cover status and its tests/docs.
- FIX-2: removal of misleading move test coverage in favor of explicit copy
  construction, assignment, source-validity, and self-assignment checks, with
  ownership documentation.
- FIX-4A: the corrected directed binary64 overflow test expectation in
  `tests/test_numeric.cpp`.
- Test-run side effects in the manifest/log and the two calibration inputs
  listed above.

The complete configured build did pass in PROMPT 09-FIX-3. It ran:
`cmake --build build --parallel 1`, with all configured targets including
`kdc-core`, `kdc-solver`, the three benchmark executables, and `kdc-tests`.
That build preceded the FIX-4A test edit and is not a build of the final test
source.

## Root-cover semantics

`RootCoverStatus` assigns the prior values explicitly:

- `CANDIDATE_COVER_ONLY = 0`
- `BUDGET_EXHAUSTED = 1`
- `PRECISION_LIMIT = 2`
- `COMPLETE_COVER` remains an alias of `CANDIDATE_COVER_ONLY`
- `IDENTICALLY_ZERO_ON_DOMAIN = 3`

`root_cover_quadratic` identifies the special case only when all three
coefficient intervals compare equal to the exact singleton zero interval.
Intervals that merely contain zero compare as overlap and are not classified
as identically zero. The special case returns one interval exactly equal to
`[start,end]`, with status `IDENTICALLY_ZERO_ON_DOMAIN`; it bypasses
width-based subdivision, so the returned interval is not required to satisfy
`maximum_width`. For an identically-zero pairwise difference, the functions
coincide throughout the domain; this is not an ordinary isolated root event.

The focused tests cover the full `[0,1]` domain with `maximum_width=0.1`, a
point domain, an uncertain coefficient interval containing zero, and the
legacy alias/numeric values. Repository-wide search found no production
consumer, switch statement, or serialization path for `RootCoverStatus` or
`RootCoverResult`; callers are the implementation and tests. No code treats
the special status as root uniqueness, root ordering, or event completeness.
The candidate, budget, and depth/representability-limited statuses likewise
do not establish those properties.

## NumericInterval ownership

The actual class declares a copy constructor, the by-value
`operator=(NumericInterval other)` implemented with swap, and a destructor
that clears both MPFR values. It has no explicit move constructor or move
assignment; implicit move members are suppressed by its declared copy and
destructor members. Rvalues can bind through available copy semantics, but
there is no move optimization.

The test now named `NumericInterval copy construction and assignment preserve
precision` checks copied precision/endpoints, assignment from an object with a
different prior precision, source validity and unchanged endpoints, and
self-assignment. It does not claim or test move semantics.

These are lifetime/regression checks, not leak instrumentation: no
ASan/LSan/Valgrind run or MPFR allocation counter was used. The tests did not
report a crash or invalid access, but they do not independently prove absence
of leaks for every copy/assignment path.

## Arithmetic, serialization, and platform evidence

The numeric tests exercise directed addition/subtraction/multiplication/
division, squaring, square root, quadratic evaluation, interval comparisons,
root candidate coverage (including simple, tangent, and endpoint roots),
identically-zero behavior, integration, directed summation, `pi`, binary64
input, and decimal endpoint conversion/restoration. Underflow and binary64
overflow conversion are also covered by a test, but its corrected expectation
has not yet been compiled or run after FIX-4A.

An earlier targeted numeric run recorded 7 cases and 38 assertions passing;
that run included the endpoint decimal round-trip test. In the later full
run, the failed lower-conversion assertion occurred before the restoration
assertion in the same test case, so that restoration assertion was not reached
there. `lower_decimal()` and `upper_decimal()` produce outward-rounded strings
and `from_decimal_bounds()` restores an enclosure. There is **no versioned
JSON serialization schema or JSON serializer/deserializer for
`NumericInterval`**. These string methods do not resolve that gap.

Local dependency discovery via pkg-config reports MPFR 4.2.2 and GMP 6.3.0.
The existing build cache is Release / Unix Makefiles with
`FETCHCONTENT_FULLY_DISCONNECTED=ON`. Linux has not been tested.

## Build and test evidence

Keep historical results separate from final-closeout execution:

| Evidence | Actual outcome |
|---|---|
| PROMPT 09-FIX-3 full build | PASS before FIX-4A: `cmake --build build --parallel 1` completed all configured targets. |
| PROMPT 09-FIX-2 focused test run | PASS: 4 cases, 36 assertions, including the root-cover and copy-semantics tests as they existed then. |
| PROMPT 09-FIX-4 full direct Catch2 run | FAIL, exit code 1: 379 cases, 378 passed, 1 failed; 40,440 assertions, 40,439 passed, 1 failed. The failed expectation was that `lower_as_double()` throws for the positive out-of-range endpoint. |
| PROMPT 09-FIX-4 CTest | FAIL, exit code 8: registered target `all`, 1/1 failed with the same Catch2 result. |
| PROMPT 09-FIX-4A test-source correction | No build or test run, as required by that prompt. |
| Subsequent PROMPT 09-FIX-4 freshness check | `STALE_OR_UNVERIFIED_BINARY`: `build/kdc-tests` timestamp `19:23:14+0330` preceded `tests/test_numeric.cpp` at `19:29:23+0330`; tests were correctly not run. |
| This final closeout | No build or tests executed. `git diff --check` PASS. |

The full suite failure was in a test expectation, not a reported production
arithmetic failure: lower conversion uses `MPFR_RNDD` and can return the
largest finite binary64 value, while upper conversion uses `MPFR_RNDU` and
overflows to positive infinity. FIX-4A corrected the test, but no fresh
executable has yet verified the correction. Therefore the latest full Catch2
suite did **not** pass, and CTest did **not** pass.

## Remaining limitations and acceptance

- The current test source after FIX-4A has not been compiled or run.
- Full Catch2 and CTest both have a recorded failure from the pre-correction
  binary; there is no fresh passing result.
- JSON persistence for arbitrary-precision endpoints is not defined by a
  versioned schema.
- Linux compatibility remains unverified.
- `NumericInterval` primitives are not integrated into `D(t)`, `D^-(I)`,
  `D^+(I)`, Lipschitz bounds, the weighted Oracle, upper envelope, or temporal
  solver. The temporal solver is not validated or certified.

**Final verdict: `PARTIAL_BLOCKED`.** Acceptance criteria for
`FOUNDATION_READY_FOR_INTEGRATION` are not met because the corrected test has
no fresh execution and the latest full Catch2/CTest results failed.

**Next permitted development step:** rebuild the current tree so the
FIX-4A test source is included, then run the focused NumericInterval tests,
the complete Catch2 suite, and CTest while preserving or isolating
test-generated outputs. If all required checks pass, reassess this foundation
before any Stage 10 integration. Do not connect MPFR to temporal bounds or
begin E0, pilot, or benchmark work as part of this closeout.
