#pragma once

#include "kdc/istatic_solver.hpp"
#include "kdc/solver_interface.hpp"

namespace kdc {
class ShiftingStrategySolver final : public IStaticSolver {
 public:
  struct Config {
    int l{8};
    bool use_ip_for_cells{false};
    double cell_ip_gap{0.01};
    double cell_ip_time_limit_sec{10.0};
  };

  explicit ShiftingStrategySolver(ILPSolver* ilp);
  ShiftingStrategySolver(ILPSolver* ilp, Config config);
  StaticSolution solve(const Instance& instance, double time) override;
  std::string name() const override { return "shifting"; }
  bool is_exact() const override { return false; }
  bool provides_lower_bound() const override { return true; }
  void set_time_limit(double time_limit_sec) override;

 private:
  ILPSolver* ilp_{nullptr};
  Config config_;
};
}
