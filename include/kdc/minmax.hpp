#pragma once

#include "kdc/solution.hpp"
#include "kdc/trace.hpp"
#include "kdc/istatic_solver.hpp"
#include "kdc/solver_interface.hpp"

#include <string>
#include <vector>

namespace kdc {
class MinMaxSolver {
 public:
  struct Config {
    double initial_ip_gap{0.01};
    double final_ip_gap{0.0001};
    double time_limit_per_ip{600.0};
    double gap_target{0.01};
    bool use_handovers{true};
    bool use_no_dup{true};
    bool use_partial_ext{true};
    bool verify_after{true};
    std::string trace_csv_path{};
  };

  struct Result {
    KineticSolution solution;
    double peak_cost{0.0};
    double lower_bound{0.0};
    double gap{0.0};
    double total_time_sec{0.0};
    int num_ip_solves{0};
    int num_iterations{0};
    bool verified{false};
    std::vector<double> gap_trace;
    std::vector<IterTrace> trace;
  };

  static Result solve(const Instance& instance, ILPSolver& solver,
                      const Config& config);
  static Result solve(const Instance& instance, IStaticSolver& static_solver,
                      const Config& config);

 private:
  static double find_max_area_time(const KineticSolution& solution);
};
}
