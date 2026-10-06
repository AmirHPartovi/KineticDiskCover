#pragma once

#include "kdc/solution.hpp"
#include "kdc/result_status.hpp"
#include "kdc/trace.hpp"
#include "kdc/verify.hpp"
#include "kdc/istatic_solver.hpp"
#include "kdc/solver_interface.hpp"

#include <string>
#include <limits>
#include <optional>
#include <vector>

namespace kdc {
class MinMaxSolver {
 public:
  struct Config {
    double initial_ip_gap{0.01};
    double final_ip_gap{0.0001};
    double time_limit_per_ip{600.0};
    double gap_target{0.01};
    double global_time_limit_sec{0.0};  // Zero selects 30s fast / 600s exact.
    int max_iterations{64};
    int verify_every_n_iters{0};
    bool verify_each_iteration{false};
    bool use_handovers{true};
    bool use_no_dup{true};
    bool use_partial_ext{true};
    bool verify_after{true};
    std::string trace_csv_path{};
  };

  struct Result {
    KineticSolution solution;
    double peak_cost{0.0};
    double peak_time{0.0};
    double initial_peak_cost{0.0};
    double lower_bound{0.0};
    BoundStatus bound_status{BoundStatus::NONE};
    double upper_bound{std::numeric_limits<double>::infinity()};
    std::optional<double> certified_gap;
    OptimalityStatus optimality_status{OptimalityStatus::FAILED};
    bool exact_solver{false};
    double certified_lower_bound{0.0};
    double heuristic_lower_bound{0.0};
    double heuristic_gap{0.0};
    double gap{0.0};
    double total_time_sec{0.0};
    int num_ip_solves{0};
    int num_iterations{0};
    bool verified{false};
    VerificationKind verification_kind{VerificationKind::NONE};
    double verification_time_sec{0.0};
    bool feasible{false};
    bool peak_consistent{false};
    bool time_limited{false};
    std::vector<double> gap_trace;
    std::vector<IterTrace> trace;
  };

  static Result solve(const Instance& instance, ILPSolver& solver,
                      const Config& config);
  static Result solve(const Instance& instance, ILPSolver& solver,
                      const Config& config, SolverBudget& budget);
  static Result solve(const Instance& instance, IStaticSolver& static_solver,
                      const Config& config);
  static Result solve(const Instance& instance, IStaticSolver& static_solver,
                      const Config& config, SolverBudget& budget);

 private:
  static double find_peak_time(const KineticSolution& solution);
};
}
