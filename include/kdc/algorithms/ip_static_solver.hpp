#pragma once

#include "kdc/istatic_solver.hpp"
#include "kdc/solver_interface.hpp"

namespace kdc {
class IPStaticSolver final : public IStaticSolver {
 public:
  struct Config {
    double time_limit_sec{600.0};
    double gap_target{0.0001};
  };

  explicit IPStaticSolver(ILPSolver* ilp);
  IPStaticSolver(ILPSolver* ilp, Config config);
  StaticSolution solve(const Instance& instance, double time) override;
  std::string name() const override { return "ip-kont"; }
  bool is_exact() const override { return true; }
  bool provides_lower_bound() const override { return true; }
  void set_time_limit(double time_limit_sec) override;

 private:
  ILPSolver* ilp_{nullptr};
  Config config_;
};
}
