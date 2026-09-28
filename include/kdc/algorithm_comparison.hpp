#pragma once

#include "kdc/istatic_solver.hpp"
#include "kdc/solver_interface.hpp"

#include <string>
#include <vector>

namespace kdc {
struct AlgorithmComparisonConfig {
  std::vector<std::string> algorithm_names;
  std::vector<std::string> instance_paths;
  std::string output_dir{"results/comparison"};
  std::string reference_algorithm{"ip-kont"};
  int num_repeats{1};
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
  int n{0};
  int m{0};
  double objective_value{0.0};
  double lower_bound{0.0};
  double gap{0.0};
  double wall_time_sec{0.0};
  int num_iterations{0};
  bool verified{false};
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
