# Independent final smoke20 artifact audit

- **Audit status:** FAIL
- **Experiment status:** PARTIAL (not COMPLETE)
- **Experiment ID:** `kdc-smoke20-all-registered-final-20261012T074200`
- **Experiment directory:** `/Users/amirpartovi/Desktop/Codes/KineticDiskCover/results/experiments/kdc-smoke20-all-registered-final-20261012T074200`

## Local build, tests and certification

- Release build: `COMPLETED`, exit 0; solver and kinetic certification executables present. Build directory: `/Users/amirpartovi/Desktop/Codes/KineticDiskCover/build/prompt8-clean-20261012`.
- CTest: exit 0.
- Python tests: exit 0; -- Docs: https://docs.pytest.org/en/stable/how-to/capture-warnings.html; 121 passed, 608 warnings in 34.66s.
- Certification: exit `0`, 600 records, Markdown companion present: True.

## Selection and run plan

- 20 selected unique instances; family distribution: {'us-night': 1, 'sbgdb-20200507-pntset': 1, 'sbgdb-20200507-fpg-poly': 1, 'uniform': 1, 'london': 1, 'stars': 1, 'euro-night': 1, 'att48': 1, 'berlin52': 1, 'eil51': 1, 'st70': 1, 'eil76': 1, 'pr76': 1, 'c-right': 1, 'c-lower': 1, 'gr96': 1, 'rat99': 1, 'kroe100': 1, 'rd100': 1, 'krod100': 1}.
- Independent selection integrity: **PASS** — all 20 source, canonical, and materialized SHA-256 values plus canonical dimensions were checked; source dataset fingerprint matches the manifest.
- Registry: branch-and-bound, brute-force, genetic, greedy, ip-kont, local-search, lp-rounding, nn, primal-dual, sa, shifting.
- Recomputed logical plan: 20 × 11 × 3 × 1 = 660; observed 660 planned slots.
- Terminal logical outcomes 660/660; missing 0; physical attempts 833; statuses: {'COMPLETED': 487, 'FAILED': 51, 'INVALID': 39, 'TIME_LIMIT': 23, 'SKIPPED': 60}.
- Exact reference: `branch-and-bound`; KONT native availability `False`.
- Reproduction command: `bash scripts/run_experiment.sh --pipeline smoke --dataset data/instances/public_instance_set --algorithms all-registered --modes all --exact-reference auto --threads 2 --with-animations --animation-policy all-solutions --build-dir build/prompt8-clean-20261012 --output results/experiments/kdc-smoke20-all-registered-final-20261012T074200 --resume`.

## Per-algorithm/per-objective outcomes

| Algorithm | Objective | Planned | Terminal | Feasible | Verified | Continuous | Optimal | Time limit | Failed | Invalid | Skipped | Missing |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| branch-and-bound | minmax | 20 | 20 | 13 | 13 | 13 | 7 | 3 | 0 | 7 | 0 | 0 |
| branch-and-bound | minsum | 20 | 20 | 20 | 20 | 20 | 0 | 10 | 0 | 0 | 0 | 0 |
| branch-and-bound | minmaxsum | 20 | 20 | 12 | 12 | 12 | 0 | 10 | 0 | 0 | 0 | 0 |
| brute-force | minmax | 20 | 20 | 7 | 7 | 7 | 6 | 0 | 13 | 0 | 0 | 0 |
| brute-force | minsum | 20 | 20 | 7 | 7 | 7 | 0 | 0 | 13 | 0 | 0 | 0 |
| brute-force | minmaxsum | 20 | 20 | 7 | 7 | 7 | 0 | 0 | 13 | 0 | 0 | 0 |
| genetic | minmax | 20 | 20 | 17 | 17 | 17 | 0 | 0 | 0 | 3 | 0 | 0 |
| genetic | minsum | 20 | 20 | 20 | 20 | 20 | 0 | 0 | 0 | 0 | 0 | 0 |
| genetic | minmaxsum | 20 | 20 | 19 | 19 | 19 | 0 | 0 | 1 | 0 | 0 | 0 |
| greedy | minmax | 20 | 20 | 15 | 15 | 15 | 0 | 0 | 0 | 5 | 0 | 0 |
| greedy | minsum | 20 | 20 | 20 | 20 | 20 | 0 | 0 | 0 | 0 | 0 | 0 |
| greedy | minmaxsum | 20 | 20 | 19 | 19 | 19 | 0 | 0 | 1 | 0 | 0 | 0 |
| ip-kont | minmax | 20 | 20 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 20 | 0 |
| ip-kont | minsum | 20 | 20 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 20 | 0 |
| ip-kont | minmaxsum | 20 | 20 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 20 | 0 |
| local-search | minmax | 20 | 20 | 16 | 16 | 16 | 0 | 0 | 0 | 4 | 0 | 0 |
| local-search | minsum | 20 | 20 | 20 | 20 | 20 | 0 | 0 | 0 | 0 | 0 | 0 |
| local-search | minmaxsum | 20 | 20 | 19 | 19 | 19 | 0 | 0 | 1 | 0 | 0 | 0 |
| lp-rounding | minmax | 20 | 20 | 17 | 17 | 17 | 0 | 0 | 0 | 3 | 0 | 0 |
| lp-rounding | minsum | 20 | 20 | 20 | 20 | 20 | 0 | 0 | 0 | 0 | 0 | 0 |
| lp-rounding | minmaxsum | 20 | 20 | 19 | 19 | 19 | 0 | 0 | 1 | 0 | 0 | 0 |
| nn | minmax | 20 | 20 | 16 | 16 | 16 | 0 | 0 | 0 | 4 | 0 | 0 |
| nn | minsum | 20 | 20 | 20 | 20 | 20 | 0 | 0 | 0 | 0 | 0 | 0 |
| nn | minmaxsum | 20 | 20 | 18 | 18 | 18 | 0 | 0 | 2 | 0 | 0 | 0 |
| primal-dual | minmax | 20 | 20 | 16 | 16 | 16 | 0 | 0 | 0 | 4 | 0 | 0 |
| primal-dual | minsum | 20 | 20 | 20 | 20 | 20 | 0 | 0 | 0 | 0 | 0 | 0 |
| primal-dual | minmaxsum | 20 | 20 | 17 | 17 | 17 | 0 | 0 | 3 | 0 | 0 | 0 |
| sa | minmax | 20 | 20 | 15 | 15 | 15 | 0 | 0 | 0 | 5 | 0 | 0 |
| sa | minsum | 20 | 20 | 20 | 20 | 20 | 0 | 0 | 0 | 0 | 0 | 0 |
| sa | minmaxsum | 20 | 20 | 19 | 19 | 19 | 0 | 0 | 1 | 0 | 0 | 0 |
| shifting | minmax | 20 | 20 | 16 | 16 | 16 | 0 | 0 | 0 | 4 | 0 | 0 |
| shifting | minsum | 20 | 20 | 20 | 20 | 20 | 0 | 0 | 0 | 0 | 0 | 0 |
| shifting | minmaxsum | 20 | 20 | 18 | 18 | 18 | 0 | 0 | 2 | 0 | 0 | 0 |

## Artifacts and integrity
- Tables: 13 table CSV files; recursive CSV/TSV total 2155; missing companions 0; invalid companions 0.
- Figures generated: 10; triptychs 502; animations 502; comparison sheets 60/60.
- Artifact index entries 21588; checksum/path errors 0.
- Feasibility 502; verified 502; continuous 502; proven optimal 13. Empirical and continuous verification remain separate. MinMaxSum retains two components.
- Exact-ratio rows 61; invalid prerequisites/labels 0. They are empirical ratios, not theoretical guarantees.
- Source commit `f28b4e3866a51575ea47dddb3362694d84c6de25`, run dirty-tree `True`, current HEAD `92ccfa5de16d5a763823f903fa23c4eebf744a90`. Source tree status `clean`; 21623 tracked files under results/ and 21589 in this experiment.

## Audit failures
- **nonterminal_metric_nulls**: {"offenders": [{"fields": {"heuristic_gap": "0.0", "lower_bound": "0.0", "objective_value": "0.0"}, "run_id": "run_20261012T042830Z__branch-and-bound__minmax__C-lower-90fec4bb6118-801fe3006e__r00__65943c0b5504", "status": "INVALID"}, {"fields": {"heuristic_gap": "0.0", "lower_bound": "0.0", "objective_value": "0.0"}, "run_id": "run_20261012T042830Z__brute-force__minmax__C-lower-90fec4bb6118-801fe3006e__r00__71feb2324191", "status": "FAILED"}, {"fields": {"heuristic_gap": "0.0", "lower_bound": "0.0", "objective_value": "0.0"}, "run_id": "run_20261012T042830Z__genetic__minmax__C-lower-90fec4bb6118-801fe3006e__r00__efd46495f85c", "status": "INVALID"}, {"fields": {"heuristic_gap": "0.0", "lower_bound": "0.0", "objective_value": "0.0"}, "run_id": "run_20261012T042831Z__greedy__minmax__C-lower-90fec4bb6118-801fe3006e__r00__1f0be2ec88ee", "status": "INVALID"}, {"fields": {"heuristic_gap": "0.0", "lower_bound": "0.0", "objective_value": "0.0"}, "run_id": "run_20261012T042831Z__local-search__minmax__C-lower-90fec4bb6118-801fe3006e__r00__43ff012000d1", "status": "INVALID"}, {"fields": {"heuristic_gap": "0.0", "lower_bound": "0.0", "objective_value": "0.0"}, "run_id": "run_20261012T042831Z__lp-rounding__minmax__C-lower-90fec4bb6118-801fe3006e__r00__f6b5619205de", "status": "INVALID"}, {"fields": {"heuristic_gap": "0.0", "lower_bound": "0.0", "objective_value": "0.0"}, "run_id": "run_20261012T042831Z__nn__minmax__C-lower-90fec4bb6118-801fe3006e__r00__3af19fe04a08", "status": "INVALID"}, {"fields": {"heuristic_gap": "0.0", "lower_bound": "0.0", "objective_value": "0.0"}, "run_id": "run_20261012T042832Z__primal-dual__minmax__C-lower-90fec4bb6118-801fe3006e__r00__84487952e28e", "status": "INVALID"}, {"fields": {"heuristic_gap": "0.0", "lower_bound": "0.0", "objective_value": "0.0"}, "run_id": "run_20261012T042832Z__sa__minmax__C-lower-90fec4bb6118-801fe3006e__r00__6f5348f42586", "status": "INVALID"}, {"fields": {"heuristic_gap": "0.0", "lower_bound": "0.0", "objective_value": "0.0"}, "run_id": "run_20261012T042832Z__shifting__minmax__C-lower-90fec4bb6118-801fe3006e__r00__09abb453691f", "status": "INVALID"}, {"fields": {"heuristic_gap": "0.0", "lower_bound": "0.0", "objective_value": "0.0"}, "run_id": "run_20261012T042838Z__brute-force__minsum__C-lower-90fec4bb6118-801fe3006e__r00__d5447aac82c5", "status": "FAILED"}, {"fields": {"heuristic_gap": "0.0", "lower_bound": "0.0", "objective_value": "0.0"}, "run_id": "run_20261012T042838Z__branch-and-bound__minmax__C-right-8bf4b5770668-e0f2e6a705__r00__468da17d9076", "status": "INVALID"}, {"fields": {"heuristic_gap": "0.0", "lower_bound": "0.0", "objective_value": "0.0"}, "run_id": "run_20261012T042839Z__brute-force__minmax__C-right-8bf4b5770668-e0f2e6a705__r00__e416f4ed1ba2", "status": "FAILED"}, {"fields": {"heuristic_gap": "0.0", "lower_bound": "0.0", "objective_value": "0.0"}, "run_id": "run_20261012T042840Z__greedy__minmax__C-right-8bf4b5770668-e0f2e6a705__r00__39f28a75d187", "status": "INVALID"}, {"fields": {"heuristic_gap": "0.0", "lower_bound": "0.0", "objective_value": "0.0"}, "run_id": "run_20261012T042840Z__local-search__minmax__C-right-8bf4b5770668-e0f2e6a705__r00__85a2709648ac", "status": "INVALID"}, {"fields": {"heuristic_gap": "0.0", "lower_bound": "0.0", "objective_value": "0.0"}, "run_id": "run_20261012T042840Z__lp-rounding__minmax__C-right-8bf4b5770668-e0f2e6a705__r00__e1290ca8720a", "status": "INVALID"}, {"fields": {"heuristic_gap": "0.0", "lower_bound": "0.0", "objective_value": "0.0"}, "run_id": "run_20261012T042840Z__nn__minmax__C-right-8bf4b5770668-e0f2e6a705__r00__4831fc75affd", "status": "INVALID"}, {"fields": {"heuristic_gap": "0.0", "lower_bound": "0.0", "objective_value": "0.0"}, "run_id": "run_20261012T042840Z__primal-dual__minmax__C-right-8bf4b5770668-e0f2e6a705__r00__c722b5ad6f4e", "status": "INVALID"}, {"fields": {"heuristic_gap": "0.0", "lower_bound": "0.0", "objective_value": "0.0"}, "run_id": "run_20261012T042840Z__sa__minmax__C-right-8bf4b5770668-e0f2e6a705__r00__1a7890b4f647", "status": "INVALID"}, {"fields": {"heuristic_gap": "0.0", "lower_bound": "0.0", "objective_value": "0.0"}, "run_id": "run_20261012T042840Z__shifting__minmax__C-right-8bf4b5770668-e0f2e6a705__r00__eb009e8f1fb1", "status": "INVALID"}, {"fields": {"heuristic_gap": "0.0", "lower_bound": "0.0", "objective_value": "0.0"}, "run_id": "run_20261012T042844Z__brute-force__minsum__C-right-8bf4b5770668-e0f2e6a705__r00__e5636d12c1e6", "status": "FAILED"}, {"fields": {"heuristic_gap": "0.0", "lower_bound": "0.0", "objective_value": "0.0"}, "run_id": "run_20261012T042844Z__brute-force__minmax__att48-8efa61a1d9ab-5196644df6__r00__6aab6ab92e9e", "status": "FAILED"}, {"fields": {"heuristic_gap": "0.0", "lower_bound": "0.0", "objective_value": "0.0"}, "run_id": "run_20261012T042844Z__sa__minmax__att48-8efa61a1d9ab-5196644df6__r00__979c4c935327", "status": "INVALID"}, {"fields": {"heuristic_gap": "0.0", "lower_bound": "0.0", "objective_value": "0.0"}, "run_id": "run_20261012T042844Z__brute-force__minsum__att48-8efa61a1d9ab-5196644df6__r00__468341f059d2", "status": "FAILED"}, {"fields": {"heuristic_gap": "0.0", "lower_bound": "0.0", "objective_value": "0.0"}, "run_id": "run_20261012T042845Z__brute-force__minmax__berlin52-3939d9e3cc19-6ce5d7089a__r00__8a1a6808703c", "status": "FAILED"}, {"fields": {"heuristic_gap": "0.0", "lower_bound": "0.0", "objective_value": "0.0"}, "run_id": "run_20261012T042845Z__greedy__minmax__berlin52-3939d9e3cc19-6ce5d7089a__r00__ae7463098356", "status": "INVALID"}, {"fields": {"heuristic_gap": "0.0", "lower_bound": "0.0", "objective_value": "0.0"}, "run_id": "run_20261012T042845Z__brute-force__minsum__berlin52-3939d9e3cc19-6ce5d7089a__r00__44805882d0d7", "status": "FAILED"}, {"fields": {"heuristic_gap": "0.0", "lower_bound": "0.0", "objective_value": "0.0"}, "run_id": "run_20261012T042845Z__brute-force__minmax__eil51-f6e4afd67825-e956423838__r00__6c86712d548c", "status": "FAILED"}, {"fields": {"heuristic_gap": "0.0", "lower_bound": "0.0", "objective_value": "0.0"}, "run_id": "run_20261012T042845Z__brute-force__minsum__eil51-f6e4afd67825-e956423838__r00__c14b60a5f46b", "status": "FAILED"}, {"fields": {"heuristic_gap": "0.0", "lower_bound": "0.0", "objective_value": "0.0"}, "run_id": "run_20261012T042845Z__branch-and-bound__minmax__eil76-e404104dd7bb-8eb626a3b8__r00__f6a35869a8f5", "status": "INVALID"}, {"fields": {"heuristic_gap": "0.0", "lower_bound": "0.0", "objective_value": "0.0"}, "run_id": "run_20261012T042845Z__brute-force__minmax__eil76-e404104dd7bb-8eb626a3b8__r00__c2e1295ad2a7", "status": "FAILED"}, {"fields": {"heuristic_gap": "0.0", "lower_bound": "0.0", "objective_value": "0.0"}, "run_id": "run_20261012T042855Z__brute-force__minsum__eil76-e404104dd7bb-8eb626a3b8__r00__4d866e28de7f", "status": "FAILED"}, {"fields": {"heuristic_gap": "0.0", "lower_bound": "0.0", "objective_value": "0.0"}, "run_id": "run_20261012T042856Z__brute-force__minmax__gr96-a23a7ba30c4c-e195dfa06d__r00__fa78e2a0ba31", "status": "FAILED"}, {"fields": {"heuristic_gap": "0.0", "lower_bound": "0.0", "objective_value": "0.0"}, "run_id": "run_20261012T042900Z__brute-force__minsum__gr96-a23a7ba30c4c-e195dfa06d__r00__96a2c6d0aa4a", "status": "FAILED"}, {"fields": {"heuristic_gap": "0.0", "lower_bound": "0.0", "objective_value": "0.0"}, "run_id": "run_20261012T042900Z__branch-and-bound__minmax__kroD100-100b54b48d61-5062f763b8__r00__2b74b85dfbca", "status": "INVALID"}, {"fields": {"heuristic_gap": "0.0", "lower_bound": "0.0", "objective_value": "0.0"}, "run_id": "run_20261012T042901Z__brute-force__minmax__kroD100-100b54b48d61-5062f763b8__r00__cb7adf16545c", "status": "FAILED"}, {"fields": {"heuristic_gap": "0.0", "lower_bound": "0.0", "objective_value": "0.0"}, "run_id": "run_20261012T042901Z__genetic__minmax__kroD100-100b54b48d61-5062f763b8__r00__0622251f593e", "status": "INVALID"}, {"fields": {"heuristic_gap": "0.0", "lower_bound": "0.0", "objective_value": "0.0"}, "run_id": "run_20261012T042901Z__local-search__minmax__kroD100-100b54b48d61-5062f763b8__r00__71a3ac8c9a94", "status": "INVALID"}, {"fields": {"heuristic_gap": "0.0", "lower_bound": "0.0", "objective_value": "0.0"}, "run_id": "run_20261012T042901Z__nn__minmax__kroD100-100b54b48d61-5062f763b8__r00__d88d7953c669", "status": "INVALID"}, {"fields": {"heuristic_gap": "0.0", "lower_bound": "0.0", "objective_value": "0.0"}, "run_id": "run_20261012T042901Z__primal-dual__minmax__kroD100-100b54b48d61-5062f763b8__r00__e03b334b27c5", "status": "INVALID"}, {"fields": {"heuristic_gap": "0.0", "lower_bound": "0.0", "objective_value": "0.0"}, "run_id": "run_20261012T042902Z__sa__minmax__kroD100-100b54b48d61-5062f763b8__r00__c8d6ee97edc5", "status": "INVALID"}, {"fields": {"heuristic_gap": "0.0", "lower_bound": "0.0", "objective_value": "0.0"}, "run_id": "run_20261012T042905Z__brute-force__minsum__kroD100-100b54b48d61-5062f763b8__r00__e9f086458c93", "status": "FAILED"}, {"fields": {"heuristic_gap": "0.0", "lower_bound": "0.0", "objective_value": "0.0"}, "run_id": "run_20261012T042905Z__branch-and-bound__minmax__kroE100-02a50775c87e-d8d2268527__r00__aa514240ed48", "status": "INVALID"}, {"fields": {"heuristic_gap": "0.0", "lower_bound": "0.0", "objective_value": "0.0"}, "run_id": "run_20261012T042906Z__brute-force__minmax__kroE100-02a50775c87e-d8d2268527__r00__7498aa8ed84c", "status": "FAILED"}, {"fields": {"heuristic_gap": "0.0", "lower_bound": "0.0", "objective_value": "0.0"}, "run_id": "run_20261012T042906Z__primal-dual__minmax__kroE100-02a50775c87e-d8d2268527__r00__104e2eee822f", "status": "INVALID"}, {"fields": {"heuristic_gap": "0.0", "lower_bound": "0.0", "objective_value": "0.0"}, "run_id": "run_20261012T042910Z__brute-force__minsum__kroE100-02a50775c87e-d8d2268527__r00__e02f3331ecdf", "status": "FAILED"}, {"fields": {"heuristic_gap": "0.0", "lower_bound": "0.0", "objective_value": "0.0"}, "run_id": "run_20261012T042910Z__branch-and-bound__minmax__pr76-4001f85eab1a-dc636944a6__r00__bbbfbfaf0fb7", "status": "INVALID"}, {"fields": {"heuristic_gap": "0.0", "lower_bound": "0.0", "objective_value": "0.0"}, "run_id": "run_20261012T042911Z__brute-force__minmax__pr76-4001f85eab1a-dc636944a6__r00__868797eded3f", "status": "FAILED"}, {"fields": {"heuristic_gap": "0.0", "lower_bound": "0.0", "objective_value": "0.0"}, "run_id": "run_20261012T042911Z__greedy__minmax__pr76-4001f85eab1a-dc636944a6__r00__bb54c8d3c10d", "status": "INVALID"}, {"fields": {"heuristic_gap": "0.0", "lower_bound": "0.0", "objective_value": "0.0"}, "run_id": "run_20261012T042911Z__shifting__minmax__pr76-4001f85eab1a-dc636944a6__r00__e025ab09d7cc", "status": "INVALID"}, {"fields": {"heuristic_gap": "0.0", "lower_bound": "0.0", "objective_value": "0.0"}, "run_id": "run_20261012T042915Z__brute-force__minsum__pr76-4001f85eab1a-dc636944a6__r00__74de92d27501", "status": "FAILED"}, {"fields": {"heuristic_gap": "0.0", "lower_bound": "0.0", "objective_value": "0.0"}, "run_id": "run_20261012T042916Z__brute-force__minmax__rat99-a364923ee329-1be1f1f758__r00__98d709b4d513", "status": "FAILED"}, {"fields": {"heuristic_gap": "0.0", "lower_bound": "0.0", "objective_value": "0.0"}, "run_id": "run_20261012T042916Z__genetic__minmax__rat99-a364923ee329-1be1f1f758__r00__7465c5ac728d", "status": "INVALID"}, {"fields": {"heuristic_gap": "0.0", "lower_bound": "0.0", "objective_value": "0.0"}, "run_id": "run_20261012T042916Z__greedy__minmax__rat99-a364923ee329-1be1f1f758__r00__6eff7a217d53", "status": "INVALID"}, {"fields": {"heuristic_gap": "0.0", "lower_bound": "0.0", "objective_value": "0.0"}, "run_id": "run_20261012T042917Z__local-search__minmax__rat99-a364923ee329-1be1f1f758__r00__d70f00555298", "status": "INVALID"}, {"fields": {"heuristic_gap": "0.0", "lower_bound": "0.0", "objective_value": "0.0"}, "run_id": "run_20261012T042917Z__lp-rounding__minmax__rat99-a364923ee329-1be1f1f758__r00__a9027100581a", "status": "INVALID"}, {"fields": {"heuristic_gap": "0.0", "lower_bound": "0.0", "objective_value": "0.0"}, "run_id": "run_20261012T042917Z__nn__minmax__rat99-a364923ee329-1be1f1f758__r00__276d4a7d1b09", "status": "INVALID"}, {"fields": {"heuristic_gap": "0.0", "lower_bound": "0.0", "objective_value": "0.0"}, "run_id": "run_20261012T042917Z__sa__minmax__rat99-a364923ee329-1be1f1f758__r00__7f19e39faa09", "status": "INVALID"}, {"fields": {"heuristic_gap": "0.0", "lower_bound": "0.0", "objective_value": "0.0"}, "run_id": "run_20261012T042917Z__shifting__minmax__rat99-a364923ee329-1be1f1f758__r00__4426f64da005", "status": "INVALID"}, {"fields": {"heuristic_gap": "0.0", "lower_bound": "0.0", "objective_value": "0.0"}, "run_id": "run_20261012T042922Z__brute-force__minsum__rat99-a364923ee329-1be1f1f758__r00__64924406b080", "status": "FAILED"}, {"fields": {"heuristic_gap": "0.0", "lower_bound": "0.0", "objective_value": "0.0"}, "run_id": "run_20261012T042922Z__branch-and-bound__minmax__rd100-9407ab41999f-b9917bc5f5__r00__1dbcb9f41cbc", "status": "INVALID"}, {"fields": {"heuristic_gap": "0.0", "lower_bound": "0.0", "objective_value": "0.0"}, "run_id": "run_20261012T042922Z__brute-force__minmax__rd100-9407ab41999f-b9917bc5f5__r00__06601d19300e", "status": "FAILED"}, {"fields": {"heuristic_gap": "0.0", "lower_bound": "0.0", "objective_value": "0.0"}, "run_id": "run_20261012T042926Z__brute-force__minsum__rd100-9407ab41999f-b9917bc5f5__r00__d7b5957888c9", "status": "FAILED"}, {"fields": {"heuristic_gap": "0.0", "lower_bound": "0.0", "objective_value": "0.0"}, "run_id": "run_20261012T042927Z__brute-force__minmax__st70-fa79167d4566-93e4a763b9__r00__1963d5f64912", "status": "FAILED"}, {"fields": {"heuristic_gap": "0.0", "lower_bound": "0.0", "objective_value": "0.0"}, "run_id": "run_20261012T042937Z__brute-force__minsum__st70-fa79167d4566-93e4a763b9__r00__296f41d466fe", "status": "FAILED"}], "offending_rows": 65}
- **stale_figure_status**: "three-time figures marked NOT_GENERATED although triptychs exist"

### Unsuccessful-result metric fields

| Run ID | Status | Non-null fields |
|---|---|---|
| `run_20261012T042830Z__branch-and-bound__minmax__C-lower-90fec4bb6118-801fe3006e__r00__65943c0b5504` | INVALID | objective_value=0.0, lower_bound=0.0, heuristic_gap=0.0 |
| `run_20261012T042830Z__brute-force__minmax__C-lower-90fec4bb6118-801fe3006e__r00__71feb2324191` | FAILED | objective_value=0.0, lower_bound=0.0, heuristic_gap=0.0 |
| `run_20261012T042830Z__genetic__minmax__C-lower-90fec4bb6118-801fe3006e__r00__efd46495f85c` | INVALID | objective_value=0.0, lower_bound=0.0, heuristic_gap=0.0 |
| `run_20261012T042831Z__greedy__minmax__C-lower-90fec4bb6118-801fe3006e__r00__1f0be2ec88ee` | INVALID | objective_value=0.0, lower_bound=0.0, heuristic_gap=0.0 |
| `run_20261012T042831Z__local-search__minmax__C-lower-90fec4bb6118-801fe3006e__r00__43ff012000d1` | INVALID | objective_value=0.0, lower_bound=0.0, heuristic_gap=0.0 |
| `run_20261012T042831Z__lp-rounding__minmax__C-lower-90fec4bb6118-801fe3006e__r00__f6b5619205de` | INVALID | objective_value=0.0, lower_bound=0.0, heuristic_gap=0.0 |
| `run_20261012T042831Z__nn__minmax__C-lower-90fec4bb6118-801fe3006e__r00__3af19fe04a08` | INVALID | objective_value=0.0, lower_bound=0.0, heuristic_gap=0.0 |
| `run_20261012T042832Z__primal-dual__minmax__C-lower-90fec4bb6118-801fe3006e__r00__84487952e28e` | INVALID | objective_value=0.0, lower_bound=0.0, heuristic_gap=0.0 |
| `run_20261012T042832Z__sa__minmax__C-lower-90fec4bb6118-801fe3006e__r00__6f5348f42586` | INVALID | objective_value=0.0, lower_bound=0.0, heuristic_gap=0.0 |
| `run_20261012T042832Z__shifting__minmax__C-lower-90fec4bb6118-801fe3006e__r00__09abb453691f` | INVALID | objective_value=0.0, lower_bound=0.0, heuristic_gap=0.0 |
| `run_20261012T042838Z__brute-force__minsum__C-lower-90fec4bb6118-801fe3006e__r00__d5447aac82c5` | FAILED | objective_value=0.0, lower_bound=0.0, heuristic_gap=0.0 |
| `run_20261012T042838Z__branch-and-bound__minmax__C-right-8bf4b5770668-e0f2e6a705__r00__468da17d9076` | INVALID | objective_value=0.0, lower_bound=0.0, heuristic_gap=0.0 |
| `run_20261012T042839Z__brute-force__minmax__C-right-8bf4b5770668-e0f2e6a705__r00__e416f4ed1ba2` | FAILED | objective_value=0.0, lower_bound=0.0, heuristic_gap=0.0 |
| `run_20261012T042840Z__greedy__minmax__C-right-8bf4b5770668-e0f2e6a705__r00__39f28a75d187` | INVALID | objective_value=0.0, lower_bound=0.0, heuristic_gap=0.0 |
| `run_20261012T042840Z__local-search__minmax__C-right-8bf4b5770668-e0f2e6a705__r00__85a2709648ac` | INVALID | objective_value=0.0, lower_bound=0.0, heuristic_gap=0.0 |
| `run_20261012T042840Z__lp-rounding__minmax__C-right-8bf4b5770668-e0f2e6a705__r00__e1290ca8720a` | INVALID | objective_value=0.0, lower_bound=0.0, heuristic_gap=0.0 |
| `run_20261012T042840Z__nn__minmax__C-right-8bf4b5770668-e0f2e6a705__r00__4831fc75affd` | INVALID | objective_value=0.0, lower_bound=0.0, heuristic_gap=0.0 |
| `run_20261012T042840Z__primal-dual__minmax__C-right-8bf4b5770668-e0f2e6a705__r00__c722b5ad6f4e` | INVALID | objective_value=0.0, lower_bound=0.0, heuristic_gap=0.0 |
| `run_20261012T042840Z__sa__minmax__C-right-8bf4b5770668-e0f2e6a705__r00__1a7890b4f647` | INVALID | objective_value=0.0, lower_bound=0.0, heuristic_gap=0.0 |
| `run_20261012T042840Z__shifting__minmax__C-right-8bf4b5770668-e0f2e6a705__r00__eb009e8f1fb1` | INVALID | objective_value=0.0, lower_bound=0.0, heuristic_gap=0.0 |
| `run_20261012T042844Z__brute-force__minsum__C-right-8bf4b5770668-e0f2e6a705__r00__e5636d12c1e6` | FAILED | objective_value=0.0, lower_bound=0.0, heuristic_gap=0.0 |
| `run_20261012T042844Z__brute-force__minmax__att48-8efa61a1d9ab-5196644df6__r00__6aab6ab92e9e` | FAILED | objective_value=0.0, lower_bound=0.0, heuristic_gap=0.0 |
| `run_20261012T042844Z__sa__minmax__att48-8efa61a1d9ab-5196644df6__r00__979c4c935327` | INVALID | objective_value=0.0, lower_bound=0.0, heuristic_gap=0.0 |
| `run_20261012T042844Z__brute-force__minsum__att48-8efa61a1d9ab-5196644df6__r00__468341f059d2` | FAILED | objective_value=0.0, lower_bound=0.0, heuristic_gap=0.0 |
| `run_20261012T042845Z__brute-force__minmax__berlin52-3939d9e3cc19-6ce5d7089a__r00__8a1a6808703c` | FAILED | objective_value=0.0, lower_bound=0.0, heuristic_gap=0.0 |
| `run_20261012T042845Z__greedy__minmax__berlin52-3939d9e3cc19-6ce5d7089a__r00__ae7463098356` | INVALID | objective_value=0.0, lower_bound=0.0, heuristic_gap=0.0 |
| `run_20261012T042845Z__brute-force__minsum__berlin52-3939d9e3cc19-6ce5d7089a__r00__44805882d0d7` | FAILED | objective_value=0.0, lower_bound=0.0, heuristic_gap=0.0 |
| `run_20261012T042845Z__brute-force__minmax__eil51-f6e4afd67825-e956423838__r00__6c86712d548c` | FAILED | objective_value=0.0, lower_bound=0.0, heuristic_gap=0.0 |
| `run_20261012T042845Z__brute-force__minsum__eil51-f6e4afd67825-e956423838__r00__c14b60a5f46b` | FAILED | objective_value=0.0, lower_bound=0.0, heuristic_gap=0.0 |
| `run_20261012T042845Z__branch-and-bound__minmax__eil76-e404104dd7bb-8eb626a3b8__r00__f6a35869a8f5` | INVALID | objective_value=0.0, lower_bound=0.0, heuristic_gap=0.0 |
| `run_20261012T042845Z__brute-force__minmax__eil76-e404104dd7bb-8eb626a3b8__r00__c2e1295ad2a7` | FAILED | objective_value=0.0, lower_bound=0.0, heuristic_gap=0.0 |
| `run_20261012T042855Z__brute-force__minsum__eil76-e404104dd7bb-8eb626a3b8__r00__4d866e28de7f` | FAILED | objective_value=0.0, lower_bound=0.0, heuristic_gap=0.0 |
| `run_20261012T042856Z__brute-force__minmax__gr96-a23a7ba30c4c-e195dfa06d__r00__fa78e2a0ba31` | FAILED | objective_value=0.0, lower_bound=0.0, heuristic_gap=0.0 |
| `run_20261012T042900Z__brute-force__minsum__gr96-a23a7ba30c4c-e195dfa06d__r00__96a2c6d0aa4a` | FAILED | objective_value=0.0, lower_bound=0.0, heuristic_gap=0.0 |
| `run_20261012T042900Z__branch-and-bound__minmax__kroD100-100b54b48d61-5062f763b8__r00__2b74b85dfbca` | INVALID | objective_value=0.0, lower_bound=0.0, heuristic_gap=0.0 |
| `run_20261012T042901Z__brute-force__minmax__kroD100-100b54b48d61-5062f763b8__r00__cb7adf16545c` | FAILED | objective_value=0.0, lower_bound=0.0, heuristic_gap=0.0 |
| `run_20261012T042901Z__genetic__minmax__kroD100-100b54b48d61-5062f763b8__r00__0622251f593e` | INVALID | objective_value=0.0, lower_bound=0.0, heuristic_gap=0.0 |
| `run_20261012T042901Z__local-search__minmax__kroD100-100b54b48d61-5062f763b8__r00__71a3ac8c9a94` | INVALID | objective_value=0.0, lower_bound=0.0, heuristic_gap=0.0 |
| `run_20261012T042901Z__nn__minmax__kroD100-100b54b48d61-5062f763b8__r00__d88d7953c669` | INVALID | objective_value=0.0, lower_bound=0.0, heuristic_gap=0.0 |
| `run_20261012T042901Z__primal-dual__minmax__kroD100-100b54b48d61-5062f763b8__r00__e03b334b27c5` | INVALID | objective_value=0.0, lower_bound=0.0, heuristic_gap=0.0 |
| `run_20261012T042902Z__sa__minmax__kroD100-100b54b48d61-5062f763b8__r00__c8d6ee97edc5` | INVALID | objective_value=0.0, lower_bound=0.0, heuristic_gap=0.0 |
| `run_20261012T042905Z__brute-force__minsum__kroD100-100b54b48d61-5062f763b8__r00__e9f086458c93` | FAILED | objective_value=0.0, lower_bound=0.0, heuristic_gap=0.0 |
| `run_20261012T042905Z__branch-and-bound__minmax__kroE100-02a50775c87e-d8d2268527__r00__aa514240ed48` | INVALID | objective_value=0.0, lower_bound=0.0, heuristic_gap=0.0 |
| `run_20261012T042906Z__brute-force__minmax__kroE100-02a50775c87e-d8d2268527__r00__7498aa8ed84c` | FAILED | objective_value=0.0, lower_bound=0.0, heuristic_gap=0.0 |
| `run_20261012T042906Z__primal-dual__minmax__kroE100-02a50775c87e-d8d2268527__r00__104e2eee822f` | INVALID | objective_value=0.0, lower_bound=0.0, heuristic_gap=0.0 |
| `run_20261012T042910Z__brute-force__minsum__kroE100-02a50775c87e-d8d2268527__r00__e02f3331ecdf` | FAILED | objective_value=0.0, lower_bound=0.0, heuristic_gap=0.0 |
| `run_20261012T042910Z__branch-and-bound__minmax__pr76-4001f85eab1a-dc636944a6__r00__bbbfbfaf0fb7` | INVALID | objective_value=0.0, lower_bound=0.0, heuristic_gap=0.0 |
| `run_20261012T042911Z__brute-force__minmax__pr76-4001f85eab1a-dc636944a6__r00__868797eded3f` | FAILED | objective_value=0.0, lower_bound=0.0, heuristic_gap=0.0 |
| `run_20261012T042911Z__greedy__minmax__pr76-4001f85eab1a-dc636944a6__r00__bb54c8d3c10d` | INVALID | objective_value=0.0, lower_bound=0.0, heuristic_gap=0.0 |
| `run_20261012T042911Z__shifting__minmax__pr76-4001f85eab1a-dc636944a6__r00__e025ab09d7cc` | INVALID | objective_value=0.0, lower_bound=0.0, heuristic_gap=0.0 |
| `run_20261012T042915Z__brute-force__minsum__pr76-4001f85eab1a-dc636944a6__r00__74de92d27501` | FAILED | objective_value=0.0, lower_bound=0.0, heuristic_gap=0.0 |
| `run_20261012T042916Z__brute-force__minmax__rat99-a364923ee329-1be1f1f758__r00__98d709b4d513` | FAILED | objective_value=0.0, lower_bound=0.0, heuristic_gap=0.0 |
| `run_20261012T042916Z__genetic__minmax__rat99-a364923ee329-1be1f1f758__r00__7465c5ac728d` | INVALID | objective_value=0.0, lower_bound=0.0, heuristic_gap=0.0 |
| `run_20261012T042916Z__greedy__minmax__rat99-a364923ee329-1be1f1f758__r00__6eff7a217d53` | INVALID | objective_value=0.0, lower_bound=0.0, heuristic_gap=0.0 |
| `run_20261012T042917Z__local-search__minmax__rat99-a364923ee329-1be1f1f758__r00__d70f00555298` | INVALID | objective_value=0.0, lower_bound=0.0, heuristic_gap=0.0 |
| `run_20261012T042917Z__lp-rounding__minmax__rat99-a364923ee329-1be1f1f758__r00__a9027100581a` | INVALID | objective_value=0.0, lower_bound=0.0, heuristic_gap=0.0 |
| `run_20261012T042917Z__nn__minmax__rat99-a364923ee329-1be1f1f758__r00__276d4a7d1b09` | INVALID | objective_value=0.0, lower_bound=0.0, heuristic_gap=0.0 |
| `run_20261012T042917Z__sa__minmax__rat99-a364923ee329-1be1f1f758__r00__7f19e39faa09` | INVALID | objective_value=0.0, lower_bound=0.0, heuristic_gap=0.0 |
| `run_20261012T042917Z__shifting__minmax__rat99-a364923ee329-1be1f1f758__r00__4426f64da005` | INVALID | objective_value=0.0, lower_bound=0.0, heuristic_gap=0.0 |
| `run_20261012T042922Z__brute-force__minsum__rat99-a364923ee329-1be1f1f758__r00__64924406b080` | FAILED | objective_value=0.0, lower_bound=0.0, heuristic_gap=0.0 |
| `run_20261012T042922Z__branch-and-bound__minmax__rd100-9407ab41999f-b9917bc5f5__r00__1dbcb9f41cbc` | INVALID | objective_value=0.0, lower_bound=0.0, heuristic_gap=0.0 |
| `run_20261012T042922Z__brute-force__minmax__rd100-9407ab41999f-b9917bc5f5__r00__06601d19300e` | FAILED | objective_value=0.0, lower_bound=0.0, heuristic_gap=0.0 |
| `run_20261012T042926Z__brute-force__minsum__rd100-9407ab41999f-b9917bc5f5__r00__d7b5957888c9` | FAILED | objective_value=0.0, lower_bound=0.0, heuristic_gap=0.0 |
| `run_20261012T042927Z__brute-force__minmax__st70-fa79167d4566-93e4a763b9__r00__1963d5f64912` | FAILED | objective_value=0.0, lower_bound=0.0, heuristic_gap=0.0 |
| `run_20261012T042937Z__brute-force__minsum__st70-fa79167d4566-93e4a763b9__r00__296f41d466fe` | FAILED | objective_value=0.0, lower_bound=0.0, heuristic_gap=0.0 |

### Missing/invalid artifacts
- None.

## Limitations

- A single repeat is a descriptive smoke comparison only; no statistical significance or universal superiority claim is supported.
- Run outcomes include failures, invalids, skipped backend slots, and timeouts. These terminal outcomes do not imply successful execution.
- `ip-kont` cannot execute without the unavailable native backend; `brute-force` remains a bounded diagnostic method.
- Three-time figure status is stale: visualization manifest contains triptychs, but `figure_status.json` says NOT_GENERATED.
- Results are tracked by Git; no remote action or repository mutation was performed.

**Disposition: FAIL**. No existing experiment outputs were repaired or regenerated.
