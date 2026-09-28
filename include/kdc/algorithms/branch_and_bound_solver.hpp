#pragma once

#include "kdc/istatic_solver.hpp"
#include "kdc/solver_interface.hpp"

#include <cstdint>
#include <queue>
#include <vector>

namespace kdc {
class BranchAndBoundSolver final : public IStaticSolver {
 public:
  struct Config {
    double time_limit_sec{600.0};
    double gap_target{0.001};
    std::uint64_t node_limit{10'000'000ULL};
    bool use_best_first{true};
    bool use_lp_lower_bound{true};
    bool use_presolve{true};
  };

  explicit BranchAndBoundSolver(ILPSolver* ilp);
  BranchAndBoundSolver(ILPSolver* ilp, Config config);
  StaticSolution solve(const Instance& instance, double time) override;
  std::string name() const override { return "branch-and-bound"; }
  bool is_exact() const override { return true; }
  bool provides_lower_bound() const override { return true; }
  void set_time_limit(double time_limit_sec) override;

 private:
  struct BBNode {
    std::vector<int> assignment;
    std::vector<int> remaining;
    std::vector<double> current_radius;
    double partial_cost{0.0};
    double lower_bound{0.0};
    int depth{0};
  };

  struct NodeCompare {
    bool operator()(const BBNode& lhs, const BBNode& rhs) const {
      return lhs.lower_bound > rhs.lower_bound;
    }
  };

  int select_branching_point(
      const BBNode& node,
      const std::vector<std::vector<double>>& distances) const;
  double compute_lower_bound(
      const BBNode& node,
      const std::vector<std::vector<double>>& distances, int station_count,
      int point_count, double time, const Instance& instance);
  double compute_lp_lower_bound(
      const BBNode& node,
      const std::vector<std::vector<double>>& distances, int station_count,
      int point_count, double time, const Instance& instance) const;
  ILPSolver* ilp_{nullptr};
  Config config_;
  double global_lp_lower_bound_{0.0};
};
}
