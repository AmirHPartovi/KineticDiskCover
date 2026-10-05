# Smoke / development validation charts

These figures describe smoke / development validation only; they are not the full scientific benchmark.

## Dataset description

- Instances: 10
- Families: 9
- Algorithms: 9
- n range: 1–13
- m range: 25–25
- Plot-level comparison subset: records with `feasible == true` and `verified == true`, with finite plotted metrics. Failed and timed-out records remain represented in the separate raw result integrity report.
- Gap values are empirical/result-schema quantities; they are not theoretical approximation guarantees or certified gaps unless explicitly identified as `certified_gap` in raw records.

## Summary statistics

| Algorithm | Wall time median [IQR] (s) | Memory median [IQR] (MB) | Gap median [IQR] (%) | Objective median [IQR] |
|---|---:|---:|---:|---:|
| branch-and-bound | 0.001 [0.001, 0.010] | 17.2 [17.1, 17.8] | 99.87 [0.42, 629.99] | 3296.94 [722.922, 8012.71] |
| genetic | 0.065 [0.028, 0.154] | 17.3 [17.2, 17.4] | 250.19 [46.77, 1398.82] | 3347.56 [728.858, 8012.71] |
| greedy | 0.001 [0.000, 0.005] | 17.1 [17.1, 17.4] | 321.79 [45.58, 1398.82] | 4149.44 [724.744, 8846.83] |
| local-search | 0.001 [0.001, 0.005] | 17.2 [17.1, 17.4] | 260.87 [45.58, 1398.82] | 3347.56 [724.744, 8012.71] |
| lp-rounding | 0.002 [0.001, 0.008] | 17.3 [17.2, 19.3] | 99.87 [0.42, 630.56] | 3296.94 [722.922, 8012.71] |
| nn | 0.001 [0.000, 0.003] | 17.1 [17.0, 17.3] | 363169.39 [72474.39, 768483.43] | 3631.69 [724.744, 7684.83] |
| primal-dual | 0.001 [0.001, 0.005] | 17.2 [17.1, 17.4] | 4896.70 [2678.03, 18235.57] | 3435.94 [730.11, 7785.57] |
| sa | 0.018 [0.007, 0.049] | 17.2 [17.1, 17.4] | 243.26 [48.87, 1398.82] | 3310.07 [730.11, 8012.71] |
| shifting | 0.021 [0.006, 0.040] | 17.3 [17.2, 17.6] | 306.08 [45.58, 1398.82] | 4046.92 [724.744, 7684.83] |

## Figures

### A1_runtime_bar_att48

![A1_runtime_bar_att48](A1_runtime_bar_att48.png)

Runtime across algorithms and objectives for att48. **Takeaway:** Best observed runtime: 0.0030475 s.

Vector PDF: [A1_runtime_bar_att48.pdf](A1_runtime_bar_att48.pdf)

### A1_runtime_bar_berlin52

![A1_runtime_bar_berlin52](A1_runtime_bar_berlin52.png)

Runtime across algorithms and objectives for berlin52. **Takeaway:** Best observed runtime: 0.00443892 s.

Vector PDF: [A1_runtime_bar_berlin52.pdf](A1_runtime_bar_berlin52.pdf)

### A1_runtime_bar_eil51

![A1_runtime_bar_eil51](A1_runtime_bar_eil51.png)

Runtime across algorithms and objectives for eil51. **Takeaway:** Best observed runtime: 0.004302 s.

Vector PDF: [A1_runtime_bar_eil51.pdf](A1_runtime_bar_eil51.pdf)

### A1_runtime_bar_euro-night-0000030

![A1_runtime_bar_euro-night-0000030](A1_runtime_bar_euro-night-0000030.png)

Runtime across algorithms and objectives for euro-night-0000030. **Takeaway:** Best observed runtime: 0.000458125 s.

Vector PDF: [A1_runtime_bar_euro-night-0000030.pdf](A1_runtime_bar_euro-night-0000030.pdf)

### A1_runtime_bar_london-0000030.instance

![A1_runtime_bar_london-0000030.instance](A1_runtime_bar_london-0000030.instance.png)

Runtime across algorithms and objectives for london-0000030.instance. **Takeaway:** Best observed runtime: 0.000389916 s.

Vector PDF: [A1_runtime_bar_london-0000030.instance.pdf](A1_runtime_bar_london-0000030.instance.pdf)

### A1_runtime_bar_sbgdb-20200507-fpg-poly_0000000030

![A1_runtime_bar_sbgdb-20200507-fpg-poly_0000000030](A1_runtime_bar_sbgdb-20200507-fpg-poly_0000000030.png)

Runtime across algorithms and objectives for sbgdb-20200507-fpg-poly_0000000030. **Takeaway:** Best observed runtime: 0.000417791 s.

Vector PDF: [A1_runtime_bar_sbgdb-20200507-fpg-poly_0000000030.pdf](A1_runtime_bar_sbgdb-20200507-fpg-poly_0000000030.pdf)

### A1_runtime_bar_sbgdb-20200507-pntset-0000030

![A1_runtime_bar_sbgdb-20200507-pntset-0000030](A1_runtime_bar_sbgdb-20200507-pntset-0000030.png)

Runtime across algorithms and objectives for sbgdb-20200507-pntset-0000030. **Takeaway:** Best observed runtime: 0.000350625 s.

Vector PDF: [A1_runtime_bar_sbgdb-20200507-pntset-0000030.pdf](A1_runtime_bar_sbgdb-20200507-pntset-0000030.pdf)

### A1_runtime_bar_stars-0000030.instance

![A1_runtime_bar_stars-0000030.instance](A1_runtime_bar_stars-0000030.instance.png)

Runtime across algorithms and objectives for stars-0000030.instance. **Takeaway:** Best observed runtime: 0.00034725 s.

Vector PDF: [A1_runtime_bar_stars-0000030.instance.pdf](A1_runtime_bar_stars-0000030.instance.pdf)

### A1_runtime_bar_uniform-0000030-2

![A1_runtime_bar_uniform-0000030-2](A1_runtime_bar_uniform-0000030-2.png)

Runtime across algorithms and objectives for uniform-0000030-2. **Takeaway:** Best observed runtime: 0.000421417 s.

Vector PDF: [A1_runtime_bar_uniform-0000030-2.pdf](A1_runtime_bar_uniform-0000030-2.pdf)

### A1_runtime_bar_us-night-0000030.instance

![A1_runtime_bar_us-night-0000030.instance](A1_runtime_bar_us-night-0000030.instance.png)

Runtime across algorithms and objectives for us-night-0000030.instance. **Takeaway:** Best observed runtime: 0.000380041 s.

Vector PDF: [A1_runtime_bar_us-night-0000030.instance.pdf](A1_runtime_bar_us-night-0000030.instance.pdf)

### A2_runtime_box

![A2_runtime_box](A2_runtime_box.png)

Runtime distribution per algorithm. **Takeaway:** Compare per-algorithm distributions across the available instances.

Vector PDF: [A2_runtime_box.pdf](A2_runtime_box.pdf)

### A3_runtime_vs_n_m25

![A3_runtime_vs_n_m25](A3_runtime_vs_n_m25.png)

Scaling of runtime (s) against n with m fixed at 25. **Takeaway:** Lines show medians and shaded regions show interquartile ranges.

Vector PDF: [A3_runtime_vs_n_m25.pdf](A3_runtime_vs_n_m25.pdf)

### A4_runtime_vs_m_n500

![A4_runtime_vs_m_n500](A4_runtime_vs_m_n500.png)

Scaling of runtime (s) against m with n fixed at 500. **Takeaway:** Lines show medians and shaded regions show interquartile ranges.

Vector PDF: [A4_runtime_vs_m_n500.pdf](A4_runtime_vs_m_n500.pdf)

### A5_runtime_heatmap

![A5_runtime_heatmap](A5_runtime_heatmap.png)

Runtime heatmap (median across objectives). **Takeaway:** Cells summarize median values across objectives.

Vector PDF: [A5_runtime_heatmap.pdf](A5_runtime_heatmap.pdf)

### A6_runtime_ecdf

![A6_runtime_ecdf](A6_runtime_ecdf.png)

Empirical cumulative distribution of runtime per algorithm. **Takeaway:** Curves further left indicate faster runs.

Vector PDF: [A6_runtime_ecdf.pdf](A6_runtime_ecdf.pdf)

### B1_memory_bar_att48

![B1_memory_bar_att48](B1_memory_bar_att48.png)

Peak memory across algorithms and objectives for att48. **Takeaway:** Best observed peak memory: 17.2812 MB.

Vector PDF: [B1_memory_bar_att48.pdf](B1_memory_bar_att48.pdf)

### B1_memory_bar_berlin52

![B1_memory_bar_berlin52](B1_memory_bar_berlin52.png)

Peak memory across algorithms and objectives for berlin52. **Takeaway:** Best observed peak memory: 17.3438 MB.

Vector PDF: [B1_memory_bar_berlin52.pdf](B1_memory_bar_berlin52.pdf)

### B1_memory_bar_eil51

![B1_memory_bar_eil51](B1_memory_bar_eil51.png)

Peak memory across algorithms and objectives for eil51. **Takeaway:** Best observed peak memory: 17.2812 MB.

Vector PDF: [B1_memory_bar_eil51.pdf](B1_memory_bar_eil51.pdf)

### B1_memory_bar_euro-night-0000030

![B1_memory_bar_euro-night-0000030](B1_memory_bar_euro-night-0000030.png)

Peak memory across algorithms and objectives for euro-night-0000030. **Takeaway:** Best observed peak memory: 17 MB.

Vector PDF: [B1_memory_bar_euro-night-0000030.pdf](B1_memory_bar_euro-night-0000030.pdf)

### B1_memory_bar_london-0000030.instance

![B1_memory_bar_london-0000030.instance](B1_memory_bar_london-0000030.instance.png)

Peak memory across algorithms and objectives for london-0000030.instance. **Takeaway:** Best observed peak memory: 16.9844 MB.

Vector PDF: [B1_memory_bar_london-0000030.instance.pdf](B1_memory_bar_london-0000030.instance.pdf)

### B1_memory_bar_sbgdb-20200507-fpg-poly_0000000030

![B1_memory_bar_sbgdb-20200507-fpg-poly_0000000030](B1_memory_bar_sbgdb-20200507-fpg-poly_0000000030.png)

Peak memory across algorithms and objectives for sbgdb-20200507-fpg-poly_0000000030. **Takeaway:** Best observed peak memory: 17 MB.

Vector PDF: [B1_memory_bar_sbgdb-20200507-fpg-poly_0000000030.pdf](B1_memory_bar_sbgdb-20200507-fpg-poly_0000000030.pdf)

### B1_memory_bar_sbgdb-20200507-pntset-0000030

![B1_memory_bar_sbgdb-20200507-pntset-0000030](B1_memory_bar_sbgdb-20200507-pntset-0000030.png)

Peak memory across algorithms and objectives for sbgdb-20200507-pntset-0000030. **Takeaway:** Best observed peak memory: 16.9844 MB.

Vector PDF: [B1_memory_bar_sbgdb-20200507-pntset-0000030.pdf](B1_memory_bar_sbgdb-20200507-pntset-0000030.pdf)

### B1_memory_bar_stars-0000030.instance

![B1_memory_bar_stars-0000030.instance](B1_memory_bar_stars-0000030.instance.png)

Peak memory across algorithms and objectives for stars-0000030.instance. **Takeaway:** Best observed peak memory: 16.9688 MB.

Vector PDF: [B1_memory_bar_stars-0000030.instance.pdf](B1_memory_bar_stars-0000030.instance.pdf)

### B1_memory_bar_uniform-0000030-2

![B1_memory_bar_uniform-0000030-2](B1_memory_bar_uniform-0000030-2.png)

Peak memory across algorithms and objectives for uniform-0000030-2. **Takeaway:** Best observed peak memory: 17.0156 MB.

Vector PDF: [B1_memory_bar_uniform-0000030-2.pdf](B1_memory_bar_uniform-0000030-2.pdf)

### B1_memory_bar_us-night-0000030.instance

![B1_memory_bar_us-night-0000030.instance](B1_memory_bar_us-night-0000030.instance.png)

Peak memory across algorithms and objectives for us-night-0000030.instance. **Takeaway:** Best observed peak memory: 16.9688 MB.

Vector PDF: [B1_memory_bar_us-night-0000030.instance.pdf](B1_memory_bar_us-night-0000030.instance.pdf)

### B2_memory_box

![B2_memory_box](B2_memory_box.png)

Peak memory distribution per algorithm. **Takeaway:** Compare per-algorithm distributions across the available instances.

Vector PDF: [B2_memory_box.pdf](B2_memory_box.pdf)

### B3_memory_vs_n_m25

![B3_memory_vs_n_m25](B3_memory_vs_n_m25.png)

Scaling of peak memory (mb) against n with m fixed at 25. **Takeaway:** Lines show medians and shaded regions show interquartile ranges.

Vector PDF: [B3_memory_vs_n_m25.pdf](B3_memory_vs_n_m25.pdf)

### B4_memory_heatmap

![B4_memory_heatmap](B4_memory_heatmap.png)

Peak memory heatmap (median across objectives). **Takeaway:** Cells summarize median values across objectives.

Vector PDF: [B4_memory_heatmap.pdf](B4_memory_heatmap.pdf)

### B5_memory_vs_m_n500

![B5_memory_vs_m_n500](B5_memory_vs_m_n500.png)

Scaling of peak memory (mb) against m with n fixed at 500. **Takeaway:** Lines show medians and shaded regions show interquartile ranges.

Vector PDF: [B5_memory_vs_m_n500.pdf](B5_memory_vs_m_n500.pdf)

### C1_gap_boxplot

![C1_gap_boxplot](C1_gap_boxplot.png)

Optimality gap distribution per algorithm. **Takeaway:** Compare per-algorithm distributions across the available instances.

Vector PDF: [C1_gap_boxplot.pdf](C1_gap_boxplot.pdf)

### C2_gap_ecdf

![C2_gap_ecdf](C2_gap_ecdf.png)

Empirical cumulative distribution of optimality gaps, with median and 95th-percentile values in each legend entry. **Takeaway:** The curve furthest left has lower gaps overall.

Vector PDF: [C2_gap_ecdf.pdf](C2_gap_ecdf.pdf)

### C3_gap_vs_n

![C3_gap_vs_n](C3_gap_vs_n.png)

Scatter points and LOWESS-smoothed trends show optimality gap against point count. **Takeaway:** Lower trend lines indicate smaller gaps as instance size changes.

Vector PDF: [C3_gap_vs_n.pdf](C3_gap_vs_n.pdf)

### C4_objective_value_vs_n_m25

![C4_objective_value_vs_n_m25](C4_objective_value_vs_n_m25.png)

MinMax peak and MinSum integral objective values versus instance size; lines are medians with IQR bands. **Takeaway:** Compare objective trends separately for peak and integral cost.

Vector PDF: [C4_objective_value_vs_n_m25.pdf](C4_objective_value_vs_n_m25.pdf)

### C5_gap_vs_runtime

![C5_gap_vs_runtime](C5_gap_vs_runtime.png)

Optimality gap vs runtime. **Takeaway:** Each point represents one algorithm/objective run.

Vector PDF: [C5_gap_vs_runtime.pdf](C5_gap_vs_runtime.pdf)

### C6_objective_boxplot

![C6_objective_boxplot](C6_objective_boxplot.png)

Objective value distribution per algorithm. **Takeaway:** Compare per-algorithm distributions across the available instances.

Vector PDF: [C6_objective_boxplot.pdf](C6_objective_boxplot.pdf)

### D1_pareto_att48

![D1_pareto_att48](D1_pareto_att48.png)

Runtime-quality trade-offs for att48. **Takeaway:** Points on a front are non-dominated in runtime and objective.

Vector PDF: [D1_pareto_att48.pdf](D1_pareto_att48.pdf)

### D1_pareto_berlin52

![D1_pareto_berlin52](D1_pareto_berlin52.png)

Runtime-quality trade-offs for berlin52. **Takeaway:** Points on a front are non-dominated in runtime and objective.

Vector PDF: [D1_pareto_berlin52.pdf](D1_pareto_berlin52.pdf)

### D1_pareto_eil51

![D1_pareto_eil51](D1_pareto_eil51.png)

Runtime-quality trade-offs for eil51. **Takeaway:** Points on a front are non-dominated in runtime and objective.

Vector PDF: [D1_pareto_eil51.pdf](D1_pareto_eil51.pdf)

### D1_pareto_euro-night-0000030

![D1_pareto_euro-night-0000030](D1_pareto_euro-night-0000030.png)

Runtime-quality trade-offs for euro-night-0000030. **Takeaway:** Points on a front are non-dominated in runtime and objective.

Vector PDF: [D1_pareto_euro-night-0000030.pdf](D1_pareto_euro-night-0000030.pdf)

### D1_pareto_london-0000030.instance

![D1_pareto_london-0000030.instance](D1_pareto_london-0000030.instance.png)

Runtime-quality trade-offs for london-0000030.instance. **Takeaway:** Points on a front are non-dominated in runtime and objective.

Vector PDF: [D1_pareto_london-0000030.instance.pdf](D1_pareto_london-0000030.instance.pdf)

### D1_pareto_sbgdb-20200507-fpg-poly_0000000030

![D1_pareto_sbgdb-20200507-fpg-poly_0000000030](D1_pareto_sbgdb-20200507-fpg-poly_0000000030.png)

Runtime-quality trade-offs for sbgdb-20200507-fpg-poly_0000000030. **Takeaway:** Points on a front are non-dominated in runtime and objective.

Vector PDF: [D1_pareto_sbgdb-20200507-fpg-poly_0000000030.pdf](D1_pareto_sbgdb-20200507-fpg-poly_0000000030.pdf)

### D1_pareto_sbgdb-20200507-pntset-0000030

![D1_pareto_sbgdb-20200507-pntset-0000030](D1_pareto_sbgdb-20200507-pntset-0000030.png)

Runtime-quality trade-offs for sbgdb-20200507-pntset-0000030. **Takeaway:** Points on a front are non-dominated in runtime and objective.

Vector PDF: [D1_pareto_sbgdb-20200507-pntset-0000030.pdf](D1_pareto_sbgdb-20200507-pntset-0000030.pdf)

### D1_pareto_stars-0000030.instance

![D1_pareto_stars-0000030.instance](D1_pareto_stars-0000030.instance.png)

Runtime-quality trade-offs for stars-0000030.instance. **Takeaway:** Points on a front are non-dominated in runtime and objective.

Vector PDF: [D1_pareto_stars-0000030.instance.pdf](D1_pareto_stars-0000030.instance.pdf)

### D1_pareto_uniform-0000030-2

![D1_pareto_uniform-0000030-2](D1_pareto_uniform-0000030-2.png)

Runtime-quality trade-offs for uniform-0000030-2. **Takeaway:** Points on a front are non-dominated in runtime and objective.

Vector PDF: [D1_pareto_uniform-0000030-2.pdf](D1_pareto_uniform-0000030-2.pdf)

### D1_pareto_us-night-0000030.instance

![D1_pareto_us-night-0000030.instance](D1_pareto_us-night-0000030.instance.png)

Runtime-quality trade-offs for us-night-0000030.instance. **Takeaway:** Points on a front are non-dominated in runtime and objective.

Vector PDF: [D1_pareto_us-night-0000030.instance.pdf](D1_pareto_us-night-0000030.instance.pdf)

### D2_pareto_aggregated

![D2_pareto_aggregated](D2_pareto_aggregated.png)

Runtime-quality trade-offs aggregated by algorithm and objective. **Takeaway:** Points on a front are non-dominated in median runtime and normalized objective.

Vector PDF: [D2_pareto_aggregated.pdf](D2_pareto_aggregated.pdf)

### E2_minmax_quality_heatmap

![E2_minmax_quality_heatmap](E2_minmax_quality_heatmap.png)

MinMax quality heatmap (rank per instance). **Takeaway:** Lower ranks indicate better objective values within each instance.

Vector PDF: [E2_minmax_quality_heatmap.pdf](E2_minmax_quality_heatmap.pdf)

### E3_minsum_quality_heatmap

![E3_minsum_quality_heatmap](E3_minsum_quality_heatmap.png)

MinSum quality heatmap (rank per instance). **Takeaway:** Lower ranks indicate better objective values within each instance.

Vector PDF: [E3_minsum_quality_heatmap.pdf](E3_minsum_quality_heatmap.pdf)

### D3_tradeoff_branch-and-bound_minmax

![D3_tradeoff_branch-and-bound_minmax](D3_tradeoff_branch-and-bound_minmax.png)

Runtime-quality observations for branch-and-bound under minmax. **Takeaway:** Faster and lower points are preferred.

Vector PDF: [D3_tradeoff_branch-and-bound_minmax.pdf](D3_tradeoff_branch-and-bound_minmax.pdf)

### D3_tradeoff_branch-and-bound_minsum

![D3_tradeoff_branch-and-bound_minsum](D3_tradeoff_branch-and-bound_minsum.png)

Runtime-quality observations for branch-and-bound under minsum. **Takeaway:** Faster and lower points are preferred.

Vector PDF: [D3_tradeoff_branch-and-bound_minsum.pdf](D3_tradeoff_branch-and-bound_minsum.pdf)

### D3_tradeoff_genetic_minmax

![D3_tradeoff_genetic_minmax](D3_tradeoff_genetic_minmax.png)

Runtime-quality observations for genetic under minmax. **Takeaway:** Faster and lower points are preferred.

Vector PDF: [D3_tradeoff_genetic_minmax.pdf](D3_tradeoff_genetic_minmax.pdf)

### D3_tradeoff_genetic_minsum

![D3_tradeoff_genetic_minsum](D3_tradeoff_genetic_minsum.png)

Runtime-quality observations for genetic under minsum. **Takeaway:** Faster and lower points are preferred.

Vector PDF: [D3_tradeoff_genetic_minsum.pdf](D3_tradeoff_genetic_minsum.pdf)

### D3_tradeoff_greedy_minmax

![D3_tradeoff_greedy_minmax](D3_tradeoff_greedy_minmax.png)

Runtime-quality observations for greedy under minmax. **Takeaway:** Faster and lower points are preferred.

Vector PDF: [D3_tradeoff_greedy_minmax.pdf](D3_tradeoff_greedy_minmax.pdf)

### D3_tradeoff_greedy_minsum

![D3_tradeoff_greedy_minsum](D3_tradeoff_greedy_minsum.png)

Runtime-quality observations for greedy under minsum. **Takeaway:** Faster and lower points are preferred.

Vector PDF: [D3_tradeoff_greedy_minsum.pdf](D3_tradeoff_greedy_minsum.pdf)

### D3_tradeoff_local-search_minmax

![D3_tradeoff_local-search_minmax](D3_tradeoff_local-search_minmax.png)

Runtime-quality observations for local-search under minmax. **Takeaway:** Faster and lower points are preferred.

Vector PDF: [D3_tradeoff_local-search_minmax.pdf](D3_tradeoff_local-search_minmax.pdf)

### D3_tradeoff_local-search_minsum

![D3_tradeoff_local-search_minsum](D3_tradeoff_local-search_minsum.png)

Runtime-quality observations for local-search under minsum. **Takeaway:** Faster and lower points are preferred.

Vector PDF: [D3_tradeoff_local-search_minsum.pdf](D3_tradeoff_local-search_minsum.pdf)

### D3_tradeoff_lp-rounding_minmax

![D3_tradeoff_lp-rounding_minmax](D3_tradeoff_lp-rounding_minmax.png)

Runtime-quality observations for lp-rounding under minmax. **Takeaway:** Faster and lower points are preferred.

Vector PDF: [D3_tradeoff_lp-rounding_minmax.pdf](D3_tradeoff_lp-rounding_minmax.pdf)

### D3_tradeoff_lp-rounding_minsum

![D3_tradeoff_lp-rounding_minsum](D3_tradeoff_lp-rounding_minsum.png)

Runtime-quality observations for lp-rounding under minsum. **Takeaway:** Faster and lower points are preferred.

Vector PDF: [D3_tradeoff_lp-rounding_minsum.pdf](D3_tradeoff_lp-rounding_minsum.pdf)

### D3_tradeoff_nn_minmax

![D3_tradeoff_nn_minmax](D3_tradeoff_nn_minmax.png)

Runtime-quality observations for nn under minmax. **Takeaway:** Faster and lower points are preferred.

Vector PDF: [D3_tradeoff_nn_minmax.pdf](D3_tradeoff_nn_minmax.pdf)

### D3_tradeoff_nn_minsum

![D3_tradeoff_nn_minsum](D3_tradeoff_nn_minsum.png)

Runtime-quality observations for nn under minsum. **Takeaway:** Faster and lower points are preferred.

Vector PDF: [D3_tradeoff_nn_minsum.pdf](D3_tradeoff_nn_minsum.pdf)

### D3_tradeoff_primal-dual_minmax

![D3_tradeoff_primal-dual_minmax](D3_tradeoff_primal-dual_minmax.png)

Runtime-quality observations for primal-dual under minmax. **Takeaway:** Faster and lower points are preferred.

Vector PDF: [D3_tradeoff_primal-dual_minmax.pdf](D3_tradeoff_primal-dual_minmax.pdf)

### D3_tradeoff_primal-dual_minsum

![D3_tradeoff_primal-dual_minsum](D3_tradeoff_primal-dual_minsum.png)

Runtime-quality observations for primal-dual under minsum. **Takeaway:** Faster and lower points are preferred.

Vector PDF: [D3_tradeoff_primal-dual_minsum.pdf](D3_tradeoff_primal-dual_minsum.pdf)

### D3_tradeoff_sa_minmax

![D3_tradeoff_sa_minmax](D3_tradeoff_sa_minmax.png)

Runtime-quality observations for sa under minmax. **Takeaway:** Faster and lower points are preferred.

Vector PDF: [D3_tradeoff_sa_minmax.pdf](D3_tradeoff_sa_minmax.pdf)

### D3_tradeoff_sa_minsum

![D3_tradeoff_sa_minsum](D3_tradeoff_sa_minsum.png)

Runtime-quality observations for sa under minsum. **Takeaway:** Faster and lower points are preferred.

Vector PDF: [D3_tradeoff_sa_minsum.pdf](D3_tradeoff_sa_minsum.pdf)

### D3_tradeoff_shifting_minmax

![D3_tradeoff_shifting_minmax](D3_tradeoff_shifting_minmax.png)

Runtime-quality observations for shifting under minmax. **Takeaway:** Faster and lower points are preferred.

Vector PDF: [D3_tradeoff_shifting_minmax.pdf](D3_tradeoff_shifting_minmax.pdf)

### D3_tradeoff_shifting_minsum

![D3_tradeoff_shifting_minsum](D3_tradeoff_shifting_minsum.png)

Runtime-quality observations for shifting under minsum. **Takeaway:** Faster and lower points are preferred.

Vector PDF: [D3_tradeoff_shifting_minsum.pdf](D3_tradeoff_shifting_minsum.pdf)

### E1_quality_heatmap

![E1_quality_heatmap](E1_quality_heatmap.png)

Quality heatmap (rank per instance). **Takeaway:** Lower ranks indicate better objective values within each instance.

Vector PDF: [E1_quality_heatmap.pdf](E1_quality_heatmap.pdf)

### F1_convergence_att48

![F1_convergence_att48](F1_convergence_att48.png)

MinMax and MinSum relative-gap traces for att48. **Takeaway:** Falling curves indicate convergence to a smaller relative gap.

Vector PDF: [F1_convergence_att48.pdf](F1_convergence_att48.pdf)

### F1_convergence_berlin52

![F1_convergence_berlin52](F1_convergence_berlin52.png)

MinMax and MinSum relative-gap traces for berlin52. **Takeaway:** Falling curves indicate convergence to a smaller relative gap.

Vector PDF: [F1_convergence_berlin52.pdf](F1_convergence_berlin52.pdf)

### F1_convergence_eil51

![F1_convergence_eil51](F1_convergence_eil51.png)

MinMax and MinSum relative-gap traces for eil51. **Takeaway:** Falling curves indicate convergence to a smaller relative gap.

Vector PDF: [F1_convergence_eil51.pdf](F1_convergence_eil51.pdf)

### F1_convergence_euro-night-0000030

![F1_convergence_euro-night-0000030](F1_convergence_euro-night-0000030.png)

MinMax and MinSum relative-gap traces for euro-night-0000030. **Takeaway:** Falling curves indicate convergence to a smaller relative gap.

Vector PDF: [F1_convergence_euro-night-0000030.pdf](F1_convergence_euro-night-0000030.pdf)

### F1_convergence_london-0000030.instance

![F1_convergence_london-0000030.instance](F1_convergence_london-0000030.instance.png)

MinMax and MinSum relative-gap traces for london-0000030.instance. **Takeaway:** Falling curves indicate convergence to a smaller relative gap.

Vector PDF: [F1_convergence_london-0000030.instance.pdf](F1_convergence_london-0000030.instance.pdf)

### F1_convergence_sbgdb-20200507-fpg-poly_0000000030

![F1_convergence_sbgdb-20200507-fpg-poly_0000000030](F1_convergence_sbgdb-20200507-fpg-poly_0000000030.png)

MinMax and MinSum relative-gap traces for sbgdb-20200507-fpg-poly_0000000030. **Takeaway:** Falling curves indicate convergence to a smaller relative gap.

Vector PDF: [F1_convergence_sbgdb-20200507-fpg-poly_0000000030.pdf](F1_convergence_sbgdb-20200507-fpg-poly_0000000030.pdf)

### F1_convergence_sbgdb-20200507-pntset-0000030

![F1_convergence_sbgdb-20200507-pntset-0000030](F1_convergence_sbgdb-20200507-pntset-0000030.png)

MinMax and MinSum relative-gap traces for sbgdb-20200507-pntset-0000030. **Takeaway:** Falling curves indicate convergence to a smaller relative gap.

Vector PDF: [F1_convergence_sbgdb-20200507-pntset-0000030.pdf](F1_convergence_sbgdb-20200507-pntset-0000030.pdf)

### F1_convergence_stars-0000030.instance

![F1_convergence_stars-0000030.instance](F1_convergence_stars-0000030.instance.png)

MinMax and MinSum relative-gap traces for stars-0000030.instance. **Takeaway:** Falling curves indicate convergence to a smaller relative gap.

Vector PDF: [F1_convergence_stars-0000030.instance.pdf](F1_convergence_stars-0000030.instance.pdf)

### F1_convergence_uniform-0000030-2

![F1_convergence_uniform-0000030-2](F1_convergence_uniform-0000030-2.png)

MinMax and MinSum relative-gap traces for uniform-0000030-2. **Takeaway:** Falling curves indicate convergence to a smaller relative gap.

Vector PDF: [F1_convergence_uniform-0000030-2.pdf](F1_convergence_uniform-0000030-2.pdf)

### F1_convergence_us-night-0000030.instance

![F1_convergence_us-night-0000030.instance](F1_convergence_us-night-0000030.instance.png)

MinMax and MinSum relative-gap traces for us-night-0000030.instance. **Takeaway:** Falling curves indicate convergence to a smaller relative gap.

Vector PDF: [F1_convergence_us-night-0000030.instance.pdf](F1_convergence_us-night-0000030.instance.pdf)

### F2_convergence_grid

![F2_convergence_grid](F2_convergence_grid.png)

Per-instance overlay of MinMax and MinSum convergence traces. **Takeaway:** Compare convergence behavior across the full dataset.

Vector PDF: [F2_convergence_grid.pdf](F2_convergence_grid.pdf)

### G1_summary_dashboard

![G1_summary_dashboard](G1_summary_dashboard.png)

Dashboard combining runtime and gap distributions with objective-specific Pareto trade-offs. **Takeaway:** Trade-offs balance low runtime against low normalized objective.

Vector PDF: [G1_summary_dashboard.pdf](G1_summary_dashboard.pdf)

## Key findings

- Fastest by median runtime: **nn** (0.000626438 s).
- Lowest median optimality gap: **branch-and-bound** (99.868%).
- Most represented on aggregated Pareto fronts: **greedy**.
- Algorithms dominated on every comparable instance/objective: none detected.
