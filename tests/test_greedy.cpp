#include "test_utils.hpp"

#include "kdc/algorithms/greedy_set_cover_solver.hpp"
#include "kdc/kont_solver.hpp"
#include "kdc/static_solver_registry.hpp"
#include "kdc/stationary.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <memory>
#include <vector>

TEST_CASE("Greedy: all points covered") {
  const auto instance = kdc::test::make_dummy_instance(50, 5, 3U);
  const auto solution = kdc::GreedySetCoverSolver().solve(instance, 0.5);
  REQUIRE(solution.feasible);
  for (int point = 0; point < instance.n; ++point) {
    const auto position =
        instance.trajectories[static_cast<kdc::Index>(point)].position(0.5);
    bool covered = false;
    for (int station = 0; station < instance.m; ++station) {
      const auto station_index = static_cast<kdc::Index>(station);
      if (solution.supporting_point[station_index] == -1) {
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

TEST_CASE("Greedy: cost is within twice IP on n=50 m=5") {
  const auto instance = kdc::test::make_dummy_instance(50, 5, 3U);
  auto kont = std::make_unique<kdc::KontSolver>();
  const auto ip =
      kdc::StationarySolver::solve_ip(instance, 0.5, *kont, 60.0, 1e-4);
  const auto greedy = kdc::GreedySetCoverSolver().solve(instance, 0.5);
  REQUIRE(greedy.feasible);
  REQUIRE(greedy.cost <= 2.0 * ip.cost + 1e-6);
}

TEST_CASE("Greedy: lower bound is valid") {
  const auto instance = kdc::test::make_dummy_instance(30, 5, 3U);
  auto kont = std::make_unique<kdc::KontSolver>();
  const auto ip =
      kdc::StationarySolver::solve_ip(instance, 0.5, *kont, 60.0, 1e-4);
  const auto greedy = kdc::GreedySetCoverSolver().solve(instance, 0.5);
  REQUIRE(greedy.lower_bound <= ip.cost + 1e-6);
}

TEST_CASE("Greedy: performance on n=500 m=25") {
  const auto instance = kdc::test::make_dummy_instance(500, 25, 3U);
  const auto start = std::chrono::steady_clock::now();
  const auto solution = kdc::GreedySetCoverSolver().solve(instance, 0.5);
  const double elapsed =
      std::chrono::duration<double>(std::chrono::steady_clock::now() - start)
          .count();
  REQUIRE(elapsed < 0.1);
  REQUIRE(solution.feasible);
}

TEST_CASE("Greedy: all tie-break modes produce valid solutions") {
  const auto instance = kdc::test::make_dummy_instance(50, 5, 3U);
  const std::vector<kdc::GreedySetCoverSolver::TieBreak> modes{
      kdc::GreedySetCoverSolver::TieBreak::DENSITY,
      kdc::GreedySetCoverSolver::TieBreak::MAX_NEW_POINTS,
      kdc::GreedySetCoverSolver::TieBreak::MIN_COST};
  for (const auto mode : modes) {
    kdc::GreedySetCoverSolver::Config config;
    config.tie_break = mode;
    REQUIRE(kdc::GreedySetCoverSolver(config).solve(instance, 0.5).feasible);
  }
}

TEST_CASE("Greedy: no duplicate stations") {
  const auto instance = kdc::test::make_dummy_instance(50, 5, 3U);
  const auto solution = kdc::GreedySetCoverSolver().solve(instance, 0.5);
  int active_stations = 0;
  for (const int support : solution.supporting_point) {
    if (support != -1) {
      ++active_stations;
    }
  }
  REQUIRE(active_stations <= instance.m);
}

TEST_CASE("Greedy: deterministic") {
  const auto instance = kdc::test::make_dummy_instance(50, 5, 3U);
  const auto first = kdc::GreedySetCoverSolver().solve(instance, 0.5);
  const auto second = kdc::GreedySetCoverSolver().solve(instance, 0.5);
  REQUIRE(kdc::test::near(first.cost, second.cost, 1e-12));
  REQUIRE(first.supporting_point == second.supporting_point);
}

TEST_CASE("Greedy: registered") {
  const auto names = kdc::StaticSolverRegistry::list();
  REQUIRE(std::find(names.begin(), names.end(), "greedy") != names.end());
  auto solver = kdc::StaticSolverRegistry::create("greedy", nullptr);
  REQUIRE(solver != nullptr);
  REQUIRE(solver->name() == "greedy");
  REQUIRE_FALSE(solver->is_exact());
  REQUIRE(solver->provides_lower_bound());
}
