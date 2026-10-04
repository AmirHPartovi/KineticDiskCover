#include "test_utils.hpp"

#include "kdc/solution.hpp"
#include "kdc/stationary.hpp"
#include "kdc/verify.hpp"

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

kdc::KineticSolution make_polynomial_solution(double a, double b, double c,
                                             int support = 0, int owner = 0) {
  kdc::KineticSolution solution;
  kdc::SolutionInterval interval;
  interval.t_start = 0.0;
  interval.t_end = 1.0;
  interval.supporting_point = {support};
  interval.assigned_points = {owner};
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

  SECTION("handover transfers the source support to a covering station") {
    const auto instance = kdc::test::make_instance_linear(
        {{kdc::Point(2.0, 0.0), kdc::Point(2.0, 0.0)},
         {kdc::Point(1.0, 0.0), kdc::Point(1.0, 0.0)},
         {kdc::Point(7.0, 0.0), kdc::Point(1.0, 0.0)}},
        {{0.0, 0.0}, {10.0, 0.0}});
    const kdc::StaticAssignment assignment{
        {0, 2}, {2.0, 3.0}, 13.0 * std::acos(-1.0), true, {0, 0, 1}};

    const auto extended = kdc::KineticSolution::extend(
        instance, assignment, 0.0, 1.0, true, true,
        kdc::ObjectiveType::MIN_SUM);

    REQUIRE(extended.is_well_formed());
    REQUIRE(extended.intervals.size() == 2U);
    const double handover_time = 5.0 / 6.0;
    constexpr double time_offset = 1e-7;
    REQUIRE(kdc::test::near(extended.intervals[0].t_end, handover_time));
    REQUIRE(kdc::test::near(extended.intervals[1].t_start, handover_time));
    REQUIRE(extended.intervals[0].assigned_points ==
            std::vector<int>{0, 0, 1});
    REQUIRE(extended.intervals[1].assigned_points ==
            std::vector<int>{1, 0, 1});
    REQUIRE(kdc::Verifier::check_coverage(instance, extended,
                                           handover_time - time_offset, 1e-8));
    REQUIRE(kdc::Verifier::check_coverage(instance, extended, handover_time,
                                           1e-8));
    REQUIRE(kdc::Verifier::check_coverage(instance, extended,
                                           handover_time + time_offset, 1e-8));
    REQUIRE(extended.intervals[0].supporting_point ==
            std::vector<int>{0, 2});
    REQUIRE(extended.intervals[1].supporting_point ==
            std::vector<int>{1, 2});
    const double pi = std::acos(-1.0);
    const double before_area =
        (extended.intervals[0].a * handover_time +
         extended.intervals[0].b) *
            handover_time +
        extended.intervals[0].c;
    const double after_area =
        (extended.intervals[1].a * handover_time +
         extended.intervals[1].b) *
            handover_time +
        extended.intervals[1].c;
    REQUIRE(kdc::test::near(before_area, 68.0 * pi));
    REQUIRE(kdc::test::near(after_area, 65.0 * pi));
    REQUIRE(after_area < before_area);
    REQUIRE(kdc::test::near(
        (instance.trajectories[2].position(handover_time) -
         instance.stations[1].pos)
            .norm(),
        8.0));
    REQUIRE(kdc::Verifier::verify_continuous(instance, extended).all_ok());
  }

  SECTION("already-covered support transfers before the first interval") {
    const auto instance = kdc::test::make_instance_linear(
        {{kdc::Point(8.0, 0.0), kdc::Point(8.0, 0.0)},
         {kdc::Point(1.0, 0.0), kdc::Point(1.0, 0.0)},
         {kdc::Point(4.0, 0.0), kdc::Point(4.0, 0.0)}},
        {{0.0, 0.0}, {10.0, 0.0}});
    const kdc::StaticAssignment assignment{
        {0, 2}, {8.0, 6.0}, 100.0 * std::acos(-1.0), true, {0, 0, 1}};

    const auto extended = kdc::KineticSolution::extend(
        instance, assignment, 0.0, 1.0, true, true,
        kdc::ObjectiveType::MIN_SUM);

    REQUIRE(extended.is_well_formed());
    REQUIRE(extended.intervals.front().assigned_points ==
            std::vector<int>{1, 0, 1});
    REQUIRE(extended.intervals.front().supporting_point ==
            std::vector<int>{1, 2});
    REQUIRE(kdc::Verifier::verify_continuous(instance, extended).all_ok());
  }

  SECTION("handover at the exact extension start is not deferred") {
    const auto instance = kdc::test::make_instance_linear(
        {{kdc::Point(2.0, 0.0), kdc::Point(2.0, 0.0)},
         {kdc::Point(1.0, 0.0), kdc::Point(1.0, 0.0)},
         {kdc::Point(7.0, 0.0), kdc::Point(1.0, 0.0)}},
        {{0.0, 0.0}, {10.0, 0.0}});
    const kdc::StaticAssignment assignment{
        {0, 2}, {2.0, 8.0}, 68.0 * std::acos(-1.0), true, {0, 0, 1}};
    const double handover_time = 5.0 / 6.0;

    const auto extended = kdc::KineticSolution::extend(
        instance, assignment, handover_time, 1.0, true, true,
        kdc::ObjectiveType::MIN_SUM);

    REQUIRE(extended.is_well_formed());
    REQUIRE(extended.intervals.size() == 1U);
    REQUIRE(extended.intervals.front().assigned_points ==
            std::vector<int>{1, 0, 1});
    REQUIRE(extended.intervals.front().supporting_point ==
            std::vector<int>{1, 2});
    REQUIRE(kdc::Verifier::verify_continuous(instance, extended).all_ok());
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

  SECTION("MinSum combination uses the pointwise lower envelope") {
    const auto first = make_polynomial_solution(0.0, 0.0, 2.0);
    const auto second = make_polynomial_solution(8.0, -8.0, 3.0, 1, 1);
    const auto combined = kdc::KineticSolution::combine(
        first, second, kdc::ObjectiveType::MIN_SUM);
    const double first_crossing = (1.0 - std::sqrt(0.5)) / 2.0;
    const double second_crossing = (1.0 + std::sqrt(0.5)) / 2.0;

    REQUIRE(combined.is_well_formed());
    REQUIRE(combined.intervals.size() == 3U);
    REQUIRE(kdc::test::near(combined.intervals[0].t_end, first_crossing));
    REQUIRE(kdc::test::near(combined.intervals[1].t_start, first_crossing));
    REQUIRE(kdc::test::near(combined.intervals[1].t_end, second_crossing));
    REQUIRE(kdc::test::near(combined.intervals[2].t_start, second_crossing));
    REQUIRE(combined.intervals[0].supporting_point ==
            first.intervals[0].supporting_point);
    REQUIRE(combined.intervals[1].supporting_point ==
            second.intervals[0].supporting_point);
    REQUIRE(combined.intervals[0].assigned_points ==
            first.intervals[0].assigned_points);
    REQUIRE(combined.intervals[1].assigned_points ==
            second.intervals[0].assigned_points);
    REQUIRE(combined.intervals[2].assigned_points ==
            first.intervals[0].assigned_points);
    REQUIRE(combined.cost_at(0.5) < combined.cost_at(0.05));
    REQUIRE(combined.cost_at(0.5) < first.cost_at(0.5));

    constexpr int sample_count = 10000;
    const double step = 1.0 / sample_count;
    double numerical_integral = 0.0;
    for (int sample = 0; sample < sample_count; ++sample) {
      const double time = (sample + 0.5) * step;
      numerical_integral +=
          std::min(first.cost_at(time), second.cost_at(time)) * step;
    }
    REQUIRE(kdc::test::near(combined.total_integral(), numerical_integral,
                            1e-6));
    REQUIRE(combined.total_integral() < first.total_integral());
    REQUIRE(combined.total_integral() < second.total_integral());
  }

  SECTION("MinSum switches to a locally better candidate despite its integral") {
    const auto first = make_polynomial_solution(0.0, 0.0, 1.0);
    const auto second = make_polynomial_solution(10.0, -10.0, 2.9, 1, 1);
    REQUIRE(first.total_integral() < second.total_integral());

    const auto partial = kdc::KineticSolution::partial_extend(
        second, first, kdc::ObjectiveType::MIN_SUM);
    const auto combined = kdc::KineticSolution::combine(
        first, partial, kdc::ObjectiveType::MIN_SUM);

    REQUIRE(partial.intervals.size() == second.intervals.size());
    REQUIRE(combined.intervals.size() == 3U);
    REQUIRE(combined.cost_at(0.5) == second.cost_at(0.5));
    REQUIRE(combined.total_integral() < first.total_integral());
    REQUIRE(combined.total_integral() < second.total_integral());
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
