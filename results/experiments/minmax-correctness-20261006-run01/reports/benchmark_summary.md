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
| branch-and-bound | minmax | 10 | 0.000217 | 0.000117928 | 0.0172958 | 6.9708e-05 | 0.0569578 | 0.0457182 | 1316.63 | 1313.79 | 1316.63 | CERTIFIED | 2.91015e-16 | 1316.63 | 1 | 100.0% | 100.0% | 0.0% | 0.0% | 3.5 | 4 |
| greedy | minmax | 10 | 0.00018648 | 0.000162478 | 0.00282781 | 6.0876e-05 | 0.00715379 | 0.0063512 | 1316.63 | 1082.06 | 1316.63 | CERTIFIED | N/A | 1316.63 | N/A | 100.0% | 100.0% | 0.0% | 0.0% | 4 | 5 |
| nn | minmax | 10 | 0.000181124 | 0.000119823 | 0.00278875 | 7.7041e-05 | 0.00510955 | 0.00462924 | 1316.63 | 0 | 1316.63 | CERTIFIED | N/A | 1316.63 | N/A | 100.0% | 100.0% | 0.0% | 0.0% | 4 | 5 |

## Interpretation

`certified_gap` is reported separately from heuristic gap/progress. Empirical ratios are not theoretical approximation ratios. Reliability rates include every record, including timeouts and failures.
