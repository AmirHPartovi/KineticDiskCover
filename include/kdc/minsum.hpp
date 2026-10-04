#pragma once

#include "kdc/kinetic.hpp"
#include "kdc/solution.hpp"
#include "kdc/result_status.hpp"
#include "kdc/trace.hpp"
#include "kdc/istatic_solver.hpp"
#include "kdc/solver_interface.hpp"
#include "kdc/solver_budget.hpp"
#include "kdc/verify.hpp"

#include <utility>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace kdc {
enum class MinSumRefinementPolicy {
  CERTIFIED_BOUND,
  HEURISTIC_ADAPTIVE
};

inline std::string minsum_refinement_policy_to_string(
    MinSumRefinementPolicy policy) {
  switch (policy) {
    case MinSumRefinementPolicy::CERTIFIED_BOUND:
      return "CERTIFIED_BOUND";
    case MinSumRefinementPolicy::HEURISTIC_ADAPTIVE:
      return "HEURISTIC_ADAPTIVE";
  }
  throw std::invalid_argument("unknown MinSum refinement policy");
}

inline MinSumRefinementPolicy minsum_refinement_policy_from_string(
    const std::string& value) {
  if (value == "CERTIFIED_BOUND") {
    return MinSumRefinementPolicy::CERTIFIED_BOUND;
  }
  if (value == "HEURISTIC_ADAPTIVE") {
    return MinSumRefinementPolicy::HEURISTIC_ADAPTIVE;
  }
  throw std::invalid_argument("unknown MinSum refinement policy: " + value);
}

// Apply integral-improving support handovers within existing intervals.
// Requires a non-empty, well-formed solution whose time range is covered by
// the instance; throws std::invalid_argument when those preconditions fail.
// The result remains well-formed and its total integral cannot increase.
void apply_integral_handovers(const Instance& instance,
                              KineticSolution& solution,
                              SolverBudget* budget = nullptr);

class MinSumSolver {
 public:
  struct Config {
    double initial_ip_gap{0.01};
    double final_ip_gap{0.0001};
    double time_limit_per_ip{600.0};
    double gap_target{0.01};
    double global_time_limit_sec{0.0};  // Zero selects 30s fast / 600s exact.
    MinSumRefinementPolicy refinement_policy{
        MinSumRefinementPolicy::HEURISTIC_ADAPTIVE};
    int lb_num_samples{4};
    int max_iterations{64};
    int stagnation_patience{3};
    double improvement_tolerance{1e-6};
    int verify_every_n_iters{0};
    bool verify_each_iteration{false};
    bool use_handovers{true};
    bool use_no_dup{true};
    bool use_partial_ext{true};
    bool verify_after{true};
  };

  struct Result {
    KineticSolution solution;
    double total_integral{0.0};
    double lower_bound{0.0};
    double lower_bound_integral{0.0};
    MinSumRefinementPolicy refinement_policy{
        MinSumRefinementPolicy::HEURISTIC_ADAPTIVE};
    BoundStatus bound_status{BoundStatus::NONE};
    double upper_bound{std::numeric_limits<double>::infinity()};
    std::optional<double> certified_gap;
    OptimalityStatus optimality_status{OptimalityStatus::FAILED};
    bool exact_solver{false};
    double certified_lower_bound_integral{0.0};
    double heuristic_lower_bound_integral{0.0};
    double gap{0.0};
    double total_time_sec{0.0};
    int num_ip_solves{0};
    int num_iterations{0};
    bool verified{false};
    VerificationKind verification_kind{VerificationKind::NONE};
    double verification_time_sec{0.0};
    bool feasible{false};
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
  static std::pair<double, double> find_max_contribution_interval(
      const KineticSolution& solution,
      const std::vector<std::pair<double, double>>& lower_bound_samples);
  static double compute_integral_lower_bound(
      const Instance& instance, IStaticSolver& static_solver, int num_samples,
      const Config& config,
      std::vector<std::pair<double, double>>& output_samples,
      SolverBudget* budget, bool* time_limited);
  static KineticSolution combine_integral(const KineticSolution& first,
                                          const KineticSolution& second);
  static void local_improvement_integral(const Instance& instance,
                                         KineticSolution& solution,
                                         SolverBudget* budget);
};
}
