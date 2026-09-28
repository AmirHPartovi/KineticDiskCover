#pragma once

#include "kdc/types.hpp"

#include <limits>
#include <string>
#include <vector>

namespace kdc {
struct StaticSolution {
  std::vector<int> supporting_point;
  std::vector<double> radius;
  double cost{0.0};
  bool feasible{false};
  double lower_bound{0.0};
  double upper_bound{std::numeric_limits<double>::infinity()};
  double solve_time_sec{0.0};
  std::string solver_name;
};

class IStaticSolver {
 public:
  virtual ~IStaticSolver() = default;
  virtual StaticSolution solve(const Instance& instance, double time) = 0;
  virtual std::string name() const = 0;
  virtual bool is_exact() const = 0;
  virtual bool provides_lower_bound() const = 0;
  virtual void set_time_limit(double time_limit_sec) {
    (void)time_limit_sec;
  }
};
}
