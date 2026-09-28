#include "test_utils.hpp"

#include "kdc/algorithms/brute_force_solver.hpp"
#include "kdc/kont_solver.hpp"
#include "kdc/static_solver_registry.hpp"
#include "kdc/stationary.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <limits>
#include <memory>

TEST_CASE("BruteForce: matches IP on n=6 m=3") {
  const auto instance = kdc::test::make_dummy_instance(6, 3, 3U);
  constexpr double time = 0.4;
  const auto brute_force = kdc::BruteForceSolver().solve(instance, time);
  auto kont = std::make_unique<kdc::KontSolver>();
  const auto ip =
      kdc::StationarySolver::solve_ip(instance, time, *kont, 60.0, 1e-6);
  REQUIRE(brute_force.feasible);
  REQUIRE(kdc::test::near(brute_force.cost, ip.cost, 1e-6));
}

TEST_CASE("BruteForce: matches IP on n=8 m=4") {
  const auto instance = kdc::test::make_dummy_instance(8, 4, 11U);
  constexpr double time = 0.7;
  const auto brute_force = kdc::BruteForceSolver().solve(instance, time);
  auto kont = std::make_unique<kdc::KontSolver>();
  const auto ip =
      kdc::StationarySolver::solve_ip(instance, time, *kont, 60.0, 1e-6);
  REQUIRE(brute_force.feasible);
  REQUIRE(kdc::test::near(brute_force.cost, ip.cost, 1e-6));
}

TEST_CASE("BruteForce: assignment cap is enforced") {
  kdc::BruteForceSolver::Config config;
  config.max_assignments = 1000U;
  const auto instance = kdc::test::make_dummy_instance(12, 6, 3U);
  const auto result = kdc::BruteForceSolver(config).solve(instance, 0.5);
  REQUIRE_FALSE(result.feasible);
}

TEST_CASE("BruteForce: time limit is enforced") {
  kdc::BruteForceSolver::Config config;
  config.time_limit_sec = 0.001;
  config.max_assignments = std::numeric_limits<std::uint64_t>::max();
  const auto instance = kdc::test::make_dummy_instance(20, 5, 3U);
  const auto start = std::chrono::steady_clock::now();
  const auto result = kdc::BruteForceSolver(config).solve(instance, 0.5);
  const double elapsed =
      std::chrono::duration<double>(std::chrono::steady_clock::now() - start)
          .count();
  REQUIRE(elapsed < 0.5);
  if (result.feasible) {
    REQUIRE(result.lower_bound <= result.cost + 1e-9);
    REQUIRE(result.upper_bound >= result.cost - 1e-9);
  }
}

TEST_CASE("BruteForce: bounds equal exact cost") {
  const auto instance = kdc::test::make_dummy_instance(5, 2, 3U);
  const auto result = kdc::BruteForceSolver().solve(instance, 0.5);
  REQUIRE(result.feasible);
  REQUIRE(kdc::test::near(result.lower_bound, result.cost, 1e-12));
  REQUIRE(kdc::test::near(result.upper_bound, result.cost, 1e-12));
}

TEST_CASE("BruteForce: infeasible when m=0 and n>0") {
  kdc::Instance instance;
  instance.n = 3;
  instance.m = 0;
  instance.T_end = 1.0;
  instance.trajectories.resize(3U);
  for (auto& trajectory : instance.trajectories) {
    trajectory.t_breaks = {0.0, 1.0};
    trajectory.waypoints = {kdc::Point(0.0, 0.0), kdc::Point(1.0, 1.0)};
  }
  REQUIRE_FALSE(kdc::BruteForceSolver().solve(instance, 0.5).feasible);
}

TEST_CASE("BruteForce: solution covers every point") {
  const auto instance = kdc::test::make_dummy_instance(6, 3, 5U);
  const auto solution = kdc::BruteForceSolver().solve(instance, 0.5);
  REQUIRE(solution.feasible);
  for (int point_id = 0; point_id < instance.n; ++point_id) {
    const auto point =
        instance.trajectories[static_cast<kdc::Index>(point_id)].position(0.5);
    bool covered = false;
    for (int station_id = 0; station_id < instance.m; ++station_id) {
      if (solution.supporting_point[static_cast<kdc::Index>(station_id)] == -1) {
        continue;
      }
      if ((instance.stations[static_cast<kdc::Index>(station_id)].pos - point)
                  .norm() <=
          solution.radius[static_cast<kdc::Index>(station_id)] + 1e-9) {
        covered = true;
        break;
      }
    }
    REQUIRE(covered);
  }
}

TEST_CASE("BruteForce: radius and cost are consistent") {
  const auto instance = kdc::test::make_dummy_instance(6, 3, 5U);
  const auto solution = kdc::BruteForceSolver().solve(instance, 0.5);
  double expected_cost = 0.0;
  const double pi = std::acos(-1.0);
  for (int station_id = 0; station_id < instance.m; ++station_id) {
    const int support =
        solution.supporting_point[static_cast<kdc::Index>(station_id)];
    if (support == -1) {
      continue;
    }
    const auto point =
        instance.trajectories[static_cast<kdc::Index>(support)].position(0.5);
    const double expected_radius =
        (instance.stations[static_cast<kdc::Index>(station_id)].pos - point)
            .norm();
    REQUIRE(kdc::test::near(
        solution.radius[static_cast<kdc::Index>(station_id)], expected_radius,
        1e-9));
    expected_cost += pi * expected_radius * expected_radius;
  }
  REQUIRE(kdc::test::near(solution.cost, expected_cost, 1e-9));
}

TEST_CASE("BruteForce: name, capability, and registry") {
  const kdc::BruteForceSolver solver;
  REQUIRE(solver.name() == "brute-force");
  REQUIRE(solver.is_exact());
  REQUIRE(solver.provides_lower_bound());
  const auto names = kdc::StaticSolverRegistry::list();
  REQUIRE(std::find(names.begin(), names.end(), "brute-force") != names.end());
  REQUIRE(std::is_sorted(names.begin(), names.end()));
}
