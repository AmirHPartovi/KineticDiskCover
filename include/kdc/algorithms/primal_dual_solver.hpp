#pragma once

#include "kdc/istatic_solver.hpp"

namespace kdc {
class PrimalDualSolver final : public IStaticSolver {
 public:
  struct Config {
    double epsilon{1e-9};
    bool use_reverse_delete{true};
  };

  PrimalDualSolver();
  explicit PrimalDualSolver(Config config);
  StaticSolution solve(const Instance& instance, double time) override;
  std::string name() const override { return "primal-dual"; }
  bool is_exact() const override { return false; }
  bool provides_lower_bound() const override { return true; }

 private:
  Config config_;
};
}
