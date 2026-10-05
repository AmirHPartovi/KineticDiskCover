# Development / smoke validation report

This is a smoke / development validation run and must not be interpreted as the full scientific benchmark.

## 1. Experiment configuration

- Status: **COMPLETE**
- Experiment ID: `algorithm-smoke-20261005-complete`
- Reproduction command: `bash scripts/run_experiment.sh --pipeline smoke --output results/experiments/algorithm-smoke-20261005-complete`
- Pipeline: `smoke` (development / smoke validation)
- Algorithms/profile: `nn, greedy, primal-dual, local-search, sa, genetic, lp-rounding, shifting, branch-and-bound` / debug
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

# Smoke / development validation summary

This is a smoke / development validation dataset, not the full scientific benchmark.

## Dataset summary

- Profile: smoke10
- Instances: 10
- n range: 1 to 13
- m range: 25 to 25
- T_end range: 1 to 1
- Families: att48, berlin52, eil51, euro, london, sbgdb, stars, uniform, us

## Runtime and quality statistics

Runtime uses `solve_time_sec` when present, otherwise `wall_time_sec`; quality metrics are not filtered to successful rows.

| Algorithm | Objective | Runs | Runtime median | Q1 | Q3 | Min | Max | P95 | Objective median | Lower-bound median | Upper-bound median | Bound status | Certified-gap median | Heuristic-gap median | Empirical ratio median | Feasibility | Verification | Timeout | Failure | Iterations median | Static solves median |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| branch-and-bound | minmax | 10 | 9.7709e-05 | 7.79372e-05 | 0.00369064 | 6.2917e-05 | 0.012064 | 0.0118008 | 8199.82 | 1160.37 | 8199.82 | CERTIFIED | 6.67055 | N/A | N/A | 100.0% | 100.0% | 0.0% | 0.0% | 1 | 2 |
| branch-and-bound | minsum | 10 | 0.000496729 | 0.00019348 | 0.0398303 | 0.00014175 | 0.155895 | 0.143412 | 628.813 | 0 | 628.813 | CERTIFIED | N/A | N/A | N/A | 100.0% | 100.0% | 0.0% | 0.0% | 4.5 | 10 |
| genetic | minmax | 10 | 0.0275542 | 0.0258071 | 0.0352418 | 0.025437 | 0.0401768 | 0.0385965 | 8199.82 | 702.873 | 8199.82 | CERTIFIED | N/A | N/A | N/A | 100.0% | 100.0% | 0.0% | 0.0% | 1 | 2 |
| genetic | minsum | 10 | 0.155656 | 0.121908 | 0.238226 | 0.087167 | 0.330397 | 0.311096 | 640.685 | 0 | 640.685 | CERTIFIED | N/A | N/A | N/A | 100.0% | 100.0% | 0.0% | 0.0% | 6.5 | 12.5 |
| greedy | minmax | 10 | 6.9104e-05 | 6.23542e-05 | 0.0011676 | 5.5417e-05 | 0.00243892 | 0.00218476 | 9119.63 | 702.873 | 9119.63 | CERTIFIED | N/A | N/A | N/A | 100.0% | 100.0% | 0.0% | 0.0% | 1 | 2 |
| greedy | minsum | 10 | 0.000481501 | 0.000150177 | 0.0206482 | 0.000110459 | 0.0779577 | 0.0595932 | 632.457 | 0 | 632.457 | CERTIFIED | N/A | N/A | N/A | 100.0% | 100.0% | 0.0% | 0.0% | 4 | 10 |
| local-search | minmax | 10 | 0.000102104 | 9.5907e-05 | 0.00144458 | 8.2125e-05 | 0.00214033 | 0.00203153 | 8199.82 | 702.873 | 8199.82 | CERTIFIED | N/A | N/A | N/A | 100.0% | 100.0% | 0.0% | 0.0% | 1 | 2 |
| local-search | minsum | 10 | 0.000676895 | 0.000275959 | 0.00906895 | 0.000213792 | 0.0626917 | 0.0472419 | 632.457 | 0 | 632.457 | CERTIFIED | N/A | N/A | N/A | 100.0% | 100.0% | 0.0% | 0.0% | 6.5 | 12.5 |
| lp-rounding | minmax | 10 | 0.000201417 | 0.000140989 | 0.00397333 | 0.000118417 | 0.008467 | 0.00706281 | 8199.82 | 1160.37 | 8199.82 | CERTIFIED | N/A | N/A | N/A | 100.0% | 100.0% | 0.0% | 0.0% | 1 | 2 |
| lp-rounding | minsum | 10 | 0.000910459 | 0.000490489 | 0.0371938 | 0.00028725 | 0.129833 | 0.11108 | 628.813 | 0 | 628.813 | CERTIFIED | N/A | N/A | N/A | 100.0% | 100.0% | 0.0% | 0.0% | 4.5 | 10 |
| nn | minmax | 10 | 6.68965e-05 | 6.02292e-05 | 0.000792687 | 5.1667e-05 | 0.00218458 | 0.00178448 | 7981.23 | 0 | 7981.23 | CERTIFIED | N/A | N/A | N/A | 100.0% | 100.0% | 0.0% | 0.0% | 1 | 2 |
| nn | minsum | 10 | 0.000408125 | 0.00014949 | 0.00935482 | 0.000127958 | 0.0183283 | 0.0168379 | 632.457 | 0 | 632.457 | CERTIFIED | N/A | N/A | N/A | 100.0% | 100.0% | 0.0% | 0.0% | 6 | 12 |
| primal-dual | minmax | 10 | 0.000100063 | 7.4969e-05 | 0.00157997 | 6.3375e-05 | 0.00240521 | 0.00237001 | 8048.39 | 0 | 8048.39 | CERTIFIED | N/A | N/A | N/A | 100.0% | 100.0% | 0.0% | 0.0% | 1 | 2 |
| primal-dual | minsum | 10 | 0.000548937 | 0.000204167 | 0.0161244 | 0.0001675 | 0.0282658 | 0.0253259 | 643.188 | 0 | 643.188 | CERTIFIED | N/A | N/A | N/A | 100.0% | 100.0% | 0.0% | 0.0% | 6 | 12 |
| sa | minmax | 10 | 0.00689171 | 0.00638381 | 0.0102596 | 0.005785 | 0.0124103 | 0.0120638 | 8199.82 | 702.873 | 8199.82 | CERTIFIED | N/A | N/A | N/A | 100.0% | 100.0% | 0.0% | 0.0% | 1 | 2 |
| sa | minsum | 10 | 0.047561 | 0.0302709 | 0.069113 | 0.0207124 | 0.320792 | 0.22904 | 643.188 | 0 | 643.188 | CERTIFIED | N/A | N/A | N/A | 100.0% | 100.0% | 0.0% | 0.0% | 6.5 | 12.5 |
| shifting | minmax | 10 | 0.00577188 | 0.00366967 | 0.0234961 | 0.00271921 | 0.0359561 | 0.0354684 | 7981.23 | 702.873 | 7981.23 | CERTIFIED | N/A | N/A | N/A | 100.0% | 100.0% | 0.0% | 0.0% | 1 | 2 |
| shifting | minsum | 10 | 0.0365854 | 0.0172312 | 0.1279 | 0.00998971 | 0.211282 | 0.210063 | 632.457 | 0 | 632.457 | CERTIFIED | N/A | N/A | N/A | 100.0% | 100.0% | 0.0% | 0.0% | 5 | 11 |

## Interpretation

`certified_gap` is reported separately from heuristic gap/progress. Empirical ratios are not theoretical approximation ratios. Reliability rates include every record, including timeouts and failures.


### Raw integrity

# Raw result integrity report

- Runs: 180 (expected 180)
- Instances: 10
- Algorithms: 9
- Successful (not marked failed): 180
- Feasible: 180
- Verified: 180
- Failed: 0
- Timeouts: 0
- OPTIMAL / FEASIBLE / TIME_LIMIT / INFEASIBLE / FAILED: 0 / 180 / 0 / 0 / 0
- Duplicate run keys: 0
- Missing solution files among declared paths: 0
- Missing trace files among declared paths: 0
- Declared solution/trace paths checked: 360
- Missing records against expected count: 0
- Canonical aggregate complete: True

## By objective

| Objective | Runs | Successful | Feasible | Verified | Failed | Timeouts | OPTIMAL | FEASIBLE | TIME_LIMIT | INFEASIBLE | FAILED |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| minmax | 90 | 90 | 90 | 90 | 0 | 0 | 0 | 90 | 0 | 0 | 0 |
| minsum | 90 | 90 | 90 | 90 | 0 | 0 | 0 | 90 | 0 | 0 | 0 |

## Exact backend provenance

- requested=auto; actual=branch-and-bound

## Missing declared paths

### Solutions

- None

### Traces

- None


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

# Animation report

- Policy: `all-algorithms`
- Objectives: `both`
- Instance selection: all benchmark instances
- Generated animations: 160
- Heuristic animations require feasible, continuously verified results.
- Exact animations require feasibility, continuous verification, and OPTIMAL status.

- Animation path: animations/genetic/att48_minmax__run_20261005T010716Z__genetic__minmax__att48-5196644df6__r00__687d979c4b69.gif
- Animation path: animations/greedy/att48_minmax__run_20261005T010716Z__greedy__minmax__att48-5196644df6__r00__3a8d465e875e.gif
- Animation path: animations/local-search/att48_minmax__run_20261005T010716Z__local-search__minmax__att48-5196644df6__r00__e90accae173a.gif
- Animation path: animations/lp-rounding/att48_minmax__run_20261005T010716Z__lp-rounding__minmax__att48-5196644df6__r00__afe6d7ff6313.gif
- Animation path: animations/nn/att48_minmax__run_20261005T010716Z__nn__minmax__att48-5196644df6__r00__f0ca3c8ad947.gif
- Animation path: animations/primal-dual/att48_minmax__run_20261005T010716Z__primal-dual__minmax__att48-5196644df6__r00__e5b3bb72863a.gif
- Animation path: animations/sa/att48_minmax__run_20261005T010716Z__sa__minmax__att48-5196644df6__r00__6fb903242033.gif
- Animation path: animations/shifting/att48_minmax__run_20261005T010716Z__shifting__minmax__att48-5196644df6__r00__91b5d617ec20.gif
- Animation path: animations/genetic/att48_minsum__run_20261005T010716Z__genetic__minsum__att48-5196644df6__r00__e2952a340e75.gif
- Animation path: animations/greedy/att48_minsum__run_20261005T010716Z__greedy__minsum__att48-5196644df6__r00__81c47e47432b.gif
- Animation path: animations/local-search/att48_minsum__run_20261005T010717Z__local-search__minsum__att48-5196644df6__r00__2140dc5e37ed.gif
- Animation path: animations/lp-rounding/att48_minsum__run_20261005T010717Z__lp-rounding__minsum__att48-5196644df6__r00__25e5d3e4977a.gif
- Animation path: animations/nn/att48_minsum__run_20261005T010717Z__nn__minsum__att48-5196644df6__r00__33ed4dfffe61.gif
- Animation path: animations/primal-dual/att48_minsum__run_20261005T010717Z__primal-dual__minsum__att48-5196644df6__r00__4277c7a96d17.gif
- Animation path: animations/sa/att48_minsum__run_20261005T010717Z__sa__minsum__att48-5196644df6__r00__d32eba22d594.gif
- Animation path: animations/shifting/att48_minsum__run_20261005T010717Z__shifting__minsum__att48-5196644df6__r00__7bd354551f49.gif
- Animation path: animations/genetic/berlin52_minmax__run_20261005T010717Z__genetic__minmax__berlin52-6ce5d7089a__r00__0ef72cbf85e7.gif
- Animation path: animations/greedy/berlin52_minmax__run_20261005T010717Z__greedy__minmax__berlin52-6ce5d7089a__r00__7f6e03072cc8.gif
- Animation path: animations/local-search/berlin52_minmax__run_20261005T010717Z__local-search__minmax__berlin52-6ce5d7089a__r00__47f65c4850ba.gif
- Animation path: animations/lp-rounding/berlin52_minmax__run_20261005T010717Z__lp-rounding__minmax__berlin52-6ce5d7089a__r00__9a13e6cf3c96.gif
- Animation path: animations/nn/berlin52_minmax__run_20261005T010717Z__nn__minmax__berlin52-6ce5d7089a__r00__22d41ac68e34.gif
- Animation path: animations/primal-dual/berlin52_minmax__run_20261005T010717Z__primal-dual__minmax__berlin52-6ce5d7089a__r00__ea946355810b.gif
- Animation path: animations/sa/berlin52_minmax__run_20261005T010717Z__sa__minmax__berlin52-6ce5d7089a__r00__bc31f4493905.gif
- Animation path: animations/shifting/berlin52_minmax__run_20261005T010717Z__shifting__minmax__berlin52-6ce5d7089a__r00__c8724523af70.gif
- Animation path: animations/genetic/berlin52_minsum__run_20261005T010717Z__genetic__minsum__berlin52-6ce5d7089a__r00__2ecb41154337.gif
- Animation path: animations/greedy/berlin52_minsum__run_20261005T010717Z__greedy__minsum__berlin52-6ce5d7089a__r00__cb47880e530f.gif
- Animation path: animations/local-search/berlin52_minsum__run_20261005T010717Z__local-search__minsum__berlin52-6ce5d7089a__r00__266c83046abb.gif
- Animation path: animations/lp-rounding/berlin52_minsum__run_20261005T010718Z__lp-rounding__minsum__berlin52-6ce5d7089a__r00__9e13462268e2.gif
- Animation path: animations/nn/berlin52_minsum__run_20261005T010718Z__nn__minsum__berlin52-6ce5d7089a__r00__b5e1813cee9b.gif
- Animation path: animations/primal-dual/berlin52_minsum__run_20261005T010718Z__primal-dual__minsum__berlin52-6ce5d7089a__r00__dc6e60cc6b5c.gif
- Animation path: animations/sa/berlin52_minsum__run_20261005T010718Z__sa__minsum__berlin52-6ce5d7089a__r00__97adae38bf5a.gif
- Animation path: animations/shifting/berlin52_minsum__run_20261005T010718Z__shifting__minsum__berlin52-6ce5d7089a__r00__b17d84552354.gif
- Animation path: animations/genetic/eil51_minmax__run_20261005T010718Z__genetic__minmax__eil51-e956423838__r00__97fa61dc8d9b.gif
- Animation path: animations/greedy/eil51_minmax__run_20261005T010718Z__greedy__minmax__eil51-e956423838__r00__cee7a137bc78.gif
- Animation path: animations/local-search/eil51_minmax__run_20261005T010718Z__local-search__minmax__eil51-e956423838__r00__0d4fa7bbd379.gif
- Animation path: animations/lp-rounding/eil51_minmax__run_20261005T010718Z__lp-rounding__minmax__eil51-e956423838__r00__81f5155900af.gif
- Animation path: animations/nn/eil51_minmax__run_20261005T010718Z__nn__minmax__eil51-e956423838__r00__ca6b74c99f95.gif
- Animation path: animations/primal-dual/eil51_minmax__run_20261005T010718Z__primal-dual__minmax__eil51-e956423838__r00__3727e203c255.gif
- Animation path: animations/sa/eil51_minmax__run_20261005T010718Z__sa__minmax__eil51-e956423838__r00__3c760f12232c.gif
- Animation path: animations/shifting/eil51_minmax__run_20261005T010718Z__shifting__minmax__eil51-e956423838__r00__7d45900602e6.gif
- Animation path: animations/genetic/eil51_minsum__run_20261005T010718Z__genetic__minsum__eil51-e956423838__r00__6d30dbc9c230.gif
- Animation path: animations/greedy/eil51_minsum__run_20261005T010718Z__greedy__minsum__eil51-e956423838__r00__9e7387b85599.gif
- Animation path: animations/local-search/eil51_minsum__run_20261005T010719Z__local-search__minsum__eil51-e956423838__r00__2bd3e1f541a5.gif
- Animation path: animations/lp-rounding/eil51_minsum__run_20261005T010719Z__lp-rounding__minsum__eil51-e956423838__r00__1767800f683d.gif
- Animation path: animations/nn/eil51_minsum__run_20261005T010719Z__nn__minsum__eil51-e956423838__r00__dd6757e68806.gif
- Animation path: animations/primal-dual/eil51_minsum__run_20261005T010719Z__primal-dual__minsum__eil51-e956423838__r00__cd4b9d83dbb6.gif
- Animation path: animations/sa/eil51_minsum__run_20261005T010719Z__sa__minsum__eil51-e956423838__r00__0cfa900edb1f.gif
- Animation path: animations/shifting/eil51_minsum__run_20261005T010719Z__shifting__minsum__eil51-e956423838__r00__89dabc2c38e1.gif
- Animation path: animations/genetic/euro-night-0000030_minmax__run_20261005T010719Z__genetic__minmax__euro-night-0000030-164a859873__r00__b34c696ac79c.gif
- Animation path: animations/greedy/euro-night-0000030_minmax__run_20261005T010719Z__greedy__minmax__euro-night-0000030-164a859873__r00__7aa2e8a8ac33.gif
- Animation path: animations/local-search/euro-night-0000030_minmax__run_20261005T010719Z__local-search__minmax__euro-night-0000030-164a859873__r00__6fa4c399bd45.gif
- Animation path: animations/lp-rounding/euro-night-0000030_minmax__run_20261005T010719Z__lp-rounding__minmax__euro-night-0000030-164a859873__r00__b6fe10dded74.gif
- Animation path: animations/nn/euro-night-0000030_minmax__run_20261005T010719Z__nn__minmax__euro-night-0000030-164a859873__r00__92ed34c2c7b6.gif
- Animation path: animations/primal-dual/euro-night-0000030_minmax__run_20261005T010719Z__primal-dual__minmax__euro-night-0000030-164a859873__r00__a149dedacacb.gif
- Animation path: animations/sa/euro-night-0000030_minmax__run_20261005T010719Z__sa__minmax__euro-night-0000030-164a859873__r00__fffc6d7ea16a.gif
- Animation path: animations/shifting/euro-night-0000030_minmax__run_20261005T010719Z__shifting__minmax__euro-night-0000030-164a859873__r00__db06896cf43d.gif
- Animation path: animations/genetic/euro-night-0000030_minsum__run_20261005T010720Z__genetic__minsum__euro-night-0000030-164a859873__r00__018fd3d1efe1.gif
- Animation path: animations/greedy/euro-night-0000030_minsum__run_20261005T010720Z__greedy__minsum__euro-night-0000030-164a859873__r00__30eefa2f1c12.gif
- Animation path: animations/local-search/euro-night-0000030_minsum__run_20261005T010720Z__local-search__minsum__euro-night-0000030-164a859873__r00__a17e451ac114.gif
- Animation path: animations/lp-rounding/euro-night-0000030_minsum__run_20261005T010720Z__lp-rounding__minsum__euro-night-0000030-164a859873__r00__f3048428fe7f.gif
- Animation path: animations/nn/euro-night-0000030_minsum__run_20261005T010720Z__nn__minsum__euro-night-0000030-164a859873__r00__8bec7ed1aaad.gif
- Animation path: animations/primal-dual/euro-night-0000030_minsum__run_20261005T010720Z__primal-dual__minsum__euro-night-0000030-164a859873__r00__6de0a9948be9.gif
- Animation path: animations/sa/euro-night-0000030_minsum__run_20261005T010720Z__sa__minsum__euro-night-0000030-164a859873__r00__12fa7f6ab298.gif
- Animation path: animations/shifting/euro-night-0000030_minsum__run_20261005T010720Z__shifting__minsum__euro-night-0000030-164a859873__r00__17fd81322244.gif
- Animation path: animations/genetic/london-0000030.instance_minmax__run_20261005T010720Z__genetic__minmax__london-0000030.instance-3bd5c0ea09__r00__f2cf79971f85.gif
- Animation path: animations/greedy/london-0000030.instance_minmax__run_20261005T010720Z__greedy__minmax__london-0000030.instance-3bd5c0ea09__r00__d496e2bed1d5.gif
- Animation path: animations/local-search/london-0000030.instance_minmax__run_20261005T010720Z__local-search__minmax__london-0000030.instance-3bd5c0ea09__r00__42338f2400ef.gif
- Animation path: animations/lp-rounding/london-0000030.instance_minmax__run_20261005T010720Z__lp-rounding__minmax__london-0000030.instance-3bd5c0ea09__r00__d808409ecac0.gif
- Animation path: animations/nn/london-0000030.instance_minmax__run_20261005T010720Z__nn__minmax__london-0000030.instance-3bd5c0ea09__r00__98357c4a4b6a.gif
- Animation path: animations/primal-dual/london-0000030.instance_minmax__run_20261005T010720Z__primal-dual__minmax__london-0000030.instance-3bd5c0ea09__r00__86f7e1c3a196.gif
- Animation path: animations/sa/london-0000030.instance_minmax__run_20261005T010720Z__sa__minmax__london-0000030.instance-3bd5c0ea09__r00__1c1e162c516a.gif
- Animation path: animations/shifting/london-0000030.instance_minmax__run_20261005T010720Z__shifting__minmax__london-0000030.instance-3bd5c0ea09__r00__47c75ee2c58f.gif
- Animation path: animations/genetic/london-0000030.instance_minsum__run_20261005T010720Z__genetic__minsum__london-0000030.instance-3bd5c0ea09__r00__ffeb1eddd8ac.gif
- Animation path: animations/greedy/london-0000030.instance_minsum__run_20261005T010720Z__greedy__minsum__london-0000030.instance-3bd5c0ea09__r00__d76a5e3d0e75.gif
- Animation path: animations/local-search/london-0000030.instance_minsum__run_20261005T010720Z__local-search__minsum__london-0000030.instance-3bd5c0ea09__r00__2fcc858e8292.gif
- Animation path: animations/lp-rounding/london-0000030.instance_minsum__run_20261005T010720Z__lp-rounding__minsum__london-0000030.instance-3bd5c0ea09__r00__105612b1da2d.gif
- Animation path: animations/nn/london-0000030.instance_minsum__run_20261005T010720Z__nn__minsum__london-0000030.instance-3bd5c0ea09__r00__0622cda5bf92.gif
- Animation path: animations/primal-dual/london-0000030.instance_minsum__run_20261005T010720Z__primal-dual__minsum__london-0000030.instance-3bd5c0ea09__r00__617e9f9193b7.gif
- Animation path: animations/sa/london-0000030.instance_minsum__run_20261005T010720Z__sa__minsum__london-0000030.instance-3bd5c0ea09__r00__a9aff2591691.gif
- Animation path: animations/shifting/london-0000030.instance_minsum__run_20261005T010720Z__shifting__minsum__london-0000030.instance-3bd5c0ea09__r00__1d7aaaddb95b.gif
- Animation path: animations/genetic/sbgdb-20200507-fpg-poly_0000000030_minmax__run_20261005T010720Z__genetic__minmax__sbgdb-20200507-fpg-poly_0000000030-f90__r00__3eef0dd6530b.gif
- Animation path: animations/greedy/sbgdb-20200507-fpg-poly_0000000030_minmax__run_20261005T010720Z__greedy__minmax__sbgdb-20200507-fpg-poly_0000000030-f90__r00__d1b5eff5c936.gif
- Animation path: animations/local-search/sbgdb-20200507-fpg-poly_0000000030_minmax__run_20261005T010721Z__local-search__minmax__sbgdb-20200507-fpg-poly_0000000030-f90__r00__663cd5ed60e6.gif
- Animation path: animations/lp-rounding/sbgdb-20200507-fpg-poly_0000000030_minmax__run_20261005T010721Z__lp-rounding__minmax__sbgdb-20200507-fpg-poly_0000000030-f90__r00__6c0061e5c380.gif
- Animation path: animations/nn/sbgdb-20200507-fpg-poly_0000000030_minmax__run_20261005T010721Z__nn__minmax__sbgdb-20200507-fpg-poly_0000000030-f90__r00__209980e03688.gif
- Animation path: animations/primal-dual/sbgdb-20200507-fpg-poly_0000000030_minmax__run_20261005T010721Z__primal-dual__minmax__sbgdb-20200507-fpg-poly_0000000030-f90__r00__56fd60ce7389.gif
- Animation path: animations/sa/sbgdb-20200507-fpg-poly_0000000030_minmax__run_20261005T010721Z__sa__minmax__sbgdb-20200507-fpg-poly_0000000030-f90__r00__e58ee83ad2b7.gif
- Animation path: animations/shifting/sbgdb-20200507-fpg-poly_0000000030_minmax__run_20261005T010721Z__shifting__minmax__sbgdb-20200507-fpg-poly_0000000030-f90__r00__1ff625335f01.gif
- Animation path: animations/genetic/sbgdb-20200507-fpg-poly_0000000030_minsum__run_20261005T010721Z__genetic__minsum__sbgdb-20200507-fpg-poly_0000000030-f90__r00__88742a117511.gif
- Animation path: animations/greedy/sbgdb-20200507-fpg-poly_0000000030_minsum__run_20261005T010721Z__greedy__minsum__sbgdb-20200507-fpg-poly_0000000030-f90__r00__2a4f60a61449.gif
- Animation path: animations/local-search/sbgdb-20200507-fpg-poly_0000000030_minsum__run_20261005T010721Z__local-search__minsum__sbgdb-20200507-fpg-poly_0000000030-f90__r00__e857e2a95449.gif
- Animation path: animations/lp-rounding/sbgdb-20200507-fpg-poly_0000000030_minsum__run_20261005T010721Z__lp-rounding__minsum__sbgdb-20200507-fpg-poly_0000000030-f90__r00__5c14d7929dad.gif
- Animation path: animations/nn/sbgdb-20200507-fpg-poly_0000000030_minsum__run_20261005T010721Z__nn__minsum__sbgdb-20200507-fpg-poly_0000000030-f90__r00__2a17a68bd76d.gif
- Animation path: animations/primal-dual/sbgdb-20200507-fpg-poly_0000000030_minsum__run_20261005T010721Z__primal-dual__minsum__sbgdb-20200507-fpg-poly_0000000030-f90__r00__ca876de47a96.gif
- Animation path: animations/sa/sbgdb-20200507-fpg-poly_0000000030_minsum__run_20261005T010721Z__sa__minsum__sbgdb-20200507-fpg-poly_0000000030-f90__r00__9d452e2d0001.gif
- Animation path: animations/shifting/sbgdb-20200507-fpg-poly_0000000030_minsum__run_20261005T010721Z__shifting__minsum__sbgdb-20200507-fpg-poly_0000000030-f90__r00__8792d4d1b4bd.gif
- Animation path: animations/genetic/sbgdb-20200507-pntset-0000030_minmax__run_20261005T010721Z__genetic__minmax__sbgdb-20200507-pntset-0000030-d00c0eac__r00__b4d4b031005f.gif
- Animation path: animations/greedy/sbgdb-20200507-pntset-0000030_minmax__run_20261005T010721Z__greedy__minmax__sbgdb-20200507-pntset-0000030-d00c0eac__r00__1f709da5d3b9.gif
- Animation path: animations/local-search/sbgdb-20200507-pntset-0000030_minmax__run_20261005T010721Z__local-search__minmax__sbgdb-20200507-pntset-0000030-d00c0eac__r00__18b2d81b24c8.gif
- Animation path: animations/lp-rounding/sbgdb-20200507-pntset-0000030_minmax__run_20261005T010721Z__lp-rounding__minmax__sbgdb-20200507-pntset-0000030-d00c0eac__r00__8057871a3201.gif
- Animation path: animations/nn/sbgdb-20200507-pntset-0000030_minmax__run_20261005T010721Z__nn__minmax__sbgdb-20200507-pntset-0000030-d00c0eac__r00__57dd8d9daf8d.gif
- Animation path: animations/primal-dual/sbgdb-20200507-pntset-0000030_minmax__run_20261005T010721Z__primal-dual__minmax__sbgdb-20200507-pntset-0000030-d00c0eac__r00__9642ab73bd2b.gif
- Animation path: animations/sa/sbgdb-20200507-pntset-0000030_minmax__run_20261005T010721Z__sa__minmax__sbgdb-20200507-pntset-0000030-d00c0eac__r00__2d70b230431a.gif
- Animation path: animations/shifting/sbgdb-20200507-pntset-0000030_minmax__run_20261005T010721Z__shifting__minmax__sbgdb-20200507-pntset-0000030-d00c0eac__r00__8a10e4c95db7.gif
- Animation path: animations/genetic/sbgdb-20200507-pntset-0000030_minsum__run_20261005T010721Z__genetic__minsum__sbgdb-20200507-pntset-0000030-d00c0eac__r00__eae414fc44d9.gif
- Animation path: animations/greedy/sbgdb-20200507-pntset-0000030_minsum__run_20261005T010721Z__greedy__minsum__sbgdb-20200507-pntset-0000030-d00c0eac__r00__cd17eed2dde3.gif
- Animation path: animations/local-search/sbgdb-20200507-pntset-0000030_minsum__run_20261005T010721Z__local-search__minsum__sbgdb-20200507-pntset-0000030-d00c0eac__r00__47a7bc78399f.gif
- Animation path: animations/lp-rounding/sbgdb-20200507-pntset-0000030_minsum__run_20261005T010721Z__lp-rounding__minsum__sbgdb-20200507-pntset-0000030-d00c0eac__r00__147e77ad6180.gif
- Animation path: animations/nn/sbgdb-20200507-pntset-0000030_minsum__run_20261005T010721Z__nn__minsum__sbgdb-20200507-pntset-0000030-d00c0eac__r00__de19b235a34d.gif
- Animation path: animations/primal-dual/sbgdb-20200507-pntset-0000030_minsum__run_20261005T010721Z__primal-dual__minsum__sbgdb-20200507-pntset-0000030-d00c0eac__r00__6f065d7d5a07.gif
- Animation path: animations/sa/sbgdb-20200507-pntset-0000030_minsum__run_20261005T010721Z__sa__minsum__sbgdb-20200507-pntset-0000030-d00c0eac__r00__64d1edd4b897.gif
- Animation path: animations/shifting/sbgdb-20200507-pntset-0000030_minsum__run_20261005T010722Z__shifting__minsum__sbgdb-20200507-pntset-0000030-d00c0eac__r00__759e0cf8e104.gif
- Animation path: animations/genetic/stars-0000030.instance_minmax__run_20261005T010722Z__genetic__minmax__stars-0000030.instance-2f24c57225__r00__fbcf7c45fa8a.gif
- Animation path: animations/greedy/stars-0000030.instance_minmax__run_20261005T010722Z__greedy__minmax__stars-0000030.instance-2f24c57225__r00__e548adeb16eb.gif
- Animation path: animations/local-search/stars-0000030.instance_minmax__run_20261005T010722Z__local-search__minmax__stars-0000030.instance-2f24c57225__r00__ae5af01b76d5.gif
- Animation path: animations/lp-rounding/stars-0000030.instance_minmax__run_20261005T010722Z__lp-rounding__minmax__stars-0000030.instance-2f24c57225__r00__a760d23c959d.gif
- Animation path: animations/nn/stars-0000030.instance_minmax__run_20261005T010722Z__nn__minmax__stars-0000030.instance-2f24c57225__r00__230a0c01ee3b.gif
- Animation path: animations/primal-dual/stars-0000030.instance_minmax__run_20261005T010722Z__primal-dual__minmax__stars-0000030.instance-2f24c57225__r00__cfbc3c0b01d8.gif
- Animation path: animations/sa/stars-0000030.instance_minmax__run_20261005T010722Z__sa__minmax__stars-0000030.instance-2f24c57225__r00__a9f4dcec18e5.gif
- Animation path: animations/shifting/stars-0000030.instance_minmax__run_20261005T010722Z__shifting__minmax__stars-0000030.instance-2f24c57225__r00__c108ca94ec02.gif
- Animation path: animations/genetic/stars-0000030.instance_minsum__run_20261005T010722Z__genetic__minsum__stars-0000030.instance-2f24c57225__r00__f1757b116997.gif
- Animation path: animations/greedy/stars-0000030.instance_minsum__run_20261005T010722Z__greedy__minsum__stars-0000030.instance-2f24c57225__r00__712103b642aa.gif
- Animation path: animations/local-search/stars-0000030.instance_minsum__run_20261005T010722Z__local-search__minsum__stars-0000030.instance-2f24c57225__r00__0aae5dde10dc.gif
- Animation path: animations/lp-rounding/stars-0000030.instance_minsum__run_20261005T010722Z__lp-rounding__minsum__stars-0000030.instance-2f24c57225__r00__6402ca7777d0.gif
- Animation path: animations/nn/stars-0000030.instance_minsum__run_20261005T010722Z__nn__minsum__stars-0000030.instance-2f24c57225__r00__135875006040.gif
- Animation path: animations/primal-dual/stars-0000030.instance_minsum__run_20261005T010722Z__primal-dual__minsum__stars-0000030.instance-2f24c57225__r00__9088366668a8.gif
- Animation path: animations/sa/stars-0000030.instance_minsum__run_20261005T010722Z__sa__minsum__stars-0000030.instance-2f24c57225__r00__945f182b50c0.gif
- Animation path: animations/shifting/stars-0000030.instance_minsum__run_20261005T010722Z__shifting__minsum__stars-0000030.instance-2f24c57225__r00__e532530a6704.gif
- Animation path: animations/genetic/uniform-0000030-2_minmax__run_20261005T010722Z__genetic__minmax__uniform-0000030-2-ac5ba17054__r00__a362d0cce02a.gif
- Animation path: animations/greedy/uniform-0000030-2_minmax__run_20261005T010722Z__greedy__minmax__uniform-0000030-2-ac5ba17054__r00__2549e77ac62d.gif
- Animation path: animations/local-search/uniform-0000030-2_minmax__run_20261005T010722Z__local-search__minmax__uniform-0000030-2-ac5ba17054__r00__86fd4001ca35.gif
- Animation path: animations/lp-rounding/uniform-0000030-2_minmax__run_20261005T010722Z__lp-rounding__minmax__uniform-0000030-2-ac5ba17054__r00__808b1bfd9025.gif
- Animation path: animations/nn/uniform-0000030-2_minmax__run_20261005T010722Z__nn__minmax__uniform-0000030-2-ac5ba17054__r00__5348b1c1bf7a.gif
- Animation path: animations/primal-dual/uniform-0000030-2_minmax__run_20261005T010722Z__primal-dual__minmax__uniform-0000030-2-ac5ba17054__r00__51fd0070ae76.gif
- Animation path: animations/sa/uniform-0000030-2_minmax__run_20261005T010722Z__sa__minmax__uniform-0000030-2-ac5ba17054__r00__2c4349805216.gif
- Animation path: animations/shifting/uniform-0000030-2_minmax__run_20261005T010722Z__shifting__minmax__uniform-0000030-2-ac5ba17054__r00__40a80c283851.gif
- Animation path: animations/genetic/uniform-0000030-2_minsum__run_20261005T010722Z__genetic__minsum__uniform-0000030-2-ac5ba17054__r00__8cec5c989af0.gif
- Animation path: animations/greedy/uniform-0000030-2_minsum__run_20261005T010722Z__greedy__minsum__uniform-0000030-2-ac5ba17054__r00__5052bea8381c.gif
- Animation path: animations/local-search/uniform-0000030-2_minsum__run_20261005T010723Z__local-search__minsum__uniform-0000030-2-ac5ba17054__r00__f7dc62a70598.gif
- Animation path: animations/lp-rounding/uniform-0000030-2_minsum__run_20261005T010723Z__lp-rounding__minsum__uniform-0000030-2-ac5ba17054__r00__aa5d27d0122a.gif
- Animation path: animations/nn/uniform-0000030-2_minsum__run_20261005T010723Z__nn__minsum__uniform-0000030-2-ac5ba17054__r00__1aea58757c7d.gif
- Animation path: animations/primal-dual/uniform-0000030-2_minsum__run_20261005T010723Z__primal-dual__minsum__uniform-0000030-2-ac5ba17054__r00__3238739bc283.gif
- Animation path: animations/sa/uniform-0000030-2_minsum__run_20261005T010723Z__sa__minsum__uniform-0000030-2-ac5ba17054__r00__67ef8511537a.gif
- Animation path: animations/shifting/uniform-0000030-2_minsum__run_20261005T010723Z__shifting__minsum__uniform-0000030-2-ac5ba17054__r00__b84ea48a8b6f.gif
- Animation path: animations/genetic/us-night-0000030.instance_minmax__run_20261005T010723Z__genetic__minmax__us-night-0000030.instance-3704ac03dc__r00__a1729896bb24.gif
- Animation path: animations/greedy/us-night-0000030.instance_minmax__run_20261005T010723Z__greedy__minmax__us-night-0000030.instance-3704ac03dc__r00__cd07038acabe.gif
- Animation path: animations/local-search/us-night-0000030.instance_minmax__run_20261005T010723Z__local-search__minmax__us-night-0000030.instance-3704ac03dc__r00__8405fcf8cd20.gif
- Animation path: animations/lp-rounding/us-night-0000030.instance_minmax__run_20261005T010723Z__lp-rounding__minmax__us-night-0000030.instance-3704ac03dc__r00__3b7d58214e62.gif
- Animation path: animations/nn/us-night-0000030.instance_minmax__run_20261005T010723Z__nn__minmax__us-night-0000030.instance-3704ac03dc__r00__a8c0154fb4e3.gif
- Animation path: animations/primal-dual/us-night-0000030.instance_minmax__run_20261005T010723Z__primal-dual__minmax__us-night-0000030.instance-3704ac03dc__r00__b44bae667acf.gif
- Animation path: animations/sa/us-night-0000030.instance_minmax__run_20261005T010723Z__sa__minmax__us-night-0000030.instance-3704ac03dc__r00__f195c2b02b3e.gif
- Animation path: animations/shifting/us-night-0000030.instance_minmax__run_20261005T010723Z__shifting__minmax__us-night-0000030.instance-3704ac03dc__r00__3662f56d0eb8.gif
- Animation path: animations/genetic/us-night-0000030.instance_minsum__run_20261005T010723Z__genetic__minsum__us-night-0000030.instance-3704ac03dc__r00__be8815efe0a8.gif
- Animation path: animations/greedy/us-night-0000030.instance_minsum__run_20261005T010723Z__greedy__minsum__us-night-0000030.instance-3704ac03dc__r00__2dbbc9169a12.gif
- Animation path: animations/local-search/us-night-0000030.instance_minsum__run_20261005T010723Z__local-search__minsum__us-night-0000030.instance-3704ac03dc__r00__22a21449f143.gif
- Animation path: animations/lp-rounding/us-night-0000030.instance_minsum__run_20261005T010723Z__lp-rounding__minsum__us-night-0000030.instance-3704ac03dc__r00__a7b136e16bc8.gif
- Animation path: animations/nn/us-night-0000030.instance_minsum__run_20261005T010723Z__nn__minsum__us-night-0000030.instance-3704ac03dc__r00__adf0d0b2fe80.gif
- Animation path: animations/primal-dual/us-night-0000030.instance_minsum__run_20261005T010723Z__primal-dual__minsum__us-night-0000030.instance-3704ac03dc__r00__167853d1fc45.gif
- Animation path: animations/sa/us-night-0000030.instance_minsum__run_20261005T010723Z__sa__minsum__us-night-0000030.instance-3704ac03dc__r00__94d091dc21c6.gif
- Animation path: animations/shifting/us-night-0000030.instance_minsum__run_20261005T010723Z__shifting__minsum__us-night-0000030.instance-3704ac03dc__r00__1b9e70d5f93a.gif


## 15. Generated tables

- `tables/00_index.md`
- `tables/failed_runs.csv`
- `tables/failed_runs.md`
- `tables/master.csv`
- `tables/master.md`
- `tables/per_algorithm/branch-and-bound.csv`
- `tables/per_algorithm/branch-and-bound.md`
- `tables/per_algorithm/genetic.csv`
- `tables/per_algorithm/genetic.md`
- `tables/per_algorithm/greedy.csv`
- `tables/per_algorithm/greedy.md`
- `tables/per_algorithm/local-search.csv`
- `tables/per_algorithm/local-search.md`
- `tables/per_algorithm/lp-rounding.csv`
- `tables/per_algorithm/lp-rounding.md`
- `tables/per_algorithm/nn.csv`
- `tables/per_algorithm/nn.md`
- `tables/per_algorithm/primal-dual.csv`
- `tables/per_algorithm/primal-dual.md`
- `tables/per_algorithm/sa.csv`
- `tables/per_algorithm/sa.md`
- `tables/per_algorithm/shifting.csv`
- `tables/per_algorithm/shifting.md`
- `tables/per_family/att48.csv`
- `tables/per_family/att48.md`
- `tables/per_family/berlin52.csv`
- `tables/per_family/berlin52.md`
- `tables/per_family/eil51.csv`
- `tables/per_family/eil51.md`
- `tables/per_family/euro.csv`
- `tables/per_family/euro.md`
- `tables/per_family/london.csv`
- `tables/per_family/london.md`
- `tables/per_family/sbgdb.csv`
- `tables/per_family/sbgdb.md`
- `tables/per_family/stars.csv`
- `tables/per_family/stars.md`
- `tables/per_family/uniform.csv`
- `tables/per_family/uniform.md`
- `tables/per_family/us.csv`
- `tables/per_family/us.md`
- `tables/per_instance/att48.csv`
- `tables/per_instance/att48.md`
- `tables/per_instance/berlin52.csv`
- `tables/per_instance/berlin52.md`
- `tables/per_instance/eil51.csv`
- `tables/per_instance/eil51.md`
- `tables/per_instance/euro-night-0000030.csv`
- `tables/per_instance/euro-night-0000030.md`
- `tables/per_instance/london-0000030.csv`
- `tables/per_instance/london-0000030.md`
- `tables/per_instance/sbgdb-20200507-fpg-poly_0000000030.csv`
- `tables/per_instance/sbgdb-20200507-fpg-poly_0000000030.md`
- `tables/per_instance/sbgdb-20200507-pntset-0000030.csv`
- `tables/per_instance/sbgdb-20200507-pntset-0000030.md`
- `tables/per_instance/stars-0000030.csv`
- `tables/per_instance/stars-0000030.md`
- `tables/per_instance/uniform-0000030-2.csv`
- `tables/per_instance/uniform-0000030-2.md`
- `tables/per_instance/us-night-0000030.csv`
- `tables/per_instance/us-night-0000030.md`

## 16. Generated figures

- `figures/A1_runtime_bar_att48.pdf`
- `figures/A1_runtime_bar_att48.png`
- `figures/A1_runtime_bar_berlin52.pdf`
- `figures/A1_runtime_bar_berlin52.png`
- `figures/A1_runtime_bar_eil51.pdf`
- `figures/A1_runtime_bar_eil51.png`
- `figures/A1_runtime_bar_euro-night-0000030.pdf`
- `figures/A1_runtime_bar_euro-night-0000030.png`
- `figures/A1_runtime_bar_london-0000030.instance.pdf`
- `figures/A1_runtime_bar_london-0000030.instance.png`
- `figures/A1_runtime_bar_sbgdb-20200507-fpg-poly_0000000030.pdf`
- `figures/A1_runtime_bar_sbgdb-20200507-fpg-poly_0000000030.png`
- `figures/A1_runtime_bar_sbgdb-20200507-pntset-0000030.pdf`
- `figures/A1_runtime_bar_sbgdb-20200507-pntset-0000030.png`
- `figures/A1_runtime_bar_stars-0000030.instance.pdf`
- `figures/A1_runtime_bar_stars-0000030.instance.png`
- `figures/A1_runtime_bar_uniform-0000030-2.pdf`
- `figures/A1_runtime_bar_uniform-0000030-2.png`
- `figures/A1_runtime_bar_us-night-0000030.instance.pdf`
- `figures/A1_runtime_bar_us-night-0000030.instance.png`
- `figures/A2_runtime_box.pdf`
- `figures/A2_runtime_box.png`
- `figures/A3_runtime_vs_n_m25.pdf`
- `figures/A3_runtime_vs_n_m25.png`
- `figures/A4_runtime_vs_m_n500.pdf`
- `figures/A4_runtime_vs_m_n500.png`
- `figures/A5_runtime_heatmap.pdf`
- `figures/A5_runtime_heatmap.png`
- `figures/A6_runtime_ecdf.pdf`
- `figures/A6_runtime_ecdf.png`
- `figures/B1_memory_bar_att48.pdf`
- `figures/B1_memory_bar_att48.png`
- `figures/B1_memory_bar_berlin52.pdf`
- `figures/B1_memory_bar_berlin52.png`
- `figures/B1_memory_bar_eil51.pdf`
- `figures/B1_memory_bar_eil51.png`
- `figures/B1_memory_bar_euro-night-0000030.pdf`
- `figures/B1_memory_bar_euro-night-0000030.png`
- `figures/B1_memory_bar_london-0000030.instance.pdf`
- `figures/B1_memory_bar_london-0000030.instance.png`
- `figures/B1_memory_bar_sbgdb-20200507-fpg-poly_0000000030.pdf`
- `figures/B1_memory_bar_sbgdb-20200507-fpg-poly_0000000030.png`
- `figures/B1_memory_bar_sbgdb-20200507-pntset-0000030.pdf`
- `figures/B1_memory_bar_sbgdb-20200507-pntset-0000030.png`
- `figures/B1_memory_bar_stars-0000030.instance.pdf`
- `figures/B1_memory_bar_stars-0000030.instance.png`
- `figures/B1_memory_bar_uniform-0000030-2.pdf`
- `figures/B1_memory_bar_uniform-0000030-2.png`
- `figures/B1_memory_bar_us-night-0000030.instance.pdf`
- `figures/B1_memory_bar_us-night-0000030.instance.png`
- `figures/B2_memory_box.pdf`
- `figures/B2_memory_box.png`
- `figures/B3_memory_vs_n_m25.pdf`
- `figures/B3_memory_vs_n_m25.png`
- `figures/B4_memory_heatmap.pdf`
- `figures/B4_memory_heatmap.png`
- `figures/B5_memory_vs_m_n500.pdf`
- `figures/B5_memory_vs_m_n500.png`
- `figures/C1_gap_boxplot.pdf`
- `figures/C1_gap_boxplot.png`
- `figures/C2_gap_ecdf.pdf`
- `figures/C2_gap_ecdf.png`
- `figures/C3_gap_vs_n.pdf`
- `figures/C3_gap_vs_n.png`
- `figures/C4_objective_value_vs_n_m25.pdf`
- `figures/C4_objective_value_vs_n_m25.png`
- `figures/C5_gap_vs_runtime.pdf`
- `figures/C5_gap_vs_runtime.png`
- `figures/C6_objective_boxplot.pdf`
- `figures/C6_objective_boxplot.png`
- `figures/D1_pareto_att48.pdf`
- `figures/D1_pareto_att48.png`
- `figures/D1_pareto_berlin52.pdf`
- `figures/D1_pareto_berlin52.png`
- `figures/D1_pareto_eil51.pdf`
- `figures/D1_pareto_eil51.png`
- `figures/D1_pareto_euro-night-0000030.pdf`
- `figures/D1_pareto_euro-night-0000030.png`
- `figures/D1_pareto_london-0000030.instance.pdf`
- `figures/D1_pareto_london-0000030.instance.png`
- `figures/D1_pareto_sbgdb-20200507-fpg-poly_0000000030.pdf`
- `figures/D1_pareto_sbgdb-20200507-fpg-poly_0000000030.png`
- `figures/D1_pareto_sbgdb-20200507-pntset-0000030.pdf`
- `figures/D1_pareto_sbgdb-20200507-pntset-0000030.png`
- `figures/D1_pareto_stars-0000030.instance.pdf`
- `figures/D1_pareto_stars-0000030.instance.png`
- `figures/D1_pareto_uniform-0000030-2.pdf`
- `figures/D1_pareto_uniform-0000030-2.png`
- `figures/D1_pareto_us-night-0000030.instance.pdf`
- `figures/D1_pareto_us-night-0000030.instance.png`
- `figures/D2_pareto_aggregated.pdf`
- `figures/D2_pareto_aggregated.png`
- `figures/D3_tradeoff_branch-and-bound_minmax.pdf`
- `figures/D3_tradeoff_branch-and-bound_minmax.png`
- `figures/D3_tradeoff_branch-and-bound_minsum.pdf`
- `figures/D3_tradeoff_branch-and-bound_minsum.png`
- `figures/D3_tradeoff_genetic_minmax.pdf`
- `figures/D3_tradeoff_genetic_minmax.png`
- `figures/D3_tradeoff_genetic_minsum.pdf`
- `figures/D3_tradeoff_genetic_minsum.png`
- `figures/D3_tradeoff_greedy_minmax.pdf`
- `figures/D3_tradeoff_greedy_minmax.png`
- `figures/D3_tradeoff_greedy_minsum.pdf`
- `figures/D3_tradeoff_greedy_minsum.png`
- `figures/D3_tradeoff_local-search_minmax.pdf`
- `figures/D3_tradeoff_local-search_minmax.png`
- `figures/D3_tradeoff_local-search_minsum.pdf`
- `figures/D3_tradeoff_local-search_minsum.png`
- `figures/D3_tradeoff_lp-rounding_minmax.pdf`
- `figures/D3_tradeoff_lp-rounding_minmax.png`
- `figures/D3_tradeoff_lp-rounding_minsum.pdf`
- `figures/D3_tradeoff_lp-rounding_minsum.png`
- `figures/D3_tradeoff_nn_minmax.pdf`
- `figures/D3_tradeoff_nn_minmax.png`
- `figures/D3_tradeoff_nn_minsum.pdf`
- `figures/D3_tradeoff_nn_minsum.png`
- `figures/D3_tradeoff_primal-dual_minmax.pdf`
- `figures/D3_tradeoff_primal-dual_minmax.png`
- `figures/D3_tradeoff_primal-dual_minsum.pdf`
- `figures/D3_tradeoff_primal-dual_minsum.png`
- `figures/D3_tradeoff_sa_minmax.pdf`
- `figures/D3_tradeoff_sa_minmax.png`
- `figures/D3_tradeoff_sa_minsum.pdf`
- `figures/D3_tradeoff_sa_minsum.png`
- `figures/D3_tradeoff_shifting_minmax.pdf`
- `figures/D3_tradeoff_shifting_minmax.png`
- `figures/D3_tradeoff_shifting_minsum.pdf`
- `figures/D3_tradeoff_shifting_minsum.png`
- `figures/E1_quality_heatmap.pdf`
- `figures/E1_quality_heatmap.png`
- `figures/E2_minmax_quality_heatmap.pdf`
- `figures/E2_minmax_quality_heatmap.png`
- `figures/E3_minsum_quality_heatmap.pdf`
- `figures/E3_minsum_quality_heatmap.png`
- `figures/F1_convergence_att48.pdf`
- `figures/F1_convergence_att48.png`
- `figures/F1_convergence_berlin52.pdf`
- `figures/F1_convergence_berlin52.png`
- `figures/F1_convergence_eil51.pdf`
- `figures/F1_convergence_eil51.png`
- `figures/F1_convergence_euro-night-0000030.pdf`
- `figures/F1_convergence_euro-night-0000030.png`
- `figures/F1_convergence_london-0000030.instance.pdf`
- `figures/F1_convergence_london-0000030.instance.png`
- `figures/F1_convergence_sbgdb-20200507-fpg-poly_0000000030.pdf`
- `figures/F1_convergence_sbgdb-20200507-fpg-poly_0000000030.png`
- `figures/F1_convergence_sbgdb-20200507-pntset-0000030.pdf`
- `figures/F1_convergence_sbgdb-20200507-pntset-0000030.png`
- `figures/F1_convergence_stars-0000030.instance.pdf`
- `figures/F1_convergence_stars-0000030.instance.png`
- `figures/F1_convergence_uniform-0000030-2.pdf`
- `figures/F1_convergence_uniform-0000030-2.png`
- `figures/F1_convergence_us-night-0000030.instance.pdf`
- `figures/F1_convergence_us-night-0000030.instance.png`
- `figures/F2_convergence_grid.pdf`
- `figures/F2_convergence_grid.png`
- `figures/G1_summary_dashboard.pdf`
- `figures/G1_summary_dashboard.png`
- `figures/REPORT.md`

## 17. Generated animations

- `animations/genetic/att48_minmax__run_20261005T010716Z__genetic__minmax__att48-5196644df6__r00__687d979c4b69.gif`
- `animations/genetic/att48_minmax__run_20261005T010716Z__genetic__minmax__att48-5196644df6__r00__687d979c4b69.gif.metadata.json`
- `animations/genetic/att48_minsum__run_20261005T010716Z__genetic__minsum__att48-5196644df6__r00__e2952a340e75.gif`
- `animations/genetic/att48_minsum__run_20261005T010716Z__genetic__minsum__att48-5196644df6__r00__e2952a340e75.gif.metadata.json`
- `animations/genetic/berlin52_minmax__run_20261005T010717Z__genetic__minmax__berlin52-6ce5d7089a__r00__0ef72cbf85e7.gif`
- `animations/genetic/berlin52_minmax__run_20261005T010717Z__genetic__minmax__berlin52-6ce5d7089a__r00__0ef72cbf85e7.gif.metadata.json`
- `animations/genetic/berlin52_minsum__run_20261005T010717Z__genetic__minsum__berlin52-6ce5d7089a__r00__2ecb41154337.gif`
- `animations/genetic/berlin52_minsum__run_20261005T010717Z__genetic__minsum__berlin52-6ce5d7089a__r00__2ecb41154337.gif.metadata.json`
- `animations/genetic/eil51_minmax__run_20261005T010718Z__genetic__minmax__eil51-e956423838__r00__97fa61dc8d9b.gif`
- `animations/genetic/eil51_minmax__run_20261005T010718Z__genetic__minmax__eil51-e956423838__r00__97fa61dc8d9b.gif.metadata.json`
- `animations/genetic/eil51_minsum__run_20261005T010718Z__genetic__minsum__eil51-e956423838__r00__6d30dbc9c230.gif`
- `animations/genetic/eil51_minsum__run_20261005T010718Z__genetic__minsum__eil51-e956423838__r00__6d30dbc9c230.gif.metadata.json`
- `animations/genetic/euro-night-0000030_minmax__run_20261005T010719Z__genetic__minmax__euro-night-0000030-164a859873__r00__b34c696ac79c.gif`
- `animations/genetic/euro-night-0000030_minmax__run_20261005T010719Z__genetic__minmax__euro-night-0000030-164a859873__r00__b34c696ac79c.gif.metadata.json`
- `animations/genetic/euro-night-0000030_minsum__run_20261005T010720Z__genetic__minsum__euro-night-0000030-164a859873__r00__018fd3d1efe1.gif`
- `animations/genetic/euro-night-0000030_minsum__run_20261005T010720Z__genetic__minsum__euro-night-0000030-164a859873__r00__018fd3d1efe1.gif.metadata.json`
- `animations/genetic/london-0000030.instance_minmax__run_20261005T010720Z__genetic__minmax__london-0000030.instance-3bd5c0ea09__r00__f2cf79971f85.gif`
- `animations/genetic/london-0000030.instance_minmax__run_20261005T010720Z__genetic__minmax__london-0000030.instance-3bd5c0ea09__r00__f2cf79971f85.gif.metadata.json`
- `animations/genetic/london-0000030.instance_minsum__run_20261005T010720Z__genetic__minsum__london-0000030.instance-3bd5c0ea09__r00__ffeb1eddd8ac.gif`
- `animations/genetic/london-0000030.instance_minsum__run_20261005T010720Z__genetic__minsum__london-0000030.instance-3bd5c0ea09__r00__ffeb1eddd8ac.gif.metadata.json`
- `animations/genetic/sbgdb-20200507-fpg-poly_0000000030_minmax__run_20261005T010720Z__genetic__minmax__sbgdb-20200507-fpg-poly_0000000030-f90__r00__3eef0dd6530b.gif`
- `animations/genetic/sbgdb-20200507-fpg-poly_0000000030_minmax__run_20261005T010720Z__genetic__minmax__sbgdb-20200507-fpg-poly_0000000030-f90__r00__3eef0dd6530b.gif.metadata.json`
- `animations/genetic/sbgdb-20200507-fpg-poly_0000000030_minsum__run_20261005T010721Z__genetic__minsum__sbgdb-20200507-fpg-poly_0000000030-f90__r00__88742a117511.gif`
- `animations/genetic/sbgdb-20200507-fpg-poly_0000000030_minsum__run_20261005T010721Z__genetic__minsum__sbgdb-20200507-fpg-poly_0000000030-f90__r00__88742a117511.gif.metadata.json`
- `animations/genetic/sbgdb-20200507-pntset-0000030_minmax__run_20261005T010721Z__genetic__minmax__sbgdb-20200507-pntset-0000030-d00c0eac__r00__b4d4b031005f.gif`
- `animations/genetic/sbgdb-20200507-pntset-0000030_minmax__run_20261005T010721Z__genetic__minmax__sbgdb-20200507-pntset-0000030-d00c0eac__r00__b4d4b031005f.gif.metadata.json`
- `animations/genetic/sbgdb-20200507-pntset-0000030_minsum__run_20261005T010721Z__genetic__minsum__sbgdb-20200507-pntset-0000030-d00c0eac__r00__eae414fc44d9.gif`
- `animations/genetic/sbgdb-20200507-pntset-0000030_minsum__run_20261005T010721Z__genetic__minsum__sbgdb-20200507-pntset-0000030-d00c0eac__r00__eae414fc44d9.gif.metadata.json`
- `animations/genetic/stars-0000030.instance_minmax__run_20261005T010722Z__genetic__minmax__stars-0000030.instance-2f24c57225__r00__fbcf7c45fa8a.gif`
- `animations/genetic/stars-0000030.instance_minmax__run_20261005T010722Z__genetic__minmax__stars-0000030.instance-2f24c57225__r00__fbcf7c45fa8a.gif.metadata.json`
- `animations/genetic/stars-0000030.instance_minsum__run_20261005T010722Z__genetic__minsum__stars-0000030.instance-2f24c57225__r00__f1757b116997.gif`
- `animations/genetic/stars-0000030.instance_minsum__run_20261005T010722Z__genetic__minsum__stars-0000030.instance-2f24c57225__r00__f1757b116997.gif.metadata.json`
- `animations/genetic/uniform-0000030-2_minmax__run_20261005T010722Z__genetic__minmax__uniform-0000030-2-ac5ba17054__r00__a362d0cce02a.gif`
- `animations/genetic/uniform-0000030-2_minmax__run_20261005T010722Z__genetic__minmax__uniform-0000030-2-ac5ba17054__r00__a362d0cce02a.gif.metadata.json`
- `animations/genetic/uniform-0000030-2_minsum__run_20261005T010722Z__genetic__minsum__uniform-0000030-2-ac5ba17054__r00__8cec5c989af0.gif`
- `animations/genetic/uniform-0000030-2_minsum__run_20261005T010722Z__genetic__minsum__uniform-0000030-2-ac5ba17054__r00__8cec5c989af0.gif.metadata.json`
- `animations/genetic/us-night-0000030.instance_minmax__run_20261005T010723Z__genetic__minmax__us-night-0000030.instance-3704ac03dc__r00__a1729896bb24.gif`
- `animations/genetic/us-night-0000030.instance_minmax__run_20261005T010723Z__genetic__minmax__us-night-0000030.instance-3704ac03dc__r00__a1729896bb24.gif.metadata.json`
- `animations/genetic/us-night-0000030.instance_minsum__run_20261005T010723Z__genetic__minsum__us-night-0000030.instance-3704ac03dc__r00__be8815efe0a8.gif`
- `animations/genetic/us-night-0000030.instance_minsum__run_20261005T010723Z__genetic__minsum__us-night-0000030.instance-3704ac03dc__r00__be8815efe0a8.gif.metadata.json`
- `animations/greedy/att48_minmax__run_20261005T010716Z__greedy__minmax__att48-5196644df6__r00__3a8d465e875e.gif`
- `animations/greedy/att48_minmax__run_20261005T010716Z__greedy__minmax__att48-5196644df6__r00__3a8d465e875e.gif.metadata.json`
- `animations/greedy/att48_minsum__run_20261005T010716Z__greedy__minsum__att48-5196644df6__r00__81c47e47432b.gif`
- `animations/greedy/att48_minsum__run_20261005T010716Z__greedy__minsum__att48-5196644df6__r00__81c47e47432b.gif.metadata.json`
- `animations/greedy/berlin52_minmax__run_20261005T010717Z__greedy__minmax__berlin52-6ce5d7089a__r00__7f6e03072cc8.gif`
- `animations/greedy/berlin52_minmax__run_20261005T010717Z__greedy__minmax__berlin52-6ce5d7089a__r00__7f6e03072cc8.gif.metadata.json`
- `animations/greedy/berlin52_minsum__run_20261005T010717Z__greedy__minsum__berlin52-6ce5d7089a__r00__cb47880e530f.gif`
- `animations/greedy/berlin52_minsum__run_20261005T010717Z__greedy__minsum__berlin52-6ce5d7089a__r00__cb47880e530f.gif.metadata.json`
- `animations/greedy/eil51_minmax__run_20261005T010718Z__greedy__minmax__eil51-e956423838__r00__cee7a137bc78.gif`
- `animations/greedy/eil51_minmax__run_20261005T010718Z__greedy__minmax__eil51-e956423838__r00__cee7a137bc78.gif.metadata.json`
- `animations/greedy/eil51_minsum__run_20261005T010718Z__greedy__minsum__eil51-e956423838__r00__9e7387b85599.gif`
- `animations/greedy/eil51_minsum__run_20261005T010718Z__greedy__minsum__eil51-e956423838__r00__9e7387b85599.gif.metadata.json`
- `animations/greedy/euro-night-0000030_minmax__run_20261005T010719Z__greedy__minmax__euro-night-0000030-164a859873__r00__7aa2e8a8ac33.gif`
- `animations/greedy/euro-night-0000030_minmax__run_20261005T010719Z__greedy__minmax__euro-night-0000030-164a859873__r00__7aa2e8a8ac33.gif.metadata.json`
- `animations/greedy/euro-night-0000030_minsum__run_20261005T010720Z__greedy__minsum__euro-night-0000030-164a859873__r00__30eefa2f1c12.gif`
- `animations/greedy/euro-night-0000030_minsum__run_20261005T010720Z__greedy__minsum__euro-night-0000030-164a859873__r00__30eefa2f1c12.gif.metadata.json`
- `animations/greedy/london-0000030.instance_minmax__run_20261005T010720Z__greedy__minmax__london-0000030.instance-3bd5c0ea09__r00__d496e2bed1d5.gif`
- `animations/greedy/london-0000030.instance_minmax__run_20261005T010720Z__greedy__minmax__london-0000030.instance-3bd5c0ea09__r00__d496e2bed1d5.gif.metadata.json`
- `animations/greedy/london-0000030.instance_minsum__run_20261005T010720Z__greedy__minsum__london-0000030.instance-3bd5c0ea09__r00__d76a5e3d0e75.gif`
- `animations/greedy/london-0000030.instance_minsum__run_20261005T010720Z__greedy__minsum__london-0000030.instance-3bd5c0ea09__r00__d76a5e3d0e75.gif.metadata.json`
- `animations/greedy/sbgdb-20200507-fpg-poly_0000000030_minmax__run_20261005T010720Z__greedy__minmax__sbgdb-20200507-fpg-poly_0000000030-f90__r00__d1b5eff5c936.gif`
- `animations/greedy/sbgdb-20200507-fpg-poly_0000000030_minmax__run_20261005T010720Z__greedy__minmax__sbgdb-20200507-fpg-poly_0000000030-f90__r00__d1b5eff5c936.gif.metadata.json`
- `animations/greedy/sbgdb-20200507-fpg-poly_0000000030_minsum__run_20261005T010721Z__greedy__minsum__sbgdb-20200507-fpg-poly_0000000030-f90__r00__2a4f60a61449.gif`
- `animations/greedy/sbgdb-20200507-fpg-poly_0000000030_minsum__run_20261005T010721Z__greedy__minsum__sbgdb-20200507-fpg-poly_0000000030-f90__r00__2a4f60a61449.gif.metadata.json`
- `animations/greedy/sbgdb-20200507-pntset-0000030_minmax__run_20261005T010721Z__greedy__minmax__sbgdb-20200507-pntset-0000030-d00c0eac__r00__1f709da5d3b9.gif`
- `animations/greedy/sbgdb-20200507-pntset-0000030_minmax__run_20261005T010721Z__greedy__minmax__sbgdb-20200507-pntset-0000030-d00c0eac__r00__1f709da5d3b9.gif.metadata.json`
- `animations/greedy/sbgdb-20200507-pntset-0000030_minsum__run_20261005T010721Z__greedy__minsum__sbgdb-20200507-pntset-0000030-d00c0eac__r00__cd17eed2dde3.gif`
- `animations/greedy/sbgdb-20200507-pntset-0000030_minsum__run_20261005T010721Z__greedy__minsum__sbgdb-20200507-pntset-0000030-d00c0eac__r00__cd17eed2dde3.gif.metadata.json`
- `animations/greedy/stars-0000030.instance_minmax__run_20261005T010722Z__greedy__minmax__stars-0000030.instance-2f24c57225__r00__e548adeb16eb.gif`
- `animations/greedy/stars-0000030.instance_minmax__run_20261005T010722Z__greedy__minmax__stars-0000030.instance-2f24c57225__r00__e548adeb16eb.gif.metadata.json`
- `animations/greedy/stars-0000030.instance_minsum__run_20261005T010722Z__greedy__minsum__stars-0000030.instance-2f24c57225__r00__712103b642aa.gif`
- `animations/greedy/stars-0000030.instance_minsum__run_20261005T010722Z__greedy__minsum__stars-0000030.instance-2f24c57225__r00__712103b642aa.gif.metadata.json`
- `animations/greedy/uniform-0000030-2_minmax__run_20261005T010722Z__greedy__minmax__uniform-0000030-2-ac5ba17054__r00__2549e77ac62d.gif`
- `animations/greedy/uniform-0000030-2_minmax__run_20261005T010722Z__greedy__minmax__uniform-0000030-2-ac5ba17054__r00__2549e77ac62d.gif.metadata.json`
- `animations/greedy/uniform-0000030-2_minsum__run_20261005T010722Z__greedy__minsum__uniform-0000030-2-ac5ba17054__r00__5052bea8381c.gif`
- `animations/greedy/uniform-0000030-2_minsum__run_20261005T010722Z__greedy__minsum__uniform-0000030-2-ac5ba17054__r00__5052bea8381c.gif.metadata.json`
- `animations/greedy/us-night-0000030.instance_minmax__run_20261005T010723Z__greedy__minmax__us-night-0000030.instance-3704ac03dc__r00__cd07038acabe.gif`
- `animations/greedy/us-night-0000030.instance_minmax__run_20261005T010723Z__greedy__minmax__us-night-0000030.instance-3704ac03dc__r00__cd07038acabe.gif.metadata.json`
- `animations/greedy/us-night-0000030.instance_minsum__run_20261005T010723Z__greedy__minsum__us-night-0000030.instance-3704ac03dc__r00__2dbbc9169a12.gif`
- `animations/greedy/us-night-0000030.instance_minsum__run_20261005T010723Z__greedy__minsum__us-night-0000030.instance-3704ac03dc__r00__2dbbc9169a12.gif.metadata.json`
- `animations/local-search/att48_minmax__run_20261005T010716Z__local-search__minmax__att48-5196644df6__r00__e90accae173a.gif`
- `animations/local-search/att48_minmax__run_20261005T010716Z__local-search__minmax__att48-5196644df6__r00__e90accae173a.gif.metadata.json`
- `animations/local-search/att48_minsum__run_20261005T010717Z__local-search__minsum__att48-5196644df6__r00__2140dc5e37ed.gif`
- `animations/local-search/att48_minsum__run_20261005T010717Z__local-search__minsum__att48-5196644df6__r00__2140dc5e37ed.gif.metadata.json`
- `animations/local-search/berlin52_minmax__run_20261005T010717Z__local-search__minmax__berlin52-6ce5d7089a__r00__47f65c4850ba.gif`
- `animations/local-search/berlin52_minmax__run_20261005T010717Z__local-search__minmax__berlin52-6ce5d7089a__r00__47f65c4850ba.gif.metadata.json`
- `animations/local-search/berlin52_minsum__run_20261005T010717Z__local-search__minsum__berlin52-6ce5d7089a__r00__266c83046abb.gif`
- `animations/local-search/berlin52_minsum__run_20261005T010717Z__local-search__minsum__berlin52-6ce5d7089a__r00__266c83046abb.gif.metadata.json`
- `animations/local-search/eil51_minmax__run_20261005T010718Z__local-search__minmax__eil51-e956423838__r00__0d4fa7bbd379.gif`
- `animations/local-search/eil51_minmax__run_20261005T010718Z__local-search__minmax__eil51-e956423838__r00__0d4fa7bbd379.gif.metadata.json`
- `animations/local-search/eil51_minsum__run_20261005T010719Z__local-search__minsum__eil51-e956423838__r00__2bd3e1f541a5.gif`
- `animations/local-search/eil51_minsum__run_20261005T010719Z__local-search__minsum__eil51-e956423838__r00__2bd3e1f541a5.gif.metadata.json`
- `animations/local-search/euro-night-0000030_minmax__run_20261005T010719Z__local-search__minmax__euro-night-0000030-164a859873__r00__6fa4c399bd45.gif`
- `animations/local-search/euro-night-0000030_minmax__run_20261005T010719Z__local-search__minmax__euro-night-0000030-164a859873__r00__6fa4c399bd45.gif.metadata.json`
- `animations/local-search/euro-night-0000030_minsum__run_20261005T010720Z__local-search__minsum__euro-night-0000030-164a859873__r00__a17e451ac114.gif`
- `animations/local-search/euro-night-0000030_minsum__run_20261005T010720Z__local-search__minsum__euro-night-0000030-164a859873__r00__a17e451ac114.gif.metadata.json`
- `animations/local-search/london-0000030.instance_minmax__run_20261005T010720Z__local-search__minmax__london-0000030.instance-3bd5c0ea09__r00__42338f2400ef.gif`
- `animations/local-search/london-0000030.instance_minmax__run_20261005T010720Z__local-search__minmax__london-0000030.instance-3bd5c0ea09__r00__42338f2400ef.gif.metadata.json`
- `animations/local-search/london-0000030.instance_minsum__run_20261005T010720Z__local-search__minsum__london-0000030.instance-3bd5c0ea09__r00__2fcc858e8292.gif`
- `animations/local-search/london-0000030.instance_minsum__run_20261005T010720Z__local-search__minsum__london-0000030.instance-3bd5c0ea09__r00__2fcc858e8292.gif.metadata.json`
- `animations/local-search/sbgdb-20200507-fpg-poly_0000000030_minmax__run_20261005T010721Z__local-search__minmax__sbgdb-20200507-fpg-poly_0000000030-f90__r00__663cd5ed60e6.gif`
- `animations/local-search/sbgdb-20200507-fpg-poly_0000000030_minmax__run_20261005T010721Z__local-search__minmax__sbgdb-20200507-fpg-poly_0000000030-f90__r00__663cd5ed60e6.gif.metadata.json`
- `animations/local-search/sbgdb-20200507-fpg-poly_0000000030_minsum__run_20261005T010721Z__local-search__minsum__sbgdb-20200507-fpg-poly_0000000030-f90__r00__e857e2a95449.gif`
- `animations/local-search/sbgdb-20200507-fpg-poly_0000000030_minsum__run_20261005T010721Z__local-search__minsum__sbgdb-20200507-fpg-poly_0000000030-f90__r00__e857e2a95449.gif.metadata.json`
- `animations/local-search/sbgdb-20200507-pntset-0000030_minmax__run_20261005T010721Z__local-search__minmax__sbgdb-20200507-pntset-0000030-d00c0eac__r00__18b2d81b24c8.gif`
- `animations/local-search/sbgdb-20200507-pntset-0000030_minmax__run_20261005T010721Z__local-search__minmax__sbgdb-20200507-pntset-0000030-d00c0eac__r00__18b2d81b24c8.gif.metadata.json`
- `animations/local-search/sbgdb-20200507-pntset-0000030_minsum__run_20261005T010721Z__local-search__minsum__sbgdb-20200507-pntset-0000030-d00c0eac__r00__47a7bc78399f.gif`
- `animations/local-search/sbgdb-20200507-pntset-0000030_minsum__run_20261005T010721Z__local-search__minsum__sbgdb-20200507-pntset-0000030-d00c0eac__r00__47a7bc78399f.gif.metadata.json`
- `animations/local-search/stars-0000030.instance_minmax__run_20261005T010722Z__local-search__minmax__stars-0000030.instance-2f24c57225__r00__ae5af01b76d5.gif`
- `animations/local-search/stars-0000030.instance_minmax__run_20261005T010722Z__local-search__minmax__stars-0000030.instance-2f24c57225__r00__ae5af01b76d5.gif.metadata.json`
- `animations/local-search/stars-0000030.instance_minsum__run_20261005T010722Z__local-search__minsum__stars-0000030.instance-2f24c57225__r00__0aae5dde10dc.gif`
- `animations/local-search/stars-0000030.instance_minsum__run_20261005T010722Z__local-search__minsum__stars-0000030.instance-2f24c57225__r00__0aae5dde10dc.gif.metadata.json`
- `animations/local-search/uniform-0000030-2_minmax__run_20261005T010722Z__local-search__minmax__uniform-0000030-2-ac5ba17054__r00__86fd4001ca35.gif`
- `animations/local-search/uniform-0000030-2_minmax__run_20261005T010722Z__local-search__minmax__uniform-0000030-2-ac5ba17054__r00__86fd4001ca35.gif.metadata.json`
- `animations/local-search/uniform-0000030-2_minsum__run_20261005T010723Z__local-search__minsum__uniform-0000030-2-ac5ba17054__r00__f7dc62a70598.gif`
- `animations/local-search/uniform-0000030-2_minsum__run_20261005T010723Z__local-search__minsum__uniform-0000030-2-ac5ba17054__r00__f7dc62a70598.gif.metadata.json`
- `animations/local-search/us-night-0000030.instance_minmax__run_20261005T010723Z__local-search__minmax__us-night-0000030.instance-3704ac03dc__r00__8405fcf8cd20.gif`
- `animations/local-search/us-night-0000030.instance_minmax__run_20261005T010723Z__local-search__minmax__us-night-0000030.instance-3704ac03dc__r00__8405fcf8cd20.gif.metadata.json`
- `animations/local-search/us-night-0000030.instance_minsum__run_20261005T010723Z__local-search__minsum__us-night-0000030.instance-3704ac03dc__r00__22a21449f143.gif`
- `animations/local-search/us-night-0000030.instance_minsum__run_20261005T010723Z__local-search__minsum__us-night-0000030.instance-3704ac03dc__r00__22a21449f143.gif.metadata.json`
- `animations/lp-rounding/att48_minmax__run_20261005T010716Z__lp-rounding__minmax__att48-5196644df6__r00__afe6d7ff6313.gif`
- `animations/lp-rounding/att48_minmax__run_20261005T010716Z__lp-rounding__minmax__att48-5196644df6__r00__afe6d7ff6313.gif.metadata.json`
- `animations/lp-rounding/att48_minsum__run_20261005T010717Z__lp-rounding__minsum__att48-5196644df6__r00__25e5d3e4977a.gif`
- `animations/lp-rounding/att48_minsum__run_20261005T010717Z__lp-rounding__minsum__att48-5196644df6__r00__25e5d3e4977a.gif.metadata.json`
- `animations/lp-rounding/berlin52_minmax__run_20261005T010717Z__lp-rounding__minmax__berlin52-6ce5d7089a__r00__9a13e6cf3c96.gif`
- `animations/lp-rounding/berlin52_minmax__run_20261005T010717Z__lp-rounding__minmax__berlin52-6ce5d7089a__r00__9a13e6cf3c96.gif.metadata.json`
- `animations/lp-rounding/berlin52_minsum__run_20261005T010718Z__lp-rounding__minsum__berlin52-6ce5d7089a__r00__9e13462268e2.gif`
- `animations/lp-rounding/berlin52_minsum__run_20261005T010718Z__lp-rounding__minsum__berlin52-6ce5d7089a__r00__9e13462268e2.gif.metadata.json`
- `animations/lp-rounding/eil51_minmax__run_20261005T010718Z__lp-rounding__minmax__eil51-e956423838__r00__81f5155900af.gif`
- `animations/lp-rounding/eil51_minmax__run_20261005T010718Z__lp-rounding__minmax__eil51-e956423838__r00__81f5155900af.gif.metadata.json`
- `animations/lp-rounding/eil51_minsum__run_20261005T010719Z__lp-rounding__minsum__eil51-e956423838__r00__1767800f683d.gif`
- `animations/lp-rounding/eil51_minsum__run_20261005T010719Z__lp-rounding__minsum__eil51-e956423838__r00__1767800f683d.gif.metadata.json`
- `animations/lp-rounding/euro-night-0000030_minmax__run_20261005T010719Z__lp-rounding__minmax__euro-night-0000030-164a859873__r00__b6fe10dded74.gif`
- `animations/lp-rounding/euro-night-0000030_minmax__run_20261005T010719Z__lp-rounding__minmax__euro-night-0000030-164a859873__r00__b6fe10dded74.gif.metadata.json`
- `animations/lp-rounding/euro-night-0000030_minsum__run_20261005T010720Z__lp-rounding__minsum__euro-night-0000030-164a859873__r00__f3048428fe7f.gif`
- `animations/lp-rounding/euro-night-0000030_minsum__run_20261005T010720Z__lp-rounding__minsum__euro-night-0000030-164a859873__r00__f3048428fe7f.gif.metadata.json`
- `animations/lp-rounding/london-0000030.instance_minmax__run_20261005T010720Z__lp-rounding__minmax__london-0000030.instance-3bd5c0ea09__r00__d808409ecac0.gif`
- `animations/lp-rounding/london-0000030.instance_minmax__run_20261005T010720Z__lp-rounding__minmax__london-0000030.instance-3bd5c0ea09__r00__d808409ecac0.gif.metadata.json`
- `animations/lp-rounding/london-0000030.instance_minsum__run_20261005T010720Z__lp-rounding__minsum__london-0000030.instance-3bd5c0ea09__r00__105612b1da2d.gif`
- `animations/lp-rounding/london-0000030.instance_minsum__run_20261005T010720Z__lp-rounding__minsum__london-0000030.instance-3bd5c0ea09__r00__105612b1da2d.gif.metadata.json`
- `animations/lp-rounding/sbgdb-20200507-fpg-poly_0000000030_minmax__run_20261005T010721Z__lp-rounding__minmax__sbgdb-20200507-fpg-poly_0000000030-f90__r00__6c0061e5c380.gif`
- `animations/lp-rounding/sbgdb-20200507-fpg-poly_0000000030_minmax__run_20261005T010721Z__lp-rounding__minmax__sbgdb-20200507-fpg-poly_0000000030-f90__r00__6c0061e5c380.gif.metadata.json`
- `animations/lp-rounding/sbgdb-20200507-fpg-poly_0000000030_minsum__run_20261005T010721Z__lp-rounding__minsum__sbgdb-20200507-fpg-poly_0000000030-f90__r00__5c14d7929dad.gif`
- `animations/lp-rounding/sbgdb-20200507-fpg-poly_0000000030_minsum__run_20261005T010721Z__lp-rounding__minsum__sbgdb-20200507-fpg-poly_0000000030-f90__r00__5c14d7929dad.gif.metadata.json`
- `animations/lp-rounding/sbgdb-20200507-pntset-0000030_minmax__run_20261005T010721Z__lp-rounding__minmax__sbgdb-20200507-pntset-0000030-d00c0eac__r00__8057871a3201.gif`
- `animations/lp-rounding/sbgdb-20200507-pntset-0000030_minmax__run_20261005T010721Z__lp-rounding__minmax__sbgdb-20200507-pntset-0000030-d00c0eac__r00__8057871a3201.gif.metadata.json`
- `animations/lp-rounding/sbgdb-20200507-pntset-0000030_minsum__run_20261005T010721Z__lp-rounding__minsum__sbgdb-20200507-pntset-0000030-d00c0eac__r00__147e77ad6180.gif`
- `animations/lp-rounding/sbgdb-20200507-pntset-0000030_minsum__run_20261005T010721Z__lp-rounding__minsum__sbgdb-20200507-pntset-0000030-d00c0eac__r00__147e77ad6180.gif.metadata.json`
- `animations/lp-rounding/stars-0000030.instance_minmax__run_20261005T010722Z__lp-rounding__minmax__stars-0000030.instance-2f24c57225__r00__a760d23c959d.gif`
- `animations/lp-rounding/stars-0000030.instance_minmax__run_20261005T010722Z__lp-rounding__minmax__stars-0000030.instance-2f24c57225__r00__a760d23c959d.gif.metadata.json`
- `animations/lp-rounding/stars-0000030.instance_minsum__run_20261005T010722Z__lp-rounding__minsum__stars-0000030.instance-2f24c57225__r00__6402ca7777d0.gif`
- `animations/lp-rounding/stars-0000030.instance_minsum__run_20261005T010722Z__lp-rounding__minsum__stars-0000030.instance-2f24c57225__r00__6402ca7777d0.gif.metadata.json`
- `animations/lp-rounding/uniform-0000030-2_minmax__run_20261005T010722Z__lp-rounding__minmax__uniform-0000030-2-ac5ba17054__r00__808b1bfd9025.gif`
- `animations/lp-rounding/uniform-0000030-2_minmax__run_20261005T010722Z__lp-rounding__minmax__uniform-0000030-2-ac5ba17054__r00__808b1bfd9025.gif.metadata.json`
- `animations/lp-rounding/uniform-0000030-2_minsum__run_20261005T010723Z__lp-rounding__minsum__uniform-0000030-2-ac5ba17054__r00__aa5d27d0122a.gif`
- `animations/lp-rounding/uniform-0000030-2_minsum__run_20261005T010723Z__lp-rounding__minsum__uniform-0000030-2-ac5ba17054__r00__aa5d27d0122a.gif.metadata.json`
- `animations/lp-rounding/us-night-0000030.instance_minmax__run_20261005T010723Z__lp-rounding__minmax__us-night-0000030.instance-3704ac03dc__r00__3b7d58214e62.gif`
- `animations/lp-rounding/us-night-0000030.instance_minmax__run_20261005T010723Z__lp-rounding__minmax__us-night-0000030.instance-3704ac03dc__r00__3b7d58214e62.gif.metadata.json`
- `animations/lp-rounding/us-night-0000030.instance_minsum__run_20261005T010723Z__lp-rounding__minsum__us-night-0000030.instance-3704ac03dc__r00__a7b136e16bc8.gif`
- `animations/lp-rounding/us-night-0000030.instance_minsum__run_20261005T010723Z__lp-rounding__minsum__us-night-0000030.instance-3704ac03dc__r00__a7b136e16bc8.gif.metadata.json`
- `animations/nn/att48_minmax__run_20261005T010716Z__nn__minmax__att48-5196644df6__r00__f0ca3c8ad947.gif`
- `animations/nn/att48_minmax__run_20261005T010716Z__nn__minmax__att48-5196644df6__r00__f0ca3c8ad947.gif.metadata.json`
- `animations/nn/att48_minsum__run_20261005T010717Z__nn__minsum__att48-5196644df6__r00__33ed4dfffe61.gif`
- `animations/nn/att48_minsum__run_20261005T010717Z__nn__minsum__att48-5196644df6__r00__33ed4dfffe61.gif.metadata.json`
- `animations/nn/berlin52_minmax__run_20261005T010717Z__nn__minmax__berlin52-6ce5d7089a__r00__22d41ac68e34.gif`
- `animations/nn/berlin52_minmax__run_20261005T010717Z__nn__minmax__berlin52-6ce5d7089a__r00__22d41ac68e34.gif.metadata.json`
- `animations/nn/berlin52_minsum__run_20261005T010718Z__nn__minsum__berlin52-6ce5d7089a__r00__b5e1813cee9b.gif`
- `animations/nn/berlin52_minsum__run_20261005T010718Z__nn__minsum__berlin52-6ce5d7089a__r00__b5e1813cee9b.gif.metadata.json`
- `animations/nn/eil51_minmax__run_20261005T010718Z__nn__minmax__eil51-e956423838__r00__ca6b74c99f95.gif`
- `animations/nn/eil51_minmax__run_20261005T010718Z__nn__minmax__eil51-e956423838__r00__ca6b74c99f95.gif.metadata.json`
- `animations/nn/eil51_minsum__run_20261005T010719Z__nn__minsum__eil51-e956423838__r00__dd6757e68806.gif`
- `animations/nn/eil51_minsum__run_20261005T010719Z__nn__minsum__eil51-e956423838__r00__dd6757e68806.gif.metadata.json`
- `animations/nn/euro-night-0000030_minmax__run_20261005T010719Z__nn__minmax__euro-night-0000030-164a859873__r00__92ed34c2c7b6.gif`
- `animations/nn/euro-night-0000030_minmax__run_20261005T010719Z__nn__minmax__euro-night-0000030-164a859873__r00__92ed34c2c7b6.gif.metadata.json`
- `animations/nn/euro-night-0000030_minsum__run_20261005T010720Z__nn__minsum__euro-night-0000030-164a859873__r00__8bec7ed1aaad.gif`
- `animations/nn/euro-night-0000030_minsum__run_20261005T010720Z__nn__minsum__euro-night-0000030-164a859873__r00__8bec7ed1aaad.gif.metadata.json`
- `animations/nn/london-0000030.instance_minmax__run_20261005T010720Z__nn__minmax__london-0000030.instance-3bd5c0ea09__r00__98357c4a4b6a.gif`
- `animations/nn/london-0000030.instance_minmax__run_20261005T010720Z__nn__minmax__london-0000030.instance-3bd5c0ea09__r00__98357c4a4b6a.gif.metadata.json`
- `animations/nn/london-0000030.instance_minsum__run_20261005T010720Z__nn__minsum__london-0000030.instance-3bd5c0ea09__r00__0622cda5bf92.gif`
- `animations/nn/london-0000030.instance_minsum__run_20261005T010720Z__nn__minsum__london-0000030.instance-3bd5c0ea09__r00__0622cda5bf92.gif.metadata.json`
- `animations/nn/sbgdb-20200507-fpg-poly_0000000030_minmax__run_20261005T010721Z__nn__minmax__sbgdb-20200507-fpg-poly_0000000030-f90__r00__209980e03688.gif`
- `animations/nn/sbgdb-20200507-fpg-poly_0000000030_minmax__run_20261005T010721Z__nn__minmax__sbgdb-20200507-fpg-poly_0000000030-f90__r00__209980e03688.gif.metadata.json`
- `animations/nn/sbgdb-20200507-fpg-poly_0000000030_minsum__run_20261005T010721Z__nn__minsum__sbgdb-20200507-fpg-poly_0000000030-f90__r00__2a17a68bd76d.gif`
- `animations/nn/sbgdb-20200507-fpg-poly_0000000030_minsum__run_20261005T010721Z__nn__minsum__sbgdb-20200507-fpg-poly_0000000030-f90__r00__2a17a68bd76d.gif.metadata.json`
- `animations/nn/sbgdb-20200507-pntset-0000030_minmax__run_20261005T010721Z__nn__minmax__sbgdb-20200507-pntset-0000030-d00c0eac__r00__57dd8d9daf8d.gif`
- `animations/nn/sbgdb-20200507-pntset-0000030_minmax__run_20261005T010721Z__nn__minmax__sbgdb-20200507-pntset-0000030-d00c0eac__r00__57dd8d9daf8d.gif.metadata.json`
- `animations/nn/sbgdb-20200507-pntset-0000030_minsum__run_20261005T010721Z__nn__minsum__sbgdb-20200507-pntset-0000030-d00c0eac__r00__de19b235a34d.gif`
- `animations/nn/sbgdb-20200507-pntset-0000030_minsum__run_20261005T010721Z__nn__minsum__sbgdb-20200507-pntset-0000030-d00c0eac__r00__de19b235a34d.gif.metadata.json`
- `animations/nn/stars-0000030.instance_minmax__run_20261005T010722Z__nn__minmax__stars-0000030.instance-2f24c57225__r00__230a0c01ee3b.gif`
- `animations/nn/stars-0000030.instance_minmax__run_20261005T010722Z__nn__minmax__stars-0000030.instance-2f24c57225__r00__230a0c01ee3b.gif.metadata.json`
- `animations/nn/stars-0000030.instance_minsum__run_20261005T010722Z__nn__minsum__stars-0000030.instance-2f24c57225__r00__135875006040.gif`
- `animations/nn/stars-0000030.instance_minsum__run_20261005T010722Z__nn__minsum__stars-0000030.instance-2f24c57225__r00__135875006040.gif.metadata.json`
- `animations/nn/uniform-0000030-2_minmax__run_20261005T010722Z__nn__minmax__uniform-0000030-2-ac5ba17054__r00__5348b1c1bf7a.gif`
- `animations/nn/uniform-0000030-2_minmax__run_20261005T010722Z__nn__minmax__uniform-0000030-2-ac5ba17054__r00__5348b1c1bf7a.gif.metadata.json`
- `animations/nn/uniform-0000030-2_minsum__run_20261005T010723Z__nn__minsum__uniform-0000030-2-ac5ba17054__r00__1aea58757c7d.gif`
- `animations/nn/uniform-0000030-2_minsum__run_20261005T010723Z__nn__minsum__uniform-0000030-2-ac5ba17054__r00__1aea58757c7d.gif.metadata.json`
- `animations/nn/us-night-0000030.instance_minmax__run_20261005T010723Z__nn__minmax__us-night-0000030.instance-3704ac03dc__r00__a8c0154fb4e3.gif`
- `animations/nn/us-night-0000030.instance_minmax__run_20261005T010723Z__nn__minmax__us-night-0000030.instance-3704ac03dc__r00__a8c0154fb4e3.gif.metadata.json`
- `animations/nn/us-night-0000030.instance_minsum__run_20261005T010723Z__nn__minsum__us-night-0000030.instance-3704ac03dc__r00__adf0d0b2fe80.gif`
- `animations/nn/us-night-0000030.instance_minsum__run_20261005T010723Z__nn__minsum__us-night-0000030.instance-3704ac03dc__r00__adf0d0b2fe80.gif.metadata.json`
- `animations/primal-dual/att48_minmax__run_20261005T010716Z__primal-dual__minmax__att48-5196644df6__r00__e5b3bb72863a.gif`
- `animations/primal-dual/att48_minmax__run_20261005T010716Z__primal-dual__minmax__att48-5196644df6__r00__e5b3bb72863a.gif.metadata.json`
- `animations/primal-dual/att48_minsum__run_20261005T010717Z__primal-dual__minsum__att48-5196644df6__r00__4277c7a96d17.gif`
- `animations/primal-dual/att48_minsum__run_20261005T010717Z__primal-dual__minsum__att48-5196644df6__r00__4277c7a96d17.gif.metadata.json`
- `animations/primal-dual/berlin52_minmax__run_20261005T010717Z__primal-dual__minmax__berlin52-6ce5d7089a__r00__ea946355810b.gif`
- `animations/primal-dual/berlin52_minmax__run_20261005T010717Z__primal-dual__minmax__berlin52-6ce5d7089a__r00__ea946355810b.gif.metadata.json`
- `animations/primal-dual/berlin52_minsum__run_20261005T010718Z__primal-dual__minsum__berlin52-6ce5d7089a__r00__dc6e60cc6b5c.gif`
- `animations/primal-dual/berlin52_minsum__run_20261005T010718Z__primal-dual__minsum__berlin52-6ce5d7089a__r00__dc6e60cc6b5c.gif.metadata.json`
- `animations/primal-dual/eil51_minmax__run_20261005T010718Z__primal-dual__minmax__eil51-e956423838__r00__3727e203c255.gif`
- `animations/primal-dual/eil51_minmax__run_20261005T010718Z__primal-dual__minmax__eil51-e956423838__r00__3727e203c255.gif.metadata.json`
- `animations/primal-dual/eil51_minsum__run_20261005T010719Z__primal-dual__minsum__eil51-e956423838__r00__cd4b9d83dbb6.gif`
- `animations/primal-dual/eil51_minsum__run_20261005T010719Z__primal-dual__minsum__eil51-e956423838__r00__cd4b9d83dbb6.gif.metadata.json`
- `animations/primal-dual/euro-night-0000030_minmax__run_20261005T010719Z__primal-dual__minmax__euro-night-0000030-164a859873__r00__a149dedacacb.gif`
- `animations/primal-dual/euro-night-0000030_minmax__run_20261005T010719Z__primal-dual__minmax__euro-night-0000030-164a859873__r00__a149dedacacb.gif.metadata.json`
- `animations/primal-dual/euro-night-0000030_minsum__run_20261005T010720Z__primal-dual__minsum__euro-night-0000030-164a859873__r00__6de0a9948be9.gif`
- `animations/primal-dual/euro-night-0000030_minsum__run_20261005T010720Z__primal-dual__minsum__euro-night-0000030-164a859873__r00__6de0a9948be9.gif.metadata.json`
- `animations/primal-dual/london-0000030.instance_minmax__run_20261005T010720Z__primal-dual__minmax__london-0000030.instance-3bd5c0ea09__r00__86f7e1c3a196.gif`
- `animations/primal-dual/london-0000030.instance_minmax__run_20261005T010720Z__primal-dual__minmax__london-0000030.instance-3bd5c0ea09__r00__86f7e1c3a196.gif.metadata.json`
- `animations/primal-dual/london-0000030.instance_minsum__run_20261005T010720Z__primal-dual__minsum__london-0000030.instance-3bd5c0ea09__r00__617e9f9193b7.gif`
- `animations/primal-dual/london-0000030.instance_minsum__run_20261005T010720Z__primal-dual__minsum__london-0000030.instance-3bd5c0ea09__r00__617e9f9193b7.gif.metadata.json`
- `animations/primal-dual/sbgdb-20200507-fpg-poly_0000000030_minmax__run_20261005T010721Z__primal-dual__minmax__sbgdb-20200507-fpg-poly_0000000030-f90__r00__56fd60ce7389.gif`
- `animations/primal-dual/sbgdb-20200507-fpg-poly_0000000030_minmax__run_20261005T010721Z__primal-dual__minmax__sbgdb-20200507-fpg-poly_0000000030-f90__r00__56fd60ce7389.gif.metadata.json`
- `animations/primal-dual/sbgdb-20200507-fpg-poly_0000000030_minsum__run_20261005T010721Z__primal-dual__minsum__sbgdb-20200507-fpg-poly_0000000030-f90__r00__ca876de47a96.gif`
- `animations/primal-dual/sbgdb-20200507-fpg-poly_0000000030_minsum__run_20261005T010721Z__primal-dual__minsum__sbgdb-20200507-fpg-poly_0000000030-f90__r00__ca876de47a96.gif.metadata.json`
- `animations/primal-dual/sbgdb-20200507-pntset-0000030_minmax__run_20261005T010721Z__primal-dual__minmax__sbgdb-20200507-pntset-0000030-d00c0eac__r00__9642ab73bd2b.gif`
- `animations/primal-dual/sbgdb-20200507-pntset-0000030_minmax__run_20261005T010721Z__primal-dual__minmax__sbgdb-20200507-pntset-0000030-d00c0eac__r00__9642ab73bd2b.gif.metadata.json`
- `animations/primal-dual/sbgdb-20200507-pntset-0000030_minsum__run_20261005T010721Z__primal-dual__minsum__sbgdb-20200507-pntset-0000030-d00c0eac__r00__6f065d7d5a07.gif`
- `animations/primal-dual/sbgdb-20200507-pntset-0000030_minsum__run_20261005T010721Z__primal-dual__minsum__sbgdb-20200507-pntset-0000030-d00c0eac__r00__6f065d7d5a07.gif.metadata.json`
- `animations/primal-dual/stars-0000030.instance_minmax__run_20261005T010722Z__primal-dual__minmax__stars-0000030.instance-2f24c57225__r00__cfbc3c0b01d8.gif`
- `animations/primal-dual/stars-0000030.instance_minmax__run_20261005T010722Z__primal-dual__minmax__stars-0000030.instance-2f24c57225__r00__cfbc3c0b01d8.gif.metadata.json`
- `animations/primal-dual/stars-0000030.instance_minsum__run_20261005T010722Z__primal-dual__minsum__stars-0000030.instance-2f24c57225__r00__9088366668a8.gif`
- `animations/primal-dual/stars-0000030.instance_minsum__run_20261005T010722Z__primal-dual__minsum__stars-0000030.instance-2f24c57225__r00__9088366668a8.gif.metadata.json`
- `animations/primal-dual/uniform-0000030-2_minmax__run_20261005T010722Z__primal-dual__minmax__uniform-0000030-2-ac5ba17054__r00__51fd0070ae76.gif`
- `animations/primal-dual/uniform-0000030-2_minmax__run_20261005T010722Z__primal-dual__minmax__uniform-0000030-2-ac5ba17054__r00__51fd0070ae76.gif.metadata.json`
- `animations/primal-dual/uniform-0000030-2_minsum__run_20261005T010723Z__primal-dual__minsum__uniform-0000030-2-ac5ba17054__r00__3238739bc283.gif`
- `animations/primal-dual/uniform-0000030-2_minsum__run_20261005T010723Z__primal-dual__minsum__uniform-0000030-2-ac5ba17054__r00__3238739bc283.gif.metadata.json`
- `animations/primal-dual/us-night-0000030.instance_minmax__run_20261005T010723Z__primal-dual__minmax__us-night-0000030.instance-3704ac03dc__r00__b44bae667acf.gif`
- `animations/primal-dual/us-night-0000030.instance_minmax__run_20261005T010723Z__primal-dual__minmax__us-night-0000030.instance-3704ac03dc__r00__b44bae667acf.gif.metadata.json`
- `animations/primal-dual/us-night-0000030.instance_minsum__run_20261005T010723Z__primal-dual__minsum__us-night-0000030.instance-3704ac03dc__r00__167853d1fc45.gif`
- `animations/primal-dual/us-night-0000030.instance_minsum__run_20261005T010723Z__primal-dual__minsum__us-night-0000030.instance-3704ac03dc__r00__167853d1fc45.gif.metadata.json`
- `animations/sa/att48_minmax__run_20261005T010716Z__sa__minmax__att48-5196644df6__r00__6fb903242033.gif`
- `animations/sa/att48_minmax__run_20261005T010716Z__sa__minmax__att48-5196644df6__r00__6fb903242033.gif.metadata.json`
- `animations/sa/att48_minsum__run_20261005T010717Z__sa__minsum__att48-5196644df6__r00__d32eba22d594.gif`
- `animations/sa/att48_minsum__run_20261005T010717Z__sa__minsum__att48-5196644df6__r00__d32eba22d594.gif.metadata.json`
- `animations/sa/berlin52_minmax__run_20261005T010717Z__sa__minmax__berlin52-6ce5d7089a__r00__bc31f4493905.gif`
- `animations/sa/berlin52_minmax__run_20261005T010717Z__sa__minmax__berlin52-6ce5d7089a__r00__bc31f4493905.gif.metadata.json`
- `animations/sa/berlin52_minsum__run_20261005T010718Z__sa__minsum__berlin52-6ce5d7089a__r00__97adae38bf5a.gif`
- `animations/sa/berlin52_minsum__run_20261005T010718Z__sa__minsum__berlin52-6ce5d7089a__r00__97adae38bf5a.gif.metadata.json`
- `animations/sa/eil51_minmax__run_20261005T010718Z__sa__minmax__eil51-e956423838__r00__3c760f12232c.gif`
- `animations/sa/eil51_minmax__run_20261005T010718Z__sa__minmax__eil51-e956423838__r00__3c760f12232c.gif.metadata.json`
- `animations/sa/eil51_minsum__run_20261005T010719Z__sa__minsum__eil51-e956423838__r00__0cfa900edb1f.gif`
- `animations/sa/eil51_minsum__run_20261005T010719Z__sa__minsum__eil51-e956423838__r00__0cfa900edb1f.gif.metadata.json`
- `animations/sa/euro-night-0000030_minmax__run_20261005T010719Z__sa__minmax__euro-night-0000030-164a859873__r00__fffc6d7ea16a.gif`
- `animations/sa/euro-night-0000030_minmax__run_20261005T010719Z__sa__minmax__euro-night-0000030-164a859873__r00__fffc6d7ea16a.gif.metadata.json`
- `animations/sa/euro-night-0000030_minsum__run_20261005T010720Z__sa__minsum__euro-night-0000030-164a859873__r00__12fa7f6ab298.gif`
- `animations/sa/euro-night-0000030_minsum__run_20261005T010720Z__sa__minsum__euro-night-0000030-164a859873__r00__12fa7f6ab298.gif.metadata.json`
- `animations/sa/london-0000030.instance_minmax__run_20261005T010720Z__sa__minmax__london-0000030.instance-3bd5c0ea09__r00__1c1e162c516a.gif`
- `animations/sa/london-0000030.instance_minmax__run_20261005T010720Z__sa__minmax__london-0000030.instance-3bd5c0ea09__r00__1c1e162c516a.gif.metadata.json`
- `animations/sa/london-0000030.instance_minsum__run_20261005T010720Z__sa__minsum__london-0000030.instance-3bd5c0ea09__r00__a9aff2591691.gif`
- `animations/sa/london-0000030.instance_minsum__run_20261005T010720Z__sa__minsum__london-0000030.instance-3bd5c0ea09__r00__a9aff2591691.gif.metadata.json`
- `animations/sa/sbgdb-20200507-fpg-poly_0000000030_minmax__run_20261005T010721Z__sa__minmax__sbgdb-20200507-fpg-poly_0000000030-f90__r00__e58ee83ad2b7.gif`
- `animations/sa/sbgdb-20200507-fpg-poly_0000000030_minmax__run_20261005T010721Z__sa__minmax__sbgdb-20200507-fpg-poly_0000000030-f90__r00__e58ee83ad2b7.gif.metadata.json`
- `animations/sa/sbgdb-20200507-fpg-poly_0000000030_minsum__run_20261005T010721Z__sa__minsum__sbgdb-20200507-fpg-poly_0000000030-f90__r00__9d452e2d0001.gif`
- `animations/sa/sbgdb-20200507-fpg-poly_0000000030_minsum__run_20261005T010721Z__sa__minsum__sbgdb-20200507-fpg-poly_0000000030-f90__r00__9d452e2d0001.gif.metadata.json`
- `animations/sa/sbgdb-20200507-pntset-0000030_minmax__run_20261005T010721Z__sa__minmax__sbgdb-20200507-pntset-0000030-d00c0eac__r00__2d70b230431a.gif`
- `animations/sa/sbgdb-20200507-pntset-0000030_minmax__run_20261005T010721Z__sa__minmax__sbgdb-20200507-pntset-0000030-d00c0eac__r00__2d70b230431a.gif.metadata.json`
- `animations/sa/sbgdb-20200507-pntset-0000030_minsum__run_20261005T010721Z__sa__minsum__sbgdb-20200507-pntset-0000030-d00c0eac__r00__64d1edd4b897.gif`
- `animations/sa/sbgdb-20200507-pntset-0000030_minsum__run_20261005T010721Z__sa__minsum__sbgdb-20200507-pntset-0000030-d00c0eac__r00__64d1edd4b897.gif.metadata.json`
- `animations/sa/stars-0000030.instance_minmax__run_20261005T010722Z__sa__minmax__stars-0000030.instance-2f24c57225__r00__a9f4dcec18e5.gif`
- `animations/sa/stars-0000030.instance_minmax__run_20261005T010722Z__sa__minmax__stars-0000030.instance-2f24c57225__r00__a9f4dcec18e5.gif.metadata.json`
- `animations/sa/stars-0000030.instance_minsum__run_20261005T010722Z__sa__minsum__stars-0000030.instance-2f24c57225__r00__945f182b50c0.gif`
- `animations/sa/stars-0000030.instance_minsum__run_20261005T010722Z__sa__minsum__stars-0000030.instance-2f24c57225__r00__945f182b50c0.gif.metadata.json`
- `animations/sa/uniform-0000030-2_minmax__run_20261005T010722Z__sa__minmax__uniform-0000030-2-ac5ba17054__r00__2c4349805216.gif`
- `animations/sa/uniform-0000030-2_minmax__run_20261005T010722Z__sa__minmax__uniform-0000030-2-ac5ba17054__r00__2c4349805216.gif.metadata.json`
- `animations/sa/uniform-0000030-2_minsum__run_20261005T010723Z__sa__minsum__uniform-0000030-2-ac5ba17054__r00__67ef8511537a.gif`
- `animations/sa/uniform-0000030-2_minsum__run_20261005T010723Z__sa__minsum__uniform-0000030-2-ac5ba17054__r00__67ef8511537a.gif.metadata.json`
- `animations/sa/us-night-0000030.instance_minmax__run_20261005T010723Z__sa__minmax__us-night-0000030.instance-3704ac03dc__r00__f195c2b02b3e.gif`
- `animations/sa/us-night-0000030.instance_minmax__run_20261005T010723Z__sa__minmax__us-night-0000030.instance-3704ac03dc__r00__f195c2b02b3e.gif.metadata.json`
- `animations/sa/us-night-0000030.instance_minsum__run_20261005T010723Z__sa__minsum__us-night-0000030.instance-3704ac03dc__r00__94d091dc21c6.gif`
- `animations/sa/us-night-0000030.instance_minsum__run_20261005T010723Z__sa__minsum__us-night-0000030.instance-3704ac03dc__r00__94d091dc21c6.gif.metadata.json`
- `animations/shifting/att48_minmax__run_20261005T010716Z__shifting__minmax__att48-5196644df6__r00__91b5d617ec20.gif`
- `animations/shifting/att48_minmax__run_20261005T010716Z__shifting__minmax__att48-5196644df6__r00__91b5d617ec20.gif.metadata.json`
- `animations/shifting/att48_minsum__run_20261005T010717Z__shifting__minsum__att48-5196644df6__r00__7bd354551f49.gif`
- `animations/shifting/att48_minsum__run_20261005T010717Z__shifting__minsum__att48-5196644df6__r00__7bd354551f49.gif.metadata.json`
- `animations/shifting/berlin52_minmax__run_20261005T010717Z__shifting__minmax__berlin52-6ce5d7089a__r00__c8724523af70.gif`
- `animations/shifting/berlin52_minmax__run_20261005T010717Z__shifting__minmax__berlin52-6ce5d7089a__r00__c8724523af70.gif.metadata.json`
- `animations/shifting/berlin52_minsum__run_20261005T010718Z__shifting__minsum__berlin52-6ce5d7089a__r00__b17d84552354.gif`
- `animations/shifting/berlin52_minsum__run_20261005T010718Z__shifting__minsum__berlin52-6ce5d7089a__r00__b17d84552354.gif.metadata.json`
- `animations/shifting/eil51_minmax__run_20261005T010718Z__shifting__minmax__eil51-e956423838__r00__7d45900602e6.gif`
- `animations/shifting/eil51_minmax__run_20261005T010718Z__shifting__minmax__eil51-e956423838__r00__7d45900602e6.gif.metadata.json`
- `animations/shifting/eil51_minsum__run_20261005T010719Z__shifting__minsum__eil51-e956423838__r00__89dabc2c38e1.gif`
- `animations/shifting/eil51_minsum__run_20261005T010719Z__shifting__minsum__eil51-e956423838__r00__89dabc2c38e1.gif.metadata.json`
- `animations/shifting/euro-night-0000030_minmax__run_20261005T010719Z__shifting__minmax__euro-night-0000030-164a859873__r00__db06896cf43d.gif`
- `animations/shifting/euro-night-0000030_minmax__run_20261005T010719Z__shifting__minmax__euro-night-0000030-164a859873__r00__db06896cf43d.gif.metadata.json`
- `animations/shifting/euro-night-0000030_minsum__run_20261005T010720Z__shifting__minsum__euro-night-0000030-164a859873__r00__17fd81322244.gif`
- `animations/shifting/euro-night-0000030_minsum__run_20261005T010720Z__shifting__minsum__euro-night-0000030-164a859873__r00__17fd81322244.gif.metadata.json`
- `animations/shifting/london-0000030.instance_minmax__run_20261005T010720Z__shifting__minmax__london-0000030.instance-3bd5c0ea09__r00__47c75ee2c58f.gif`
- `animations/shifting/london-0000030.instance_minmax__run_20261005T010720Z__shifting__minmax__london-0000030.instance-3bd5c0ea09__r00__47c75ee2c58f.gif.metadata.json`
- `animations/shifting/london-0000030.instance_minsum__run_20261005T010720Z__shifting__minsum__london-0000030.instance-3bd5c0ea09__r00__1d7aaaddb95b.gif`
- `animations/shifting/london-0000030.instance_minsum__run_20261005T010720Z__shifting__minsum__london-0000030.instance-3bd5c0ea09__r00__1d7aaaddb95b.gif.metadata.json`
- `animations/shifting/sbgdb-20200507-fpg-poly_0000000030_minmax__run_20261005T010721Z__shifting__minmax__sbgdb-20200507-fpg-poly_0000000030-f90__r00__1ff625335f01.gif`
- `animations/shifting/sbgdb-20200507-fpg-poly_0000000030_minmax__run_20261005T010721Z__shifting__minmax__sbgdb-20200507-fpg-poly_0000000030-f90__r00__1ff625335f01.gif.metadata.json`
- `animations/shifting/sbgdb-20200507-fpg-poly_0000000030_minsum__run_20261005T010721Z__shifting__minsum__sbgdb-20200507-fpg-poly_0000000030-f90__r00__8792d4d1b4bd.gif`
- `animations/shifting/sbgdb-20200507-fpg-poly_0000000030_minsum__run_20261005T010721Z__shifting__minsum__sbgdb-20200507-fpg-poly_0000000030-f90__r00__8792d4d1b4bd.gif.metadata.json`
- `animations/shifting/sbgdb-20200507-pntset-0000030_minmax__run_20261005T010721Z__shifting__minmax__sbgdb-20200507-pntset-0000030-d00c0eac__r00__8a10e4c95db7.gif`
- `animations/shifting/sbgdb-20200507-pntset-0000030_minmax__run_20261005T010721Z__shifting__minmax__sbgdb-20200507-pntset-0000030-d00c0eac__r00__8a10e4c95db7.gif.metadata.json`
- `animations/shifting/sbgdb-20200507-pntset-0000030_minsum__run_20261005T010722Z__shifting__minsum__sbgdb-20200507-pntset-0000030-d00c0eac__r00__759e0cf8e104.gif`
- `animations/shifting/sbgdb-20200507-pntset-0000030_minsum__run_20261005T010722Z__shifting__minsum__sbgdb-20200507-pntset-0000030-d00c0eac__r00__759e0cf8e104.gif.metadata.json`
- `animations/shifting/stars-0000030.instance_minmax__run_20261005T010722Z__shifting__minmax__stars-0000030.instance-2f24c57225__r00__c108ca94ec02.gif`
- `animations/shifting/stars-0000030.instance_minmax__run_20261005T010722Z__shifting__minmax__stars-0000030.instance-2f24c57225__r00__c108ca94ec02.gif.metadata.json`
- `animations/shifting/stars-0000030.instance_minsum__run_20261005T010722Z__shifting__minsum__stars-0000030.instance-2f24c57225__r00__e532530a6704.gif`
- `animations/shifting/stars-0000030.instance_minsum__run_20261005T010722Z__shifting__minsum__stars-0000030.instance-2f24c57225__r00__e532530a6704.gif.metadata.json`
- `animations/shifting/uniform-0000030-2_minmax__run_20261005T010722Z__shifting__minmax__uniform-0000030-2-ac5ba17054__r00__40a80c283851.gif`
- `animations/shifting/uniform-0000030-2_minmax__run_20261005T010722Z__shifting__minmax__uniform-0000030-2-ac5ba17054__r00__40a80c283851.gif.metadata.json`
- `animations/shifting/uniform-0000030-2_minsum__run_20261005T010723Z__shifting__minsum__uniform-0000030-2-ac5ba17054__r00__b84ea48a8b6f.gif`
- `animations/shifting/uniform-0000030-2_minsum__run_20261005T010723Z__shifting__minsum__uniform-0000030-2-ac5ba17054__r00__b84ea48a8b6f.gif.metadata.json`
- `animations/shifting/us-night-0000030.instance_minmax__run_20261005T010723Z__shifting__minmax__us-night-0000030.instance-3704ac03dc__r00__3662f56d0eb8.gif`
- `animations/shifting/us-night-0000030.instance_minmax__run_20261005T010723Z__shifting__minmax__us-night-0000030.instance-3704ac03dc__r00__3662f56d0eb8.gif.metadata.json`
- `animations/shifting/us-night-0000030.instance_minsum__run_20261005T010723Z__shifting__minsum__us-night-0000030.instance-3704ac03dc__r00__1b9e70d5f93a.gif`
- `animations/shifting/us-night-0000030.instance_minsum__run_20261005T010723Z__shifting__minsum__us-night-0000030.instance-3704ac03dc__r00__1b9e70d5f93a.gif.metadata.json`

## 18. Reproduction command

`bash scripts/run_experiment.sh --pipeline smoke --output results/experiments/algorithm-smoke-20261005-complete`

## 19. Failed/skipped stages

None recorded.

## 20. Remaining limitations

This report covers 10 dataset instance(s) only; it does not imply that other dataset inputs were executed.
Native KONT/COPT availability: unavailable or not used.
A visualization is not a feasibility or optimality proof. Exact animation is emitted only for results marked exact-reference, feasible, continuously verified, and OPTIMAL. Review `result_integrity_report.md` and per-run metadata for failed and timed-out cases.
