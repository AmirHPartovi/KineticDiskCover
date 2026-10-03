#pragma once

#include "kdc/istatic_solver.hpp"

#include <random>
#include <vector>

namespace kdc {
class GeneticSolver final : public IStaticSolver {
 public:
  struct Config {
    int population_size{100};
    int generations{500};
    double crossover_rate{0.8};
    double mutation_rate{0.05};
    int tournament_size{5};
    double elitism_fraction{0.05};
    unsigned seed{42U};
  };

  GeneticSolver();
  explicit GeneticSolver(Config config);
  StaticSolution solve(const Instance& instance, double time) override;
  std::string name() const override { return "genetic"; }
  bool is_exact() const override { return false; }
  bool provides_lower_bound() const override { return true; }
  void set_seed(unsigned seed) override { config_.seed = seed; }

 private:
  struct Individual {
    std::vector<int> assignment;
    double cost{0.0};
  };

  double evaluate(const Individual& individual,
                  const std::vector<std::vector<double>>& distances,
                  int point_count, int station_count) const;
  Individual tournament_select(const std::vector<Individual>& population,
                              int tournament_size,
                              std::mt19937& random) const;
  Individual uniform_crossover(const Individual& first,
                               const Individual& second,
                               std::mt19937& random) const;
  void mutate(Individual& individual, int station_count,
              std::mt19937& random) const;

  Config config_;
};
}
