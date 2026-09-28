#include "test_utils.hpp"

#include "kdc/kont_solver.hpp"
#include "kdc/minmax.hpp"
#include "kdc/minsum.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>

namespace {
kdc::Instance make_minsum_instance() {
  return kdc::test::make_instance_linear(
      {{kdc::Point(0.0, 0.0), kdc::Point(0.0, 0.0)},
       {kdc::Point(1.0, 0.0), kdc::Point(1.0, 0.0)},
       {kdc::Point(2.0, 0.0), kdc::Point(2.0, 0.0)},
       {kdc::Point(4.0, 0.0), kdc::Point(4.0, 0.0)},
       {kdc::Point(5.0, 0.0), kdc::Point(5.0, 0.0)}},
      {{0.0, 0.0}, {3.0, 0.0}, {6.0, 0.0}});
}

kdc::MinSumSolver::Result solve_minsum(const kdc::Instance& instance) {
  kdc::KontSolver solver;
  kdc::MinSumSolver::Config config;
  config.lb_num_samples = 4;
  config.time_limit_per_ip = 10.0;
  config.gap_target = 0.01;
  return kdc::MinSumSolver::solve(instance, solver, config);
}
}

TEST_CASE("MinSum solver produces a verified kinetic solution") {
  const auto instance = make_minsum_instance();
  const auto result = solve_minsum(instance);

  SECTION("small") {
    REQUIRE(result.verified);
    REQUIRE(result.solution.is_well_formed());
    REQUIRE(result.num_ip_solves >= 1);
    REQUIRE(result.num_iterations >= 1);
  }

  SECTION("int <= minmax int") {
    kdc::KontSolver solver;
    kdc::MinMaxSolver::Config config;
    config.time_limit_per_ip = 10.0;
    const auto minmax = kdc::MinMaxSolver::solve(instance, solver, config);
    REQUIRE(result.total_integral <= minmax.solution.total_integral() + 1e-6);
  }

  SECTION("lb valid") {
    REQUIRE(result.lower_bound_integral <= result.total_integral + 1e-6);
  }

  SECTION("gap nonincreasing") {
    REQUIRE_FALSE(result.gap_trace.empty());
    for (kdc::Index index = 1; index < result.gap_trace.size(); ++index) {
      REQUIRE(result.gap_trace[index] <= result.gap_trace[index - 1U] + 1e-9);
    }
    REQUIRE(result.gap <= result.gap_trace.front() + 1e-9);
  }
}

TEST_CASE("MinSum solver verifies a moving instance") {
  const auto instance = kdc::test::make_dummy_instance(5, 3, 271U);
  const auto result = solve_minsum(instance);
  REQUIRE(result.verified);
  REQUIRE(result.solution.is_well_formed());
  REQUIRE(result.lower_bound_integral <= result.total_integral + 1e-6);
  for (kdc::Index index = 1; index < result.gap_trace.size(); ++index) {
    REQUIRE(result.gap_trace[index] <= result.gap_trace[index - 1U] + 1e-9);
  }
}
