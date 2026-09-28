#pragma once

#include "kdc/kinetic.hpp"
#include "kdc/solution.hpp"
#include "kdc/trace.hpp"
#include "kdc/istatic_solver.hpp"
#include "kdc/solver_interface.hpp"

#include <utility>
#include <vector>

namespace kdc {
// Apply integral-improving support handovers within existing intervals.
// Requires a non-empty, well-formed solution whose time range is covered by
// the instance; throws std::invalid_argument when those preconditions fail.
// The result remains well-formed and its total integral cannot increase.
void apply_integral_handovers(const Instance& instance,
                              KineticSolution& solution);

class MinSumSolver {
 public:
  struct Config {
    double initial_ip_gap{0.01};
    double final_ip_gap{0.0001};
    double time_limit_per_ip{600.0};
    double gap_target{0.01};
    int lb_num_samples{20};
    bool use_handovers{true};
    bool use_no_dup{true};
    bool use_partial_ext{true};
    bool verify_after{true};
  };

  struct Result {
    KineticSolution solution;
    double total_integral{0.0};
    double lower_bound_integral{0.0};
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
  static std::pair<double, double> find_max_contribution_interval(
      const KineticSolution& solution,
      const std::vector<std::pair<double, double>>& lower_bound_samples);
  static double compute_integral_lower_bound(
      const Instance& instance, IStaticSolver& static_solver, int num_samples,
      const Config& config,
      std::vector<std::pair<double, double>>& output_samples);
  static KineticSolution combine_integral(const KineticSolution& first,
                                          const KineticSolution& second);
  static void local_improvement_integral(const Instance& instance,
                                         KineticSolution& solution);
};
}
