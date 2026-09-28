#pragma once

#include "kdc/istatic_solver.hpp"
#include "kdc/solver_interface.hpp"

namespace kdc {
class LPRoundingSolver final : public IStaticSolver {
 public:
  struct Config {
    int num_trials{50};
    unsigned seed{42U};
    double lp_time_limit_sec{30.0};
  };

  explicit LPRoundingSolver(ILPSolver* ilp);
  LPRoundingSolver(ILPSolver* ilp, Config config);
  StaticSolution solve(const Instance& instance, double time) override;
  std::string name() const override { return "lp-rounding"; }
  bool is_exact() const override { return false; }
  bool provides_lower_bound() const override { return true; }
  void set_time_limit(double time_limit_sec) override;

 private:
  ILPSolver* ilp_{nullptr};
  Config config_;
};
}
