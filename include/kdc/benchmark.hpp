#pragma once

#include "kdc/minmax.hpp"
#include "kdc/minsum.hpp"
#include "kdc/benchmark_protocol.hpp"
#include "kdc/objective.hpp"
#include "kdc/result_status.hpp"
#include "kdc/solver_interface.hpp"
#include "kdc/types.hpp"
#include <nlohmann/json.hpp>

#include <string>
#include <optional>
#include <limits>
#include <vector>

namespace kdc {
struct BenchmarkConfig {
  std::string dataset_dir{"data"};
  std::string output_dir{"results"};
  std::vector<std::string> instance_names;
  std::vector<std::string> algorithm_names;
  ObjectiveType objective{ObjectiveType::MIN_MAX};
  int num_repeats{1};
  unsigned seed{42U};
  BenchmarkProfile profile{BenchmarkProfile::EXACT_REFERENCE};
  bool both_objectives{false};
  std::string exact_reference{"auto"};
  std::string selected_backend;
  std::string solver_version{"unknown"};
  bool native_kont{false};
  bool fallback_used{false};
  double per_static_time_limit_sec{60.0};
  double fast_time_limit_sec{30.0};
  double exact_time_limit_sec{600.0};
  bool measure_memory{true};
  bool verify_after{true};
  bool verify_each_iteration{false};
  bool parallel{false};
  int num_threads{1};
  MinMaxSolver::Config minmax_cfg;
  MinSumSolver::Config minsum_cfg;
  std::string solver_name{"KONT"};
  std::string algorithm_name;
  std::string actual_backend;
  std::string experiment_id;
  int repeat_index{0};
};

struct BenchmarkResult {
  std::string instance_name;
  std::string algorithm_name{"exact-reference"};
  std::string algorithm_category{"exact"};
  std::string requested_backend;
  std::string selected_backend;
  std::string actual_backend;
  std::string solver_name;
  std::string solver_version{"unknown"};
  bool native_kont{false};
  bool fallback_used{false};
  int repeat{0};
  int n{0};
  int m{0};
  ObjectiveType objective{ObjectiveType::MIN_MAX};
  double wall_time_sec{0.0};
  double solve_time_sec{0.0};
  double cpu_time_sec{0.0};
  double ip_time_sec{0.0};
  double peak_memory_mb{0.0};
  double objective_value{0.0};
  double lower_bound{0.0};
  BoundStatus bound_status{BoundStatus::NONE};
  double upper_bound{std::numeric_limits<double>::infinity()};
  std::optional<double> certified_gap;
  OptimalityStatus optimality_status{OptimalityStatus::FAILED};
  MinSumRefinementPolicy minsum_refinement_policy{
      MinSumRefinementPolicy::HEURISTIC_ADAPTIVE};
  bool exact_solver{false};
  bool feasible{false};
  double certified_lower_bound{0.0};
  double heuristic_lower_bound{0.0};
  double gap{0.0};
  int num_ip_solves{0};
  int num_iterations{0};
  bool verified{false};
  VerificationKind verification_kind{VerificationKind::NONE};
  double verification_time_sec{0.0};
  double serialization_time_sec{0.0};
  double total_wall_time_sec{0.0};
  double peak_cost{0.0};
  double integral_cost{0.0};
  std::optional<double> empirical_ratio_to_exact;
  std::optional<double> ratio_to_incumbent;
  unsigned seed{0};
  double global_time_limit_sec{0.0};
  double per_static_time_limit_sec{0.0};
  bool verify_each_iteration{false};
  bool verify_after{true};
  bool handovers_enabled{true};
  std::size_t num_static_solves{0};
  bool timeout{false};
  bool failed{false};
  std::string git_commit;
  std::string compiler;
  std::string build_type;
  int thread_count{1};
  std::string experiment_id;
  nlohmann::json configuration;
  bool time_limited{false};
  std::string timestamp;
};

class BenchmarkRunner {
 public:
  static BenchmarkResult run_single(const Instance& instance,
                                    ILPSolver& solver,
                                    ObjectiveType objective,
                                    const BenchmarkConfig& config);
  static void run_all(const BenchmarkConfig& config);
  static void save_json(const std::vector<BenchmarkResult>& results,
                        const std::string& path);
  static std::vector<BenchmarkResult> load_json(const std::string& path);
  static void save_csv(const std::vector<BenchmarkResult>& results,
                       const std::string& path);
  static double get_peak_memory_mb();
};

int run_benchmark();
}
