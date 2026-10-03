#pragma once

#include "kdc/solver_budget.hpp"
#include "kdc/result_status.hpp"
#include "kdc/stationary.hpp"
#include "kdc/types.hpp"

#include <limits>
#include <optional>
#include <algorithm>
#include <cmath>
#include <string>
#include <stdexcept>
#include <vector>

namespace kdc {
struct StaticSolution {
  std::vector<int> supporting_point;
  std::vector<double> radius;
  double cost{0.0};
  bool feasible{false};
  double lower_bound{0.0};
  BoundStatus bound_status{BoundStatus::NONE};
  double upper_bound{std::numeric_limits<double>::infinity()};
  std::optional<double> certified_gap;
  OptimalityStatus optimality_status{OptimalityStatus::FAILED};
  bool exact_solver{false};
  double solve_time_sec{0.0};
  std::string solver_name;
  bool time_limited{false};
  std::vector<int> assigned_points;
};

inline void set_static_result_status(StaticSolution& solution,
                                     BoundStatus bound_status,
                                     OptimalityStatus optimality_status,
                                     bool exact_solver) {
  if (bound_status == BoundStatus::CERTIFIED &&
      (!std::isfinite(solution.lower_bound) || solution.lower_bound < 0.0)) {
    bound_status = BoundStatus::NONE;
  }
  solution.bound_status = bound_status;
  solution.exact_solver = exact_solver;
  if (optimality_status == OptimalityStatus::TIME_LIMIT) {
    solution.time_limited = true;
  }
  if (solution.time_limited ||
      optimality_status == OptimalityStatus::TIME_LIMIT) {
    solution.optimality_status = OptimalityStatus::TIME_LIMIT;
  } else if (!solution.feasible) {
    solution.optimality_status =
        optimality_status == OptimalityStatus::INFEASIBLE
            ? OptimalityStatus::INFEASIBLE
            : OptimalityStatus::FAILED;
  } else if (!exact_solver &&
             optimality_status == OptimalityStatus::OPTIMAL) {
    solution.optimality_status = OptimalityStatus::FEASIBLE;
  } else {
    solution.optimality_status = optimality_status;
  }
  if (!solution.feasible) {
    solution.upper_bound = std::numeric_limits<double>::infinity();
    solution.certified_gap.reset();
    return;
  }
  solution.upper_bound = solution.cost;
  if (bound_status == BoundStatus::CERTIFIED &&
      solution.lower_bound >
          solution.upper_bound +
              1e-9 * std::max(1.0, std::abs(solution.upper_bound))) {
    solution.bound_status = BoundStatus::HEURISTIC;
    bound_status = BoundStatus::HEURISTIC;
  }
  if (exact_solver && bound_status == BoundStatus::CERTIFIED) {
    solution.certified_gap =
        std::max(0.0, solution.upper_bound - solution.lower_bound) /
        std::max(1.0, std::abs(solution.lower_bound));
  } else {
    solution.certified_gap.reset();
  }
  if (solution.optimality_status == OptimalityStatus::OPTIMAL &&
      (!solution.certified_gap.has_value() ||
       *solution.certified_gap > 1e-12)) {
    solution.optimality_status = OptimalityStatus::FEASIBLE;
  } else if (solution.optimality_status == OptimalityStatus::FEASIBLE &&
             exact_solver && solution.certified_gap.has_value() &&
             *solution.certified_gap <= 1e-12) {
    solution.optimality_status = OptimalityStatus::OPTIMAL;
  }
}

class IStaticSolver {
 public:
  virtual ~IStaticSolver() = default;
  virtual StaticSolution solve(const Instance& instance, double time) = 0;
  StaticSolution solve_with_budget(const Instance& instance, double time,
                                   SolverBudget& budget,
                                   double static_time_limit_sec) {
    budget.checkpoint();
    const double allowed =
        budget.limit_seconds(static_time_limit_sec);
    if (allowed <= 0.0) {
      throw SolverBudgetExpired();
    }
    set_time_limit(allowed);
    SolverBudget* previous = active_budget_;
    active_budget_ = &budget;
    try {
      StaticSolution solution = solve(instance, time);
      active_budget_ = previous;
      if (budget.expired()) {
        throw SolverBudgetExpired();
      }
      if (solution.feasible && solution.assigned_points.empty()) {
        const StaticAssignment assignment =
            StationarySolver::assign_points_to_disks(
                instance, time, solution.supporting_point, solution.radius,
                &budget);
        if (!assignment.feasible) {
          throw std::runtime_error(
              "static solver returned a feasible but uncovered assignment");
        }
        solution.supporting_point = assignment.supporting_point;
        solution.radius = assignment.radius;
        solution.cost = assignment.cost;
        solution.assigned_points = assignment.assigned_points;
        if (solution.exact_solver &&
            solution.bound_status == BoundStatus::CERTIFIED &&
            solution.lower_bound >
                solution.cost +
                    1e-9 * std::max(1.0, std::abs(solution.cost))) {
          throw std::runtime_error(
              "ownership normalization contradicts the certified lower bound");
        }
        set_static_result_status(solution, solution.bound_status,
                                 solution.optimality_status,
                                 solution.exact_solver);
      }
      budget.checkpoint();
      return solution;
    } catch (...) {
      active_budget_ = previous;
      throw;
    }
  }
  virtual std::string name() const = 0;
  virtual bool is_exact() const = 0;
  virtual bool provides_lower_bound() const = 0;
  virtual void set_time_limit(double time_limit_sec) {
    (void)time_limit_sec;
  }
  virtual void set_seed(unsigned seed) { (void)seed; }

 protected:
  SolverBudget* active_budget() const noexcept { return active_budget_; }
  void check_budget() const {
    if (active_budget_ != nullptr) {
      active_budget_->checkpoint();
    }
  }
  double effective_time_limit(double local_limit) const noexcept {
    return active_budget_ == nullptr
               ? local_limit
               : active_budget_->limit_seconds(local_limit);
  }

 private:
  SolverBudget* active_budget_{nullptr};
};
}
