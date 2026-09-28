#include "test_utils.hpp"

#include "kdc/algorithms/local_search_solver.hpp"
#include "kdc/kont_solver.hpp"
#include "kdc/static_solver_registry.hpp"
#include "kdc/stationary.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <memory>

TEST_CASE("LocalSearch: improves or matches NN on n=50 m=5") {
  const auto instance = kdc::test::make_dummy_instance(50, 5, 3U);
  const auto nearest = kdc::StationarySolver::solve_nn(instance, 0.5);
  const auto local = kdc::LocalSearchSolver().solve(instance, 0.5);
  REQUIRE(local.feasible);
  REQUIRE(local.cost <= nearest.cost + 1e-6);
}

TEST_CASE("LocalSearch: cost is no lower than IP optimum") {
  const auto instance = kdc::test::make_dummy_instance(30, 5, 3U);
  auto kont = std::make_unique<kdc::KontSolver>();
  const auto ip =
      kdc::StationarySolver::solve_ip(instance, 0.5, *kont, 60.0, 1e-4);
  const auto local = kdc::LocalSearchSolver().solve(instance, 0.5);
  REQUIRE(local.feasible);
  REQUIRE(local.cost >= ip.cost - 1e-6);
}

TEST_CASE("LocalSearch: first-improvement mode terminates") {
  const auto instance = kdc::test::make_dummy_instance(100, 10, 3U);
  kdc::LocalSearchSolver::Config config;
  config.mode = kdc::LocalSearchSolver::Mode::FIRST_IMPROVEMENT;
  config.max_iterations = 100;
  const auto result = kdc::LocalSearchSolver(config).solve(instance, 0.5);
  REQUIRE(result.feasible);
}

TEST_CASE("LocalSearch: best-improvement mode terminates") {
  const auto instance = kdc::test::make_dummy_instance(100, 10, 3U);
  kdc::LocalSearchSolver::Config config;
  config.mode = kdc::LocalSearchSolver::Mode::BEST_IMPROVEMENT;
  config.max_iterations = 100;
  const auto result = kdc::LocalSearchSolver(config).solve(instance, 0.5);
  REQUIRE(result.feasible);
}

TEST_CASE("LocalSearch: deterministic with fixed seed") {
  const auto instance = kdc::test::make_dummy_instance(100, 10, 3U);
  kdc::LocalSearchSolver::Config config;
  config.seed = 7U;
  const auto first = kdc::LocalSearchSolver(config).solve(instance, 0.5);
  const auto second = kdc::LocalSearchSolver(config).solve(instance, 0.5);
  REQUIRE(kdc::test::near(first.cost, second.cost, 1e-9));
  REQUIRE(first.supporting_point == second.supporting_point);
}

TEST_CASE("LocalSearch: performance on n=500 m=25") {
  const auto instance = kdc::test::make_dummy_instance(500, 25, 3U);
  const auto start = std::chrono::steady_clock::now();
  const auto result = kdc::LocalSearchSolver().solve(instance, 0.5);
  const double elapsed =
      std::chrono::duration<double>(std::chrono::steady_clock::now() - start)
          .count();
  REQUIRE(result.feasible);
  REQUIRE(elapsed < 5.0);
}

TEST_CASE("LocalSearch: registered") {
  const auto names = kdc::StaticSolverRegistry::list();
  REQUIRE(std::find(names.begin(), names.end(), "local-search") !=
          names.end());
  auto solver = kdc::StaticSolverRegistry::create("local-search", nullptr);
  REQUIRE(solver != nullptr);
  REQUIRE(solver->name() == "local-search");
  REQUIRE_FALSE(solver->is_exact());
  REQUIRE(solver->provides_lower_bound());
}
