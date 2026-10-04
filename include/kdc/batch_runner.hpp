#pragma once

#include "kdc/istatic_solver.hpp"
#include "kdc/benchmark_protocol.hpp"
#include "kdc/minsum.hpp"
#include "kdc/objective.hpp"
#include "kdc/result_status.hpp"
#include "kdc/solver_interface.hpp"
#include "kdc/types.hpp"

#include <string>
#include <optional>
#include <limits>
#include <cstdint>
#include <string>
#include <nlohmann/json.hpp>
#include <thread>
#include <vector>

namespace kdc {

struct BatchRunRecord {
  std::string instance_name;
  std::string algorithm_name;
  std::string algorithm_category;
  std::string requested_backend;
  std::string selected_backend;
  std::string actual_backend;
  std::string solver_name;
  std::string solver_version{"unknown"};
  bool native_kont{false};
  bool fallback_used{false};
  std::string objective;
  int repeat{0};
  int n{0};
  int m{0};
  double wall_time_sec{0.0};
  double solve_time_sec{0.0};
  double cpu_time_sec{0.0};
  double peak_memory_mb{0.0};
  double time_limit_per_ip_sec{0.0};
  double objective_value{0.0};
  double lower_bound{0.0};
  BoundStatus bound_status{BoundStatus::NONE};
  double upper_bound{std::numeric_limits<double>::infinity()};
  std::optional<double> certified_gap;
  OptimalityStatus optimality_status{OptimalityStatus::FAILED};
  MinSumRefinementPolicy minsum_refinement_policy{
      MinSumRefinementPolicy::HEURISTIC_ADAPTIVE};
  bool exact_solver{false};
  double certified_lower_bound{0.0};
  double heuristic_lower_bound{0.0};
  double gap{0.0};
  int num_iterations{0};
  int num_ip_solves{0};
  bool verified{false};
  VerificationKind verification_kind{VerificationKind::NONE};
  double verification_time_sec{0.0};
  double serialization_time_sec{0.0};
  double total_wall_time_sec{0.0};
  double peak_cost{0.0};
  double integral_cost{0.0};
  std::optional<double> empirical_ratio_to_exact;
  std::optional<double> ratio_to_incumbent;
  std::optional<std::size_t> candidate_count;
  std::optional<std::size_t> coverage_nnz;
  unsigned seed{0};
  double global_time_limit_sec{0.0};
  bool verify_each_iteration{false};
  bool verify_after{true};
  bool handovers_enabled{true};
  bool timeout{false};
  bool failed{false};
  std::string git_commit;
  std::string compiler;
  std::string build_type;
  int thread_count{1};
  std::string experiment_id;
  nlohmann::json configuration;
  bool feasible{false};
  bool time_limited{false};
  std::string solution_json_path;
  std::string trace_csv_path;
  std::string result_json_path;
  std::string error_message;
};

struct BatchRunConfig {
  std::string instances_dir{"data/instances"};
  std::string output_dir{"results/batch"};
  std::vector<std::string> algorithm_names;
  std::vector<ObjectiveType> objectives{ObjectiveType::MIN_MAX,
                                        ObjectiveType::MIN_SUM};
  bool parallel{false};
  int num_threads{[] {
    const auto available = std::thread::hardware_concurrency();
    return static_cast<int>(available == 0U ? 1U : available);
  }()};
  bool verify_after{true};
  bool verify_each_iteration{false};
  bool strict_exact_reference{true};
  bool save_solutions{true};
  bool save_traces{true};
  std::string exact_reference{"auto"};
  std::string selected_backend;
  std::string solver_version{"unknown"};
  bool native_kont{false};
  bool fallback_used{false};
  BenchmarkProfile profile{BenchmarkProfile::FAST};
  unsigned seed{42U};
  int repeats{1};
  double per_ip_time_limit_sec{60.0};
  double fast_time_limit_sec{30.0};
  double exact_time_limit_sec{600.0};
  double gap_target{0.01};
  MinSumRefinementPolicy minsum_refinement_policy{
      MinSumRefinementPolicy::HEURISTIC_ADAPTIVE};
  std::string experiment_id;
  std::string actual_backend;
};

class BatchRunner {
 public:
  static void run(const BatchRunConfig& config, ILPSolver* ilp);

  static void save_master(const std::vector<BatchRunRecord>& records,
                          const std::string& json_path,
                          const std::string& csv_path);

  static void write_summary(const std::vector<BatchRunRecord>& records,
                            const std::string& path);

 private:
  static BatchRunRecord run_single(const Instance& instance,
                                   const std::string& algorithm_name,
                                   ObjectiveType objective, ILPSolver* ilp,
                                   const BatchRunConfig& config,
                                   int repeat = 0);

  static std::string make_run_dir(const std::string& base,
                                  const std::string& instance,
                                  const std::string& algorithm,
                                  const std::string& objective);
};

}  // namespace kdc
