# Smoke / development validation charts

These figures describe smoke / development validation only; they are not the full scientific benchmark.

## Dataset description

- Instances: 10
- Families: 9
- Algorithms: 3
- n range: 1–13
- m range: 25–25
- Plot-level comparison subset: records with `feasible == true` and `verified == true`, with finite plotted metrics. Failed and timed-out records remain represented in the separate raw result integrity report.
- Gap values are empirical/result-schema quantities; they are not theoretical approximation guarantees or certified gaps unless explicitly identified as `certified_gap` in raw records.

## Summary statistics

| Algorithm | Wall time median [IQR] (s) | Memory median [IQR] (MB) | Gap median [IQR] (%) | Objective median [IQR] |
|---|---:|---:|---:|---:|
| branch-and-bound | 0.001 [0.001, 0.024] | 17.1 [17.0, 17.8] | 0.00 [0.00, 0.01] | 1316.63 [977.191, 2183.99] |
| greedy | 0.001 [0.001, 0.013] | 17.1 [17.0, 17.3] | 24.74 [4.47, 115.04] | 1316.63 [977.191, 2451.8] |
| nn | 0.001 [0.001, 0.011] | 17.1 [17.0, 17.3] | 131662.51 [97719.14, 239859.58] | 1316.63 [977.191, 2398.6] |

## Figures

### A1_runtime_bar_att48

![A1_runtime_bar_att48](A1_runtime_bar_att48.png)

Runtime across algorithms and objectives for att48. **Takeaway:** Best observed runtime: 0.0146839 s.

Vector PDF: [A1_runtime_bar_att48.pdf](A1_runtime_bar_att48.pdf)

### A1_runtime_bar_berlin52

![A1_runtime_bar_berlin52](A1_runtime_bar_berlin52.png)

Runtime across algorithms and objectives for berlin52. **Takeaway:** Best observed runtime: 0.0162759 s.

Vector PDF: [A1_runtime_bar_berlin52.pdf](A1_runtime_bar_berlin52.pdf)

### A1_runtime_bar_eil51

![A1_runtime_bar_eil51](A1_runtime_bar_eil51.png)

Runtime across algorithms and objectives for eil51. **Takeaway:** Best observed runtime: 0.0243142 s.

Vector PDF: [A1_runtime_bar_eil51.pdf](A1_runtime_bar_eil51.pdf)

### A1_runtime_bar_euro-night-0000030

![A1_runtime_bar_euro-night-0000030](A1_runtime_bar_euro-night-0000030.png)

Runtime across algorithms and objectives for euro-night-0000030. **Takeaway:** Best observed runtime: 0.0006525 s.

Vector PDF: [A1_runtime_bar_euro-night-0000030.pdf](A1_runtime_bar_euro-night-0000030.pdf)

### A1_runtime_bar_london-0000030.instance

![A1_runtime_bar_london-0000030.instance](A1_runtime_bar_london-0000030.instance.png)

Runtime across algorithms and objectives for london-0000030.instance. **Takeaway:** Best observed runtime: 0.000689917 s.

Vector PDF: [A1_runtime_bar_london-0000030.instance.pdf](A1_runtime_bar_london-0000030.instance.pdf)

### A1_runtime_bar_sbgdb-20200507-fpg-poly_0000000030

![A1_runtime_bar_sbgdb-20200507-fpg-poly_0000000030](A1_runtime_bar_sbgdb-20200507-fpg-poly_0000000030.png)

Runtime across algorithms and objectives for sbgdb-20200507-fpg-poly_0000000030. **Takeaway:** Best observed runtime: 0.000496501 s.

Vector PDF: [A1_runtime_bar_sbgdb-20200507-fpg-poly_0000000030.pdf](A1_runtime_bar_sbgdb-20200507-fpg-poly_0000000030.pdf)

### A1_runtime_bar_sbgdb-20200507-pntset-0000030

![A1_runtime_bar_sbgdb-20200507-pntset-0000030](A1_runtime_bar_sbgdb-20200507-pntset-0000030.png)

Runtime across algorithms and objectives for sbgdb-20200507-pntset-0000030. **Takeaway:** Best observed runtime: 0.000483 s.

Vector PDF: [A1_runtime_bar_sbgdb-20200507-pntset-0000030.pdf](A1_runtime_bar_sbgdb-20200507-pntset-0000030.pdf)

### A1_runtime_bar_stars-0000030.instance

![A1_runtime_bar_stars-0000030.instance](A1_runtime_bar_stars-0000030.instance.png)

Runtime across algorithms and objectives for stars-0000030.instance. **Takeaway:** Best observed runtime: 0.000958666 s.

Vector PDF: [A1_runtime_bar_stars-0000030.instance.pdf](A1_runtime_bar_stars-0000030.instance.pdf)

### A1_runtime_bar_uniform-0000030-2

![A1_runtime_bar_uniform-0000030-2](A1_runtime_bar_uniform-0000030-2.png)

Runtime across algorithms and objectives for uniform-0000030-2. **Takeaway:** Best observed runtime: 0.000874958 s.

Vector PDF: [A1_runtime_bar_uniform-0000030-2.pdf](A1_runtime_bar_uniform-0000030-2.pdf)

### A1_runtime_bar_us-night-0000030.instance

![A1_runtime_bar_us-night-0000030.instance](A1_runtime_bar_us-night-0000030.instance.png)

Runtime across algorithms and objectives for us-night-0000030.instance. **Takeaway:** Best observed runtime: 0.000380959 s.

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

Peak memory across algorithms and objectives for att48. **Takeaway:** Best observed peak memory: 17.375 MB.

Vector PDF: [B1_memory_bar_att48.pdf](B1_memory_bar_att48.pdf)

### B1_memory_bar_berlin52

![B1_memory_bar_berlin52](B1_memory_bar_berlin52.png)

Peak memory across algorithms and objectives for berlin52. **Takeaway:** Best observed peak memory: 17.3594 MB.

Vector PDF: [B1_memory_bar_berlin52.pdf](B1_memory_bar_berlin52.pdf)

### B1_memory_bar_eil51

![B1_memory_bar_eil51](B1_memory_bar_eil51.png)

Peak memory across algorithms and objectives for eil51. **Takeaway:** Best observed peak memory: 17.5 MB.

Vector PDF: [B1_memory_bar_eil51.pdf](B1_memory_bar_eil51.pdf)

### B1_memory_bar_euro-night-0000030

![B1_memory_bar_euro-night-0000030](B1_memory_bar_euro-night-0000030.png)

Peak memory across algorithms and objectives for euro-night-0000030. **Takeaway:** Best observed peak memory: 17.0312 MB.

Vector PDF: [B1_memory_bar_euro-night-0000030.pdf](B1_memory_bar_euro-night-0000030.pdf)

### B1_memory_bar_london-0000030.instance

![B1_memory_bar_london-0000030.instance](B1_memory_bar_london-0000030.instance.png)

Peak memory across algorithms and objectives for london-0000030.instance. **Takeaway:** Best observed peak memory: 17.0625 MB.

Vector PDF: [B1_memory_bar_london-0000030.instance.pdf](B1_memory_bar_london-0000030.instance.pdf)

### B1_memory_bar_sbgdb-20200507-fpg-poly_0000000030

![B1_memory_bar_sbgdb-20200507-fpg-poly_0000000030](B1_memory_bar_sbgdb-20200507-fpg-poly_0000000030.png)

Peak memory across algorithms and objectives for sbgdb-20200507-fpg-poly_0000000030. **Takeaway:** Best observed peak memory: 16.9062 MB.

Vector PDF: [B1_memory_bar_sbgdb-20200507-fpg-poly_0000000030.pdf](B1_memory_bar_sbgdb-20200507-fpg-poly_0000000030.pdf)

### B1_memory_bar_sbgdb-20200507-pntset-0000030

![B1_memory_bar_sbgdb-20200507-pntset-0000030](B1_memory_bar_sbgdb-20200507-pntset-0000030.png)

Peak memory across algorithms and objectives for sbgdb-20200507-pntset-0000030. **Takeaway:** Best observed peak memory: 16.8906 MB.

Vector PDF: [B1_memory_bar_sbgdb-20200507-pntset-0000030.pdf](B1_memory_bar_sbgdb-20200507-pntset-0000030.pdf)

### B1_memory_bar_stars-0000030.instance

![B1_memory_bar_stars-0000030.instance](B1_memory_bar_stars-0000030.instance.png)

Peak memory across algorithms and objectives for stars-0000030.instance. **Takeaway:** Best observed peak memory: 17.0938 MB.

Vector PDF: [B1_memory_bar_stars-0000030.instance.pdf](B1_memory_bar_stars-0000030.instance.pdf)

### B1_memory_bar_uniform-0000030-2

![B1_memory_bar_uniform-0000030-2](B1_memory_bar_uniform-0000030-2.png)

Peak memory across algorithms and objectives for uniform-0000030-2. **Takeaway:** Best observed peak memory: 17.0781 MB.

Vector PDF: [B1_memory_bar_uniform-0000030-2.pdf](B1_memory_bar_uniform-0000030-2.pdf)

### B1_memory_bar_us-night-0000030.instance

![B1_memory_bar_us-night-0000030.instance](B1_memory_bar_us-night-0000030.instance.png)

Peak memory across algorithms and objectives for us-night-0000030.instance. **Takeaway:** Best observed peak memory: 16.8906 MB.

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

### D3_tradeoff_greedy_minmax

![D3_tradeoff_greedy_minmax](D3_tradeoff_greedy_minmax.png)

Runtime-quality observations for greedy under minmax. **Takeaway:** Faster and lower points are preferred.

Vector PDF: [D3_tradeoff_greedy_minmax.pdf](D3_tradeoff_greedy_minmax.pdf)

### D3_tradeoff_nn_minmax

![D3_tradeoff_nn_minmax](D3_tradeoff_nn_minmax.png)

Runtime-quality observations for nn under minmax. **Takeaway:** Faster and lower points are preferred.

Vector PDF: [D3_tradeoff_nn_minmax.pdf](D3_tradeoff_nn_minmax.pdf)

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

- Fastest by median runtime: **branch-and-bound** (0.000819646 s).
- Lowest median optimality gap: **branch-and-bound** (2.91015e-14%).
- Most represented on aggregated Pareto fronts: **branch-and-bound**.
- Algorithms dominated on every comparable instance/objective: none detected.
