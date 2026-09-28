#pragma once

#include "kdc/istatic_solver.hpp"

namespace kdc {
class GreedySetCoverSolver final : public IStaticSolver {
 public:
  enum class TieBreak { DENSITY, MAX_NEW_POINTS, MIN_COST };
  struct Config {
    TieBreak tie_break{TieBreak::DENSITY};
  };

  GreedySetCoverSolver();
  explicit GreedySetCoverSolver(Config config);
  StaticSolution solve(const Instance& instance, double time) override;
  std::string name() const override { return "greedy"; }
  bool is_exact() const override { return false; }
  bool provides_lower_bound() const override { return true; }

 private:
  Config config_;
};
}
