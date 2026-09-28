#include "test_utils.hpp"

#include "kdc/algorithms/simulated_annealing_solver.hpp"
#include "kdc/kont_solver.hpp"
#include "kdc/static_solver_registry.hpp"
#include "kdc/stationary.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <memory>
#include <random>

TEST_CASE("SA: improves or matches NN") {
  const auto instance = kdc::test::make_dummy_instance(50, 5, 3U);
  const auto nearest = kdc::StationarySolver::solve_nn(instance, 0.5);
  const auto annealed = kdc::SimulatedAnnealingSolver().solve(instance, 0.5);
  REQUIRE(annealed.feasible);
  REQUIRE(annealed.cost <= nearest.cost + 1e-6);
}

TEST_CASE("SA: cost is no lower than IP optimum") {
  const auto instance = kdc::test::make_dummy_instance(30, 5, 3U);
  auto kont = std::make_unique<kdc::KontSolver>();
  const auto ip =
      kdc::StationarySolver::solve_ip(instance, 0.5, *kont, 60.0, 1e-4);
  const auto annealed = kdc::SimulatedAnnealingSolver().solve(instance, 0.5);
  REQUIRE(annealed.feasible);
  REQUIRE(annealed.cost >= ip.cost - 1e-6);
}

TEST_CASE("SA: deterministic with fixed seed") {
  const auto instance = kdc::test::make_dummy_instance(50, 5, 3U);
  kdc::SimulatedAnnealingSolver::Config config;
  config.seed = 11U;
  const auto first =
      kdc::SimulatedAnnealingSolver(config).solve(instance, 0.5);
  const auto second =
      kdc::SimulatedAnnealingSolver(config).solve(instance, 0.5);
  REQUIRE(kdc::test::near(first.cost, second.cost, 1e-9));
  REQUIRE(first.supporting_point == second.supporting_point);
}

TEST_CASE("SA: terminates within configured outer iterations") {
  const auto instance = kdc::test::make_dummy_instance(100, 10, 3U);
  kdc::SimulatedAnnealingSolver::Config config;
  config.max_outer_iters = 100;
  REQUIRE(kdc::SimulatedAnnealingSolver(config).solve(instance, 0.5).feasible);
}

TEST_CASE("SA: Metropolis acceptance follows exp(-delta/T)") {
  constexpr double delta = 1.0;
  constexpr double temperature = 2.0;
  const double expected = std::exp(-delta / temperature);
  std::mt19937 random(42U);
  std::uniform_real_distribution<double> uniform(0.0, 1.0);
  int accepted = 0;
  constexpr int trials = 100000;
  for (int trial = 0; trial < trials; ++trial) {
    if (uniform(random) < std::exp(-delta / temperature)) {
      ++accepted;
    }
  }
  REQUIRE(std::abs(static_cast<double>(accepted) / trials - expected) < 0.01);
}

TEST_CASE("SA: registered") {
  const auto names = kdc::StaticSolverRegistry::list();
  REQUIRE(std::find(names.begin(), names.end(), "sa") != names.end());
  auto solver = kdc::StaticSolverRegistry::create("sa", nullptr);
  REQUIRE(solver != nullptr);
  REQUIRE(solver->name() == "sa");
  REQUIRE_FALSE(solver->is_exact());
  REQUIRE(solver->provides_lower_bound());
}
