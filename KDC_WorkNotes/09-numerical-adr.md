# KDC Stage 09 - Numeric and Validated Arithmetic ADR

Date: 2026-11-19
Status: Proposed / integration blocked
Scope: numerical design for future certified LB/UB work
Repository: `/Users/amirpartovi/Desktop/Codes/KineticDiskCover`

## Git and source baseline

- Branch: `main`
- Worktree: clean at the start of this stage, apart from earlier untracked
  KDC source additions that are outside this numerical stage.
- No source, test, CMake, dependency, or JSON file was changed.

## Chapter 5 numerical requirements

The thesis Chapter 5 requires the following numeric operations.

| Requirement | Thesis reference | PDF / printed page | Status |
|---|---|---|---|
| Lipschitz constants and interval slopes | Section 5.5.1, Equations (7.5)-(10.5) | PDF 137-140 / printed 121-124 | A |
| Midpoint interval, length, and lower/upper leaf bounds | Table 5.3 | PDF 136 / printed 120 | A |
| Conservative interval distance bound \(d^+(I)-d^-(I)\le L_{ij}(I)\Delta\) | Section 5.5.1 / consequence after Eq. (9.5) | PDF 138-140 / printed122-124 | A |
| Outward distance/constant comparisons | Section 5.5.1 | PDF 137-140 / printed121-124 | C |
| Lipschitz cone lower-envelope integral | Equations (11.5)-(13.5) | PDF 140-141? / printed124-125 | A |
| Upward/downward valid bounds for leaf integral | Table 5.3 and section 5.8.4 | PDF 136 and later / printed120 and later | A/C |
| Conservative polynomial/root processing | Continuous verifier sections 5.8.5 / Chapter 6.5 | PDF 183? / printed167-168 | C |

The source requires conservative numerical behavior, but it does not specify
an implementation library or exact machine precision. Directed rounding,
root enclosure, and decimal/binary input semantics are not defined by the
PDF. They are proposed engineering decisions below.

## Input semantics

### Current local data format

The existing JSON reader uses nlohmann/json:

- `src/io.cpp:63-64` calls `json.at(...).get<Value>()`;
- `Value` is `double`: `include/kdc/types.hpp:1-15`;
- nlohmann's JSON lexer converts a JSON decimal lexical number to
  `number_float_t` through `strtod`:
  `build/_deps/json-src/include/nlohmann/detail/input/lexer.hpp:921`.

Therefore the current implementation interprets coordinates and timestamps
as binary64 values after decimal JSON parsing. The exact original decimal
lexeme is not retained.

### Numerical ADR decision

**[PED] The current data contract is binary64.** For coordinates, timestamps,
and coefficients read by `DatasetReader`, validated arithmetic must construct
an enclosure from the parsed binary64 value, not from the original decimal
string.

In every validated operation, the enclosure starts from:

\[
[d,d]
\]

where `d` is the exact binary64 value produced by parsing. It must not
assume that `d` equals the human-visible decimal number.

A future decimal-input contract requires a schema change that:

- preserves the raw JSON token or decimal string;
- records decimal/rational interpretation explicitly;
- parses into a verified rational or arbitrary-precision value before
  converting to any binary64 enclosure;
- versioned compatibility must avoid silently changing existing datasets.

This remains a proposed engineering decision, not a thesis requirement.

## Existing numeric implementation

The current code uses ordinary binary64 operations.

Examples:

- trajectory interpolation:
  `src/kinetic.cpp:30-63`;
- support comparisons:
  `src/kinetic.cpp:58-70`;
- squared distance:
  `src/verify.cpp:196-260`;
- polynomial roots and bisection:
  `src/verify.cpp:216-350`;
- sqrt and disk radii:
  `src/stationary.cpp:132-155`;
- integration:
  `src/solution.cpp:116-202`;
- linear/binary64 result gap and tolerances:
  `include/kdc/istatic_solver.hpp:32-91`.

The existing root method is not a certified enclosure:

- linear roots use floating division;
- quadratic cuts use recursive derivative/root evaluation;
- bisection is finite and uses sign of floating values;
- tolerance equality is used for roots and comparisons;
- no outward rounding or exact input enclosure is retained.

This path is suitable only for FAST_FLOAT.

## Local numerical environment

| Component | Available | Version/check | Build form | License signal |
|---|---|---|---:|---|
| C++ compiler | Yes | Apple Clang 17 arm64 | `/usr/bin/c++` | Not applicable |
| CMake | Yes | 4.5.0 | Unix Makefiles | Not applicable |
| Eigen | Yes | 3.4.0 via CMake FetchContent | Header/header library | MPL2/GPL/BSD components; linkage policy needs per-build review |
| nlohmann/json | Yes | 3.11.3 | Header | MIT |
| GMP | Yes | 6.3.0 | `/opt/homebrew/include/gmp.h`, pkg-config | LGPL/GPL license scope requires review |
| MPFR | Yes | 4.2.2 | `/opt/homebrew/include/mpfr.h`, pkg-config | LGPL/GPL licensing requires review |
| Boost interval | Yes | Header in Boost installation | Header-only candidate | BSL-1.0 |
| Boost MPFR wrapper | Yes | Header | Needs GMP/MPFR at link | Wrapper BSL, underlying MPFR license |
| Boost MPFI wrapper | Hdr present | `boost/multiprecision/mpfi.hpp` | External `mpfi.h` missing | MPFI license not assessed |
| MPFI C library | No | `mpfi.h` not found | Blocked | Not available |
| FLINT | No | Header/library not found | Blocked | Not available |

## Selected backend

**[PED] Select a custom MPFR-backed interval pair as the VALIDATED backend.**

Reason:

- GMP/MPFR is locally available and linkable;
- MPFR provides directed rounding and multiple transcendental/elementary
  operations;
- MPFR can represent the exact binary64 input via `mpfr_set_d`;
- a pair of MPFR endpoints can implement outward-rounded interval addition,
  subtraction, multiplication, division, square, square root, and
  polynomial operations;
- it avoids silently relying on hardware epsilon or `nextafter` alone;
- no external interval package is required.

Use separate implementations:

```text
FAST_FLOAT:
  existing double calculations

VALIDATED:
  MPFR endpoints with directed rounding
  custom Interval<T> pair
  mpfr_const_pi for pi enclosure
  mpfr_sqrt for sqrt enclosure
  mpfr_add/sub/mul/div for interval endpoints
  explicit conservative polynomial/root/integral methods
```

## MPFR assurance level

Selected assurance is **operation-level validated arithmetic**, not an
automatic theorem-certifying backend.

It is appropriate for:

- LB/UB interval construction;
- interval coefficient storage;
- outward-rounded summation;
- endpoint ordering;
- quadratic and square-root enclosure;
- interval integral evaluation;
- conservative root separation.

It is not automatically sufficient for:

- unmodeled transcendental operations;
- backend dual certificates;
- exact MIP optimality;
- continuous geometric equality with no root/integral proof;
- assumptions not encoded in every call.

`CERTIFIED` issuance still requires the surrounding leaf and oracle
certificate conditions to be fulfilled.

## Operations to implement

### 1. Interval endpoints

Representation:

- left endpoint and right endpoint as MPFR values;
- precision fixed at construction and never silently lowered;
- provenance records the input binary64 or decimal policy.

### 2. Addition/subtraction

For [a,b] and [c,d]:

- lower = mpfr_add(a,c,MPFR_RNDD);
- upper = mpfr_add(b,d,MPFR_RNDU);
- subtraction lower = mpfr_sub(a,d,RNDD);
- subtraction upper = mpfr_sub(b,c,RNDU).

### 3. Multiplication

Enclose all four endpoint products and use:

- minimum under RNDD;
- maximum under RNDU;
- handle sign/zero/positive/negative sectors explicitly.

### 4. Division

Require a divisor interval not containing zero for ordinary division.

If 0 is in the denominator interval:

- return an unavailable interval;
- do not emit a bound.

Otherwise divide endpoint combinations with RNDD/RNDU.

### 5. Square

Compute:

- lower 0 if interval crosses zero;
- otherwise minimum of endpoint squares using squared directed rounding;
- upper maximum of endpoint squares using RNDU.

### 6. Square root

For [a,b] with b>=0:

- lower=max(0,a), then mpfr_sqrt(lower,RNDD);
- upper=sqrt(b,RNDU).

Negative-only intervals are unavailable.

### 7. Polynomial evaluation

Use Horner evaluation with directed endpoints.

For polynomial coefficients that are intervals:

- outward multiply and add at each step;
- keep degree and coefficient provenance;
- optionally use derivative recurrence for isolation.

No ordinary `double` Horner evaluation may be substituted for the validated
result.

### 8. Comparison and ordering

Intervals compare by:

- exact disjoint:
  - b < c -> first entirely below second;
  - d < a -> second entirely below first;
- overlap/containment -> UNORDERED/INDETERMINATE;
- reliable equality only if both intervals are exact and equal.

The default FAST_FLOAT comparison may use existing tolerances, but it cannot
return CERTIFIED.

### 9. Degree-at-most-two roots

Use:

- linear root:
  \(-a/c\) with interval bounds;
- quadratic:
  \((-b\pm\sqrt{b^2-4ac})/(2a)\);
- direct discriminant enclosure;
- reject/keep interval unknown if discriminant sign is indeterminate;
- split or bisection if sign/root isolation is not proven;
- every returned root set is an outward enclosure, not an approximated root.

The existing `double` bisection in `src/verify.cpp:216-350` remains
FAST_FLOAT.

### 10. Outward-rounded integral

For piecewise polynomial coefficients:

- split at enclosed roots/breakpoints;
- integrate each polynomial with MPFR endpoints;
- use RNDD for lower integral and RNDU for upper integral;
- sum leaf integrals only by live-PARTITION order;
- preserve local lower/upper and global directed totals separately.

The Phi/Lipschitz cone integral in Chapter 5 is not to be approximated with
`trapezoid` or a sample only.

### 11. Coefficient storage

For stored trajectory/polynomial coefficients:

- store original binary64 input plus MPFR exact binary64 enclosure;
- or store decimal lexeme plus exact/validated decimal conversion if the
  future decimal schema is adopted;
- store precision and arithmetic mode;
- never derive a validated result by re-rounding an existing FAST_FLOAT
  coefficient;
- serialize precision/source/input policy with bound provenance.

## Why current epsilon and nextafter code is insufficient

The existing code has:

- 64 epsilon comparison:
  `src/kinetic.cpp:58-70`;
- 1e-9 time and geometry tolerances:
  `src/io.cpp:18-20`;
- kBisectionIterations/1e-12 roots:
  `src/verify.cpp:216-350`;
- 1e-6 verification tolerance:
  `src/verify.cpp:1032-1110`;
- direct `sqrt/acos/cos/sin` calls:
  `src/io.cpp:250-254`, `src/stationary.cpp:132-155`,
  `src/verify.cpp:196-260`.

These are useful for FAST_FLOAT. They do not provide:

- exact input enclosure;
- operation-by-operation directed rounding;
- outward root enclosure;
- certificate validity for transcendental operations;
- an independent lower/upper proof.

## Dependency decision

### Selected

**MPFR/GMP via CMake `find_path/find_library`**

Pros:

- directed rounding;
- arbitrary precision;
- square root, polynomial operations, pi, and many transcendentals;
- macOS/Linux support;
- proven local build/runtime headers and libraries.

Cons:

- external native dependency;
- licensing review required;
- CMake integration absent;
- MPFR is not by itself an interval library;
- custom interval wrapper and tests are required.

### Not selected now

- MPFI C library: `mpfi.h` is missing.
- Boost interval: available header-only, but it is primarily hardware/double
  interval arithmetic and is not sufficient to replace arbitrary-precision
  MPFR proof.
- Boost MPFI wrapper: external MPFI missing.
- long double or one-ulp `nextafter`: insufficient for general certificates.
- exact rational-only library: FLINT unavailable and not currently needed.

## CMake integration proposal

This is a design only.

Future integration must:

1. use `find_path(MPFR_INCLUDE_DIR mpfr.h)`;
2. use `find_library(MPFR_LIBRARY mpfr)`;
3. use `find_library(GMP_LIBRARY gmp)`;
4. verify headers and libraries in one compile/link probe;
5. add them behind an explicit CMake option;
6. not enable VALIDATED if discovery/link/license probe fails;
7. report the backend in manifests and provenance;
8. keep them out of FAST_FLOAT builds unless explicitly enabled.

No CMake change or package download is made in this stage.

## Validated operation tests

Required tests:

- interval addition/subtraction:
  - identity;
  - sign sectors;
  - outward endpoints;
- multiplication:
  - positive/negative/mixed;
  - crossing zero;
- division:
  - denominator excluding zero;
  - denominator containing zero -> unavailable;
- square:
  - interval entirely negative/zero/crossing;
- square root:
  - negative-only unavailable;
  - outward endpoints;
- polynomial:
  - constant, linear, quadratic;
  - compare with exact rational/decimal examples;
- degree-one/two roots:
  - exact, tangent, no-real-root;
  - outward root encloses exact roots;
- interval ordering:
  - disjoint, overlapping, containment;
- integral:
  - constant, linear, quadratic;
  - signed intervals;
  - exact analytical comparison;
- summation:
  - local leaf sums;
  - global partition sum;
  - downward/upward accumulation.
- input semantics:
  - decimal JSON lexeme -> binary64 or decimal mode explicitly;
  - exact binary64 enclosure;
  - no silent conversion to decimal if binary64 contract is active.

## Limitations and blocker

The environment has MPFR/GMP and Boost headers, but VALIDATED mode is not
integrated into CMake or implemented as a repository arithmetic type.

Current blockers:

- MPFI external header/library is missing;
- no repository interval wrapper exists;
- no directed MPFR model exists;
- no certified coefficient storage exists;
- no outward root/integral implementation exists;
- no decimal-vs-binary64 input policy is persisted;
- no license approval or CMake dependency decision has been made.

Therefore:

- FAST_FLOAT may continue using double.
- VALIDATED result issuance is blocked.
- Existing epsilon tolerance cannot justify CERTIFIED.
- Existing scattered `nextafter` calls are not a certificate backend.

## Selected design decision

**[PED] Use FAST_FLOAT=existing double and VALIDATED=MPFR-backed interval
pairs behind explicit CMake discovery.**

No code or dependency is added in this stage.
