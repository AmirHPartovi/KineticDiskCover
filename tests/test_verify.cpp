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

kdc::Instance make_piecewise_verify_instance() {
  kdc::Instance instance;
  instance.n = 1;
  instance.m = 1;
  instance.T_end = 1.0;
  instance.trajectories.emplace_back(
      std::vector<kdc::Value>{0.0, 0.5, 1.0},
      std::vector<kdc::Point>{kdc::Point(0.0, 0.0),
                              kdc::Point(1.0, 0.0),
                              kdc::Point(0.0, 0.0)});
  instance.stations.push_back({0, kdc::Point(0.0, 0.0)});
  return instance;
}
}

TEST_CASE("Verifier checks kinetic solutions") {
  const auto instance = make_verify_instance();

  SECTION("valid") {
    const auto solution = make_verify_solution(instance);
    const auto report = kdc::Verifier::verify(instance, solution);
    REQUIRE(report.kind == kdc::VerificationKind::CERTIFIED_CONTINUOUS);
    REQUIRE(report.certified_continuous_verification);
    REQUIRE_FALSE(report.empirical_verification);
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

TEST_CASE("continuous verification detects violations between samples") {
  const auto instance = kdc::test::make_instance_linear(
      {{kdc::Point(0.0001, 0.0), kdc::Point(0.0001, 0.0)},
       {kdc::Point(-0.505, 0.0), kdc::Point(0.495, 0.0)}},
      {{0.0, 0.0}});
  kdc::KineticSolution solution;
  kdc::SolutionInterval interval;
  interval.t_start = 0.0;
  interval.t_end = 1.0;
  interval.supporting_point = {1};
  interval.assigned_points = {0, 0};
  kdc::KineticSolution::compute_quadratic_coeffs(
      instance, interval.supporting_point, interval);
  solution.intervals.push_back(interval);

  const auto empirical =
      kdc::Verifier::verify_empirical(instance, solution, 100, 1e-6);
  const auto continuous =
      kdc::Verifier::verify_continuous(instance, solution, 1e-6);
  REQUIRE(empirical.kind == kdc::VerificationKind::EMPIRICAL);
  REQUIRE(empirical.empirical_verification);
  REQUIRE(empirical.coverage_ok);
  REQUIRE(continuous.kind == kdc::VerificationKind::CERTIFIED_CONTINUOUS);
  REQUIRE(continuous.certified_continuous_verification);
  REQUIRE_FALSE(continuous.coverage_ok);
}

TEST_CASE("continuous verification splits at trajectory breakpoints") {
  const auto instance = make_piecewise_verify_instance();
  kdc::KineticSolution solution;
  for (const auto [start, end] :
       {std::pair<double, double>{0.0, 0.5}, {0.5, 1.0}}) {
    kdc::SolutionInterval interval;
    interval.t_start = start;
    interval.t_end = end;
    interval.supporting_point = {0};
    interval.assigned_points = {0};
    kdc::KineticSolution::compute_quadratic_coeffs(
        instance, interval.supporting_point, interval);
    solution.intervals.push_back(interval);
  }
  const auto report = kdc::Verifier::verify_continuous(instance, solution);
  REQUIRE(report.kind == kdc::VerificationKind::CERTIFIED_CONTINUOUS);
  REQUIRE(report.all_ok());
}
