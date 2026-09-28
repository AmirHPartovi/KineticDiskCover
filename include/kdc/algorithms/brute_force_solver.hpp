#pragma once

#include "kdc/istatic_solver.hpp"

#include <cstdint>
#include <vector>

namespace kdc {
class BruteForceSolver final : public IStaticSolver {
 public:
  struct Config {
    std::uint64_t max_assignments{100'000'000ULL};
    double time_limit_sec{60.0};
  };

  BruteForceSolver();
  explicit BruteForceSolver(Config config);
  StaticSolution solve(const Instance& instance, double time) override;
  std::string name() const override { return "brute-force"; }
  bool is_exact() const override { return true; }
  bool provides_lower_bound() const override { return true; }
  void set_time_limit(double time_limit_sec) override;

 private:
  Config config_;
  void decode_assignment(std::uint64_t code, int station_count,
                         int point_count, std::vector<int>& output) const;
  double evaluate_assignment(
      const std::vector<int>& assignment,
      const std::vector<std::vector<double>>& distances,
      int station_count) const;
};
}
