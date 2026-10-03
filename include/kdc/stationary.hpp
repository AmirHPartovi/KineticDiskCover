#pragma once

#include "kdc/solver_interface.hpp"
#include "kdc/solver_budget.hpp"
#include "kdc/types.hpp"

#include <vector>

namespace kdc {
struct StaticAssignment {
  std::vector<int> supporting_point;
  std::vector<double> radius;
  double cost{0.0};
  bool feasible{false};
  std::vector<int> assigned_points;
};

class StationarySolver {
 public:
  static StaticAssignment assign_points_to_disks(
      const Instance& instance, double time,
      const std::vector<int>& supporting_points,
      const std::vector<double>& radii, SolverBudget* budget = nullptr);
  static StaticAssignment solve_nn(const Instance& instance, double time,
                                   SolverBudget* budget = nullptr);
  static StaticAssignment solve_ip(const Instance& instance, double time,
                                   ILPSolver& solver,
                                   double time_limit_sec, double gap_target,
                                   double* out_lower_bound = nullptr,
                                   ILPResult::Status* out_status = nullptr,
                                   SolverBudget* budget = nullptr);
};
}
