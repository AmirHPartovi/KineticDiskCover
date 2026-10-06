#include "test_utils.hpp"

#include "kdc/kont_solver.hpp"
#include "kdc/minmax.hpp"
#include "kdc/stationary.hpp"
#include "kdc/verify.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <numeric>
#include <vector>

namespace {
kdc::Instance make_minmax_instance() {
  return kdc::test::make_instance_linear(
      {{kdc::Point(0.0, 0.0), kdc::Point(0.0, 0.0)},
       {kdc::Point(1.0, 0.0), kdc::Point(1.0, 0.0)},
       {kdc::Point(2.0, 0.0), kdc::Point(2.0, 0.0)},
       {kdc::Point(4.0, 0.0), kdc::Point(4.0, 0.0)},
       {kdc::Point(5.0, 0.0), kdc::Point(5.0, 0.0)}},
      {{0.0, 0.0}, {3.0, 0.0}, {6.0, 0.0}});
}

kdc::MinMaxSolver::Result solve_minmax(const kdc::Instance& instance) {
  kdc::KontSolver solver;
  kdc::MinMaxSolver::Config config;
  config.time_limit_per_ip = 10.0;
  config.gap_target = 0.01;
  return kdc::MinMaxSolver::solve(instance, solver, config);
}
}

TEST_CASE("MinMax solver produces a verified kinetic solution") {
  const auto instance = make_minmax_instance();
  const auto result = solve_minmax(instance);

  SECTION("small") {
    REQUIRE(result.verified);
    REQUIRE(result.verification_kind ==
            kdc::VerificationKind::CERTIFIED_CONTINUOUS);
    REQUIRE(result.verification_time_sec >= 0.0);
    REQUIRE(result.solution.is_well_formed());
    REQUIRE(result.num_ip_solves >= 1);
    REQUIRE(result.num_iterations >= 1);
  }

  SECTION("peak <= nn peak") {
    const auto nearest = kdc::StationarySolver::solve_nn(instance, 0.0);
    const double nearest_peak =
        std::acos(-1.0) *
        std::inner_product(nearest.radius.begin(), nearest.radius.end(),
                           nearest.radius.begin(), 0.0);
    REQUIRE(result.peak_cost <= nearest_peak + 1e-6);
  }

  SECTION("lb valid") {
    REQUIRE(result.lower_bound <= result.peak_cost + 1e-6);
  }

  SECTION("gap nonincreasing") {
    REQUIRE_FALSE(result.gap_trace.empty());
    REQUIRE(std::is_sorted(result.gap_trace.rbegin(),
                           result.gap_trace.rend()));
    REQUIRE(result.gap <= result.gap_trace.front() + 1e-9);
  }
}

TEST_CASE("MinMax solver verifies a moving instance") {
  const auto instance = kdc::test::make_dummy_instance(5, 3, 173U);
  const auto result = solve_minmax(instance);
  REQUIRE(result.verified);
  REQUIRE(result.solution.is_well_formed());
  REQUIRE(result.lower_bound <= result.peak_cost + 1e-6);
}

TEST_CASE("MinMax solver updates the bound at a later peak time") {
  const auto instance = kdc::test::make_instance_linear(
      {{kdc::Point(0.0, 0.0), kdc::Point(10.0, 0.0)},
       {kdc::Point(1.0, 0.0), kdc::Point(11.0, 0.0)},
       {kdc::Point(2.0, 0.0), kdc::Point(12.0, 0.0)},
       {kdc::Point(4.0, 0.0), kdc::Point(14.0, 0.0)},
       {kdc::Point(5.0, 0.0), kdc::Point(15.0, 0.0)}},
      {{0.0, 0.0}, {3.0, 0.0}, {6.0, 0.0}});
  const auto result = solve_minmax(instance);
  REQUIRE(result.verified);
  REQUIRE(result.num_ip_solves >= 2);
  REQUIRE(result.gap <= result.gap_trace.front() + 1e-9);
  for (kdc::Index index = 1; index < result.gap_trace.size(); ++index) {
    REQUIRE(result.gap_trace[index] <= result.gap_trace[index - 1U] + 1e-9);
  }
}

TEST_CASE("MinMax exact refinement lowers a moving point's global peak") {
  const auto instance = kdc::test::make_instance_linear(
      {{kdc::Point(0.0, 0.0), kdc::Point(10.0, 0.0)}},
      {{0.0, 0.0}, {10.0, 0.0}});
  const auto initial_assignment =
      kdc::StationarySolver::solve_nn(instance, 0.0);
  const auto initial_solution = kdc::KineticSolution::extend(
      instance, initial_assignment, 0.0, instance.T_end, true, false,
      kdc::ObjectiveType::MIN_MAX);
  const double initial_peak = initial_solution.peak_cost();

  kdc::KontSolver solver;
  kdc::MinMaxSolver::Config config;
  config.time_limit_per_ip = 10.0;
  config.gap_target = 0.0;
  config.use_handovers = false;
  const auto result = kdc::MinMaxSolver::solve(instance, solver, config);

  REQUIRE(result.verified);
  REQUIRE(result.peak_consistent);
  REQUIRE(result.solution.is_well_formed());
  REQUIRE(kdc::test::near(result.initial_peak_cost, initial_peak));
  REQUIRE(result.peak_cost < initial_peak);
  REQUIRE(result.peak_cost < result.initial_peak_cost);
  REQUIRE_FALSE(result.trace.empty());
  REQUIRE(result.trace.front().static_solver_status ==
          kdc::optimality_status_to_string(kdc::OptimalityStatus::OPTIMAL));
  REQUIRE(kdc::test::near(result.trace.front().static_cost_at_peak_time, 0.0));
  REQUIRE(result.trace.front().combined_peak < initial_peak);
}
