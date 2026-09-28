#pragma once

#include "kdc/istatic_solver.hpp"
#include "kdc/objective.hpp"
#include "kdc/solver_interface.hpp"
#include "kdc/types.hpp"

#include <string>
#include <vector>

namespace kdc {

struct BatchRunRecord {
  std::string instance_name;
  std::string algorithm_name;
  std::string objective;
  int n{0};
  int m{0};
  double wall_time_sec{0.0};
  double cpu_time_sec{0.0};
  double peak_memory_mb{0.0};
  double time_limit_per_ip_sec{0.0};
  double objective_value{0.0};
  double lower_bound{0.0};
  double gap{0.0};
  int num_iterations{0};
  int num_ip_solves{0};
  bool verified{false};
  bool feasible{false};
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
  int num_threads{1};
  bool verify_after{true};
  bool save_solutions{true};
  bool save_traces{true};
  double per_ip_time_limit_sec{10.0};
  double gap_target{0.01};
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
                                   const BatchRunConfig& config);

  static std::string make_run_dir(const std::string& base,
                                  const std::string& instance,
                                  const std::string& algorithm,
                                  const std::string& objective);
};

}  // namespace kdc
