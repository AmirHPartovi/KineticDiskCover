#include "kdc/algorithms/nn_static_solver.hpp"

#include "kdc/logging.hpp"
#include "kdc/stationary.hpp"

#include <chrono>

namespace kdc {
StaticSolution NNStaticSolver::solve(const Instance& instance, double time) {
  LOG_DEBUG("NNStaticSolver::solve t={:.6f} n={} m={}", time, instance.n,
            instance.m);
  const auto start = std::chrono::steady_clock::now();
  const StaticAssignment assignment =
      StationarySolver::solve_nn(instance, time, active_budget());
  StaticSolution solution;
  solution.supporting_point = assignment.supporting_point;
  solution.radius = assignment.radius;
  solution.cost = assignment.cost;
  solution.feasible = assignment.feasible;
  solution.lower_bound = 0.0;
  solution.upper_bound = assignment.cost;
  set_static_result_status(solution, BoundStatus::CERTIFIED,
                           assignment.feasible ? OptimalityStatus::FEASIBLE
                                               : OptimalityStatus::FAILED,
                           false);
  solution.solve_time_sec =
      std::chrono::duration<double>(std::chrono::steady_clock::now() - start)
          .count();
  solution.solver_name = name();
  return solution;
}
}
