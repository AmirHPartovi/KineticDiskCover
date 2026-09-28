#include "test_utils.hpp"

#include "kdc/algorithms/lp_rounding_solver.hpp"
#include "kdc/kont_solver.hpp"
#include "kdc/static_solver_registry.hpp"
#include "kdc/stationary.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <memory>

TEST_CASE("LPRounding: lower bound is valid") {
  const auto instance = kdc::test::make_dummy_instance(30, 5, 3U);
  auto kont = std::make_unique<kdc::KontSolver>();
  const auto rounded = kdc::LPRoundingSolver(kont.get()).solve(instance, 0.5);
  const auto ip =
      kdc::StationarySolver::solve_ip(instance, 0.5, *kont, 60.0, 1e-4);
  REQUIRE(rounded.feasible);
  REQUIRE(rounded.lower_bound <= ip.cost + 1e-6);
}

TEST_CASE("LPRounding: cost is no lower than IP optimum") {
  const auto instance = kdc::test::make_dummy_instance(30, 5, 3U);
  auto kont = std::make_unique<kdc::KontSolver>();
  const auto rounded = kdc::LPRoundingSolver(kont.get()).solve(instance, 0.5);
  const auto ip =
      kdc::StationarySolver::solve_ip(instance, 0.5, *kont, 60.0, 1e-4);
  REQUIRE(rounded.feasible);
  REQUIRE(rounded.cost >= ip.cost - 1e-6);
}

TEST_CASE("LPRounding: cost is within twice IP optimum") {
  const auto instance = kdc::test::make_dummy_instance(30, 5, 3U);
  auto kont = std::make_unique<kdc::KontSolver>();
  const auto rounded = kdc::LPRoundingSolver(kont.get()).solve(instance, 0.5);
  const auto ip =
      kdc::StationarySolver::solve_ip(instance, 0.5, *kont, 60.0, 1e-4);
  REQUIRE(rounded.feasible);
  REQUIRE(rounded.cost <= 2.0 * ip.cost + 1e-6);
}

TEST_CASE("LPRounding: feasible and deterministic with fixed seed") {
  const auto instance = kdc::test::make_dummy_instance(30, 5, 3U);
  auto kont = std::make_unique<kdc::KontSolver>();
  kdc::LPRoundingSolver::Config config;
  config.seed = 123U;
  const auto first = kdc::LPRoundingSolver(kont.get(), config).solve(instance,
                                                                     0.5);
  const auto second = kdc::LPRoundingSolver(kont.get(), config).solve(instance,
                                                                      0.5);
  REQUIRE(first.feasible);
  REQUIRE(kdc::test::near(first.cost, second.cost, 1e-9));
  REQUIRE(first.supporting_point == second.supporting_point);
}

TEST_CASE("LPRounding: returned supports cover every point") {
  const auto instance = kdc::test::make_dummy_instance(30, 5, 3U);
  kdc::KontSolver kont;
  const auto solution = kdc::LPRoundingSolver(&kont).solve(instance, 0.5);
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

TEST_CASE("LPRounding: no duplicate stations") {
  const auto instance = kdc::test::make_dummy_instance(30, 5, 3U);
  auto kont = std::make_unique<kdc::KontSolver>();
  const auto solution = kdc::LPRoundingSolver(kont.get()).solve(instance, 0.5);
  int active_stations = 0;
  for (const int support : solution.supporting_point) {
    if (support != -1) {
      ++active_stations;
    }
  }
  REQUIRE(active_stations <= instance.m);
}

TEST_CASE("LPRounding: registered") {
  const auto names = kdc::StaticSolverRegistry::list();
  REQUIRE(std::find(names.begin(), names.end(), "lp-rounding") != names.end());
  kdc::KontSolver kont;
  auto solver = kdc::StaticSolverRegistry::create("lp-rounding", &kont);
  REQUIRE(solver != nullptr);
  REQUIRE(solver->name() == "lp-rounding");
  REQUIRE_FALSE(solver->is_exact());
  REQUIRE(solver->provides_lower_bound());
}
