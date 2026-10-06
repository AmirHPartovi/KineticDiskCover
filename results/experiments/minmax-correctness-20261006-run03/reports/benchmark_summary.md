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
| branch-and-bound | minmax | 10 | 0.000202563 | 0.000128958 | 0.0170268 | 7.1625e-05 | 0.0610184 | 0.0485182 | 1316.63 | 1313.79 | 1316.63 | CERTIFIED | 2.91015e-16 | 1316.63 | 1 | 100.0% | 100.0% | 0.0% | 0.0% | 3.5 | 4 |
| greedy | minmax | 10 | 0.000194312 | 0.000137947 | 0.00303461 | 6.8374e-05 | 0.00611817 | 0.00529039 | 1316.63 | 1082.06 | 1316.63 | CERTIFIED | N/A | 1316.63 | N/A | 100.0% | 100.0% | 0.0% | 0.0% | 4 | 5 |
| nn | minmax | 10 | 0.000264059 | 0.000122845 | 0.00279058 | 8.3793e-05 | 0.00550129 | 0.00504006 | 1316.63 | 0 | 1316.63 | CERTIFIED | N/A | 1316.63 | N/A | 100.0% | 100.0% | 0.0% | 0.0% | 4 | 5 |

## Interpretation

`certified_gap` is reported separately from heuristic gap/progress. Empirical ratios are not theoretical approximation ratios. Reliability rates include every record, including timeouts and failures.
