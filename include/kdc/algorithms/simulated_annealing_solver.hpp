#pragma once

#include "kdc/istatic_solver.hpp"

namespace kdc {
class SimulatedAnnealingSolver final : public IStaticSolver {
 public:
  struct Config {
    double T_init{10.0};
    double T_min{1e-4};
    double alpha{0.995};
    int iters_per_temp{100};
    int max_outer_iters{1000};
    unsigned seed{42U};
  };

  SimulatedAnnealingSolver();
  explicit SimulatedAnnealingSolver(Config config);
  StaticSolution solve(const Instance& instance, double time) override;
  std::string name() const override { return "sa"; }
  bool is_exact() const override { return false; }
  bool provides_lower_bound() const override { return true; }

 private:
  Config config_;
};
}
