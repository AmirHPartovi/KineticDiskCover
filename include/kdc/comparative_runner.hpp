#pragma once

#include "kdc/minmax.hpp"
#include "kdc/minsum.hpp"
#include "kdc/benchmark_protocol.hpp"
#include <nlohmann/json.hpp>
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
  std::string algorithm_category;
  std::string requested_backend;
  std::string actual_backend;
  unsigned seed{0};
  int repeat{0};
  std::string experiment_id;
  VerificationKind verification_kind_minmax{VerificationKind::NONE};
  VerificationKind verification_kind_minsum{VerificationKind::NONE};
  double verification_time_minmax_sec{0.0};
  double verification_time_minsum_sec{0.0};
  double serialization_time_sec{0.0};
  double total_wall_time_sec{0.0};
  bool feasible_minmax{false};
  bool feasible_minsum{false};
  bool timeout_minmax{false};
  bool timeout_minsum{false};
  OptimalityStatus optimality_status_minmax{OptimalityStatus::FAILED};
  OptimalityStatus optimality_status_minsum{OptimalityStatus::FAILED};
  nlohmann::json configuration;
};

class ComparativeRunner {
 public:
  struct Config {
    MinMaxSolver::Config minmax_cfg;
    MinSumSolver::Config minsum_cfg;
    std::string output_dir{"results/comparison"};
    bool save_solutions{false};
    BenchmarkProfile profile{BenchmarkProfile::FAST};
    unsigned seed{42U};
    int repeats{1};
    double fast_time_limit_sec{30.0};
    double exact_time_limit_sec{600.0};
    double per_static_time_limit_sec{60.0};
    std::string requested_backend{"auto"};
    std::string actual_backend;
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
