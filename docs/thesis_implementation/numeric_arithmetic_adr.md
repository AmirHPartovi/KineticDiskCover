# Numeric arithmetic ADR

- **Status:** Accepted for the arithmetic foundation; not yet connected to
  official bound issuance.
- **Decision:** Use MPFR directed-rounding primitives behind a small
  `NumericInterval` API. Keep the existing `FAST_FLOAT` path independent.
- **Date:** 2026-11-20.

## Context and thesis requirements

Chapter 5, §5.1.1 requires exact arithmetic for exactly represented inputs or
interval arithmetic with directed rounding; ordinary floating-point results
without an enclosure are insufficient for certified bounds. Its input
preconditions additionally require every numerical operation used in a bound
to guarantee its error direction.

The operational requirements come from:

| Chapter 5 reference | Numeric operation required |
|---|---|
| §5.2.2, Eqs. (5.5)-(6.5), and §5.2.3 | Check dual feasibility with enclosures; multiply a valid dual objective by `pi` and round a lower bound downward. A primal LP objective is not an LB. |
| §5.5, Eqs. (7.5)-(13.5) | Enclose distance extrema, norms, velocity bounds, `Lambda`, midpoint values, and `Phi`; lower-bound inputs round downward and upper-bound inputs upward. |
| §5.3 and §5.5, interval bounds | Construct `D^-(I)` no higher than the true minimum and `D^+(I)` no lower than the true maximum; preserve the inequality under weighted objectives and interval-length multiplication. |
| §5.3, Proposition 5.7; §5.8.5 | Verify polynomial coverage over complete pieces and enclose the integral of a feasible program from above. |
| §5.3, Proposition 5.8, especially “Numerical accuracy of roots” | Enclose every relevant real root, certify root ordering and active-function comparisons, or use a conservative integral enclosure; unresolved ordering must not be reported as certified. |
| §5.4.2, aggregation; §5.7, contradiction recovery | Sum local lower bounds downward and local upper bounds upward. Re-evaluate contradictions without clamping. |

These are not interchangeable requirements: correct primitive rounding does
not by itself certify root ordering, a weighted Oracle result, or the complete
temporal program.

## Input-number semantics

The current data path defines `Value = double` in `include/kdc/types.hpp`.
`DatasetReader` in `src/io.cpp` parses JSON with nlohmann JSON and reads
coordinates, `t_breaks`, and `T_end` as `double`. The Chapter 7 generator
registry also specifies IEEE-754 binary64 for `FLOAT64` and round-trip JSON
numbers. The thesis permits exactly represented input or certified interval
arithmetic, but does not require decimal-real interpretation of JSON tokens.

Therefore the existing JSON contract is **the exact binary64 value produced by
the current parser**. The validated layer imports that value with MPFR's
`mpfr_set_d`; at precision at least 53 bits, every finite binary64 input is
represented exactly. Its enclosure must contain that binary value, not the
short decimal spelling in the JSON file. The layer's `from_decimal_bounds`
function is explicitly for decimal interval endpoints, not for silently
changing the existing dataset contract. A future exact-decimal input format
must preserve and parse each raw JSON number token directly into MPFR before
any conversion to `double`, and must use a versioned schema.

Serialized MPFR endpoints are decimal **strings** produced with directed
rounding. Re-importing the lower and upper strings rounds outward, so it
cannot narrow the stored enclosure. The current decimal endpoint comparator
supports exponent magnitudes up to one million; non-finite input values and
non-finite arithmetic results are rejected.

## Backend investigation and selection

The local environment was inspected without installing or downloading
anything:

| Item | Observed |
|---|---|
| Host/compiler | macOS 27.2.1, arm64, Apple Clang 17.0.6 |
| Build system | CMake 4.5.1; project minimum CMake 3.20; C++17 |
| MPFR | 4.2.2 installed; pkg-config module `mpfr`; LGPL-3.0-or-later |
| GMP | 6.3.0 installed and linked by MPFR; LGPL-3.0-or-later or GPL-2.0-or-later |
| Boost | 1.92.0 installed; Boost Software License 1.0; headers available |
| MPFI | Not installed |
| Probe | Apple Clang compiled and linked MPFR; `sqrt(2)` lower/upper endpoints were produced with `MPFR_RNDD`/`MPFR_RNDU`; importing binary64 `0.1` yielded its exact binary value. |

MPFR was selected over Boost.Interval because its elementary functions and
arithmetic explicitly accept rounding modes and use correctly rounded
arbitrary-precision operations. Boost.Interval is a viable alternative for
standard interval types, but its platform rounding policy and compiler/FENV
requirements would need separate validation, and it does not supply the
quadratic root-cover and storage contracts required here. MPFI would be
another candidate but is absent locally. MPFR and GMP are mature C libraries
available on macOS and Linux; only the current macOS toolchain was compiled
and tested here, so Linux compatibility is an expected platform property,
not a result of this local run.

CMake finds MPFR through the existing `pkg-config` integration and links its
transitive GMP dependency. The minimum accepted API version is MPFR 4.0.
MPFR was already installed locally; it was not installed or downloaded for
this change. The repository's existing CMake configuration uses FetchContent
for its other dependencies. A first configure attempt reached that existing
populate path and failed with a local process-spawn resource error; after
confirming the dependency source directories were present, configuration
succeeded with `FETCHCONTENT_FULLY_DISCONNECTED=ON`. The first attempt's
FetchContent refresh behavior was not independently audited. A build without
the MPFR development package must fail configuration rather than claim
validated support; there is no automatic fallback to unvalidated arithmetic.

## API and assurance boundary

`include/kdc/numeric.hpp` and `src/numeric.cpp` provide:

| Operation | Enclosure method | Current assurance |
|---|---|---|
| Addition/subtraction | MPFR lower endpoint with `RNDD`, upper with `RNDU` | Validated primitive |
| Multiplication/division | Evaluate all endpoint combinations in both rounding directions; division rejects a denominator containing zero | Validated primitive |
| Directed conversion | Binary64 imports are enclosed via `mpfr_set_d`; finite endpoint exports use directed `mpfr_get_d`; decimal endpoint strings are formatted outward | Validated primitive; export fails explicitly on binary64 overflow |
| Square/square root | Sign-aware endpoint square; correctly rounded MPFR square root; negative domains rejected | Validated primitive |
| Polynomial evaluation | Square-aware expanded interval evaluation for degree at most two | Validated interval enclosure; dependency may make it wide |
| Interval ordering | Return LESS/GREATER only for disjoint ordered intervals; overlap remains explicit | Validated comparison |
| Quadratic roots | Bisection with centered mean-value enclosures. Returned boxes conservatively cover every possible root; budget/depth limits are explicit. | Conservative root cover, **not** uniqueness or root-order proof |
| Polynomial integration | Outward interval evaluation of the exact quadratic antiderivative over finite binary64 endpoints | Validated enclosure for the supplied coefficient intervals and represented interval |
| `pi` and summation | MPFR directed `pi`; aggregate interval addition with endpoint-directed rounding | Validated primitive |

`FAST_FLOAT` remains ordinary `double` and is exploration-only. The new
interval primitives form the initial `VALIDATED` arithmetic layer. They do
**not** yet issue certified Model A bounds: existing geometry, interval
evaluation, the weighted Oracle/dual verifier, active-envelope ordering, and
snapshot verification have not been migrated to consume these enclosures.
Accordingly, the current overall assurance for solver output remains
`NUMERICAL_ONLY`; `CERTIFIED` must remain unavailable.

The quadratic root routine deliberately returns conservative candidate
regions, not one proven-isolating interval per distinct root. If active
function order cannot be proved from disjoint root/comparison enclosures, the
future envelope implementation must increase precision, compute a conservative
integral bound, or report `INDETERMINATE`/not certified.

## Validation evidence and limitations

`tests/test_numeric.cpp` checks outward arithmetic, division-by-zero and
negative-square-root rejection, square/root/polynomial enclosures, interval
comparison, simple and tangential roots, root-cover budget exhaustion,
quadratic integration, `pi`, aggregate sums, binary64-versus-decimal semantics,
and outward endpoint serialization.

These tests validate selected API behavior; they are not proofs that all
possible execution paths, compilers, MPFR builds, or temporal certificates are
correct. The root cover can over-approximate heavily, intervals can widen due
to dependency, and the layer does not yet represent the original input
lexemes or propagate interval-valued coordinates through trajectory parsing.
The exact data model is binary64 as parsed, as stated above.

No existing official bound implementation is upgraded by this ADR. Wiring
each `D^-`, `D^+`, Lipschitz, Oracle, envelope, integration, and aggregation
path to this API is a separate correctness change, with its own tests and
independent verification.
