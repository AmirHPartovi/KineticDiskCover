#pragma once

#include "kdc/istatic_solver.hpp"

#include <random>
#include <vector>

namespace kdc {
class LocalSearchSolver final : public IStaticSolver {
 public:
  enum class Mode { FIRST_IMPROVEMENT, BEST_IMPROVEMENT };
  struct Config {
    Mode mode{Mode::BEST_IMPROVEMENT};
    int max_iterations{10'000};
    int max_restarts{10};
    bool use_perturbation{true};
    unsigned seed{42U};
  };

  LocalSearchSolver();
  explicit LocalSearchSolver(Config config);
  StaticSolution solve(const Instance& instance, double time) override;
  std::string name() const override { return "local-search"; }
  bool is_exact() const override { return false; }
  bool provides_lower_bound() const override { return true; }

 private:
  struct State {
    std::vector<int> assignment;
    std::vector<double> radius;
    double cost{0.0};
  };
  struct Move {
    int point_id{-1};
    int from_station{-1};
    int to_station{-1};
    double delta_cost{0.0};
  };

  std::vector<Move> enumerate_moves(
      const State& state,
      const std::vector<std::vector<double>>& distances, int point_count,
      int station_count) const;
  void apply_move(State& state, const Move& move,
                  const std::vector<std::vector<double>>& distances,
                  int station_count) const;
  void recompute_radius(State& state,
                        const std::vector<std::vector<double>>& distances,
                        int point_count, int station_count) const;
  State perturb(const State& best,
                const std::vector<std::vector<double>>& distances,
                int point_count, int station_count, std::mt19937& random) const;

  Config config_;
};
}
