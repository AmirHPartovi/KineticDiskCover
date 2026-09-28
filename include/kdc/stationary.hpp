#pragma once

#include "kdc/solver_interface.hpp"
#include "kdc/types.hpp"

#include <vector>

namespace kdc {
struct StaticAssignment {
  std::vector<int> supporting_point;
  std::vector<double> radius;
  double cost{0.0};
  bool feasible{false};
};

class StationarySolver {
 public:
  static StaticAssignment solve_nn(const Instance& instance, double time);
  static StaticAssignment solve_ip(const Instance& instance, double time,
                                   ILPSolver& solver,
                                   double time_limit_sec, double gap_target,
                                   double* out_lower_bound = nullptr,
                                   ILPResult::Status* out_status = nullptr);
};
}
