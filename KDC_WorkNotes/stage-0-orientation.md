# KDC Stage 0 Orientation

Date: 2026-11-19
Repository: /Users/amirpartovi/Desktop/Codes/KineticDiskCover
Thesis: /Users/amirpartovi/Desktop/Texts/MScThesisPersian/main.pdf

## Git baseline

- Branch: main
- HEAD: 9d0bb0a4 (snapshot repair)
- Working tree: clean
- Staged changes: none
- Untracked files: none
- No source or data files were modified during orientation.

## Scope inspected

- README.md
- CMakeLists.txt
- docs/bound_semantics.md
- docs/kinetic_event_model.md
- docs/implementation_audit.md
- include/kdc/kinetic.hpp
- include/kdc/minsum.hpp
- include/kdc/solution.hpp
- src/minsum.cpp
- tests/test_minsum.cpp
- Thesis PDF text extracted with Ghostscript because pdftotext and Python PDF libraries are unavailable.

## Established repository state

- The code already has a separate `MinSumSolver` and kinetic event model.
- Model A is represented as fixed stations and continuous piecewise-linear moving points.
- The existing MinSum implementation is heuristic/adaptive and sampled-bound oriented; it does not claim global MinSum optimality.
- Exact static solvers do not, by themselves, certify kinetic MinSum optimality.
- Final continuous verification and certified continuous feasibility are implemented, but a continuous feasibility certificate is separate from an objective lower bound or exact optimum.
- `docs/bound_semantics.md` and `docs/implementation_audit.md` record that MinSum LB=0 is independently valid for nonnegative integral cost and that sampled/trapezoidal estimates are heuristic.

## Thesis extraction

- PDF size: 257 pages.
- Extracted full text and chapter ranges 5-8 into:
  - thesis.txt
  - thesis-ch5.txt
  - thesis-ch6.txt
  - thesis-ch7.txt
  - thesis-ch8.txt
- Ghostscript extraction is readable for English identifiers and formulas but has unreliable rendered order for Persian glyphs. It is not sufficient for final scientific interpretation of Persian prose.

## Open decision
No implementation stage was specified. An autonomous decision was made to stop at the orientation/audit boundary rather than invent a new public API or alter the architecture without an approved stage contract.

## Stage 0 acceptance

- PASS: Git baseline recorded precisely.
- PASS: Repository structure and relevant contracts inspected.
- PASS: Existing MinSum and static-solver architecture identified.
- PASS: Existing C++ baseline passed (CTest: 1/1 test target, 100%, 10.53 sec).
- PASS: Thesis chapter ranges 5-8 were extracted into the work-notes directory.
- BLOCKED: Final Persian-PDF interpretation is limited because Ghostscript glyph/order extraction is not a reliable source for unambiguous prose or formulas.
- PASS: No source or generated repository file was changed.

## Recommended next stage

1. Resolve the thesis ambiguity between the existing heuristic kinetic path and the Chapter 5 Model A algorithm contract.
2. Add the first thesis-aligned type-level separation for weighted static subproblems and D-(I)/D+(I) matrices, but only after the source/API conflict is recorded and approved.
3. Add targeted tests proving geometric D(t), D-(I), and D+(I) cannot be silently converted into one another.
