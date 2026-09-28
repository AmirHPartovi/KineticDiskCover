#include "test_utils.hpp"

#include "kdc/solution.hpp"
#include "kdc/stationary.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <stdexcept>
#include <vector>

namespace {
kdc::Instance moving_point_instance() {
  return kdc::test::make_instance_linear(
      {{kdc::Point(0.0, 0.0), kdc::Point(1.0, 1.0)}},
      {{0.0, 0.0}});
}

kdc::KineticSolution make_moving_point_solution() {
  const auto instance = moving_point_instance();
  kdc::SolutionInterval interval;
  interval.t_start = 0.0;
  interval.t_end = 1.0;
  interval.assigned_points = {0};
  kdc::KineticSolution::compute_quadratic_coeffs(instance, {0}, interval);
  kdc::KineticSolution solution;
  solution.intervals.push_back(std::move(interval));
  return solution;
}

kdc::KineticSolution make_polynomial_solution(double a, double b, double c) {
  kdc::KineticSolution solution;
  kdc::SolutionInterval interval;
  interval.t_start = 0.0;
  interval.t_end = 1.0;
  interval.supporting_point = {0};
  interval.assigned_points = {0};
  interval.a = a;
  interval.b = b;
  interval.c = c;
  solution.intervals.push_back(std::move(interval));
  return solution;
}
}

TEST_CASE("KineticSolution computes and evaluates quadratic costs") {
  const auto instance = moving_point_instance();

  SECTION("quadratic coeffs") {
    kdc::SolutionInterval interval;
    interval.t_start = 0.0;
    interval.t_end = 1.0;
    kdc::KineticSolution::compute_quadratic_coeffs(instance, {0}, interval);
    REQUIRE(kdc::test::near(interval.a, 2.0 * std::acos(-1.0)));
    REQUIRE(kdc::test::near(interval.b, 0.0));
    REQUIRE(kdc::test::near(interval.c, 0.0));
  }

  const auto solution = make_moving_point_solution();

  SECTION("integral") {
    REQUIRE(kdc::test::near(solution.integral_on(0.0, 1.0),
                            2.0 * std::acos(-1.0) / 3.0));
    REQUIRE(kdc::test::near(solution.total_integral(),
                            2.0 * std::acos(-1.0) / 3.0));
  }

  SECTION("peak") {
    REQUIRE(kdc::test::near(solution.cost_at(1.0),
                            2.0 * std::acos(-1.0)));
    REQUIRE(kdc::test::near(solution.peak_cost(),
                            2.0 * std::acos(-1.0)));
    REQUIRE(kdc::test::near(solution.peak_time(), 1.0));
  }

  SECTION("well formed") {
    REQUIRE(solution.is_well_formed());
    auto malformed = solution;
    malformed.intervals.front().t_end = 0.0;
    REQUIRE_FALSE(malformed.is_well_formed());
  }

  SECTION("cost_at outside") {
    REQUIRE_THROWS_AS(solution.cost_at(-0.1), std::out_of_range);
    REQUIRE_THROWS_AS(solution.cost_at(1.1), std::out_of_range);
  }

  SECTION("numeric vs analytic") {
    constexpr int sample_count = 10000;
    const double step = 1.0 / sample_count;
    double numeric_integral = 0.0;
    for (int index = 0; index < sample_count; ++index) {
      const double time = (static_cast<double>(index) + 0.5) * step;
      numeric_integral += solution.cost_at(time) * step;
    }
    REQUIRE(std::abs(numeric_integral - solution.total_integral()) < 1e-6);
  }

  SECTION("extend well formed") {
    const auto fixed_instance = kdc::test::make_instance_linear(
        {{kdc::Point(1.0, 0.0), kdc::Point(1.0, 0.0)},
         {kdc::Point(2.0, 0.0), kdc::Point(2.0, 0.0)}},
        {{0.0, 0.0}});
    const auto assignment =
        kdc::StationarySolver::solve_nn(fixed_instance, 0.0);
    const auto extended = kdc::KineticSolution::extend(
        fixed_instance, assignment, 0.0, 1.0, true, false,
        kdc::ObjectiveType::MIN_MAX);
    REQUIRE(extended.is_well_formed());
    REQUIRE(extended.intervals.front().t_start == 0.0);
    REQUIRE(extended.intervals.back().t_end == 1.0);
  }

  SECTION("extend splits at trajectory breakpoints") {
    auto piecewise = kdc::test::make_instance_linear(
        {{kdc::Point(0.0, 0.0), kdc::Point(0.0, 0.0)}},
        {{0.0, 0.0}});
    piecewise.trajectories.front() = kdc::Trajectory(
        {0.0, 0.5, 1.0},
        {kdc::Point(0.0, 0.0), kdc::Point(1.0, 0.0),
         kdc::Point(0.0, 0.0)});
    const auto assignment =
        kdc::StationarySolver::solve_nn(piecewise, 0.0);
    const auto extended = kdc::KineticSolution::extend(
        piecewise, assignment, 0.0, 1.0, true, false,
        kdc::ObjectiveType::MIN_MAX);
    REQUIRE(extended.is_well_formed());
    REQUIRE(extended.intervals.size() == 2U);
    REQUIRE(kdc::test::near(extended.cost_at(0.75),
                            std::acos(-1.0) * 0.5 * 0.5));
  }

  SECTION("combine idempotent") {
    const auto combined = kdc::KineticSolution::combine(
        solution, solution, kdc::ObjectiveType::MIN_MAX);
    REQUIRE(kdc::test::solutions_equal(combined, solution));
    const auto combined_sum = kdc::KineticSolution::combine(
        solution, solution, kdc::ObjectiveType::MIN_SUM);
    REQUIRE(kdc::test::solutions_equal(combined_sum, solution));
  }

  SECTION("combine minmax lowers peak") {
    const auto first = make_polynomial_solution(1.0, 0.0, 0.0);
    const auto second = make_polynomial_solution(1.0, -2.0, 1.0);
    const auto combined = kdc::KineticSolution::combine(
        first, second, kdc::ObjectiveType::MIN_MAX);
    REQUIRE(combined.is_well_formed());
    REQUIRE(combined.peak_cost() <=
            std::min(first.peak_cost(), second.peak_cost()) + 1e-9);
  }

  SECTION("combine minsum lowers integral") {
    const auto first = make_polynomial_solution(1.0, 0.0, 0.0);
    const auto second = make_polynomial_solution(1.0, -2.0, 1.0);
    const auto combined = kdc::KineticSolution::combine(
        first, second, kdc::ObjectiveType::MIN_SUM);
    REQUIRE(combined.is_well_formed());
    REQUIRE(combined.total_integral() <=
            std::min(first.total_integral(), second.total_integral()) + 1e-9);
  }

  SECTION("partial_extend") {
    const auto new_solution = make_polynomial_solution(0.0, 4.0, 0.0);
    const auto current_solution = make_polynomial_solution(0.0, 0.0, 1.0);
    const auto partial = kdc::KineticSolution::partial_extend(
        new_solution, current_solution, kdc::ObjectiveType::MIN_MAX);
    REQUIRE(partial.is_well_formed());
    REQUIRE(partial.intervals.back().t_end < 1.0);
    REQUIRE(kdc::test::near(partial.intervals.back().t_end, 0.5, 1e-8));
  }
}
