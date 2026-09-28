#include "test_utils.hpp"

#include "kdc/algorithms/genetic_solver.hpp"
#include "kdc/kont_solver.hpp"
#include "kdc/static_solver_registry.hpp"
#include "kdc/stationary.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <memory>

TEST_CASE("GA: improves or matches NN on n=50 m=5") {
  const auto instance = kdc::test::make_dummy_instance(50, 5, 3U);
  const auto nearest = kdc::StationarySolver::solve_nn(instance, 0.5);
  const auto genetic = kdc::GeneticSolver().solve(instance, 0.5);
  REQUIRE(genetic.feasible);
  REQUIRE(genetic.cost <= nearest.cost + 1e-6);
}

TEST_CASE("GA: cost is no lower than IP optimum") {
  const auto instance = kdc::test::make_dummy_instance(30, 5, 3U);
  auto kont = std::make_unique<kdc::KontSolver>();
  const auto ip =
      kdc::StationarySolver::solve_ip(instance, 0.5, *kont, 60.0, 1e-4);
  const auto genetic = kdc::GeneticSolver().solve(instance, 0.5);
  REQUIRE(genetic.feasible);
  REQUIRE(genetic.cost >= ip.cost - 1e-6);
}

TEST_CASE("GA: deterministic with fixed seed") {
  const auto instance = kdc::test::make_dummy_instance(50, 5, 3U);
  kdc::GeneticSolver::Config config;
  config.seed = 99U;
  config.generations = 100;
  const auto first = kdc::GeneticSolver(config).solve(instance, 0.5);
  const auto second = kdc::GeneticSolver(config).solve(instance, 0.5);
  REQUIRE(kdc::test::near(first.cost, second.cost, 1e-9));
  REQUIRE(first.supporting_point == second.supporting_point);
}

TEST_CASE("GA: feasible") {
  const auto instance = kdc::test::make_dummy_instance(30, 5, 3U);
  REQUIRE(kdc::GeneticSolver().solve(instance, 0.5).feasible);
}

TEST_CASE("GA: runs within limit on n=500 m=25") {
  const auto instance = kdc::test::make_dummy_instance(500, 25, 3U);
  const auto start = std::chrono::steady_clock::now();
  const auto result = kdc::GeneticSolver().solve(instance, 0.5);
  const double elapsed =
      std::chrono::duration<double>(std::chrono::steady_clock::now() - start)
          .count();
  REQUIRE(result.feasible);
  REQUIRE(elapsed < 60.0);
}

TEST_CASE("GA: registered") {
  const auto names = kdc::StaticSolverRegistry::list();
  REQUIRE(std::find(names.begin(), names.end(), "genetic") != names.end());
  auto solver = kdc::StaticSolverRegistry::create("genetic", nullptr);
  REQUIRE(solver != nullptr);
  REQUIRE(solver->name() == "genetic");
  REQUIRE_FALSE(solver->is_exact());
  REQUIRE(solver->provides_lower_bound());
}
