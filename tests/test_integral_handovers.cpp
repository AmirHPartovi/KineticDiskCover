#include "test_utils.hpp"

#include "kdc/algorithms/ip_static_solver.hpp"
#include "kdc/algorithms/nn_static_solver.hpp"
#include "kdc/minsum.hpp"
#include "kdc/mock_ilp_solver.hpp"
#include "kdc/verify.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <memory>

TEST_CASE("apply_integral_handovers: no handover for one station") {
  const auto instance = kdc::test::make_instance_linear(
      {{kdc::Point(1.0, 0.0), kdc::Point(2.0, 0.0)}},
      {kdc::Point(0.0, 0.0)});
  kdc::NNStaticSolver nn;
  const auto result =
      kdc::MinSumSolver::solve(instance, nn, kdc::MinSumSolver::Config{});
  auto solution = result.solution;
  const double before = solution.total_integral();

  kdc::apply_integral_handovers(instance, solution);

  REQUIRE(std::abs(solution.total_integral() - before) < 1e-9);
  REQUIRE(solution.is_well_formed());
}

TEST_CASE("apply_integral_handovers: integral is non-increasing") {
  const auto instance = kdc::test::make_instance_linear(
      {{kdc::Point(-5.0, 0.0), kdc::Point(5.0, 0.0)},
       {kdc::Point(-4.0, 0.0), kdc::Point(6.0, 0.0)},
       {kdc::Point(-3.0, 0.0), kdc::Point(7.0, 0.0)}},
      {kdc::Point(-10.0, 0.0), kdc::Point(10.0, 0.0)});
  auto mock = std::make_unique<kdc::MockILPSolver>();
  kdc::IPStaticSolver ip(mock.get());
  const auto result =
      kdc::MinSumSolver::solve(instance, ip, kdc::MinSumSolver::Config{});
  auto solution = result.solution;
  const double before = solution.total_integral();

  kdc::apply_integral_handovers(instance, solution);

  REQUIRE(solution.total_integral() <= before + 1e-9);
  REQUIRE(solution.is_well_formed());
}

TEST_CASE("apply_integral_handovers: preserves well-formedness") {
  const auto instance = kdc::test::make_dummy_instance(20, 5, 7U);
  kdc::NNStaticSolver nn;
  const auto result =
      kdc::MinSumSolver::solve(instance, nn, kdc::MinSumSolver::Config{});
  auto solution = result.solution;

  kdc::apply_integral_handovers(instance, solution);

  REQUIRE(solution.is_well_formed());
}

TEST_CASE("apply_integral_handovers: is idempotent") {
  const auto instance = kdc::test::make_dummy_instance(10, 3, 1U);
  kdc::NNStaticSolver nn;
  const auto result =
      kdc::MinSumSolver::solve(instance, nn, kdc::MinSumSolver::Config{});
  auto solution = result.solution;
  kdc::apply_integral_handovers(instance, solution);
  const double after_first = solution.total_integral();

  kdc::apply_integral_handovers(instance, solution);

  REQUIRE(std::abs(solution.total_integral() - after_first) < 1e-9);
  REQUIRE(solution.is_well_formed());
}

TEST_CASE("apply_integral_handovers: result passes verification") {
  const auto instance = kdc::test::make_dummy_instance(15, 4, 3U);
  kdc::NNStaticSolver nn;
  const auto result =
      kdc::MinSumSolver::solve(instance, nn, kdc::MinSumSolver::Config{});
  auto solution = result.solution;
  kdc::apply_integral_handovers(instance, solution);

  const auto report = kdc::Verifier::verify(instance, solution, 100, 1e-6);
  REQUIRE(report.all_ok());
}
