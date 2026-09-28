#pragma once

#include "kdc/minmax.hpp"
#include "kdc/minsum.hpp"
#include "kdc/types.hpp"

#include <string>
#include <vector>

namespace kdc {
struct ComparativeResult {
  std::string instance_name;
  int n{0};
  int m{0};
  std::string algorithm_name;

  double peak_cost_minmax{0.0};
  double lb_minmax{0.0};
  double gap_minmax{0.0};
  double time_minmax_sec{0.0};
  int iters_minmax{0};

  double integral_minsum{0.0};
  double lb_minsum{0.0};
  double gap_minsum{0.0};
  double time_minsum_sec{0.0};
  int iters_minsum{0};

  double peak_ratio{1.0};
  double integral_ratio{1.0};
  double peak_diff_abs{0.0};
  double integral_diff_abs{0.0};

  bool verified_minmax{false};
  bool verified_minsum{false};
};

class ComparativeRunner {
 public:
  struct Config {
    MinMaxSolver::Config minmax_cfg;
    MinSumSolver::Config minsum_cfg;
    std::string output_dir{"results/comparison"};
    bool save_solutions{false};
  };

  static ComparativeResult run(const Instance& instance,
                               IStaticSolver& static_solver,
                               const Config& config);
  static void run_all(const std::vector<Instance>& instances,
                      IStaticSolver& static_solver, const Config& config);
  static void save_json(const std::vector<ComparativeResult>& results,
                        const std::string& path);
  static void save_csv(const std::vector<ComparativeResult>& results,
                       const std::string& path);
};
}  // namespace kdc
