#include "test_utils.hpp"

#include "kdc/algorithms/ip_static_solver.hpp"
#include "kdc/minmax.hpp"
#include "kdc/minsum.hpp"
#include "kdc/mock_ilp_solver.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <memory>

TEST_CASE("Mock: solves a trivial set-cover instance") {
  Eigen::VectorXd costs(2);
  costs << 1.0, 2.0;
  Eigen::SparseMatrix<double> matrix(2, 2);
  matrix.insert(0, 0) = 1.0;
  matrix.insert(1, 0) = 1.0;
  matrix.insert(1, 1) = 1.0;
  matrix.makeCompressed();
  Eigen::VectorXd rhs(2);
  rhs << 1.0, 1.0;
  kdc::MockILPSolver mock;

  const auto result = mock.solve(costs, matrix, rhs, {0, 1}, 60.0, 0.0);

  REQUIRE(result.status == kdc::ILPResult::Status::FEASIBLE);
  REQUIRE(result.x.size() == 2U);
  REQUIRE(result.x[0] > 0.5);
  REQUIRE(result.x[1] < 0.5);
  REQUIRE(std::abs(result.objective - 1.0) < 1e-9);
  REQUIRE(result.lower_bound == 0.0);
}

TEST_CASE("Mock: detects a row with no covering variable") {
  Eigen::VectorXd costs(1);
  costs << 1.0;
  Eigen::SparseMatrix<double> matrix(1, 1);
  matrix.makeCompressed();
  Eigen::VectorXd rhs(1);
  rhs << 1.0;
  kdc::MockILPSolver mock;

  const auto result = mock.solve(costs, matrix, rhs, {0}, 60.0, 0.0);

  REQUIRE(result.status == kdc::ILPResult::Status::INFEASIBLE);
  REQUIRE(result.x.size() == 1U);
}

TEST_CASE("Mock: works as a drop-in IPStaticSolver backend") {
  const auto instance = kdc::test::make_dummy_instance(20, 5, 7U);
  auto mock = std::make_unique<kdc::MockILPSolver>();
  kdc::IPStaticSolver solver(mock.get());

  const auto solution = solver.solve(instance, 0.5);

  REQUIRE(solution.feasible);
  REQUIRE(solution.cost > 0.0);
  REQUIRE(solution.solver_name == "ip-kont");
}

TEST_CASE("Mock: works in MinMaxSolver pipeline") {
  const auto instance = kdc::test::make_dummy_instance(15, 4, 3U);
  auto mock = std::make_unique<kdc::MockILPSolver>();
  kdc::IPStaticSolver solver(mock.get());

  const auto result =
      kdc::MinMaxSolver::solve(instance, solver, kdc::MinMaxSolver::Config{});

  REQUIRE(result.verified);
  REQUIRE(result.peak_cost > 0.0);
}

TEST_CASE("Mock: works in MinSumSolver pipeline") {
  const auto instance = kdc::test::make_dummy_instance(15, 4, 3U);
  auto mock = std::make_unique<kdc::MockILPSolver>();
  kdc::IPStaticSolver solver(mock.get());

  const auto result =
      kdc::MinSumSolver::solve(instance, solver, kdc::MinSumSolver::Config{});

  REQUIRE(result.verified);
  REQUIRE(result.total_integral > 0.0);
}

TEST_CASE("Mock: objective equals c transpose x") {
  Eigen::VectorXd costs(3);
  costs << 1.0, 2.0, 3.0;
  Eigen::SparseMatrix<double> matrix(2, 3);
  matrix.insert(0, 0) = 1.0;
  matrix.insert(0, 1) = 1.0;
  matrix.insert(1, 1) = 1.0;
  matrix.insert(1, 2) = 1.0;
  matrix.makeCompressed();
  Eigen::VectorXd rhs(2);
  rhs << 1.0, 1.0;
  kdc::MockILPSolver mock;

  const auto result = mock.solve(costs, matrix, rhs, {0, 1, 2}, 60.0, 0.0);

  double expected = 0.0;
  for (std::size_t column = 0; column < result.x.size(); ++column) {
    expected += costs[static_cast<Eigen::Index>(column)] * result.x[column];
  }
  REQUIRE(result.status == kdc::ILPResult::Status::FEASIBLE);
  REQUIRE(std::abs(result.objective - expected) < 1e-9);
  REQUIRE(mock.name() == "mock");
}

TEST_CASE("Mock: claims optimal only for a zero-cost feasible incumbent") {
  Eigen::VectorXd costs(1);
  costs << 0.0;
  Eigen::SparseMatrix<double> matrix(1, 1);
  matrix.insert(0, 0) = 1.0;
  matrix.makeCompressed();
  Eigen::VectorXd rhs(1);
  rhs << 1.0;
  kdc::MockILPSolver::Config config;
  config.allow_optimal_claim = true;
  kdc::MockILPSolver mock(config);

  const auto result = mock.solve(costs, matrix, rhs, {0}, 60.0, 0.0);
  REQUIRE(result.status == kdc::ILPResult::Status::OPTIMAL);
  REQUIRE(result.objective == 0.0);
}
