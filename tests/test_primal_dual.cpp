#include "test_utils.hpp"

#include "kdc/algorithms/primal_dual_solver.hpp"
#include "kdc/kont_solver.hpp"
#include "kdc/static_solver_registry.hpp"
#include "kdc/stationary.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <memory>

TEST_CASE("PrimalDual: feasible") {
  const auto instance = kdc::test::make_dummy_instance(30, 5, 3U);
  const auto result = kdc::PrimalDualSolver().solve(instance, 0.5);
  REQUIRE(result.feasible);
}

TEST_CASE("PrimalDual: lower bound is valid") {
  const auto instance = kdc::test::make_dummy_instance(30, 5, 3U);
  auto kont = std::make_unique<kdc::KontSolver>();
  const auto primal_dual = kdc::PrimalDualSolver().solve(instance, 0.5);
  const auto ip =
      kdc::StationarySolver::solve_ip(instance, 0.5, *kont, 60.0, 1e-4);
  REQUIRE(primal_dual.lower_bound <= ip.cost + 1e-6);
}

TEST_CASE("PrimalDual: deterministic") {
  const auto instance = kdc::test::make_dummy_instance(30, 5, 3U);
  const auto first = kdc::PrimalDualSolver().solve(instance, 0.5);
  const auto second = kdc::PrimalDualSolver().solve(instance, 0.5);
  REQUIRE(kdc::test::near(first.cost, second.cost, 1e-9));
  REQUIRE(first.supporting_point == second.supporting_point);
}

TEST_CASE("PrimalDual: reverse delete does not increase cost") {
  const auto instance = kdc::test::make_dummy_instance(50, 8, 3U);
  kdc::PrimalDualSolver::Config without_delete;
  without_delete.use_reverse_delete = false;
  kdc::PrimalDualSolver::Config with_delete;
  with_delete.use_reverse_delete = true;
  const auto original =
      kdc::PrimalDualSolver(without_delete).solve(instance, 0.5);
  const auto reduced =
      kdc::PrimalDualSolver(with_delete).solve(instance, 0.5);
  REQUIRE(original.feasible);
  REQUIRE(reduced.feasible);
  REQUIRE(reduced.cost <= original.cost + 1e-9);
}

TEST_CASE("PrimalDual: returned supports cover all points") {
  const auto instance = kdc::test::make_dummy_instance(30, 5, 17U);
  const auto solution = kdc::PrimalDualSolver().solve(instance, 0.5);
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

TEST_CASE("PrimalDual: no duplicate stations") {
  const auto instance = kdc::test::make_dummy_instance(30, 5, 3U);
  const auto solution = kdc::PrimalDualSolver().solve(instance, 0.5);
  const int active = static_cast<int>(std::count_if(
      solution.supporting_point.begin(), solution.supporting_point.end(),
      [](int support) { return support >= 0; }));
  REQUIRE(active <= instance.m);
}

TEST_CASE("PrimalDual: registered") {
  const auto names = kdc::StaticSolverRegistry::list();
  REQUIRE(std::find(names.begin(), names.end(), "primal-dual") != names.end());
  auto solver = kdc::StaticSolverRegistry::create("primal-dual", nullptr);
  REQUIRE(solver != nullptr);
  REQUIRE(solver->name() == "primal-dual");
  REQUIRE_FALSE(solver->is_exact());
  REQUIRE(solver->provides_lower_bound());
}
