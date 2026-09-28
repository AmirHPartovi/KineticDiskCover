#include "test_utils.hpp"

#include "kdc/algorithms/branch_and_bound_solver.hpp"
#include "kdc/algorithms/brute_force_solver.hpp"
#include "kdc/kont_solver.hpp"
#include "kdc/static_solver_registry.hpp"
#include "kdc/stationary.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <memory>

TEST_CASE("BranchAndBound: matches IP on n=10 m=5") {
  const auto instance = kdc::test::make_dummy_instance(10, 5, 7U);
  kdc::KontSolver kont;
  kdc::BranchAndBoundSolver::Config config;
  config.use_lp_lower_bound = false;
  const auto bnb = kdc::BranchAndBoundSolver(&kont, config).solve(instance, 0.4);
  const auto ip =
      kdc::StationarySolver::solve_ip(instance, 0.4, kont, 60.0, 1e-6);
  REQUIRE(bnb.feasible);
  REQUIRE(kdc::test::near(bnb.cost, ip.cost, 1e-6));
}

TEST_CASE("BranchAndBound: matches IP on n=15 m=5") {
  const auto instance = kdc::test::make_dummy_instance(15, 5, 11U);
  kdc::KontSolver kont;
  kdc::BranchAndBoundSolver::Config config;
  config.use_lp_lower_bound = false;
  const auto bnb = kdc::BranchAndBoundSolver(&kont, config).solve(instance, 0.7);
  const auto ip =
      kdc::StationarySolver::solve_ip(instance, 0.7, kont, 60.0, 1e-6);
  REQUIRE(bnb.feasible);
  REQUIRE(kdc::test::near(bnb.cost, ip.cost, 1e-6));
}

TEST_CASE("BranchAndBound: lower bound is valid") {
  const auto instance = kdc::test::make_dummy_instance(8, 3, 3U);
  kdc::BranchAndBoundSolver::Config config;
  config.use_lp_lower_bound = false;
  const auto exact =
      kdc::BruteForceSolver().solve(instance, 0.5);
  const auto bounded =
      kdc::BranchAndBoundSolver(nullptr, config).solve(instance, 0.5);
  REQUIRE(bounded.feasible);
  REQUIRE(bounded.lower_bound <= exact.cost + 1e-9);
  REQUIRE(bounded.upper_bound >= exact.cost - 1e-9);
  REQUIRE(kdc::test::near(bounded.cost, exact.cost, 1e-9));
}

TEST_CASE("BranchAndBound: optional LP bound preserves optimum") {
  const auto instance = kdc::test::make_dummy_instance(7, 3, 13U);
  kdc::KontSolver kont;
  kdc::BranchAndBoundSolver solver(&kont);
  const auto result = solver.solve(instance, 0.5);
  const auto exact = kdc::BruteForceSolver().solve(instance, 0.5);
  REQUIRE(result.feasible);
  REQUIRE(result.lower_bound <= exact.cost + 1e-9);
  REQUIRE(kdc::test::near(result.cost, exact.cost, 1e-9));
}

TEST_CASE("BranchAndBound: node limit returns feasible incumbent and bound") {
  const auto instance = kdc::test::make_dummy_instance(12, 5, 19U);
  kdc::BranchAndBoundSolver::Config config;
  config.node_limit = 1U;
  config.use_lp_lower_bound = false;
  const auto result =
      kdc::BranchAndBoundSolver(nullptr, config).solve(instance, 0.5);
  REQUIRE(result.feasible);
  REQUIRE(result.lower_bound <= result.cost + 1e-9);
  REQUIRE(kdc::test::near(result.upper_bound, result.cost, 1e-9));
}

TEST_CASE("BranchAndBound: time limit is respected") {
  const auto instance = kdc::test::make_dummy_instance(14, 5, 23U);
  kdc::BranchAndBoundSolver::Config config;
  config.time_limit_sec = 0.001;
  config.use_lp_lower_bound = false;
  const auto start = std::chrono::steady_clock::now();
  const auto result =
      kdc::BranchAndBoundSolver(nullptr, config).solve(instance, 0.5);
  const double elapsed =
      std::chrono::duration<double>(std::chrono::steady_clock::now() - start)
          .count();
  REQUIRE(result.feasible);
  REQUIRE(elapsed < 0.5);
}

TEST_CASE("BranchAndBound: returned solution covers all points") {
  const auto instance = kdc::test::make_dummy_instance(10, 4, 29U);
  kdc::BranchAndBoundSolver::Config config;
  config.use_lp_lower_bound = false;
  const auto result =
      kdc::BranchAndBoundSolver(nullptr, config).solve(instance, 0.5);
  REQUIRE(result.feasible);
  for (int point = 0; point < instance.n; ++point) {
    const auto position =
        instance.trajectories[static_cast<kdc::Index>(point)].position(0.5);
    bool covered = false;
    for (int station = 0; station < instance.m; ++station) {
      const auto station_index = static_cast<kdc::Index>(station);
      if (result.supporting_point[station_index] == -1) {
        continue;
      }
      if ((instance.stations[station_index].pos - position).norm() <=
          result.radius[station_index] + 1e-9) {
        covered = true;
        break;
      }
    }
    REQUIRE(covered);
  }
}

TEST_CASE("BranchAndBound: registry and capability metadata") {
  kdc::BranchAndBoundSolver solver(nullptr);
  REQUIRE(solver.name() == "branch-and-bound");
  REQUIRE(solver.is_exact());
  REQUIRE(solver.provides_lower_bound());
  const auto names = kdc::StaticSolverRegistry::list();
  REQUIRE(std::find(names.begin(), names.end(), "branch-and-bound") !=
          names.end());
}
