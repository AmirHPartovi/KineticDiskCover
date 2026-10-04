#include "test_utils.hpp"

#include "kdc/algorithms/ip_static_solver.hpp"
#include "kdc/kont_solver.hpp"
#include "kdc/minmax.hpp"
#include "kdc/minsum.hpp"
#include "kdc/static_solver_registry.hpp"
#include "kdc/stationary.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <memory>

TEST_CASE("Registry: builtins present and sorted") {
  const auto names = kdc::StaticSolverRegistry::list();
  REQUIRE(std::find(names.begin(), names.end(), "nn") != names.end());
  REQUIRE(std::find(names.begin(), names.end(), "ip-kont") != names.end());
  REQUIRE(std::is_sorted(names.begin(), names.end()));
}

TEST_CASE("Registry: unknown name returns nullptr") {
  REQUIRE(kdc::StaticSolverRegistry::create("bogus", nullptr) == nullptr);
}

TEST_CASE("KontSolver reports whether a generic solve used native or fallback") {
  const bool native_available = kdc::KontSolver::probe_native_backend();
  Eigen::VectorXd costs(1);
  costs[0] = 1.0;
  Eigen::SparseMatrix<double> constraints(1, 1);
  constraints.insert(0, 0) = 1.0;
  Eigen::VectorXd rhs(1);
  rhs[0] = 1.0;
  kdc::KontSolver solver;
  const auto result = solver.solve(costs, constraints, rhs, {0}, 5.0, 0.0);
  REQUIRE(result.status == kdc::ILPResult::Status::OPTIMAL);
  if (native_available && result.native_backend_used) {
    REQUIRE(result.actual_backend == "KONT/COPT");
    REQUIRE_FALSE(result.fallback_used);
  } else {
    REQUIRE(result.actual_backend == "built-in-branch-and-bound-fallback");
    REQUIRE_FALSE(result.native_backend_used);
    REQUIRE(result.fallback_used);
    REQUIRE(solver.name() == "built-in-branch-and-bound-fallback");
  }
}

TEST_CASE("Registry: nn via registry matches direct nn") {
  const auto instance = kdc::test::make_dummy_instance(20, 5, 7U);
  constexpr double time = 0.3;
  auto solver = kdc::StaticSolverRegistry::create("nn", nullptr);
  REQUIRE(solver != nullptr);
  const auto from_registry = solver->solve(instance, time);
  const auto direct = kdc::StationarySolver::solve_nn(instance, time);
  REQUIRE(kdc::test::near(from_registry.cost, direct.cost, 1e-9));
  REQUIRE(from_registry.feasible == direct.feasible);
}

TEST_CASE("Registry: ip via registry matches direct ip") {
  if (!kdc::KontSolver::probe_native_backend()) {
    REQUIRE_THROWS_AS(
        kdc::StaticSolverRegistry::create("ip-kont", nullptr),
        std::runtime_error);
    return;
  }
  const auto instance = kdc::test::make_dummy_instance(20, 5, 7U);
  constexpr double time = 0.3;
  auto kont = std::make_unique<kdc::KontSolver>();
  auto solver = kdc::StaticSolverRegistry::create("ip-kont", kont.get());
  REQUIRE(solver != nullptr);
  const auto from_registry = solver->solve(instance, time);
  double lower_bound = 0.0;
  kdc::ILPResult::Status status = kdc::ILPResult::Status::ERROR;
  const auto direct = kdc::StationarySolver::solve_ip(
      instance, time, *kont, 600.0, 1e-4, &lower_bound, &status);
  REQUIRE(kdc::test::near(from_registry.cost, direct.cost, 1e-6));
  REQUIRE(kdc::test::near(from_registry.lower_bound, lower_bound, 1e-6));
}

TEST_CASE("Registry: ip-kont rejects a non-native solver instance") {
  if (!kdc::KontSolver::probe_native_backend()) {
    SUCCEED("native KONT/COPT runtime is unavailable");
    return;
  }
  kdc::MockILPSolver mock;
  REQUIRE_THROWS_AS(
      kdc::StaticSolverRegistry::create("ip-kont", &mock),
      std::invalid_argument);
}

TEST_CASE("Registry: MinMax ILP overload remains compatible") {
  if (!kdc::KontSolver::probe_native_backend()) {
    SUCCEED("native KONT/COPT runtime is unavailable");
    return;
  }
  const auto instance = kdc::test::make_dummy_instance(30, 5, 11U);
  auto kont = std::make_unique<kdc::KontSolver>();
  kdc::MinMaxSolver::Config config;
  const auto legacy = kdc::MinMaxSolver::solve(instance, *kont, config);
  kdc::IPStaticSolver wrapper(kont.get());
  const auto registered = kdc::MinMaxSolver::solve(instance, wrapper, config);
  REQUIRE(kdc::test::near(legacy.peak_cost, registered.peak_cost, 1e-6));
}

TEST_CASE("Registry: MinSum accepts heuristic and ILP solvers") {
  const auto instance = kdc::test::make_dummy_instance(8, 3, 17U);
  kdc::MinSumSolver::Config config;
  config.lb_num_samples = 2;
  config.gap_target = 0.01;
  auto nn = kdc::StaticSolverRegistry::create("nn", nullptr);
  REQUIRE(nn != nullptr);
  const auto heuristic_result =
      kdc::MinSumSolver::solve(instance, *nn, config);
  REQUIRE(heuristic_result.solution.is_well_formed());
  REQUIRE(heuristic_result.lower_bound_integral <=
          heuristic_result.total_integral + 1e-6);

  kdc::KontSolver kont;
  const auto exact_result = kdc::MinSumSolver::solve(instance, kont, config);
  REQUIRE(exact_result.solution.is_well_formed());
  REQUIRE(exact_result.lower_bound_integral <=
          exact_result.total_integral + 1e-6);
}

TEST_CASE("Registry: MinMax accepts heuristic static solver") {
  const auto instance = kdc::test::make_dummy_instance(10, 3, 1U);
  auto nn = kdc::StaticSolverRegistry::create("nn", nullptr);
  REQUIRE(nn != nullptr);
  kdc::MinMaxSolver::Config config;
  const auto result = kdc::MinMaxSolver::solve(instance, *nn, config);
  REQUIRE(result.solution.is_well_formed());
  REQUIRE(result.lower_bound == 0.0);
  REQUIRE(result.peak_cost >= 0.0);
}

TEST_CASE("Registry: static solver capability flags") {
  auto kont = std::make_unique<kdc::KontSolver>();
  auto nn = kdc::StaticSolverRegistry::create("nn", kont.get());
  REQUIRE(nn != nullptr);
  REQUIRE_FALSE(nn->is_exact());
  REQUIRE_FALSE(nn->provides_lower_bound());
  if (!kdc::KontSolver::probe_native_backend()) {
    REQUIRE_THROWS_AS(kdc::StaticSolverRegistry::create("ip-kont",
                                                        kont.get()),
                      std::runtime_error);
    return;
  }
  auto ip = kdc::StaticSolverRegistry::create("ip-kont", kont.get());
  REQUIRE(ip != nullptr);
  REQUIRE(ip->is_exact());
  REQUIRE(ip->provides_lower_bound());
}
