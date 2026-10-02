#pragma once

#include "kdc/minmax.hpp"
#include "kdc/minsum.hpp"
#include "kdc/objective.hpp"
#include "kdc/solver_interface.hpp"
#include "kdc/types.hpp"

#include <string>
#include <vector>

namespace kdc {
struct BenchmarkConfig {
  std::string dataset_dir{"data"};
  std::string output_dir{"results"};
  std::vector<std::string> instance_names;
  ObjectiveType objective{ObjectiveType::MIN_MAX};
  int num_repeats{1};
  bool both_objectives{false};
  std::string exact_reference{"auto"};
  bool measure_memory{true};
  bool verify_after{true};
  bool parallel{false};
  int num_threads{1};
  MinMaxSolver::Config minmax_cfg;
  MinSumSolver::Config minsum_cfg;
  std::string solver_name{"KONT"};
};

struct BenchmarkResult {
  std::string instance_name;
  int n{0};
  int m{0};
  ObjectiveType objective{ObjectiveType::MIN_MAX};
  double wall_time_sec{0.0};
  double cpu_time_sec{0.0};
  double ip_time_sec{0.0};
  double peak_memory_mb{0.0};
  double objective_value{0.0};
  double lower_bound{0.0};
  double gap{0.0};
  int num_ip_solves{0};
  int num_iterations{0};
  bool verified{false};
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
