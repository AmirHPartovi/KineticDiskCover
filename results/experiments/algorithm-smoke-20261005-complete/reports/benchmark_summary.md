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
