#include "test_utils.hpp"

#include "kdc/solution.hpp"
#include "kdc/stationary.hpp"
#include "kdc/verify.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <random>
#include <stdexcept>
#include <string>
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

kdc::KineticSolution make_piecewise_constant_solution(
    const std::vector<std::pair<double, double>>& intervals) {
  kdc::KineticSolution solution;
  for (const auto& [end, cost] : intervals) {
    kdc::SolutionInterval interval;
    interval.t_start = solution.intervals.empty()
                           ? 0.0
                           : solution.intervals.back().t_end;
    interval.t_end = end;
    interval.supporting_point = {0};
    interval.assigned_points = {0};
    interval.c = cost;
    solution.intervals.push_back(interval);
  }
  return solution;
}

void require_extension_invariants(const kdc::Instance& instance,
                                  const kdc::KineticSolution& solution) {
  REQUIRE(solution.is_well_formed());
  for (kdc::Index index = 0; index < solution.intervals.size(); ++index) {
    const auto& interval = solution.intervals[index];
    REQUIRE(interval.t_start < interval.t_end);
    REQUIRE(std::isfinite(interval.a));
    REQUIRE(std::isfinite(interval.b));
    REQUIRE(std::isfinite(interval.c));
    REQUIRE(interval.supporting_point.size() ==
            static_cast<kdc::Index>(instance.m));
    REQUIRE(interval.assigned_points.size() ==
            static_cast<kdc::Index>(instance.n));
    for (int station = 0; station < instance.m; ++station) {
      const int support =
          interval.supporting_point[static_cast<kdc::Index>(station)];
      if (support >= 0) {
        REQUIRE(support < instance.n);
        REQUIRE(interval.assigned_points[static_cast<kdc::Index>(support)] ==
                station);
      }
    }
    if (index == 0U) {
      continue;
    }
    const auto& previous = solution.intervals[index - 1U];
    REQUIRE(previous.t_end == interval.t_start);
    if (previous.assigned_points == interval.assigned_points) {
      const double time = interval.t_start;
      const double previous_cost =
          (previous.a * time + previous.b) * time + previous.c;
      const double next_cost =
          (interval.a * time + interval.b) * time + interval.c;
      REQUIRE(kdc::test::near(previous_cost, next_cost, 1e-8, 1e-8));
    }
  }
}

const kdc::SolutionInterval& solution_interval_at(
    const kdc::KineticSolution& solution, double time) {
  const auto interval = std::find_if(
      solution.intervals.begin(), solution.intervals.end(),
      [time](const auto& candidate) {
        return time >= candidate.t_start && time <= candidate.t_end;
      });
  if (interval == solution.intervals.end()) {
    throw std::out_of_range("time is not covered by the kinetic solution");
  }
  return *interval;
}

kdc::Instance randomized_piecewise_instance(std::uint32_t seed) {
  std::mt19937 generator(seed);
  std::uniform_real_distribution<double> coordinate(-3.0, 3.0);
  constexpr int point_count = 8;
  constexpr int station_count = 3;
  const std::vector<double> times{0.0, 0.17, 0.43, 0.76, 1.0};

  kdc::Instance instance;
  instance.id = static_cast<int>(seed);
  instance.name = "randomized-kinetic-emission";
  instance.n = point_count;
  instance.m = station_count;
  instance.T_end = 1.0;
  for (int station = 0; station < station_count; ++station) {
    instance.stations.push_back(
        {station, {static_cast<double>(station) * 5.0, 0.0}});
  }
  for (int point = 0; point < point_count; ++point) {
    std::vector<kdc::Point> waypoints;
    waypoints.reserve(times.size());
    for (kdc::Index index = 0; index < times.size(); ++index) {
      const double center = static_cast<double>(point % station_count) * 5.0;
      waypoints.emplace_back(center + coordinate(generator),
                             coordinate(generator));
    }
    instance.trajectories.emplace_back(times, std::move(waypoints));
  }
  return instance;
}

std::vector<kdc::KineticEventTraceEntry> selected_events(
    const kdc::KineticEventDiagnostics& diagnostics,
    kdc::KineticEventType type) {
  std::vector<kdc::KineticEventTraceEntry> events;
  for (const auto& event : diagnostics.trace) {
    if (event.type == type) {
      events.push_back(event);
    }
  }
  return events;
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

  SECTION("peak evaluates quadratic vertices and closed-interval endpoints") {
    const auto concave = make_polynomial_solution(-2.0, 2.0, 0.0);
    REQUIRE(kdc::test::near(concave.peak_cost(), 0.5));
    REQUIRE(kdc::test::near(concave.peak_time(), 0.5));

    const auto convex = make_polynomial_solution(2.0, -2.0, 0.0);
    REQUIRE(kdc::test::near(convex.peak_cost(), 0.0));
    REQUIRE(kdc::test::near(convex.peak_time(), 0.0));

    const auto increasing = make_polynomial_solution(0.0, 2.0, 1.0);
    REQUIRE(kdc::test::near(increasing.peak_cost(), 3.0));
    REQUIRE(kdc::test::near(increasing.peak_time(), 1.0));

    const auto constant = make_polynomial_solution(0.0, 0.0, 4.0);
    REQUIRE(kdc::test::near(constant.peak_cost(), 4.0));
    REQUIRE(kdc::test::near(constant.peak_time(), 0.0));

    const auto boundary_vertex = make_polynomial_solution(-1.0, 0.0, 0.0);
    REQUIRE(kdc::test::near(boundary_vertex.peak_cost(), 0.0));
    REQUIRE(kdc::test::near(boundary_vertex.peak_time(), 0.0));

    const auto small_curvature =
        make_polynomial_solution(-1e-16, 1e-16, 0.0);
    REQUIRE(kdc::test::near(small_curvature.peak_time(), 0.5));
  }

  SECTION("independent MinMax peak consistency check") {
    const auto concave = make_polynomial_solution(-2.0, 2.0, 0.0);
    REQUIRE(kdc::Verifier::check_peak_consistency(concave, 0.5, 0.5));
    REQUIRE_FALSE(kdc::Verifier::check_peak_consistency(concave, 0.4, 0.5));
    REQUIRE_FALSE(kdc::Verifier::check_peak_consistency(concave, 0.5, 0.0));
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
    kdc::KineticEventDiagnostics diagnostics;
    const auto extended = kdc::KineticSolution::extend(
        piecewise, assignment, 0.0, 1.0, true, false,
        kdc::ObjectiveType::MIN_MAX, nullptr,
        kdc::KineticEventEngine::REFERENCE_EXHAUSTIVE, &diagnostics);
    REQUIRE(extended.is_well_formed());
    REQUIRE(extended.intervals.size() == 2U);
    REQUIRE(diagnostics.solution_intervals_generated == 2U);
    REQUIRE(std::any_of(diagnostics.trace.begin(), diagnostics.trace.end(),
                        [](const auto& event) {
                          return event.type == kdc::KineticEventType::
                                                   ACTIVE_SUPPORT_MOTION_BREAKPOINT &&
                                 kdc::test::near(event.time, 0.5);
                        }));
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

  SECTION("combine handles no crossing and a touching root") {
    const auto lower = make_polynomial_solution(0.0, 0.0, 1.0);
    const auto higher = make_polynomial_solution(0.0, 0.0, 2.0);
    const auto no_crossing = kdc::KineticSolution::combine(
        lower, higher, kdc::ObjectiveType::MIN_MAX);
    REQUIRE(kdc::test::solutions_equal(no_crossing, lower));

    const auto tangent =
        make_polynomial_solution(1.0, -1.0, 0.25, 1, 1);
    const auto touching = make_polynomial_solution(0.0, 0.0, 0.25);
    const auto combined = kdc::KineticSolution::combine(
        tangent, touching, kdc::ObjectiveType::MIN_MAX);
    REQUIRE(combined.is_well_formed());
    REQUIRE(kdc::test::near(combined.cost_at(0.5), 0.0));
    REQUIRE(kdc::test::near(combined.cost_at(0.0), 0.25));
    REQUIRE(kdc::test::near(combined.cost_at(1.0), 0.25));
  }

  SECTION("peak_time finds global maxima across multiple intervals") {
    const auto multiple_peaks =
        make_piecewise_constant_solution({{0.25, 1.0}, {0.5, 0.0},
                                          {0.75, 1.0}, {1.0, 0.0}});
    REQUIRE(kdc::test::near(multiple_peaks.peak_cost(), 1.0));
    REQUIRE(kdc::test::near(multiple_peaks.peak_time(), 0.0));
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

  SECTION("partial_extend keeps the full MinMax candidate through the shared domain") {
    const auto new_solution = make_polynomial_solution(0.0, 4.0, 0.0);
    const auto current_solution = make_polynomial_solution(0.0, 0.0, 1.0);
    const auto partial = kdc::KineticSolution::partial_extend(
        new_solution, current_solution, kdc::ObjectiveType::MIN_MAX);
    REQUIRE(partial.is_well_formed());
    REQUIRE(kdc::test::near(partial.intervals.front().t_start, 0.0));
    REQUIRE(kdc::test::near(partial.intervals.back().t_end, 1.0));
    REQUIRE(kdc::test::near(partial.cost_at(0.75), new_solution.cost_at(0.75)));
  }

  SECTION("MinMax refinement retains a candidate with a worse integral") {
    const auto current =
        make_piecewise_constant_solution({{0.8, 5.0}, {1.0, 20.0}});
    const auto candidate =
        make_piecewise_constant_solution({{0.8, 30.0}, {1.0, 10.0}});

    REQUIRE(candidate.total_integral() > current.total_integral());
    REQUIRE(kdc::test::near(current.peak_cost(), 20.0));
    REQUIRE(kdc::test::near(candidate.peak_cost(), 30.0));

    const auto partial = kdc::KineticSolution::partial_extend(
        candidate, current, kdc::ObjectiveType::MIN_MAX);
    const auto combined = kdc::KineticSolution::combine(
        current, partial, kdc::ObjectiveType::MIN_MAX);

    REQUIRE(combined.is_well_formed());
    REQUIRE(kdc::test::near(combined.peak_cost(), 10.0));
    REQUIRE(combined.peak_cost() < current.peak_cost());
  }
}

TEST_CASE("Kinetic extension represents only relevant support events") {
  SECTION("inactive trajectory waypoints do not split emitted intervals") {
    auto instance = kdc::test::make_instance_linear(
        {{kdc::Point(10.0, 0.0), kdc::Point(10.0, 0.0)},
         {kdc::Point(1.0, 0.0), kdc::Point(1.0, 0.0)}},
        {{0.0, 0.0}});
    instance.trajectories[1] = kdc::Trajectory(
        {0.0, 0.25, 0.5, 0.75, 1.0},
        {kdc::Point(1.0, 0.0), kdc::Point(1.2, 0.2),
         kdc::Point(0.8, -0.1), kdc::Point(1.1, 0.1),
         kdc::Point(1.0, 0.0)});
    const kdc::StaticAssignment assignment{
        {0}, {10.0}, 100.0 * std::acos(-1.0), true, {0, 0}};

    const auto reference = kdc::KineticSolution::extend(
        instance, assignment, 0.0, 1.0, true, false,
        kdc::ObjectiveType::MIN_MAX, nullptr,
        kdc::KineticEventEngine::REFERENCE_EXHAUSTIVE, nullptr,
        kdc::KineticIntervalEmission::REFERENCE_ALL_TRAJECTORY_BREAKPOINTS);
    const auto optimized = kdc::KineticSolution::extend(
        instance, assignment, 0.0, 1.0, true, false,
        kdc::ObjectiveType::MIN_MAX, nullptr,
        kdc::KineticEventEngine::REFERENCE_EXHAUSTIVE, nullptr,
        kdc::KineticIntervalEmission::EXACT_RELEVANT_BOUNDARIES);

    require_extension_invariants(instance, reference);
    require_extension_invariants(instance, optimized);
    REQUIRE(reference.intervals.size() == 4U);
    REQUIRE(optimized.intervals.size() == 1U);
    REQUIRE(kdc::Verifier::verify_continuous(instance, optimized).all_ok());
    REQUIRE(kdc::test::near(reference.total_integral(),
                            optimized.total_integral()));
    REQUIRE(kdc::test::near(reference.peak_cost(), optimized.peak_cost()));
    for (int sample = 0; sample <= 100; ++sample) {
      const double time = static_cast<double>(sample) / 100.0;
      REQUIRE(kdc::test::near(reference.cost_at(time),
                              optimized.cost_at(time)));
    }
  }

  SECTION("one station and one support has no event") {
    const auto instance = kdc::test::make_instance_linear(
        {{kdc::Point(2.0, 0.0), kdc::Point(3.0, 1.0)}},
        {{0.0, 0.0}});
    const kdc::StaticAssignment assignment{
        {0}, {2.0}, 4.0 * std::acos(-1.0), true, {0}};
    kdc::KineticEventDiagnostics diagnostics;
    const auto solution = kdc::KineticSolution::extend(
        instance, assignment, 0.0, 1.0, true, false,
        kdc::ObjectiveType::MIN_MAX, nullptr,
        kdc::KineticEventEngine::REFERENCE_EXHAUSTIVE, &diagnostics);

    require_extension_invariants(instance, solution);
    REQUIRE(solution.intervals.size() == 1U);
    REQUIRE(diagnostics.point_vs_support_comparisons == 0U);
    REQUIRE(diagnostics.selected_support_events == 0U);
    REQUIRE(diagnostics.trace.empty());
  }

  SECTION("support changes exactly at a trajectory breakpoint") {
    auto instance = kdc::test::make_instance_linear(
        {{kdc::Point(1.0, 0.0), kdc::Point(1.0, 0.0)},
         {kdc::Point(0.0, 0.0), kdc::Point(2.0, 0.0)}},
        {{0.0, 0.0}});
    instance.trajectories[1] = kdc::Trajectory(
        {0.0, 0.5, 1.0},
        {kdc::Point(0.0, 0.0), kdc::Point(1.0, 0.0),
         kdc::Point(2.0, 0.0)});
    const kdc::StaticAssignment assignment{
        {0}, {1.0}, std::acos(-1.0), true, {0, 0}};
    kdc::KineticEventDiagnostics diagnostics;

    const auto solution = kdc::KineticSolution::extend(
        instance, assignment, 0.0, 1.0, true, false,
        kdc::ObjectiveType::MIN_MAX, nullptr,
        kdc::KineticEventEngine::REFERENCE_EXHAUSTIVE, &diagnostics);

    require_extension_invariants(instance, solution);
    REQUIRE(solution.intervals.size() == 2U);
    REQUIRE(kdc::test::near(solution.intervals[0].t_end, 0.5));
    REQUIRE(solution.intervals[0].supporting_point ==
            std::vector<int>{0});
    REQUIRE(solution.intervals[1].supporting_point ==
            std::vector<int>{1});
    REQUIRE(diagnostics.solution_intervals_generated == 2U);
    REQUIRE(diagnostics.selected_support_events == 1U);
    REQUIRE(std::any_of(diagnostics.trace.begin(), diagnostics.trace.end(),
                        [](const auto& event) {
                          return event.type ==
                                     kdc::KineticEventType::SUPPORT_CHANGE &&
                                 kdc::test::near(event.time, 0.5) &&
                                 event.old_support == 0 &&
                                 event.new_support == 1;
                        }));
  }

  SECTION("support change strictly inside a trajectory segment") {
    const auto instance = kdc::test::make_instance_linear(
        {{kdc::Point(1.0, 0.0), kdc::Point(1.0, 0.0)},
         {kdc::Point(0.0, 0.0), kdc::Point(2.0, 0.0)}},
        {{0.0, 0.0}});
    const kdc::StaticAssignment assignment{
        {0}, {1.0}, std::acos(-1.0), true, {0, 0}};
    const auto solution = kdc::KineticSolution::extend(
        instance, assignment, 0.0, 1.0, true, false,
        kdc::ObjectiveType::MIN_MAX);

    require_extension_invariants(instance, solution);
    REQUIRE(solution.intervals.size() == 2U);
    REQUIRE(kdc::test::near(solution.intervals.front().t_end, 0.5));
    REQUIRE(solution.intervals.front().supporting_point ==
            std::vector<int>{0});
    REQUIRE(solution.intervals.back().supporting_point ==
            std::vector<int>{1});
  }

  SECTION("a tangent equality does not change the support") {
    const auto instance = kdc::test::make_instance_linear(
        {{kdc::Point(1.0, 0.0), kdc::Point(1.0, 0.0)},
         {kdc::Point(1.0, -1.0), kdc::Point(1.0, 1.0)}},
        {{0.0, 0.0}});
    const kdc::StaticAssignment assignment{
        {1}, {std::sqrt(2.0)}, 2.0 * std::acos(-1.0), true, {0, 0}};
    kdc::KineticEventDiagnostics diagnostics;

    const auto solution = kdc::KineticSolution::extend(
        instance, assignment, 0.0, 1.0, true, false,
        kdc::ObjectiveType::MIN_MAX, nullptr,
        kdc::KineticEventEngine::REFERENCE_EXHAUSTIVE, &diagnostics);

    require_extension_invariants(instance, solution);
    REQUIRE(solution.intervals.size() == 1U);
    REQUIRE(solution.intervals.front().supporting_point ==
            std::vector<int>{1});
    REQUIRE(diagnostics.candidate_roots_rejected >= 1U);
    REQUIRE(diagnostics.selected_support_events == 0U);
  }

  SECTION("irrelevant trajectory waypoints are only internal scan breaks") {
    auto instance = kdc::test::make_instance_linear(
        {{kdc::Point(1.0, 0.0), kdc::Point(1.0, 0.0)},
         {kdc::Point(99.0, 0.0), kdc::Point(99.0, 0.0)},
         {kdc::Point(90.0, 0.0), kdc::Point(90.0, 0.0)}},
        {{0.0, 0.0}, {100.0, 0.0}});
    instance.trajectories[1] = kdc::Trajectory(
        {0.0, 0.3, 0.7, 1.0},
        {kdc::Point(99.0, 0.0), kdc::Point(99.0, 0.0),
         kdc::Point(99.0, 0.0), kdc::Point(99.0, 0.0)});
    const kdc::StaticAssignment assignment{
        {0, 2}, {1.0, 10.0}, 101.0 * std::acos(-1.0), true, {0, 1, 1}};
    kdc::KineticEventDiagnostics diagnostics;

    const auto solution = kdc::KineticSolution::extend(
        instance, assignment, 0.0, 1.0, true, false,
        kdc::ObjectiveType::MIN_MAX, nullptr,
        kdc::KineticEventEngine::REFERENCE_EXHAUSTIVE, &diagnostics);

    require_extension_invariants(instance, solution);
    REQUIRE(solution.intervals.size() == 1U);
    REQUIRE(solution.intervals.front().assigned_points ==
            std::vector<int>{0, 1, 1});
    REQUIRE(diagnostics.trajectory_segment_pair_examinations >= 6U);
    REQUIRE(diagnostics.solution_intervals_generated == 1U);
  }

  SECTION("simultaneous support changes are deterministic in both directions") {
    const auto instance = kdc::test::make_instance_linear(
        {{kdc::Point(2.0, 0.0), kdc::Point(1.0, 0.0)},
         {kdc::Point(1.0, 0.0), kdc::Point(2.0, 0.0)},
         {kdc::Point(8.0, 0.0), kdc::Point(9.0, 0.0)},
         {kdc::Point(9.0, 0.0), kdc::Point(8.0, 0.0)}},
        {{0.0, 0.0}, {10.0, 0.0}});
    const kdc::StaticAssignment forward_assignment{
        {0, 2}, {2.0, 2.0}, 8.0 * std::acos(-1.0), true, {0, 0, 1, 1}};
    kdc::KineticEventDiagnostics forward_diagnostics;
    const auto forward = kdc::KineticSolution::extend(
        instance, forward_assignment, 0.0, 1.0, true, false,
        kdc::ObjectiveType::MIN_MAX, nullptr,
        kdc::KineticEventEngine::REFERENCE_EXHAUSTIVE,
        &forward_diagnostics);

    require_extension_invariants(instance, forward);
    REQUIRE(forward.intervals.size() == 2U);
    REQUIRE(forward.intervals[0].supporting_point ==
            std::vector<int>{0, 2});
    REQUIRE(forward.intervals[1].supporting_point ==
            std::vector<int>{1, 3});
    REQUIRE(forward_diagnostics.selected_support_events == 2U);
    REQUIRE(forward_diagnostics.solution_intervals_generated == 2U);
    const double left_cost =
        (forward.intervals[0].a * 0.5 + forward.intervals[0].b) * 0.5 +
        forward.intervals[0].c;
    const double right_cost =
        (forward.intervals[1].a * 0.5 + forward.intervals[1].b) * 0.5 +
        forward.intervals[1].c;
    REQUIRE(kdc::test::near(left_cost, right_cost));

    const kdc::StaticAssignment backward_assignment{
        {1, 3}, {2.0, 2.0}, 8.0 * std::acos(-1.0), true, {0, 0, 1, 1}};
    kdc::KineticEventDiagnostics backward_diagnostics;
    const auto backward = kdc::KineticSolution::extend(
        instance, backward_assignment, 1.0, 0.0, false, false,
        kdc::ObjectiveType::MIN_MAX, nullptr,
        kdc::KineticEventEngine::REFERENCE_EXHAUSTIVE,
        &backward_diagnostics);

    require_extension_invariants(instance, backward);
    REQUIRE(backward.intervals.size() == 2U);
    REQUIRE(backward.intervals[0].supporting_point ==
            std::vector<int>{0, 2});
    REQUIRE(backward.intervals[1].supporting_point ==
            std::vector<int>{1, 3});
    REQUIRE(backward_diagnostics.selected_support_events == 2U);
  }

  SECTION("multiple equal candidates choose the lowest point id") {
    const auto instance = kdc::test::make_instance_linear(
        {{kdc::Point(1.0, 0.0), kdc::Point(1.0, 0.0)},
         {kdc::Point(0.0, 0.0), kdc::Point(2.0, 0.0)},
         {kdc::Point(0.0, 0.0), kdc::Point(2.0, 0.0)}},
        {{0.0, 0.0}});
    const kdc::StaticAssignment assignment{
        {0}, {1.0}, std::acos(-1.0), true, {0, 0, 0}};
    kdc::KineticEventDiagnostics diagnostics;
    const auto solution = kdc::KineticSolution::extend(
        instance, assignment, 0.0, 1.0, true, false,
        kdc::ObjectiveType::MIN_MAX, nullptr,
        kdc::KineticEventEngine::REFERENCE_EXHAUSTIVE, &diagnostics);

    require_extension_invariants(instance, solution);
    REQUIRE(solution.intervals.size() == 2U);
    REQUIRE(solution.intervals.back().supporting_point ==
            std::vector<int>{1});
    REQUIRE(diagnostics.selected_support_events == 1U);
    const auto transition = std::find_if(
        diagnostics.trace.begin(), diagnostics.trace.end(),
        [](const auto& event) {
          return event.type == kdc::KineticEventType::SUPPORT_CHANGE &&
                 kdc::test::near(event.time, 0.5);
        });
    REQUIRE(transition != diagnostics.trace.end());
    REQUIRE(transition->new_support == 1);
    REQUIRE(transition->tie_breaking_outcome.find("lowest point id") !=
            std::string::npos);
  }

  SECTION("zero-duration extension emits no degenerate interval") {
    const auto instance = kdc::test::make_instance_linear(
        {{kdc::Point(1.0, 0.0), kdc::Point(1.0, 0.0)}},
        {{0.0, 0.0}});
    const kdc::StaticAssignment assignment{
        {0}, {1.0}, std::acos(-1.0), true, {0}};
    kdc::KineticEventDiagnostics diagnostics;
    const auto solution = kdc::KineticSolution::extend(
        instance, assignment, 0.5, 0.5, true, false,
        kdc::ObjectiveType::MIN_MAX, nullptr,
        kdc::KineticEventEngine::REFERENCE_EXHAUSTIVE, &diagnostics);

    REQUIRE(solution.intervals.empty());
    REQUIRE_FALSE(solution.is_well_formed());
    REQUIRE(diagnostics.solution_intervals_generated == 0U);
  }
}

TEST_CASE("Support states use exact directional limits without time probes") {
  SECTION("a support change at 1e-12 is retained") {
    constexpr double event_time = 1e-12;
    const auto instance = kdc::test::make_instance_linear(
        {{kdc::Point(1.0, 0.0), kdc::Point(1.0, 0.0)},
         {kdc::Point(1.0 - event_time, 0.0),
          kdc::Point(2.0 - event_time, 0.0)}},
        {{0.0, 0.0}});
    const kdc::StaticAssignment assignment{
        {0}, {1.0}, std::acos(-1.0), true, {0, 0}};
    kdc::KineticEventDiagnostics diagnostics;

    const auto solution = kdc::KineticSolution::extend(
        instance, assignment, 0.0, 1.0, true, false,
        kdc::ObjectiveType::MIN_MAX, nullptr,
        kdc::KineticEventEngine::REFERENCE_EXHAUSTIVE, &diagnostics);

    require_extension_invariants(instance, solution);
    REQUIRE(solution.intervals.size() == 2U);
    REQUIRE(kdc::test::near(solution.intervals.front().t_end, event_time, 0.0,
                            2e-15));
    REQUIRE(solution.intervals.front().supporting_point ==
            std::vector<int>{0});
    REQUIRE(solution.intervals.back().supporting_point ==
            std::vector<int>{1});
  }

  SECTION("a support change at 1e-10 is not skipped by a 1e-8 probe scale") {
    constexpr double event_time = 1e-10;
    const auto instance = kdc::test::make_instance_linear(
        {{kdc::Point(1.0, 0.0), kdc::Point(1.0, 0.0)},
         {kdc::Point(1.0 - event_time, 0.0),
          kdc::Point(2.0 - event_time, 0.0)}},
        {{0.0, 0.0}});
    const kdc::StaticAssignment assignment{
        {0}, {1.0}, std::acos(-1.0), true, {0, 0}};

    const auto solution = kdc::KineticSolution::extend(
        instance, assignment, 0.0, 1.0, true, false,
        kdc::ObjectiveType::MIN_MAX);

    require_extension_invariants(instance, solution);
    REQUIRE(solution.intervals.size() == 2U);
    REQUIRE(kdc::test::near(solution.intervals.front().t_end, event_time, 0.0,
                            2e-14));
    REQUIRE(solution.intervals.back().supporting_point ==
            std::vector<int>{1});
  }

  SECTION("forward and backward limits resolve opposite sides of a crossing") {
    const auto instance = kdc::test::make_instance_linear(
        {{kdc::Point(1.0, 0.0), kdc::Point(1.0, 0.0)},
         {kdc::Point(0.0, 0.0), kdc::Point(2.0, 0.0)}},
        {{0.0, 0.0}});

    REQUIRE(kdc::KineticCore::resolve_support_at_time(instance, 0, {0, 1},
                                                      0.5) == 0);
    REQUIRE(kdc::KineticCore::resolve_support_directional_limit(
                instance, 0, {0, 1}, 0.5, true) == 1);
    REQUIRE(kdc::KineticCore::resolve_support_directional_limit(
                instance, 0, {0, 1}, 0.5, false) == 0);
  }

  SECTION("second-order terms resolve equal distances and first derivatives") {
    const auto instance = kdc::test::make_instance_linear(
        {{kdc::Point(1.0, 0.0), kdc::Point(1.0, 0.0)},
         {kdc::Point(1.0, -0.5), kdc::Point(1.0, 0.5)}},
        {{0.0, 0.0}});

    REQUIRE(kdc::KineticCore::compare_support_directional_limit(
                instance, 0, 1, 0, 0.5, true) > 0);
    REQUIRE(kdc::KineticCore::compare_support_directional_limit(
                instance, 0, 1, 0, 0.5, false) > 0);
  }

  SECTION("persistent equality uses the point-id tie and creates no events") {
    const auto instance = kdc::test::make_instance_linear(
        {{kdc::Point(1.0, 0.0), kdc::Point(1.0, 0.0)},
         {kdc::Point(1.0, 0.0), kdc::Point(1.0, 0.0)}},
        {{0.0, 0.0}});
    const kdc::StaticAssignment assignment{
        {0}, {1.0}, std::acos(-1.0), true, {0, 0}};
    kdc::KineticEventDiagnostics diagnostics;

    const auto solution = kdc::KineticSolution::extend(
        instance, assignment, 0.0, 1.0, true, false,
        kdc::ObjectiveType::MIN_MAX, nullptr,
        kdc::KineticEventEngine::REFERENCE_EXHAUSTIVE, &diagnostics);

    REQUIRE(solution.intervals.size() == 1U);
    REQUIRE(solution.intervals.front().supporting_point ==
            std::vector<int>{0});
    REQUIRE(diagnostics.selected_support_events == 0U);
  }

  SECTION("a breakpoint uses its adjacent segments for each direction") {
    auto instance = kdc::test::make_instance_linear(
        {{kdc::Point(1.0, 0.0), kdc::Point(1.0, 0.0)},
         {kdc::Point(0.0, 0.0), kdc::Point(3.0, 0.0)}},
        {{0.0, 0.0}});
    instance.trajectories[1] = kdc::Trajectory(
        {0.0, 0.5, 1.0},
        {kdc::Point(0.0, 0.0), kdc::Point(1.0, 0.0),
         kdc::Point(3.0, 0.0)});

    REQUIRE(kdc::KineticCore::resolve_support_directional_limit(
                instance, 0, {0, 1}, 0.5, true) == 1);
    REQUIRE(kdc::KineticCore::resolve_support_directional_limit(
                instance, 0, {0, 1}, 0.5, false) == 0);
    const kdc::StaticAssignment assignment{
        {0}, {1.0}, std::acos(-1.0), true, {0, 0}};
    const auto forward = kdc::KineticSolution::extend(
        instance, assignment, 0.5, 1.0, true, false,
        kdc::ObjectiveType::MIN_MAX);
    const auto backward = kdc::KineticSolution::extend(
        instance, assignment, 0.5, 0.0, false, false,
        kdc::ObjectiveType::MIN_MAX);

    REQUIRE(forward.intervals.front().supporting_point ==
            std::vector<int>{1});
    REQUIRE(backward.intervals.back().supporting_point ==
            std::vector<int>{0});
  }
}

TEST_CASE("Handover and support change can coincide in the event trace") {
  const auto instance = kdc::test::make_instance_linear(
      {{kdc::Point(2.0, 0.0), kdc::Point(2.0, 0.0)},
       {kdc::Point(1.0, 0.0), kdc::Point(2.2, 0.0)},
       {kdc::Point(7.0, 0.0), kdc::Point(1.0, 0.0)}},
      {{0.0, 0.0}, {10.0, 0.0}});
  const kdc::StaticAssignment assignment{
      {0, 2}, {2.0, 3.0}, 13.0 * std::acos(-1.0), true, {0, 0, 1}};
  kdc::KineticEventDiagnostics diagnostics;
  const auto solution = kdc::KineticSolution::extend(
      instance, assignment, 0.0, 1.0, true, true,
      kdc::ObjectiveType::MIN_SUM, nullptr,
      kdc::KineticEventEngine::REFERENCE_EXHAUSTIVE, &diagnostics);
  kdc::KineticEventDiagnostics global_diagnostics;
  const auto global_reference = kdc::KineticSolution::extend(
      instance, assignment, 0.0, 1.0, true, true,
      kdc::ObjectiveType::MIN_SUM, nullptr,
      kdc::KineticEventEngine::KINETIC_TOURNAMENT, &global_diagnostics,
      kdc::KineticIntervalEmission::EXACT_RELEVANT_BOUNDARIES,
      kdc::HandoverEvaluation::REFERENCE_GLOBAL);
  const double event_time = 5.0 / 6.0;

  require_extension_invariants(instance, solution);
  require_extension_invariants(instance, global_reference);
  REQUIRE(solution.intervals.size() == 2U);
  REQUIRE(global_reference.intervals.size() == solution.intervals.size());
  for (kdc::Index index = 0; index < solution.intervals.size(); ++index) {
    REQUIRE(kdc::test::near(solution.intervals[index].t_start,
                            global_reference.intervals[index].t_start));
    REQUIRE(kdc::test::near(solution.intervals[index].t_end,
                            global_reference.intervals[index].t_end));
    REQUIRE(solution.intervals[index].supporting_point ==
            global_reference.intervals[index].supporting_point);
    REQUIRE(solution.intervals[index].assigned_points ==
            global_reference.intervals[index].assigned_points);
    REQUIRE(kdc::test::near(solution.intervals[index].a,
                            global_reference.intervals[index].a));
    REQUIRE(kdc::test::near(solution.intervals[index].b,
                            global_reference.intervals[index].b));
    REQUIRE(kdc::test::near(solution.intervals[index].c,
                            global_reference.intervals[index].c));
  }
  REQUIRE(kdc::test::near(solution.intervals.front().t_end, event_time));
  REQUIRE(solution.intervals.front().assigned_points ==
          std::vector<int>{0, 0, 1});
  REQUIRE(solution.intervals.back().assigned_points ==
          std::vector<int>{1, 0, 1});
  const auto handover_trace =
      std::find_if(diagnostics.trace.begin(), diagnostics.trace.end(),
                   [event_time](const auto& event) {
                     return event.type == kdc::KineticEventType::HANDOVER &&
                            event.affected_point == 0 &&
                            kdc::test::near(event.time, event_time);
                   });
  REQUIRE(handover_trace != diagnostics.trace.end());
  REQUIRE(handover_trace->from_station == 0);
  REQUIRE(handover_trace->to_station == 1);
  REQUIRE(handover_trace->from_station_support_after == 1);
  REQUIRE(handover_trace->to_station_support_after == 2);
  REQUIRE(std::any_of(diagnostics.trace.begin(), diagnostics.trace.end(),
                      [event_time](const auto& event) {
                        return event.type ==
                                   kdc::KineticEventType::SUPPORT_CHANGE &&
                               event.station_id == 0 &&
                               event.old_support == 0 &&
                               event.new_support == 1 &&
                               kdc::test::near(event.time, event_time);
                      }));
  REQUIRE(diagnostics.handover_event_checks > 0U);
  REQUIRE(diagnostics.handover_detection_nanoseconds > 0U);
  REQUIRE(diagnostics.support_event_detection_nanoseconds > 0U);
  REQUIRE(diagnostics.total_extension_nanoseconds > 0U);
  REQUIRE(diagnostics.source_receiver_pair_count > 0U);
  REQUIRE(diagnostics.external_challenge_certificates > 0U);
  REQUIRE(diagnostics.handover_global_point_scans <=
          global_diagnostics.handover_global_point_scans);
  REQUIRE(diagnostics.handover_global_fallbacks > 0U);
}

TEST_CASE("Exact interval emission differentially matches global-breakpoint reference") {
  constexpr double tolerance = 1e-8;
  constexpr int random_cases = 12;
  constexpr int dense_samples = 128;

  for (int case_index = 0; case_index < random_cases; ++case_index) {
    const auto instance =
        randomized_piecewise_instance(7301U + static_cast<std::uint32_t>(
                                                  case_index));
    const auto assignment = kdc::StationarySolver::solve_nn(instance, 0.0);
    REQUIRE(assignment.feasible);

    for (const auto objective : {kdc::ObjectiveType::MIN_MAX,
                                 kdc::ObjectiveType::MIN_SUM}) {
      kdc::KineticEventDiagnostics reference_diagnostics;
      const auto reference = kdc::KineticSolution::extend(
          instance, assignment, 0.0, instance.T_end, true, true, objective,
          nullptr, kdc::KineticEventEngine::REFERENCE_EXHAUSTIVE,
          &reference_diagnostics,
          kdc::KineticIntervalEmission::REFERENCE_ALL_TRAJECTORY_BREAKPOINTS);
      kdc::KineticEventDiagnostics optimized_diagnostics;
      const auto optimized = kdc::KineticSolution::extend(
          instance, assignment, 0.0, instance.T_end, true, true, objective,
          nullptr, kdc::KineticEventEngine::REFERENCE_EXHAUSTIVE,
          &optimized_diagnostics,
          kdc::KineticIntervalEmission::EXACT_RELEVANT_BOUNDARIES);

      require_extension_invariants(instance, reference);
      require_extension_invariants(instance, optimized);
      REQUIRE(kdc::Verifier::verify_continuous(instance, reference).all_ok());
      REQUIRE(kdc::Verifier::verify_continuous(instance, optimized).all_ok());
      REQUIRE(optimized.intervals.size() <= reference.intervals.size());
      REQUIRE(reference_diagnostics.raw_trajectory_breakpoints ==
              optimized_diagnostics.raw_trajectory_breakpoints);
      REQUIRE(optimized_diagnostics.solution_intervals_generated ==
              optimized.intervals.size());

      const auto reference_support_events =
          selected_events(reference_diagnostics,
                          kdc::KineticEventType::SUPPORT_CHANGE);
      const auto optimized_support_events =
          selected_events(optimized_diagnostics,
                          kdc::KineticEventType::SUPPORT_CHANGE);
      REQUIRE(reference_support_events.size() ==
              optimized_support_events.size());
      for (kdc::Index event = 0; event < reference_support_events.size();
           ++event) {
        REQUIRE(kdc::test::near(reference_support_events[event].time,
                                optimized_support_events[event].time,
                                tolerance, tolerance));
        REQUIRE(reference_support_events[event].station_id ==
                optimized_support_events[event].station_id);
        REQUIRE(reference_support_events[event].old_support ==
                optimized_support_events[event].old_support);
        REQUIRE(reference_support_events[event].new_support ==
                optimized_support_events[event].new_support);
      }

      const auto reference_handovers =
          selected_events(reference_diagnostics,
                          kdc::KineticEventType::HANDOVER);
      const auto optimized_handovers =
          selected_events(optimized_diagnostics,
                          kdc::KineticEventType::HANDOVER);
      REQUIRE(reference_handovers.size() == optimized_handovers.size());
      for (kdc::Index event = 0; event < reference_handovers.size(); ++event) {
        REQUIRE(kdc::test::near(reference_handovers[event].time,
                                optimized_handovers[event].time,
                                tolerance, tolerance));
        REQUIRE(reference_handovers[event].from_station ==
                optimized_handovers[event].from_station);
        REQUIRE(reference_handovers[event].to_station ==
                optimized_handovers[event].to_station);
        REQUIRE(reference_handovers[event].affected_point ==
                optimized_handovers[event].affected_point);
      }

      std::vector<double> sample_times;
      sample_times.reserve(static_cast<kdc::Index>(dense_samples + 1) +
                           4U * reference_diagnostics.trace.size());
      for (int sample = 0; sample <= dense_samples; ++sample) {
        sample_times.push_back(static_cast<double>(sample) / dense_samples);
      }
      for (const auto& event : reference_diagnostics.trace) {
        sample_times.push_back(event.time);
        sample_times.push_back(std::nextafter(
            event.time, -std::numeric_limits<double>::infinity()));
        sample_times.push_back(std::nextafter(
            event.time, std::numeric_limits<double>::infinity()));
      }
      for (const auto& event : optimized_diagnostics.trace) {
        sample_times.push_back(event.time);
        sample_times.push_back(std::nextafter(
            event.time, -std::numeric_limits<double>::infinity()));
        sample_times.push_back(std::nextafter(
            event.time, std::numeric_limits<double>::infinity()));
      }
      for (const auto& trajectory : instance.trajectories) {
        for (const double breakpoint : trajectory.t_breaks) {
          sample_times.push_back(breakpoint);
          sample_times.push_back(std::nextafter(
              breakpoint, -std::numeric_limits<double>::infinity()));
          sample_times.push_back(std::nextafter(
              breakpoint, std::numeric_limits<double>::infinity()));
        }
      }
      std::sort(sample_times.begin(), sample_times.end());
      sample_times.erase(
          std::unique(sample_times.begin(), sample_times.end()),
          sample_times.end());
      for (const double time : sample_times) {
        if (time < 0.0 || time > instance.T_end) {
          continue;
        }
        const auto& reference_interval = solution_interval_at(reference, time);
        const auto& optimized_interval = solution_interval_at(optimized, time);
        REQUIRE(reference_interval.supporting_point ==
                optimized_interval.supporting_point);
        REQUIRE(reference_interval.assigned_points ==
                optimized_interval.assigned_points);
        REQUIRE(kdc::test::near(reference.cost_at(time),
                                optimized.cost_at(time), tolerance,
                                tolerance));
      }
      for (const auto& reference_interval : reference.intervals) {
        for (const auto& optimized_interval : optimized.intervals) {
          const double overlap_start =
              std::max(reference_interval.t_start,
                       optimized_interval.t_start);
          const double overlap_end =
              std::min(reference_interval.t_end, optimized_interval.t_end);
          if (overlap_start >= overlap_end) {
            continue;
          }
          const double midpoint =
              overlap_start + (overlap_end - overlap_start) / 2.0;
          REQUIRE(reference_interval.supporting_point ==
                  optimized_interval.supporting_point);
          REQUIRE(reference_interval.assigned_points ==
                  optimized_interval.assigned_points);
          REQUIRE(kdc::test::near(reference_interval.a,
                                  optimized_interval.a, tolerance,
                                  tolerance));
          REQUIRE(kdc::test::near(reference_interval.b,
                                  optimized_interval.b, tolerance,
                                  tolerance));
          REQUIRE(kdc::test::near(reference_interval.c,
                                  optimized_interval.c, tolerance,
                                  tolerance));
          REQUIRE(kdc::test::near(reference.cost_at(midpoint),
                                  optimized.cost_at(midpoint), tolerance,
                                  tolerance));
        }
      }
      REQUIRE(kdc::test::near(reference.total_integral(),
                              optimized.total_integral(), tolerance,
                              tolerance));
      REQUIRE(kdc::test::near(reference.peak_cost(), optimized.peak_cost(),
                              tolerance, tolerance));
      REQUIRE(kdc::test::near(reference.peak_time(), optimized.peak_time(),
                              tolerance, tolerance));
    }
  }
}
