#include "test_utils.hpp"

#include "kdc/kinetic.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <tuple>
#include <vector>

namespace {
std::vector<kdc::SupportChangeEvent> reference_support_changes(
    const kdc::Instance& instance, int station_id, int current_support,
    double t_start, double t_end, bool forward) {
  constexpr double epsilon = 1e-9;
  const auto& support = instance.trajectories[
      static_cast<kdc::Index>(current_support)];
  std::vector<kdc::SupportChangeEvent> events;
  const double interval_start = std::min(t_start, t_end);
  const double interval_end = std::max(t_start, t_end);
  for (int candidate = 0; candidate < instance.n; ++candidate) {
    if (candidate == current_support) {
      continue;
    }
    const auto& other =
        instance.trajectories[static_cast<kdc::Index>(candidate)];
    std::vector<double> breaks{interval_start, interval_end};
    for (const double time : support.t_breaks) {
      if (time > interval_start && time < interval_end) {
        breaks.push_back(time);
      }
    }
    for (const double time : other.t_breaks) {
      if (time > interval_start && time < interval_end) {
        breaks.push_back(time);
      }
    }
    std::sort(breaks.begin(), breaks.end());
    breaks.erase(std::unique(breaks.begin(), breaks.end()), breaks.end());
    for (kdc::Index index = 0; index + 1U < breaks.size(); ++index) {
      const double start = breaks[index];
      const double end = breaks[index + 1U];
      const double duration = end - start;
      if (duration <= 0.0) {
        continue;
      }
      const auto p1 = support.position(start);
      const auto p2 = other.position(start);
      const auto v1 = (support.position(end) - p1) * (1.0 / duration);
      const auto v2 = (other.position(end) - p2) * (1.0 / duration);
      const auto d1 =
          instance.stations[static_cast<kdc::Index>(station_id)].pos - p1;
      const auto d2 =
          instance.stations[static_cast<kdc::Index>(station_id)].pos - p2;
      const double a = v1.dot(v1) - v2.dot(v2);
      const double b = -2.0 * d1.dot(v1) + 2.0 * d2.dot(v2);
      const double c = d1.dot(d1) - d2.dot(d2);
      for (const double offset : kdc::KineticCore::solve_quadratic(a, b, c)) {
        if (offset < -epsilon || offset > duration + epsilon) {
          continue;
        }
        const double time = std::clamp(start + offset, start, end);
        if ((forward && time > t_start + epsilon &&
             time <= t_end + epsilon) ||
            (!forward && time < t_start - epsilon &&
             time >= t_end - epsilon)) {
          events.push_back({time, station_id, candidate, true});
        }
      }
    }
  }
  std::sort(events.begin(), events.end(),
            [forward](const auto& lhs, const auto& rhs) {
              if (lhs.time != rhs.time) {
                return forward ? lhs.time < rhs.time : lhs.time > rhs.time;
              }
              return lhs.new_supporting_point < rhs.new_supporting_point;
            });
  events.erase(std::unique(events.begin(), events.end(),
                           [epsilon](const auto& lhs, const auto& rhs) {
                             return lhs.new_supporting_point ==
                                        rhs.new_supporting_point &&
                                    std::abs(lhs.time - rhs.time) <= epsilon;
                           }),
               events.end());
  return events;
}

std::vector<kdc::HandoverEvent> reference_handovers(
    const kdc::Instance& instance, int from, int to,
    const std::vector<int>& supports, const std::vector<int>& owners,
    double start, double end, bool forward) {
  const int support_from = supports[static_cast<kdc::Index>(from)];
  const int support_to = supports[static_cast<kdc::Index>(to)];
  std::vector<std::pair<double, int>> assigned;
  const auto station_from =
      instance.stations[static_cast<kdc::Index>(from)].pos;
  for (int point = 0; point < instance.n; ++point) {
    if (owners[static_cast<kdc::Index>(point)] != from) {
      continue;
    }
    const double distance =
        (station_from - instance.trajectories[static_cast<kdc::Index>(point)]
                            .position(start))
            .norm();
    assigned.emplace_back(distance, point);
  }
  if (assigned.size() < 2U || owners[static_cast<kdc::Index>(support_from)] != from ||
      owners[static_cast<kdc::Index>(support_to)] != to) {
    return {};
  }
  std::sort(assigned.begin(), assigned.end(),
            [](const auto& lhs, const auto& rhs) {
              return lhs.first != rhs.first ? lhs.first > rhs.first
                                            : lhs.second < rhs.second;
            });
  const int second_support = assigned[1].second;
  std::vector<kdc::HandoverEvent> result;
  const auto station_to =
      instance.stations[static_cast<kdc::Index>(to)].pos;
  const auto& transferred = instance.trajectories[
      static_cast<kdc::Index>(support_from)];
  const auto& receiver_support = instance.trajectories[
      static_cast<kdc::Index>(support_to)];
  for (const auto& event : reference_support_changes(
           instance, to, support_to, start, end, forward)) {
    if (event.new_supporting_point != support_from) {
      continue;
    }
    const auto velocity = [](const kdc::Trajectory& trajectory, double time) {
      const auto segment =
          static_cast<kdc::Index>(trajectory.segment_index(time));
      return (trajectory.waypoints[segment + 1U] -
              trajectory.waypoints[segment]) *
             (1.0 / (trajectory.t_breaks[segment + 1U] -
                     trajectory.t_breaks[segment]));
    };
    const auto difference_derivative =
        2.0 * ((transferred.position(event.time) - station_to)
                   .dot(velocity(transferred, event.time)) -
               (receiver_support.position(event.time) - station_to)
                   .dot(velocity(receiver_support, event.time)));
    if ((forward ? 1.0 : -1.0) * difference_derivative < -1e-12) {
      result.push_back({event.time, from, to, support_from, second_support,
                        support_to, true});
    }
  }
  return result;
}

void require_same_handovers(const std::vector<kdc::HandoverEvent>& actual,
                            const std::vector<kdc::HandoverEvent>& expected) {
  REQUIRE(actual.size() == expected.size());
  for (kdc::Index index = 0; index < actual.size(); ++index) {
    REQUIRE(actual[index].from_station == expected[index].from_station);
    REQUIRE(actual[index].to_station == expected[index].to_station);
    REQUIRE(actual[index].point_id == expected[index].point_id);
    REQUIRE(actual[index].new_support_from == expected[index].new_support_from);
    REQUIRE(actual[index].new_support_to == expected[index].new_support_to);
    REQUIRE(kdc::test::near(actual[index].time, expected[index].time, 1e-9,
                            1e-12));
    REQUIRE(actual[index].valid == expected[index].valid);
  }
}

void require_same_events(
    const std::vector<kdc::SupportChangeEvent>& actual,
    const std::vector<kdc::SupportChangeEvent>& expected) {
  REQUIRE(actual.size() == expected.size());
  for (kdc::Index index = 0; index < actual.size(); ++index) {
    REQUIRE(actual[index].station_id == expected[index].station_id);
    REQUIRE(actual[index].new_supporting_point ==
            expected[index].new_supporting_point);
    REQUIRE(kdc::test::near(actual[index].time, expected[index].time, 1e-9,
                            1e-12));
    REQUIRE(actual[index].valid == expected[index].valid);
  }
}

kdc::Instance make_piecewise_instance() {
  kdc::Instance instance;
  instance.id = 51;
  instance.name = "piecewise-event-regression";
  instance.n = 3;
  instance.m = 2;
  instance.T_end = 1.0;
  instance.stations = {{0, {0.0, 0.0}}, {1, {6.0, 0.0}}};
  instance.trajectories.emplace_back(
      std::vector<double>{0.0, 0.3, 0.7, 1.0},
      std::vector<kdc::Point>{{-2.0, 0.0}, {-0.8, 0.0}, {0.8, 0.0},
                              {2.0, 0.0}});
  instance.trajectories.emplace_back(
      std::vector<double>{0.0, 0.3, 0.7, 1.0},
      std::vector<kdc::Point>{{1.0, 0.0}, {0.6, 0.0}, {-0.6, 0.0},
                              {-1.0, 0.0}});
  instance.trajectories.emplace_back(
      std::vector<double>{0.0, 0.3, 0.7, 1.0},
      std::vector<kdc::Point>{{5.0, 0.0}, {4.0, 0.4}, {3.0, -0.4},
                              {2.0, 0.0}});
  return instance;
}
}

TEST_CASE("KineticCore solves quadratic equations robustly") {
  SECTION("quadratic roots") {
    const auto roots = kdc::KineticCore::solve_quadratic(1.0, -3.0, 2.0);
    REQUIRE(roots.size() == 2U);
    REQUIRE(kdc::test::near(roots[0], 1.0));
    REQUIRE(kdc::test::near(roots[1], 2.0));
  }

  SECTION("no real roots") {
    const auto roots = kdc::KineticCore::solve_quadratic(1.0, 0.0, 1.0);
    REQUIRE(roots.empty());
  }

  SECTION("linear case") {
    const auto roots = kdc::KineticCore::solve_quadratic(0.0, 2.0, -4.0);
    REQUIRE(roots.size() == 1U);
    REQUIRE(kdc::test::near(roots[0], 2.0));
  }

  SECTION("degenerate quadratic") {
    const auto roots = kdc::KineticCore::solve_quadratic(
        1.0, 2.0, 1.0 + 1e-13);
    REQUIRE(roots.size() == 1U);
    REQUIRE(kdc::test::near(roots[0], -1.0));
  }
}

TEST_CASE("Precomputed event geometry preserves breakpoint semantics") {
  const auto instance = make_piecewise_instance();
  for (const auto& query :
       std::vector<std::tuple<double, double, bool>>{
           {0.0, 1.0, true}, {1.0, 0.0, false}, {0.0, 0.3, true},
           {0.3, 0.7, true}, {0.7, 1.0, true}, {1.0, 0.7, false},
           {0.7, 0.3, false}, {0.3, 0.0, false}, {0.3, 0.3, true}}) {
    const auto [start, end, forward] = query;
    const auto expected =
        reference_support_changes(instance, 0, 0, start, end, forward);
    const auto actual =
        kdc::KineticCore::find_support_changes(instance, 0, 0, start, end,
                                               forward);
    require_same_events(actual, expected);
    const auto repeated =
        kdc::KineticCore::find_support_changes(instance, 0, 0, start, end,
                                               forward);
    require_same_events(repeated, actual);
  }
}

TEST_CASE("Events exactly at first, interior, and last breakpoints are retained") {
  kdc::Instance instance;
  instance.n = 3;
  instance.m = 1;
  instance.T_end = 1.0;
  instance.stations = {{0, {0.0, 0.0}}};
  instance.trajectories.emplace_back(
      std::vector<double>{0.0, 0.3, 0.7, 1.0},
      std::vector<kdc::Point>{{-1.0, 0.0}, {-0.4, 0.0}, {0.4, 0.0},
                              {1.0, 0.0}});
  instance.trajectories.emplace_back(
      std::vector<double>{0.0, 0.3, 0.7, 1.0},
      std::vector<kdc::Point>{{0.4, 0.0}, {0.4, 0.0}, {0.4, 0.0},
                              {0.4, 0.0}});
  instance.trajectories.emplace_back(
      std::vector<double>{0.0, 0.3, 0.7, 1.0},
      std::vector<kdc::Point>{{1.0, 0.0}, {1.0, 0.0}, {1.0, 0.0},
                              {1.0, 0.0}});

  const auto expected =
      reference_support_changes(instance, 0, 0, 0.0, 1.0, true);
  const auto actual =
      kdc::KineticCore::find_support_changes(instance, 0, 0, 0.0, 1.0, true);
  require_same_events(actual, expected);
  REQUIRE(std::any_of(actual.begin(), actual.end(), [](const auto& event) {
    return event.new_supporting_point == 1 &&
           kdc::test::near(event.time, 0.3, 1e-9, 1e-12);
  }));
  REQUIRE(std::any_of(actual.begin(), actual.end(), [](const auto& event) {
    return event.new_supporting_point == 1 &&
           kdc::test::near(event.time, 0.7, 1e-9, 1e-12);
  }));
  REQUIRE(std::any_of(actual.begin(), actual.end(), [](const auto& event) {
    return event.new_supporting_point == 2 &&
           kdc::test::near(event.time, 1.0, 1e-9, 1e-12);
  }));
  const auto reverse =
      kdc::KineticCore::find_support_changes(instance, 0, 0, 1.0, 0.0, false);
  require_same_events(
      reverse, reference_support_changes(instance, 0, 0, 1.0, 0.0, false));
}

TEST_CASE("Precomputed handover sets match the reference derivation") {
  const auto instance = make_piecewise_instance();
  const std::vector<int> supports{0, 2};
  const std::vector<int> owners{0, 0, 1};
  std::vector<kdc::HandoverEvent> expected_all_from;
  kdc::HandoverEvent expected_next;
  for (int from = 0; from < instance.m; ++from) {
    for (int to = 0; to < instance.m; ++to) {
      if (from == to) {
        continue;
      }
      const auto expected = reference_handovers(
          instance, from, to, supports, owners, 0.0, 1.0, true);
      const auto actual = kdc::KineticCore::find_handovers(
          instance, from, to, supports, owners, 0.0, 1.0, true);
      require_same_handovers(actual, expected);
      expected_all_from.insert(expected_all_from.end(), expected.begin(),
                               expected.end());
      if (!expected.empty() &&
          (!expected_next.valid || expected.front().time < expected_next.time)) {
        expected_next = expected.front();
      }
    }
  }
  const auto all_from =
      kdc::KineticCore::find_handovers_from(instance, 0, supports, owners, 0.0,
                                            1.0, true);
  std::vector<kdc::HandoverEvent> expected_from;
  for (int to = 0; to < instance.m; ++to) {
    if (to == 0) {
      continue;
    }
    const auto pair = reference_handovers(
        instance, 0, to, supports, owners, 0.0, 1.0, true);
    expected_from.insert(expected_from.end(), pair.begin(), pair.end());
  }
  require_same_handovers(all_from, expected_from);

  const auto actual_next = kdc::KineticCore::find_next_handover(
      instance, supports, owners, 0.0, 1.0, true);
  REQUIRE(actual_next.valid == expected_next.valid);
  if (expected_next.valid) {
    require_same_handovers({actual_next}, {expected_next});
  }
}

TEST_CASE("KineticCore finds support changes and resolves ties") {
  const auto crossing = kdc::test::make_instance_linear(
      {{kdc::Point(-1, 0), kdc::Point(1, 0)},
       {kdc::Point(0, 0), kdc::Point(0, 0)}},
      {{0, 0}});

  SECTION("support change simple") {
    const auto events =
        kdc::KineticCore::find_support_changes(crossing, 0, 0, 0.0, 1.0,
                                               true);
    REQUIRE(events.size() == 1U);
    REQUIRE(kdc::test::near(events.front().time, 0.5));
    REQUIRE(events.front().station_id == 0);
    REQUIRE(events.front().new_supporting_point == 1);
    REQUIRE(events.front().valid);
    const auto next =
        kdc::KineticCore::find_next_event(crossing, {0}, 0.0, 1.0, true);
    REQUIRE(next.valid);
    REQUIRE(kdc::test::near(next.time, 0.5));
    const auto previous = kdc::KineticCore::find_next_event(
        crossing, {0}, 1.0, 0.0, false);
    REQUIRE(previous.valid);
    REQUIRE(kdc::test::near(previous.time, 0.5));
  }

  SECTION("degeneracy") {
    const auto tied = kdc::test::make_instance_linear(
        {{kdc::Point(-1, 0), kdc::Point(-1, 0)},
         {kdc::Point(0, 0), kdc::Point(2, 0)}},
        {{0, 0}});
    REQUIRE(kdc::KineticCore::resolve_degeneracy(tied, 0, {0, 1}, 0.5) ==
            1);
  }
}

TEST_CASE("KineticCore selects assigned supports for handovers") {
  const auto instance = kdc::test::make_instance_linear(
      {{kdc::Point(2, 0), kdc::Point(2, 0)},
       {kdc::Point(1, 0), kdc::Point(1, 0)},
       {kdc::Point(7, 0), kdc::Point(1, 0)}},
      {{0, 0}, {10, 0}});
  const std::vector<int> owners{0, 0, 1};

  SECTION("second furthest") {
    REQUIRE(kdc::KineticCore::second_furthest_assigned(
                instance, 0, {0, 1}, 0.0) == 1);
  }

  SECTION("handover simple") {
    const auto events = kdc::KineticCore::find_handovers(
        instance, 0, 1, {0, 2}, owners, 0.0, 1.0, true);
    REQUIRE(events.size() == 1U);
    REQUIRE(kdc::test::near(events.front().time, 5.0 / 6.0));
    REQUIRE(events.front().from_station == 0);
    REQUIRE(events.front().to_station == 1);
    REQUIRE(events.front().point_id == 0);
    REQUIRE(events.front().new_support_from == 1);
    REQUIRE(events.front().new_support_to == 2);
    REQUIRE(events.front().valid);
    const auto next =
        kdc::KineticCore::find_next_handover(instance, {0, 2}, owners, 0.0,
                                             1.0, true);
    REQUIRE(next.valid);
    REQUIRE(kdc::test::near(next.time, 5.0 / 6.0));
  }

  SECTION("no handover if p3 == -1") {
    const auto one_point = kdc::test::make_instance_linear(
        {{kdc::Point(0, 0), kdc::Point(0, 0)},
         {kdc::Point(10, 0), kdc::Point(10, 0)}},
        {{0, 0}, {10, 0}});
    REQUIRE(kdc::KineticCore::find_handovers(
                one_point, 0, 1, {0, 1}, {0, 1}, 0.0, 1.0, true)
                .empty());
  }
}

TEST_CASE("Exhaustive reference engine exposes deterministic diagnostics") {
  const auto instance = kdc::test::make_instance_linear(
      {{kdc::Point(1.0, 0.0), kdc::Point(1.0, 0.0)},
       {kdc::Point(0.0, 0.0), kdc::Point(2.0, 0.0)}},
      {{0.0, 0.0}});
  kdc::KineticEventDiagnostics first_diagnostics;
  const auto event = kdc::KineticCore::find_next_event(
      instance, {0}, 0.0, 1.0, true, nullptr,
      kdc::KineticEventEngine::REFERENCE_EXHAUSTIVE, &first_diagnostics);
  kdc::KineticEventDiagnostics repeated_diagnostics;
  const auto repeated = kdc::KineticCore::find_next_event(
      instance, {0}, 0.0, 1.0, true, nullptr,
      kdc::KineticEventEngine::REFERENCE_EXHAUSTIVE, &repeated_diagnostics);

  REQUIRE(event.valid);
  REQUIRE(kdc::test::near(event.time, 0.5));
  REQUIRE(event.new_supporting_point == 1);
  REQUIRE(first_diagnostics.station_support_event_searches == 1U);
  REQUIRE(first_diagnostics.point_vs_support_comparisons == 1U);
  REQUIRE(first_diagnostics.trajectory_segment_pair_examinations == 1U);
  REQUIRE(first_diagnostics.quadratic_equations_solved == 1U);
  REQUIRE(first_diagnostics.real_roots_found == 2U);
  REQUIRE(first_diagnostics.candidate_roots_rejected == 1U);
  REQUIRE(first_diagnostics.selected_support_events == 1U);
  REQUIRE(first_diagnostics.trace.size() == 1U);
  REQUIRE(first_diagnostics.trace.front().type ==
          kdc::KineticEventType::SUPPORT_CHANGE);
  REQUIRE(first_diagnostics.trace.front().old_support == 0);
  REQUIRE(first_diagnostics.trace.front().new_support == 1);
  REQUIRE(first_diagnostics.trace.front().tie_breaking_outcome ==
          repeated_diagnostics.trace.front().tie_breaking_outcome);
  REQUIRE(first_diagnostics.trace.front().time ==
          repeated_diagnostics.trace.front().time);
}

TEST_CASE("Tangent support equality is an event candidate but no transition") {
  const auto instance = kdc::test::make_instance_linear(
      {{kdc::Point(1.0, 0.0), kdc::Point(1.0, 0.0)},
       {kdc::Point(1.0, -1.0), kdc::Point(1.0, 1.0)}},
      {{0.0, 0.0}});
  const auto candidates = kdc::KineticCore::find_support_changes(
      instance, 0, 1, 0.0, 1.0, true);

  REQUIRE(candidates.size() == 1U);
  REQUIRE(kdc::test::near(candidates.front().time, 0.5));
  REQUIRE(candidates.front().new_supporting_point == 0);
}

TEST_CASE("Zero-duration support search emits no event") {
  const auto instance = kdc::test::make_instance_linear(
      {{kdc::Point(1.0, 0.0), kdc::Point(1.0, 0.0)},
       {kdc::Point(0.0, 0.0), kdc::Point(2.0, 0.0)}},
      {{0.0, 0.0}});
  kdc::KineticEventDiagnostics diagnostics;
  const auto event = kdc::KineticCore::find_next_event(
      instance, {0}, 0.5, 0.5, true, nullptr,
      kdc::KineticEventEngine::REFERENCE_EXHAUSTIVE, &diagnostics);

  REQUIRE_FALSE(event.valid);
  REQUIRE(diagnostics.station_support_event_searches == 1U);
  REQUIRE(diagnostics.trajectory_segment_pair_examinations == 0U);
  REQUIRE(diagnostics.selected_support_events == 0U);
  REQUIRE(diagnostics.trace.empty());
}
