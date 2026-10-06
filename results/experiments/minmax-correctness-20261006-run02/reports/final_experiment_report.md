# Development / smoke validation report

This is a smoke / development validation run and must not be interpreted as the full scientific benchmark.

## 1. Experiment configuration

- Status: **COMPLETE**
- Experiment ID: `minmax-correctness-20261006-run02`
- Reproduction command: `bash scripts/run_experiment.sh --pipeline smoke --algorithms branch-and-bound\,greedy\,nn --modes minmax --output results/experiments/minmax-correctness-20261006-run02 --animation-instances berlin52\,att48\,eil51 --with-animations`
- Pipeline: `smoke` (development / smoke validation)
- Algorithms/profile: `nn, greedy, branch-and-bound` / debug
- Objectives: minmax
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

- Commit: `26c953ee6e7b7d1183a7881de70ee2cc331e6426` (dirty=True)
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
| branch-and-bound | minmax | 10 | 0.000229333 | 0.000116104 | 0.0168039 | 6.8459e-05 | 0.0565375 | 0.0454376 | 1316.63 | 1313.79 | 1316.63 | CERTIFIED | 2.91015e-16 | 1316.63 | 1 | 100.0% | 100.0% | 0.0% | 0.0% | 3.5 | 4 |
| greedy | minmax | 10 | 0.000204271 | 0.000133595 | 0.00296582 | 7.1126e-05 | 0.00545213 | 0.00488467 | 1316.63 | 1082.06 | 1316.63 | CERTIFIED | N/A | 1316.63 | N/A | 100.0% | 100.0% | 0.0% | 0.0% | 4 | 5 |
| nn | minmax | 10 | 0.000186728 | 0.000169543 | 0.00287549 | 7.5499e-05 | 0.00527837 | 0.00470219 | 1316.63 | 0 | 1316.63 | CERTIFIED | N/A | 1316.63 | N/A | 100.0% | 100.0% | 0.0% | 0.0% | 4 | 5 |

## Interpretation

`certified_gap` is reported separately from heuristic gap/progress. Empirical ratios are not theoretical approximation ratios. Reliability rates include every record, including timeouts and failures.


### Raw integrity

# Raw result integrity report

- Runs: 30 (expected 30)
- Instances: 10
- Algorithms: 3
- Successful (not marked failed): 30
- Feasible: 30
- Verified: 30
- Failed: 0
- Timeouts: 0
- OPTIMAL / FEASIBLE / TIME_LIMIT / INFEASIBLE / FAILED: 7 / 23 / 0 / 0 / 0
- Duplicate run keys: 0
- Missing solution files among declared paths: 0
- Missing trace files among declared paths: 0
- Declared solution/trace paths checked: 60
- Missing records against expected count: 0
- Canonical aggregate complete: True

## By objective

| Objective | Runs | Successful | Feasible | Verified | Failed | Timeouts | OPTIMAL | FEASIBLE | TIME_LIMIT | INFEASIBLE | FAILED |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| minmax | 30 | 30 | 30 | 30 | 0 | 0 | 7 | 23 | 0 | 0 | 0 |
| minsum | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |

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
- Instance selection: berlin52, att48, eil51
- Generated animations: 7
- Heuristic animations require feasible, continuously verified results.
- Exact animations require feasibility, continuous verification, and OPTIMAL status.

- Animation path: animations/greedy/berlin52_minmax__run_20261006T023857Z__greedy__minmax__berlin52-6ce5d7089a__r00__8c05a37d3afa.gif
- Animation path: animations/nn/berlin52_minmax__run_20261006T023857Z__nn__minmax__berlin52-6ce5d7089a__r00__e2b3b3ed5ac7.gif
- Animation path: animations/branch-and-bound/att48_minmax__run_20261006T023856Z__branch-and-bound__minmax__att48-5196644df6__r00__49182d36a3a4.gif
- Animation path: animations/greedy/att48_minmax__run_20261006T023856Z__greedy__minmax__att48-5196644df6__r00__23220353f72f.gif
- Animation path: animations/nn/att48_minmax__run_20261006T023857Z__nn__minmax__att48-5196644df6__r00__d8c216ef50d7.gif
- Animation path: animations/greedy/eil51_minmax__run_20261006T023857Z__greedy__minmax__eil51-e956423838__r00__dd3c28846e90.gif
- Animation path: animations/nn/eil51_minmax__run_20261006T023857Z__nn__minmax__eil51-e956423838__r00__330664848f8b.gif


## 15. Generated tables

- `tables/00_index.md`
- `tables/failed_runs.csv`
- `tables/failed_runs.md`
- `tables/master.csv`
- `tables/master.md`
- `tables/per_algorithm/branch-and-bound.csv`
- `tables/per_algorithm/branch-and-bound.md`
- `tables/per_algorithm/greedy.csv`
- `tables/per_algorithm/greedy.md`
- `tables/per_algorithm/nn.csv`
- `tables/per_algorithm/nn.md`
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
- `figures/D3_tradeoff_greedy_minmax.pdf`
- `figures/D3_tradeoff_greedy_minmax.png`
- `figures/D3_tradeoff_nn_minmax.pdf`
- `figures/D3_tradeoff_nn_minmax.png`
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

- `animations/branch-and-bound/att48_minmax__run_20261006T023856Z__branch-and-bound__minmax__att48-5196644df6__r00__49182d36a3a4.gif`
- `animations/branch-and-bound/att48_minmax__run_20261006T023856Z__branch-and-bound__minmax__att48-5196644df6__r00__49182d36a3a4.gif.metadata.json`
- `animations/greedy/att48_minmax__run_20261006T023856Z__greedy__minmax__att48-5196644df6__r00__23220353f72f.gif`
- `animations/greedy/att48_minmax__run_20261006T023856Z__greedy__minmax__att48-5196644df6__r00__23220353f72f.gif.metadata.json`
- `animations/greedy/berlin52_minmax__run_20261006T023857Z__greedy__minmax__berlin52-6ce5d7089a__r00__8c05a37d3afa.gif`
- `animations/greedy/berlin52_minmax__run_20261006T023857Z__greedy__minmax__berlin52-6ce5d7089a__r00__8c05a37d3afa.gif.metadata.json`
- `animations/greedy/eil51_minmax__run_20261006T023857Z__greedy__minmax__eil51-e956423838__r00__dd3c28846e90.gif`
- `animations/greedy/eil51_minmax__run_20261006T023857Z__greedy__minmax__eil51-e956423838__r00__dd3c28846e90.gif.metadata.json`
- `animations/nn/att48_minmax__run_20261006T023857Z__nn__minmax__att48-5196644df6__r00__d8c216ef50d7.gif`
- `animations/nn/att48_minmax__run_20261006T023857Z__nn__minmax__att48-5196644df6__r00__d8c216ef50d7.gif.metadata.json`
- `animations/nn/berlin52_minmax__run_20261006T023857Z__nn__minmax__berlin52-6ce5d7089a__r00__e2b3b3ed5ac7.gif`
- `animations/nn/berlin52_minmax__run_20261006T023857Z__nn__minmax__berlin52-6ce5d7089a__r00__e2b3b3ed5ac7.gif.metadata.json`
- `animations/nn/eil51_minmax__run_20261006T023857Z__nn__minmax__eil51-e956423838__r00__330664848f8b.gif`
- `animations/nn/eil51_minmax__run_20261006T023857Z__nn__minmax__eil51-e956423838__r00__330664848f8b.gif.metadata.json`

## 18. Reproduction command

`bash scripts/run_experiment.sh --pipeline smoke --algorithms branch-and-bound\,greedy\,nn --modes minmax --output results/experiments/minmax-correctness-20261006-run02 --animation-instances berlin52\,att48\,eil51 --with-animations`

## 19. Failed/skipped stages

None recorded.

## 20. Remaining limitations

This report covers 10 dataset instance(s) only; it does not imply that other dataset inputs were executed.
Native KONT/COPT availability: unavailable or not used.
A visualization is not a feasibility or optimality proof. Exact animation is emitted only for results marked exact-reference, feasible, continuously verified, and OPTIMAL. Review `result_integrity_report.md` and per-run metadata for failed and timed-out cases.
