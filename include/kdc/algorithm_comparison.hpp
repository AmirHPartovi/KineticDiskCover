#pragma once

#include "kdc/istatic_solver.hpp"
#include "kdc/solver_interface.hpp"
#include "kdc/benchmark_protocol.hpp"
#include <nlohmann/json.hpp>

#include <optional>
#include <string>
#include <vector>

namespace kdc {
struct AlgorithmComparisonConfig {
  std::vector<std::string> algorithm_names;
  std::vector<std::string> instance_paths;
  std::string output_dir{"results/comparison"};
  std::string reference_algorithm{"ip-kont"};
  std::string exact_reference{"auto"};
  BenchmarkProfile profile{BenchmarkProfile::FAST};
  unsigned seed{42U};
  int num_repeats{1};
  double fast_time_limit_sec{30.0};
  double exact_time_limit_sec{600.0};
  double per_static_time_limit_sec{60.0};
  bool verify_solutions{true};
  double reference_tol{1e-6};
  std::vector<double> time_points{0.0, 0.1, 0.2, 0.3, 0.4, 0.5,
                                  0.6, 0.7, 0.8, 0.9, 1.0};
};

struct StaticComparisonResult {
  std::string instance_name;
  std::string algorithm_name;
  int n{0};
  int m{0};
  double t{0.0};
  double cost{0.0};
  double lower_bound{0.0};
  double upper_bound{0.0};
  double gap{0.0};
  double wall_time_sec{0.0};
  bool feasible{false};
  bool verified{false};
  bool matches_reference{false};
  double ratio_to_reference{1.0};
};

struct KineticComparisonResult {
  std::string instance_name;
  std::string algorithm_name;
  std::string objective;
  std::string algorithm_category;
  std::string requested_backend;
  std::string actual_backend;
  int n{0};
  int m{0};
  double objective_value{0.0};
  double peak_cost{0.0};
  double integral_cost{0.0};
  double lower_bound{0.0};
  double upper_bound{0.0};
  BoundStatus bound_status{BoundStatus::NONE};
  OptimalityStatus optimality_status{OptimalityStatus::FAILED};
  std::optional<double> certified_gap;
  std::optional<double> empirical_ratio_to_exact;
  std::optional<double> ratio_to_incumbent;
  double gap{0.0};
  double wall_time_sec{0.0};
  double solve_time_sec{0.0};
  double total_wall_time_sec{0.0};
  double cpu_time_sec{0.0};
  int num_iterations{0};
  int num_static_solves{0};
  unsigned seed{0};
  int repeat{0};
  double global_time_limit_sec{0.0};
  double per_static_time_limit_sec{0.0};
  std::string refinement_policy{"HEURISTIC_ADAPTIVE"};
  bool feasible{false};
  bool verified{false};
  double serialization_time_sec{0.0};
  bool timeout{false};
  bool failed{false};
  std::string error_message;
  VerificationKind verification_kind{VerificationKind::NONE};
  double verification_time_sec{0.0};
  bool verify_each_iteration{false};
  bool verify_after{true};
  bool handovers_enabled{true};
  std::string experiment_id;
  nlohmann::json configuration;
};

class AlgorithmComparator {
 public:
  static void compare_static(const AlgorithmComparisonConfig& config,
                             ILPSolver* ilp,
                             std::vector<StaticComparisonResult>& output);
  static void compare_kinetic(const AlgorithmComparisonConfig& config,
                              ILPSolver* ilp,
                              const std::string& objective,
                              std::vector<KineticComparisonResult>& output);
  static void save_results(const std::vector<StaticComparisonResult>& results,
                           const std::string& json_path,
                           const std::string& csv_path);
  static void save_results(
      const std::vector<KineticComparisonResult>& results,
      const std::string& json_path, const std::string& csv_path);
  static bool verify_static_solution(const Instance& instance, double time,
                                     const StaticSolution& solution,
                                     double tolerance = 1e-6);
};
}
