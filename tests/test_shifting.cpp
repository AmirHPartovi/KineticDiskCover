#include "test_utils.hpp"

#include "kdc/algorithms/shifting_strategy_solver.hpp"
#include "kdc/kont_solver.hpp"
#include "kdc/static_solver_registry.hpp"
#include "kdc/stationary.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <memory>

TEST_CASE("Shifting: valid solution") {
  const auto instance = kdc::test::make_dummy_instance(50, 8, 3U);
  const auto solution =
      kdc::ShiftingStrategySolver(nullptr).solve(instance, 0.5);
  REQUIRE(solution.feasible);
  for (int point = 0; point < instance.n; ++point) {
    const auto position =
        instance.trajectories[static_cast<kdc::Index>(point)].position(0.5);
    bool covered = false;
    for (int station = 0; station < instance.m; ++station) {
      const auto station_index = static_cast<kdc::Index>(station);
      if (solution.supporting_point[station_index] < 0) {
        continue;
      }
      if ((instance.stations[station_index].pos - position).norm() <=
          solution.radius[station_index] + 1e-9) {
        covered = true;
        break;
      }
    }
    REQUIRE(covered);
  }
}

TEST_CASE("Shifting: larger l is at least as good") {
  const auto instance = kdc::test::make_dummy_instance(50, 8, 3U);
  kdc::ShiftingStrategySolver::Config config_two;
  config_two.l = 2;
  kdc::ShiftingStrategySolver::Config config_eight;
  config_eight.l = 8;
  const auto two =
      kdc::ShiftingStrategySolver(nullptr, config_two).solve(instance, 0.5);
  const auto eight =
      kdc::ShiftingStrategySolver(nullptr, config_eight).solve(instance, 0.5);
  REQUIRE(eight.cost <= two.cost + 1e-6);
}

TEST_CASE("Shifting: lower bound is valid") {
  const auto instance = kdc::test::make_dummy_instance(30, 5, 3U);
  auto kont = std::make_unique<kdc::KontSolver>();
  const auto ip =
      kdc::StationarySolver::solve_ip(instance, 0.5, *kont, 60.0, 1e-4);
  const auto shifting =
      kdc::ShiftingStrategySolver(nullptr).solve(instance, 0.5);
  REQUIRE(shifting.lower_bound <= ip.cost + 1e-6);
}

TEST_CASE("Shifting: deterministic") {
  const auto instance = kdc::test::make_dummy_instance(50, 8, 3U);
  const auto first =
      kdc::ShiftingStrategySolver(nullptr).solve(instance, 0.5);
  const auto second =
      kdc::ShiftingStrategySolver(nullptr).solve(instance, 0.5);
  REQUIRE(kdc::test::near(first.cost, second.cost, 1e-9));
  REQUIRE(first.supporting_point == second.supporting_point);
}

TEST_CASE("Shifting: IP cells cost no worse than NN cells") {
  const auto instance = kdc::test::make_dummy_instance(30, 5, 3U);
  auto kont = std::make_unique<kdc::KontSolver>();
  kdc::ShiftingStrategySolver::Config nn_config;
  nn_config.l = 2;
  kdc::ShiftingStrategySolver::Config ip_config = nn_config;
  ip_config.use_ip_for_cells = true;
  const auto nn =
      kdc::ShiftingStrategySolver(kont.get(), nn_config).solve(instance, 0.5);
  const auto ip =
      kdc::ShiftingStrategySolver(kont.get(), ip_config).solve(instance, 0.5);
  REQUIRE(ip.feasible);
  REQUIRE(ip.cost <= nn.cost + 1e-6);
}

TEST_CASE("Shifting: registered") {
  const auto names = kdc::StaticSolverRegistry::list();
  REQUIRE(std::find(names.begin(), names.end(), "shifting") != names.end());
  auto solver = kdc::StaticSolverRegistry::create("shifting", nullptr);
  REQUIRE(solver != nullptr);
  REQUIRE(solver->name() == "shifting");
  REQUIRE_FALSE(solver->is_exact());
  REQUIRE(solver->provides_lower_bound());
}
