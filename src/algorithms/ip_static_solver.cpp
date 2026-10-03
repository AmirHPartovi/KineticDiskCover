#include "kdc/algorithms/ip_static_solver.hpp"

#include "kdc/logging.hpp"
#include "kdc/stationary.hpp"

#include <chrono>
#include <cmath>
#include <stdexcept>

namespace kdc {
IPStaticSolver::IPStaticSolver(ILPSolver* ilp)
    : IPStaticSolver(ilp, Config{}) {}

IPStaticSolver::IPStaticSolver(ILPSolver* ilp, Config config)
    : ilp_(ilp), config_(config) {
  if (!std::isfinite(config_.time_limit_sec) ||
      config_.time_limit_sec <= 0.0 || !std::isfinite(config_.gap_target) ||
      config_.gap_target < 0.0) {
    throw std::invalid_argument("IP static solver configuration is invalid");
  }
}

void IPStaticSolver::set_time_limit(double time_limit_sec) {
  if (!std::isfinite(time_limit_sec) || time_limit_sec <= 0.0) {
    throw std::invalid_argument("IP static solver time limit must be positive");
  }
  config_.time_limit_sec = time_limit_sec;
}

StaticSolution IPStaticSolver::solve(const Instance& instance, double time) {
  if (ilp_ == nullptr) {
    throw std::invalid_argument("ilp_ is null");
  }
  const auto start = std::chrono::steady_clock::now();
  check_budget();
  double lower_bound = 0.0;
  ILPResult::Status status = ILPResult::Status::ERROR;
  const StaticAssignment assignment = StationarySolver::solve_ip(
      instance, time, *ilp_, effective_time_limit(config_.time_limit_sec),
      config_.gap_target, &lower_bound, &status, active_budget());
  check_budget();
  StaticSolution solution;
  solution.supporting_point = assignment.supporting_point;
  solution.radius = assignment.radius;
  solution.assigned_points = assignment.assigned_points;
  solution.cost = assignment.cost;
  solution.feasible = assignment.feasible;
  solution.lower_bound =
      status == ILPResult::Status::INFEASIBLE ? 0.0 : lower_bound;
  solution.upper_bound = assignment.cost;
  solution.time_limited = status == ILPResult::Status::TIME_LIMIT;
  BoundStatus bound_status = BoundStatus::NONE;
  if (status == ILPResult::Status::OPTIMAL ||
      status == ILPResult::Status::FEASIBLE ||
      status == ILPResult::Status::TIME_LIMIT) {
    bound_status = std::isfinite(lower_bound) ? BoundStatus::CERTIFIED
                                              : BoundStatus::NONE;
  }
  OptimalityStatus optimality_status = OptimalityStatus::FAILED;
  switch (status) {
    case ILPResult::Status::OPTIMAL:
      optimality_status = OptimalityStatus::OPTIMAL;
      break;
    case ILPResult::Status::FEASIBLE:
      optimality_status = OptimalityStatus::FEASIBLE;
      break;
    case ILPResult::Status::TIME_LIMIT:
      optimality_status = OptimalityStatus::TIME_LIMIT;
      break;
    case ILPResult::Status::INFEASIBLE:
      optimality_status = OptimalityStatus::INFEASIBLE;
      break;
    case ILPResult::Status::UNBOUNDED:
    case ILPResult::Status::ERROR:
      optimality_status = OptimalityStatus::FAILED;
      break;
  }
  set_static_result_status(solution, bound_status, optimality_status, true);
  solution.solve_time_sec =
      std::chrono::duration<double>(std::chrono::steady_clock::now() - start)
          .count();
  solution.solver_name = name();
  return solution;
}
}
