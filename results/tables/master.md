# Master Results

| instance_name    | n | m | algorithm_name   | objective | objective_value | lower_bound | gap_pct  | wall_time_sec | cpu_time_sec | peak_memory_mb | num_iterations | num_ip_solves | verified |
| ---------------- | - | - | ---------------- | --------- | --------------- | ----------- | -------- | ------------- | ------------ | -------------- | -------------- | ------------- | -------- |
| preflight_sample | 2 | 2 | branch-and-bound | minmax    | **31.4159**     | 31.4159     | 0.00%    | 0.001s        | 0.003s       | 6.5 MB         | 1              | 1             | true     |
| preflight_sample | 2 | 2 | branch-and-bound | minsum    | **29.3215**     | 29.3215     | 0.00%    | 0.001s        | 0.004s       | 6.5 MB         | 1              | 22            | true     |
| preflight_sample | 2 | 2 | brute-force      | minmax    | **31.4159**     | 31.4159     | 0.00%    | 0.001s        | 0.003s       | 6.5 MB         | 1              | 1             | true     |
| preflight_sample | 2 | 2 | brute-force      | minsum    | **29.3215**     | 29.3215     | 0.00%    | 0.001s        | 0.003s       | 6.5 MB         | 1              | 22            | true     |
| preflight_sample | 2 | 2 | genetic          | minmax    | **31.4159**     | 25.1327     | 25.00%   | 0.024s        | 0.105s       | 7.1 MB         | 1              | 2             | true     |
| preflight_sample | 2 | 2 | genetic          | minsum    | **29.3215**     | 19.3758     | 51.33%   | 0.237s        | 0.350s       | 7.1 MB         | 1              | 23            | true     |
| preflight_sample | 2 | 2 | greedy           | minmax    | **31.4159**     | 25.1327     | 25.00%   | 0.000s        | 0.001s       | 6.6 MB         | 1              | 2             | true     |
| preflight_sample | 2 | 2 | greedy           | minsum    | **29.3215**     | 19.3758     | 51.33%   | 0.000s        | 0.002s       | 6.6 MB         | 1              | 23            | true     |
| preflight_sample | 2 | 2 | ip-kont          | minmax    | **31.4159**     | 31.4159     | 0.00%    | 0.000s        | 0.002s       | 6.6 MB         | 1              | 1             | true     |
| preflight_sample | 2 | 2 | ip-kont          | minsum    | **29.3215**     | 29.3215     | 0.00%    | 0.001s        | 0.003s       | 6.7 MB         | 1              | 22            | true     |
| preflight_sample | 2 | 2 | local-search     | minmax    | **31.4159**     | 25.1327     | 25.00%   | 0.000s        | 0.002s       | 6.7 MB         | 1              | 2             | true     |
| preflight_sample | 2 | 2 | local-search     | minsum    | **29.3215**     | 19.3758     | 51.33%   | 0.001s        | 0.003s       | 6.9 MB         | 1              | 23            | true     |
| preflight_sample | 2 | 2 | lp-rounding      | minmax    | **31.4159**     | 31.4159     | 0.00%    | 0.000s        | 0.002s       | 6.8 MB         | 1              | 1             | true     |
| preflight_sample | 2 | 2 | lp-rounding      | minsum    | **29.3215**     | 29.3215     | 0.00%    | 0.001s        | 0.004s       | 6.9 MB         | 1              | 22            | true     |
| preflight_sample | 2 | 2 | nn               | minmax    | **31.4159**     | 0           | 3141.59% | 0.000s        | 0.002s       | 6.9 MB         | 1              | 2             | true     |
| preflight_sample | 2 | 2 | nn               | minsum    | **29.3215**     | 0           | 2932.15% | 0.000s        | 0.002s       | 7.0 MB         | 1              | 23            | true     |
| preflight_sample | 2 | 2 | primal-dual      | minmax    | **31.4159**     | 15.708      | 100.00%  | 0.000s        | 0.001s       | 7.0 MB         | 1              | 2             | true     |
| preflight_sample | 2 | 2 | primal-dual      | minsum    | **29.3215**     | 14.6634     | 99.96%   | 0.000s        | 0.001s       | 7.0 MB         | 1              | 23            | true     |
| preflight_sample | 2 | 2 | sa               | minmax    | **31.4159**     | 25.1327     | 25.00%   | 0.005s        | 0.016s       | 7.0 MB         | 1              | 2             | true     |
| preflight_sample | 2 | 2 | sa               | minsum    | **29.3215**     | 19.3758     | 51.33%   | 0.051s        | 0.152s       | 7.1 MB         | 1              | 23            | true     |
| preflight_sample | 2 | 2 | shifting         | minmax    | **31.4159**     | 25.1327     | 25.00%   | 0.001s        | 0.006s       | 7.0 MB         | 1              | 2             | true     |
| preflight_sample | 2 | 2 | shifting         | minsum    | **29.3215**     | 19.3758     | 51.33%   | 0.009s        | 0.040s       | 7.1 MB         | 1              | 23            | true     |
