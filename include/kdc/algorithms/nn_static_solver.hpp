#pragma once

#include "kdc/istatic_solver.hpp"

namespace kdc {
class NNStaticSolver final : public IStaticSolver {
 public:
  StaticSolution solve(const Instance& instance, double time) override;
  std::string name() const override { return "nn"; }
  bool is_exact() const override { return false; }
  bool provides_lower_bound() const override { return false; }
};
}
