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
  double lower_bound = 0.0;
  ILPResult::Status status = ILPResult::Status::ERROR;
  const StaticAssignment assignment = StationarySolver::solve_ip(
      instance, time, *ilp_, config_.time_limit_sec, config_.gap_target,
      &lower_bound, &status);
  StaticSolution solution;
  solution.supporting_point = assignment.supporting_point;
  solution.radius = assignment.radius;
  solution.cost = assignment.cost;
  solution.feasible = assignment.feasible;
  solution.lower_bound =
      status == ILPResult::Status::INFEASIBLE ? 0.0 : lower_bound;
  solution.upper_bound = assignment.cost;
  solution.solve_time_sec =
      std::chrono::duration<double>(std::chrono::steady_clock::now() - start)
          .count();
  solution.solver_name = name();
  return solution;
}
}
