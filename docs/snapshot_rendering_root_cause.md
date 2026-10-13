# Forensic diagnosis: overlapping kinetic solution snapshots

## Scope and conclusion

This is a read-only diagnosis of
`results/experiments/kdc-smoke20-all-registered-final-20261012T074200/`.
No solver was run and no experiment artifact was changed. The only intended
write from this phase is this report.

The clutter is a combination of **too many legitimate overlays**, **metadata
that is both repeated and incorrectly decoded**, and **independently computed
panel viewports**. The most immediately visible layout failures are the
unwrapped run/status block in every axes, a legend inside every axes, and a
label on every moving point. Those elements collide with geometry and each
other, and `tight_layout()` plus `bbox_inches="tight"` then produce highly
variable, often very shallow PNG canvases. For some inputs the underlying
coverage disks also genuinely overlap; that is expected geometry, not by
itself a rendering error.

There is also a systematic metadata bug: `_render_solution_images()` passes a
flattened `rendered_record`, while `_status_label()` and `_result_metadata()`
expect a canonical run record with nested `result` and `verification`
objects. The resulting images report `PLANNED`, `feasible=False`,
`optimality=UNKNOWN`, and unavailable verification for real completed or
time-limited runs. This was visible in every representative image examined.

No support-point or station-indexing defect was found in the inspected path.
The static renderer does, however, calculate each requested time's limits
independently and includes artificial `(0,0)` and `(1,1)` anchors. It therefore
does not guarantee the same view or scale across a triptych and can include a
large empty region for translated, non-origin-centered coordinates.

## Rendering call graph and data contract

1. `visualize_experiment.generate()` reads the immutable `runs/plan.jsonl`,
   loads and validates run attempts, checks instance/objective plan coverage,
   and selects the latest attempt for each run key
   (`scripts/visualize_experiment.py:77-132, 469-525`).
2. For each selected slot it loads
   `input/<instance_id>.json`, resolves the run's
   `result.solver_result.solution_json_path` (or the default
   `result/solution.json`), requires a feasible result, and validates interval
   coverage, support and assignment vector lengths/ranges, and objective
   consistency (`scripts/visualize_experiment.py:526-613`).
3. It builds a flattened `rendered_record` from solver-result fields and
   metadata (`scripts/visualize_experiment.py:614-640`), then calls
   `_render_solution_images()` for `tau = 0, 0.5, 1`
   (`scripts/visualize_experiment.py:352-409`). Each individual-time image and
   each triptych axes calls `_draw_panel()` once.
4. `_draw_panel()` gets the active solution interval, positions from
   `animate_best.position_at()`, and each station radius from
   `animate_best.radius_at()`. The animation path uses the same
   `_interval_at()`, `position_at()`, and `radius_at()` geometry helpers
   (`scripts/animate_best.py:151-197, 299-474`).
5. `_write_comparison_sheet()` creates one HTML sheet per planned
   instance/objective group. It includes all algorithms, linking three panel
   images when available and retaining status/reason placeholders otherwise
   (`scripts/visualize_experiment.py:412-466, 650-715`).
6. The generator writes a slot/sheet manifest and Markdown report, indexes
   experiment artifacts, and calls `audit()` (`scripts/visualize_experiment.py:
   718-815`). `_write_bytes()` and the manifest/report writers use
   create-only writes; the artifact index records paths, sizes, and SHA-256
   hashes (`scripts/visualize_experiment.py:60-78, 786-814`;
   `scripts/kdc_tools/storage.py:468-495`).
7. `audit()` checks comparison-sheet coverage, artifact paths/sizes/hashes,
   solution source hashes, plan-slot coverage, and whether every currently
   renderable feasible solution is represented
   (`scripts/visualize_experiment.py:818-935`). It does **not** decode PNGs,
   assess image dimensions/layout, check text labels against canonical run
   status, or verify that axes limits are comparable. The visualization
   manifest is not registered in `scripts/kdc_tools/schemas.py:14-27`.

## Findings by suspected cause

| Suspected cause | Assessment | Evidence |
|---|---|---|
| Every full trajectory is drawn in every panel | **Confirmed contributor** | `_draw_panel()` plots every trajectory's full waypoint path in each of three panels (`visualize_experiment.py:272-285`). This is legitimate context, but becomes dense at `n=37`. |
| Every moving point has a text label | **Confirmed major contributor** | `P<index>/S<owner>` is annotated for every point (`visualize_experiment.py:298-309`). In the `rd100` examples, many labels collide with points, station markers, circles, and other labels. |
| Every station coverage disk is drawn | **Confirmed overlay; overlap can be genuine** | One translucent `Circle` and one station marker are drawn per station (`visualize_experiment.py:286-301`). Coverage disks are the actual solution geometry and can overlap; their presence is not evidence of a bug. The simultaneous 25 station markers and filled disks are visually heavy. |
| Support connectors/markers duplicate geometry | **Intentional, somewhat redundant styling; no indexing error found** | Each active support gets one station-to-support line and one open-circle outline over its moving point (`visualize_experiment.py:310-326`). The open ring is an intentional highlight, not a second moving point. The underlying station/support indices are range-checked and `radius_at()` uses the station's support index to calculate that station's radius (`visualize_experiment.py:183-215`; `animate_best.py:172-197`). |
| Legends and metadata intrude into geometry | **Confirmed major contributor** | Each panel places a legend at `lower left` (`visualize_experiment.py:345-349`), the full metadata block at the top-left axes coordinate, and verification at bottom-right (`334-344`). The block includes run ID and status and is a long, unwrapped text string. |
| Metadata repeated in panels and triptych title | **Confirmed** | Per-panel text contains instance, algorithm, objective, run, status, horizon/time, and objective; the figure-level title repeats instance, algorithm, objective, run, and status (`visualize_experiment.py:327-344, 392-396`). |
| `tight_layout()` plus `bbox_inches="tight"` | **Confirmed layout amplifier** | Both are applied to individual images; triptychs also use a figure title, `tight_layout(rect=...)`, and tight-bounding-box saving (`visualize_experiment.py:377-401`). The large axes-coordinate text and titles extend beyond their natural panel area. The actual PNG sizes vary drastically: individual panels range from roughly 800–950 pixels wide but only 114–228 pixels high in the samples below; triptychs are also unusually shallow in several cases. The exact Matplotlib contribution should be regression-tested after the layout is separated, rather than attributed to either option alone. |
| Limits recomputed independently at each time | **Confirmed** | `_draw_panel()` gets radii for the current time and computes new `xmin/xmax/ymin/ymax` on every invocation (`visualize_experiment.py:229-269`). For the actual `rd100`/`genetic`/`minmaxsum` run, x spans are 100.415, 104.813, and 113.172 units and y spans are 113.697, 109.967, and 115.451 at the three times. Max disk radii change from 15.660 to 26.444. A fixed 10-unit distance consequently occupies different pixel lengths across panels. |
| Artificial `[0.0]` and `[1.0]` anchors | **Confirmed viewport defect for translated data** | The extrema include `[0.0]` and `[1.0]` on both axes (`visualize_experiment.py:255-266`). The London input's actual geometry is wholly positive (`x≈9.99..105.36`, `y≈24.88..85.81`), yet the bounds are forced to include the origin and unit anchor. The observed London `tau-0.5.png` is only 891×133 pixels. More distant translations would waste still more viewport area. |
| Padding uses absolute coordinate magnitude | **Mostly ruled out; anchor interaction confirmed** | `pad` is 12% of the geometry's x/y spans, maximum current radius, or a one-unit floor (`visualize_experiment.py:249-254`); it does not directly use `max(abs(coordinate))`. However, the artificial origin/unit anchors extend the final bounds for translated geometry. Remove the anchors; compute padding from the geometry/disk extent. |
| Incorrect radius, support, station or point mapping | **No defect found in inspected path** | Supports are checked to be `-1` or a valid trajectory index; assignments are checked against station count (`visualize_experiment.py:183-215`). `radius_at()` maps each `station_idx` to that station and its `supporting_point[station_idx]` and computes their Euclidean distance (`animate_best.py:172-197`). Point marker color comes from its checked assigned-station index; support marker position comes from the checked support-point index. |
| Incorrect interpolation/active interval | **No defect found in inspected path** | `position_at()` uses `numpy.interp` over each trajectory's time breaks; `_interval_at()` selects the interval containing the requested time, including the final interval endpoint (`animate_best.py:151-170`). Solutions are checked for contiguous intervals spanning `[0,T_end]` (`visualize_experiment.py:183-215`). |
| Duplicated artists | **No accidental duplication found** | A new figure/axes is created for each output panel and each station, point, support line, and support ring is added once per axes (`visualize_experiment.py:272-349, 376-401`). Repeating the same geometric context in three panels is intentional, not repeated artist creation within a panel. |

### Run-label shape mismatch

The flattening at `visualize_experiment.py:615-640` stores `run_status`,
`feasible`, `verified`, `verification_kind`, and `optimality_status` at the
top level. But `_status_label()` at lines 153-166 calls `_result_metadata()`,
which only reads `record["result"]["solver_result"]` and
`record["verification"]` (lines 137-151); it then reads status from
`record["status"]` and feasibility from `record["result"]["feasible"]`.
Those keys are absent in the flattened object. `_objective_label()` therefore
also sees an empty solver dictionary. This explains the directly observed
`PLANNED; feasible=False; verified=False (NONE); optimality=UNKNOWN` image
labels for canonical records whose manifest says `COMPLETED` or `TIME_LIMIT`.
The `verification_kind` field passed to the animation is not the nested
verification object expected by `_status_label()` either.

The generator's manifest stores the correct run status and verification
metadata separately; the mismatch is in rendered text. The current
visualization audit does not compare the visible text to canonical record
fields, so it cannot catch this defect.

## Representative actual PNGs opened and examined

All listed files are from the target experiment. Dimensions and sizes were
read from the actual PNGs; run identity/status were cross-checked against the
visualization manifest and selected run records. Each `tau-0.5.png` is the
middle-time individual panel corresponding to the listed `three-time.png`.

| Objective | Input `(n,m)`; coordinate extent | Algorithm / true run status | Image | Dimensions; bytes | Run ID | Direct observation |
|---|---|---|---|---:|---|---|
| MinMax | `(1,25)`; x `[-39.24,27.52]`, y `[-35.75,40.19]` | `branch-and-bound` / `COMPLETED` | [`tau-0.5.png`](../results/experiments/kdc-smoke20-all-registered-final-20261012T074200/snapshots/sbgdb-20200507-fpg-poly_0000000030-7493750b7865-f90cd72a4c/minmax/branch-and-bound/run_20261012T042126Z__branch-and-bound__minmax__sbgdb-20200507-fpg-poly_0000000030-749__r00__f4a8a5958e70/tau-0.5.png) | 912×133; 21,289 | `run_20261012T042126Z__branch-and-bound__minmax__sbgdb-20200507-fpg-poly_0000000030-749__r00__f4a8a5958e70` | Very shallow image; per-axes metadata/legend compete with the plot. Rendered status says PLANNED although true status is COMPLETED. |
| MinMax | same input/run | same | [`three-time.png`](../results/experiments/kdc-smoke20-all-registered-final-20261012T074200/snapshots/sbgdb-20200507-fpg-poly_0000000030-7493750b7865-f90cd72a4c/minmax/branch-and-bound/run_20261012T042126Z__branch-and-bound__minmax__sbgdb-20200507-fpg-poly_0000000030-749__r00__f4a8a5958e70/three-time.png) | 2027×343; 47,068 | same | Three panels are compressed vertically; the long figure title and repeated panel labels dominate. |
| MinMax | `(37,25)`; x `[0.24,73.30]`, y `[0.09,73.63]` | `genetic` / `COMPLETED` | [`tau-0.5.png`](../results/experiments/kdc-smoke20-all-registered-final-20261012T074200/snapshots/rd100-9407ab41999f-b9917bc5f5/minmax/genetic/run_20261012T042107Z__genetic__minmax__rd100-9407ab41999f-b9917bc5f5__r00__089edcfbb0a7/tau-0.5.png) | 801×228; 44,511 | `run_20261012T042107Z__genetic__minmax__rd100-9407ab41999f-b9917bc5f5__r00__089edcfbb0a7` | Trajectory paths, stations/disks, 37 point labels, legend, and metadata heavily overlap. |
| MinMax | same input/run | same | [`three-time.png`](../results/experiments/kdc-smoke20-all-registered-final-20261012T074200/snapshots/rd100-9407ab41999f-b9917bc5f5/minmax/genetic/run_20261012T042107Z__genetic__minmax__rd100-9407ab41999f-b9917bc5f5__r00__089edcfbb0a7/three-time.png) | 1876×396; 100,226 | same | All three panels repeat the crowded annotations and false PLANNED status. |
| MinSum | `(1,25)`; x `[-39.24,27.52]`, y `[-35.75,40.19]` | `branch-and-bound` / `COMPLETED` | [`tau-0.5.png`](../results/experiments/kdc-smoke20-all-registered-final-20261012T074200/snapshots/sbgdb-20200507-fpg-poly_0000000030-7493750b7865-f90cd72a4c/minsum/branch-and-bound/run_20261012T042127Z__branch-and-bound__minsum__sbgdb-20200507-fpg-poly_0000000030-749__r00__adbfaee74290/tau-0.5.png) | 911×133; 21,248 | `run_20261012T042127Z__branch-and-bound__minsum__sbgdb-20200507-fpg-poly_0000000030-749__r00__adbfaee74290` | Same shallow layout and false run labels as MinMax. |
| MinSum | same input/run | same | [`three-time.png`](../results/experiments/kdc-smoke20-all-registered-final-20261012T074200/snapshots/sbgdb-20200507-fpg-poly_0000000030-7493750b7865-f90cd72a4c/minsum/branch-and-bound/run_20261012T042127Z__branch-and-bound__minsum__sbgdb-20200507-fpg-poly_0000000030-749__r00__adbfaee74290/three-time.png) | 2026×344; 47,233 | same | Geometry uses a small strip of the output height; panel and figure titles overlap visually. |
| MinSum | `(37,25)`; x `[0.24,73.30]`, y `[0.09,73.63]` | `branch-and-bound` / `TIME_LIMIT` | [`tau-0.5.png`](../results/experiments/kdc-smoke20-all-registered-final-20261012T074200/snapshots/rd100-9407ab41999f-b9917bc5f5/minsum/branch-and-bound/run_20261012T042924Z__branch-and-bound__minsum__rd100-9407ab41999f-b9917bc5f5__r00__2b6215fba68f/tau-0.5.png) | 845×177; 31,054 | `run_20261012T042924Z__branch-and-bound__minsum__rd100-9407ab41999f-b9917bc5f5__r00__2b6215fba68f` | A feasible time-limited incumbent is renderable, but the image falsely says PLANNED/ infeasible/unknown. |
| MinSum | same input/run | same | [`three-time.png`](../results/experiments/kdc-smoke20-all-registered-final-20261012T074200/snapshots/rd100-9407ab41999f-b9917bc5f5/minsum/branch-and-bound/run_20261012T042924Z__branch-and-bound__minsum__rd100-9407ab41999f-b9917bc5f5__r00__2b6215fba68f/three-time.png) | 1939×369; 66,076 | same | Repeats the same false status and crowded overlays in all panels. |
| MinMaxSum | `(1,25)`; x `[-39.24,27.52]`, y `[-35.75,40.19]` | `branch-and-bound` / `COMPLETED` | [`tau-0.5.png`](../results/experiments/kdc-smoke20-all-registered-final-20261012T074200/snapshots/sbgdb-20200507-fpg-poly_0000000030-7493750b7865-f90cd72a4c/minmaxsum/branch-and-bound/run_20261012T042127Z__branch-and-bound__minmaxsum__sbgdb-20200507-fpg-poly_0000000030-749__r00__096da43c7228/tau-0.5.png) | 950×114; 20,103 | `run_20261012T042127Z__branch-and-bound__minmaxsum__sbgdb-20200507-fpg-poly_0000000030-749__r00__096da43c7228` | Shallowest sample; objective vector prints as missing because the flattened solver fields are not read. |
| MinMaxSum | same input/run | same | [`three-time.png`](../results/experiments/kdc-smoke20-all-registered-final-20261012T074200/snapshots/sbgdb-20200507-fpg-poly_0000000030-7493750b7865-f90cd72a4c/minmaxsum/branch-and-bound/run_20261012T042127Z__branch-and-bound__minmaxsum__sbgdb-20200507-fpg-poly_0000000030-749__r00__096da43c7228/three-time.png) | 2036×511; 86,452 | same | Geometry is more legible than the shallow MinMax/MinSum examples, but each axes still repeats a long false-status block and legend. |
| MinMaxSum | `(37,25)`; x `[0.24,73.30]`, y `[0.09,73.63]` | `genetic` / `COMPLETED` | [`tau-0.5.png`](../results/experiments/kdc-smoke20-all-registered-final-20261012T074200/snapshots/rd100-9407ab41999f-b9917bc5f5/minmaxsum/genetic/run_20261012T042112Z__genetic__minmaxsum__rd100-9407ab41999f-b9917bc5f5__r00__88e868058b1e/tau-0.5.png) | 821×206; 39,323 | `run_20261012T042112Z__genetic__minmaxsum__rd100-9407ab41999f-b9917bc5f5__r00__88e868058b1e` | 37 point labels and colored coverage disks/markers create substantial visual congestion. |
| MinMaxSum | same input/run | same | [`three-time.png`](../results/experiments/kdc-smoke20-all-registered-final-20261012T074200/snapshots/rd100-9407ab41999f-b9917bc5f5/minmaxsum/genetic/run_20261012T042112Z__genetic__minmaxsum__rd100-9407ab41999f-b9917bc5f5__r00__88e868058b1e/three-time.png) | 1896×384; 89,066 | same | Repeated metadata and independent frame extents remain visible. |
| MinMax | London `(34,25)`; x `[9.99,105.36]`, y `[24.88,85.81]` | `branch-and-bound` / `COMPLETED` | [`tau-0.5.png`](../results/experiments/kdc-smoke20-all-registered-final-20261012T074200/snapshots/london-0000030.instance-0928f729f528-3bd5c0ea09/minmax/branch-and-bound/run_20261012T042044Z__branch-and-bound__minmax__london-0000030.instance-0928f729f528-3__r00__682c92c46558/tau-0.5.png) | 891×133; 19,995 | `run_20261012T042044Z__branch-and-bound__minmax__london-0000030.instance-0928f729f528-3__r00__682c92c46558` | Origin anchor expands the viewport below the all-positive input geometry; rendered label again contradicts the manifest. |

These 13 actual images cover all three objectives, individual panels and their
triptychs, a one-trajectory/25-station input and a 37-trajectory/25-station
input, differing coordinate extents, two algorithms, and both COMPLETED and
TIME_LIMIT outcomes.

## Independent artifact-inventory reconciliation

The filesystem and manifest were checked independently:

| Quantity | Manifest/plan | Observed on disk |
|---|---:|---:|
| Planned logical slots | 660 | 660 plan entries; 660 visualization slots |
| Renderable/generated slots | 502 | 502 `GENERATED` slots with solution and animation references |
| Ineligible slots | 158 | 158 explicit `INELIGIBLE` slots with reasons |
| Rendering failures | 0 | 0 `FAILED` slots |
| PNGs per renderable slot | 4 | Three `tau-*.png` plus one `three-time.png` |
| Snapshot PNGs | 2,008 | 2,008 present, nonempty, decodable, size/hash matched |
| Comparison sheets | 60 | 60 present, nonempty, size/hash matched |
| Comparison-sheet local links | — | 3,012 checked; all resolved |
| Visualization artifacts indexed | — | 2,570 panel/triptych/animation/sheet references checked; no missing or unindexed path |

`results/experiments/.../snapshots/` contains 2,068 files: 2,008 PNGs and 60
HTML sheets. The visualization report's 502 snapshots, 60 sheets, and zero
rendering failures are therefore accurate as counts. The 158 remaining
planned slots are intentionally ineligible (no feasible, valid solution
artifact), not missing snapshots. All 2,008 PNGs decoded successfully during
this audit; the renderer's own audit checks hashes and presence but does not
decode them or assess their appearance.

## Separate stale figure-status defect

This defect is independent of the PNG appearance:

- `scripts/run_experiment.sh:725-726` runs `report_experiment.py --stage
  figures` before `scripts/run_experiment.sh:736-740` runs the animations and
  the all-solutions visualization generator.
- At figure-generation time, `_plot_figures()` reads
  `reports/visualization_manifest.json` and uses
  `generated_solution_snapshots` to set `three_time_solution_figures`
  (`scripts/report_experiment.py:1574-1591`). In the fresh pipeline ordering,
  that manifest does not yet exist, so the figure status is recorded as
  `NOT_GENERATED`.
- The later `--stage final` call does not regenerate figures. Instead it
  preserves the existing `figure_status.json`
  (`scripts/report_experiment.py:1843-1851`).
- The actual snapshot inventory is a second status source. It says 502
  snapshots exist, while `figures/figure_status.json` says
  `NOT_GENERATED`. The figure entry also uses a visualization-manifest JSON
  path as `path_png`, not a PNG.

Thus the confirmed cause is **stage ordering plus a non-refreshing status
writer**, with duplicate status sources making the contradiction observable.
The figure checker only checks the referenced file's existence/nonempty size;
it does not reconcile the status against visualization slots.

## Precise fix plan

1. **Separate visual layers and annotation from the geometric axes**
   (`scripts/visualize_experiment.py::_draw_panel`, lines 218-349):
   keep full trajectories faint and thin as optional context; keep station
   markers and translucent coverage disks; draw support connectors/outlined
   support points only for the active interval. Remove default text labels
   from every moving point, or label only selected/support points. Put one
   shared legend outside the three geometry axes, not one legend per axes.
2. **Use a compact, truthful figure-level metadata block**
   (`_draw_panel()` and `_render_solution_images()`, lines 327-401): show
   instance, algorithm, objective, run ID, actual status, feasibility,
   verification kind, and optimality once in a reserved title/footer area.
   Keep panel-local titles to `tau` and actual time. Format MinMaxSum with its
   two real components; do not rely on generic/scalar objective text.
3. **Correct the record passed for display**: either pass the canonical run
   record to the helpers or give display helpers a typed, explicit metadata
   structure and make them read that structure consistently. Regression-test
   actual `COMPLETED`, `TIME_LIMIT`, `SKIPPED`, verification-kind, and
   MinMaxSum text against run records. The 502 existing images currently
   share the shape mismatch.
4. **Calculate one viewport for the whole solution** before drawing any panel:
   union station positions, all trajectory waypoints, and the maximum
   support radius over all solution intervals/times; add padding from that
   geometric extent only. Do not add artificial `(0,0)` or `(1,1)` anchors.
   Reuse identical `xlim`/`ylim` on all three axes and use equal aspect so
   spatial units are comparable across time. Keep margins stable and reserve
   layout space rather than combining unconstrained long text with tight
   bounding-box expansion.
5. **Keep animations consistent** (`scripts/animate_best.py::make_animation`,
   lines 299-457): avoid one legend entry per station when `m` is large, and
   include maximum disk radii in the scene limits so large coverage disks
   cannot clip. The animation frame updates already reuse artist objects and
   the shared geometry helpers; retain that pattern.
6. **Make visualization audit semantic** (`visualize_experiment.audit()`,
   lines 818-935): compare every slot's displayed status/verification and
   solution identity to its canonical run record; decode each PNG; check
   expected panel count, image dimensions, and common triptych limits; verify
   comparison-sheet links and retain explicit reasons for ineligible slots.
   Add a versioned visualization-manifest schema to `schemas.py` if the
   manifest remains a persistent contract.
7. **Refresh figure status after visualization**: reorder figure status/report
   generation after snapshots, or add a final reconciliation step that reads
   the completed visualization manifest and updates `figure_status.json` and
   `figures/REPORT.md`. Represent snapshots as manifest/artifact links rather
   than putting a JSON manifest in `path_png`. Make `check-figures` compare
   the reported count/state to the visualization manifest.

### Regression tests required

- A far-translated synthetic input (e.g. coordinates near `1e6`) must produce
  bounds around the actual scene, without an origin-induced empty region.
- Three axes for one solution must have exactly equal x/y limits and data-unit
  scale; exercise radii that vary between support intervals.
- Dense `(n,m)` scenes must remain within stable image aspect/dimension
  expectations and must not label every point by default; test legend and
  metadata placement independently from the geometry region.
- Rendered text must reflect canonical `COMPLETED` and `TIME_LIMIT` run
  records, feasibility, verifier kind, optimality, and both MinMaxSum vector
  components.
- Assert support station `j` uses `supporting_point[j]`, assigned point
  ownership is valid, and no duplicate artists are added while rendering a
  panel.
- Decode output PNGs in the audit; require exactly four snapshot PNGs per
  generated solution, correct checksums, three expected times, and valid
  local links in all comparison sheets.
- Test pipeline order from no visualization manifest through snapshot
  generation to final figure reporting; the final report must not preserve a
  stale `NOT_GENERATED` state after real snapshots exist.

Current tests in `tests/python/test_visualize_experiment.py:148-273` cover
time interpolation/boundaries, all three objective generation, comparison
rows, GIF fallback, missing solutions, checksum failure, and no-overwrite
behavior. They do not assert image geometry/layout, visible metadata
correctness, cross-panel limits, PNG decoding, or figure-status refresh order.

## Safe regeneration procedure for the existing experiment

Do **not** rerun `generate()` against the original directory: snapshot and
comparison outputs are create-only, so existing paths will collide, and
replacing them would destroy the current evidence. Do not rerun algorithms.

After the fixes are implemented:

1. Preserve the original experiment directory unchanged. Make a full,
   checksum-preserving copy under an isolated staging parent, keeping the
   copied directory basename exactly
   `kdc-smoke20-all-registered-final-20261012T074200` so the immutable run
   records' `experiment_id` remains valid.
2. In that copy only, omit the old `snapshots/`, `animations/`,
   `reports/visualization_manifest.json`, `reports/visualization_report.md`,
   and `artifacts.json` (or use a dedicated output-root option added by the
   fix). Keep the plan, canonical inputs, run records, and solutions
   byte-for-byte unchanged.
3. Run the corrected visualizer against the staged copy, then generate figure
   status/report after visualization. Run the visualization audit and
   independently verify decoded PNGs, checksums, common panel limits,
   statuses, and HTML links. Compare source solution hashes to the original.
4. Keep the original experiment and the regenerated copy as distinct,
   immutable evidence. If the new rendering is accepted, publish it as a new
   visualization revision or a new experiment artifact package; do not
   silently overwrite the original snapshot paths.

## Renderer fix implementation

The follow-up change in `scripts/visualize_experiment.py` implements the
diagnosed renderer corrections without changing solver results or regenerating
the target experiment:

- `_snapshot_bounds()` builds one square viewport for all three requested
  times from station centers, trajectory geometry, sampled positions and
  actual support-point radii. It has no origin/unit anchors and adds padding
  from the true spatial span.
- `_draw_panel()` keeps the shared limits and equal spatial aspect, reduces
  trajectory prominence, labels active supports by default, supports a
  deliberate all/none point-label option, and hides support connectors by
  default. Circles and station/point/support markers remain distinct layers.
- `_render_solution_images()` uses a reserved GridSpec layout with shared
  metadata and figure-level legends. It sends the canonical run record to
  title/status rendering, preserving status, feasibility, verification and
  objective-vector details.
- CLI options expose trajectory visibility, point-label policy and support
  connectors. The visualization manifest records renderer/schema versions,
  requested and actual times, common bounds, layer policies, opacities,
  line widths, layout, format and actual PNG dimensions. Snapshot paths and
  filenames remain unchanged; writes remain create-only.
- The focused tests cover translated geometry, shared square extent, support
  labels, canonical status text, all three objective outputs and dimensions
  parsed from the resulting PNGs.

Validation after implementation: `pytest -q tests/python` passed (123 tests).
Matplotlib image generation was exercised in temporary test directories. The
target smoke20 experiment was not regenerated or modified. For a future
visual comparison, use the isolated-copy procedure above and retain the
original artifacts as immutable evidence.

## Files and checks performed

Inspected `scripts/visualize_experiment.py`, `scripts/animate_best.py`,
`scripts/kdc_tools/storage.py`, `scripts/kdc_tools/schemas.py`,
`scripts/report_experiment.py`, `scripts/run_experiment.sh`,
`tests/python/test_visualize_experiment.py`, and the target experiment's
visualization manifest/report, figure status, final audit, run plan, selected
inputs, run records, solution files, snapshots, comparison sheets, and
artifact index.

Thirteen actual PNGs were opened. The PNG inventory and manifest references
were independently reconciled; all 2,008 PNGs decoded and matched recorded
size/checksum, all 60 comparison sheets and 3,012 local links resolved, and
2,570 visualization references were present in the experiment artifact
index. No tests or solver runs were executed in this diagnostic phase.
