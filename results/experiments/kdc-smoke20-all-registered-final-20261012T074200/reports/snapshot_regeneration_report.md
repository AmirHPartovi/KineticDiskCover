# Snapshot regeneration report

- Target experiment ID: `kdc-smoke20-all-registered-final-20261012T074200`
- Renderer version: `2`
- Source code revision (Git HEAD): `e3ebc62443410975c261748d97a64300ac87f51e`
- Renderer source SHA-256 (working tree): `306156c578945fb327084903a16ea8bd9137f9852368d85d806392c88802f19f`
- Completed at (UTC): `2026-10-13T19:05:50.434379+00:00`
- Planned slots: 660
- Renderable solutions: 502
- PNGs regenerated: 2008
- Ineligible slots: 158
- Rendering failures: 0
- PNG validation: all staged images decoded with Pillow; dimensions, sizes, checksums, output paths and four-file-per-slot coverage verified.
- Solver execution, calibration, run-plan creation and animation generation: not performed.
- Solver run records, results, serialized solutions, traces, aggregates, tables and source inputs: not modified.
- Visualization manifest, report, global artifact index and snapshot figure status: refreshed transactionally.

## Ineligible planned slots

| Reason | Slots |
|---|---:|
| `NO_SOLUTION_ARTIFACT` | 158 |

## Validation

- Panels: 1506; triptychs: 502.
- Expected and decoded PNGs: 2008.
- Comparison sheets verified: 60/60 retained; all 3,012 existing relative PNG links resolve to nonempty regenerated files.
- Global artifact index: refreshed; 21,591 entries match the current files and checksums.
- Snapshot figure status: `GENERATED` for 502 verified triptychs; the stale status contradiction is cleared.
- Existing final smoke20 audit remains FAIL if unrelated solver-result integrity failures persist; snapshot status is reported separately.

## Visual review

- Contact sheet (outside the source tree): `/var/folders/w5/1mkrqmbs53lg9vy__m6vlyhh0000gn/T/kdc-smoke20-visual-review-1a17dk83/contact-sheet.png`; it combines three actual dense rd100 triptychs (MinMax, MinSum, MinMaxSum; 37 trajectories and 25 stations) and one separately labeled translated-coordinate regression fixture.
- Inspected the contact sheet and each actual objective triptych at full resolution. All three panels retain a common scale; status, verification and objective metadata remain visible; shared legend stays outside the geometry.
- Remaining trade-off: support-point labels are still numerous on dense 25-station cases and overlap locally, but are substantially fewer than labels for every moving point. Coverage disks overlap where the solution geometry overlaps. The translated fixture preserves the spatial arrangement and uses offset axis notation.
- PNG validation failures: none.
