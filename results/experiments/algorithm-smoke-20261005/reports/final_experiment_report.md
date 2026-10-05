# Development / smoke validation report

This is a smoke / development validation run and must not be interpreted as the full scientific benchmark.

## 1. Experiment configuration

- Status: **PARTIAL**
- Experiment ID: `algorithm-smoke-20261005`
- Reproduction command: `bash scripts/run_experiment.sh --pipeline smoke --output results/experiments/algorithm-smoke-20261005`
- Pipeline: `smoke` (development / smoke validation)
- Algorithms/profile: `all` / debug
- Objectives: minmax, minsum
- Seed/repeats/threads: 42 / 1 / 2

## 2. Dataset description

- Profile/source: `smoke10` / `data/instances/public_instance_set`
- Canonical instances: 10
- Fingerprint: `1e0ddb3329f4520e28b5080cc58b7e5b784b9a06f510f8fd3d778db3ef605fa9`
- Selection policy: select eligible small candidates first, maximizing family coverage, then fill by size; use oversized family representatives only when fewer than the requested count of valid in-limit instances exist
- Selected files:
  - `data/instances/public_instance_set/us-night-0000030.instance.mdc` (us-night, n=1, m=25)
  - `data/instances/public_instance_set/sbgdb-20200507-pntset-0000030.mdc` (sbgdb-20200507-pntset, n=1, m=25)
  - `data/instances/public_instance_set/sbgdb-20200507-fpg-poly_0000000030.mdc` (sbgdb-20200507-fpg-poly, n=1, m=25)
  - `data/instances/public_instance_set/uniform-0000030-2.mdc` (uniform, n=2, m=25)
  - `data/instances/public_instance_set/london-0000030.instance.mdc` (london, n=2, m=25)
  - `data/instances/public_instance_set/stars-0000030.instance.mdc` (stars, n=2, m=25)
  - `data/instances/public_instance_set/euro-night-0000030.mdc` (euro-night, n=2, m=25)
  - `data/instances/public_instance_set/att48.mdc` (att48, n=11, m=25)
  - `data/instances/public_instance_set/berlin52.mdc` (berlin52, n=13, m=25)
  - `data/instances/public_instance_set/eil51.mdc` (eil51, n=13, m=25)
- Source dataset fingerprint: `81450bc74e582372abac761376730e1fc786af37a25f9d2cbf988638fb06e721`

## 3. Environment

- Commit: `88c683f46e83540a43aaab82d355020b429b6a26` (dirty=True)
- Compiler/build: unreported / unreported
- Python: 3.9.6; host: macOS-27.2-arm64-arm-64bit

## 4. Exact-reference selection

- Requested: auto
- Selected: branch-and-bound
- Actual: branch-and-bound

## 5-11. FAST benchmark, per-objective results, runtime, quality, and reliability

Not available.


### Raw integrity

Not available.


## 12. Bound semantics

`certified_gap` is only meaningful for certified lower bounds and feasible upper bounds. Empirical ratios are not theoretical approximation guarantees.

## 13. Exact-reference evidence

{
  "calibration_instances": [],
  "successful_runs": [],
  "rejected_runs": [
    {
      "accepted": false,
      "backend": "ip-kont",
      "instance": null,
      "objective": "minmax",
      "rejection_reason": "dataset contains no valid calibration instance within the configured size cap"
    },
    {
      "accepted": false,
      "backend": "ip-kont",
      "instance": null,
      "objective": "minsum",
      "rejection_reason": "dataset contains no valid calibration instance within the configured size cap"
    },
    {
      "accepted": false,
      "backend": "branch-and-bound",
      "instance": null,
      "objective": "minmax",
      "rejection_reason": "dataset contains no valid calibration instance within the configured size cap"
    },
    {
      "accepted": false,
      "backend": "branch-and-bound",
      "instance": null,
      "objective": "minsum",
      "rejection_reason": "dataset contains no valid calibration instance within the configured size cap"
    }
  ],
  "runtime_statistics": {
    "branch-and-bound_median_sec": -1.0,
    "by_objective": {
      "minmax": {
        "branch-and-bound_median_sec": -1.0,
        "ip-kont_median_sec": -1.0,
        "paired_run_count": 0
      },
      "minsum": {
        "branch-and-bound_median_sec": -1.0,
        "ip-kont_median_sec": -1.0,
        "paired_run_count": 0
      }
    },
    "ip-kont_median_sec": -1.0,
    "paired_run_count": 0,
    "runtime_metric": "median solver total_time_sec over paired accepted runs for the same instance and objective"
  },
  "selection_rule": "native KONT/COPT API/license/runtime probe failed; selected the built-in branch-and-bound algorithm. Kinetic optimality certification is not used for backend availability.",
  "selected_backend": "branch-and-bound",
  "actual_backend": "branch-and-bound",
  "kont_runtime_available": false
}

## 14. Exact-animation results

Not generated.


## 15. Generated tables

None.

## 16. Generated figures

None.

## 17. Generated animations

None.

## 18. Reproduction command

`bash scripts/run_experiment.sh --pipeline smoke --output results/experiments/algorithm-smoke-20261005`

## 19. Failed/skipped stages

benchmark, result_validation, tables, figures, animations, final_report, summary

## 20. Remaining limitations

This report covers 10 dataset instance(s) only; it does not imply that other dataset inputs were executed.
Native KONT/COPT availability: unavailable or not used.
A visualization is not a feasibility or optimality proof. Exact animation is emitted only for results marked exact-reference, feasible, continuously verified, and OPTIMAL. Review `result_integrity_report.md` and per-run metadata for failed and timed-out cases.
