#include "test_utils.hpp"

#include "kdc/solution.hpp"
#include "kdc/verify.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cmath>

namespace {
kdc::Instance make_verify_instance() {
  return kdc::test::make_instance_linear(
      {{kdc::Point(0.0, 0.0), kdc::Point(1.0, 0.0)}},
      {{0.0, 0.0}});
}

kdc::KineticSolution make_verify_solution(const kdc::Instance& instance) {
  kdc::KineticSolution solution;
  kdc::SolutionInterval interval;
  interval.t_start = 0.0;
  interval.t_end = 1.0;
  interval.assigned_points = {0};
  kdc::KineticSolution::compute_quadratic_coeffs(instance, {0}, interval);
  solution.intervals.push_back(interval);
  return solution;
}
}

TEST_CASE("Verifier checks kinetic solutions") {
  const auto instance = make_verify_instance();

  SECTION("valid") {
    const auto solution = make_verify_solution(instance);
    const auto report = kdc::Verifier::verify(instance, solution);
    REQUIRE(report.all_ok());
    REQUIRE(report.coverage_ok);
    REQUIRE(report.supporting_points_ok);
    REQUIRE(report.cost_consistent_ok);
    REQUIRE(report.integral_consistent_ok);
    REQUIRE(report.assignment_consistent_ok);
    REQUIRE(report.max_coverage_violation == 0.0);
  }

  SECTION("invalid coverage") {
    auto solution = make_verify_solution(instance);
    solution.intervals.front().supporting_point.front() = -1;
    kdc::KineticSolution::compute_quadratic_coeffs(
        instance, solution.intervals.front().supporting_point,
        solution.intervals.front());
    const auto report = kdc::Verifier::verify(instance, solution);
    REQUIRE_FALSE(report.coverage_ok);
    REQUIRE(report.max_coverage_violation > 0.0);
    double violation = 0.0;
    REQUIRE_FALSE(kdc::Verifier::check_coverage(instance, solution, 0.5,
                                                1e-6, &violation));
    REQUIRE(violation > 0.0);
  }

  SECTION("invalid integral") {
    auto solution = make_verify_solution(instance);
    solution.intervals.front().c += 1.0;
    const auto report = kdc::Verifier::verify(instance, solution);
    REQUIRE_FALSE(report.integral_consistent_ok);
    REQUIRE_FALSE(report.cost_consistent_ok);
  }

  SECTION("empty solution") {
    const kdc::KineticSolution solution;
    const auto report = kdc::Verifier::verify(instance, solution);
    REQUIRE_FALSE(report.coverage_ok);
    REQUIRE_FALSE(report.all_ok());
  }
}
