#pragma once

#include "kdc/minmax.hpp"
#include "kdc/minsum.hpp"
#include "kdc/result_status.hpp"
#include "kdc/solver_budget.hpp"

#include <limits>
#include <optional>
#include <string>

namespace kdc {

enum class MinMaxSumComponentStatus {
  NOT_RUN,
  COMPLETED,
  TIME_LIMIT,
  FAILED
};

inline const char* to_string(MinMaxSumComponentStatus status) {
  switch (status) {
    case MinMaxSumComponentStatus::NOT_RUN:
      return "NOT_RUN";
    case MinMaxSumComponentStatus::COMPLETED:
      return "COMPLETED";
    case MinMaxSumComponentStatus::TIME_LIMIT:
      return "TIME_LIMIT";
    case MinMaxSumComponentStatus::FAILED:
      return "FAILED";
  }
  throw std::invalid_argument("unknown MinMaxSum component status");
}

class MinMaxSumSolver {
 public:
  struct Config {
    MinMaxSolver::Config minmax_config;
    MinSumSolver::Config minsum_config;
    double global_time_limit_sec{600.0};
    double minmax_budget_fraction{0.5};
    double minsum_budget_fraction{0.5};
    double absolute_tolerance{1e-8};
    double relative_tolerance{1e-9};
    unsigned seed{42U};
    std::string minmax_source_run;
    std::string minsum_source_run;
  };

  struct Result {
    KineticSolution solution;
    double peak_cost{std::numeric_limits<double>::quiet_NaN()};
    double integral_cost{std::numeric_limits<double>::quiet_NaN()};
    std::optional<double> minmax_component_peak;
    std::optional<double> minmax_component_integral;
    std::optional<double> minsum_component_peak;
    std::optional<double> minsum_component_integral;
    bool feasible{false};
    bool verified{false};
    bool coverage_ok{false};
    bool supporting_points_ok{false};
    bool assignment_ok{false};
    bool cost_consistent{false};
    bool peak_consistent{false};
    bool integral_consistent{false};
    bool dominates_minmax{false};
    bool dominates_minsum{false};
    bool dominance_invariants_ok{false};
    MinMaxSumComponentStatus minmax_component_status{
        MinMaxSumComponentStatus::NOT_RUN};
    MinMaxSumComponentStatus minsum_component_status{
        MinMaxSumComponentStatus::NOT_RUN};
    OptimalityStatus minmax_component_optimality{OptimalityStatus::FAILED};
    OptimalityStatus minsum_component_optimality{OptimalityStatus::FAILED};
    OptimalityStatus joint_optimality_status{OptimalityStatus::FAILED};
    std::optional<double> minmax_component_certified_gap;
    std::optional<double> minsum_component_certified_gap;
    std::string minmax_source_run;
    std::string minsum_source_run;
    std::string minmax_error;
    std::string minsum_error;
    unsigned seed{0U};
    double minmax_time_sec{0.0};
    double minsum_time_sec{0.0};
    double total_time_sec{0.0};
    double verification_time_sec{0.0};
    bool time_limited{false};
  };

  static Result solve(const Instance& instance, IStaticSolver& static_solver,
                      const Config& config);
  static Result solve(const Instance& instance, IStaticSolver& static_solver,
                      const Config& config, SolverBudget& budget);
};

}  // namespace kdc
