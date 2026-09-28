#include "kdc/kinetic.hpp"

#include "kdc/logging.hpp"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <utility>

namespace kdc {
Trajectory::Trajectory(std::vector<Value> time_breaks,
                       std::vector<Point> points)
    : t_breaks(std::move(time_breaks)), waypoints(std::move(points)) {
  if (t_breaks.size() < 2U || waypoints.size() != t_breaks.size()) {
    throw std::invalid_argument(
        "a trajectory requires matching positions and at least two time breaks");
  }
  for (Index index = 0; index < t_breaks.size(); ++index) {
    if (!std::isfinite(t_breaks[index])) {
      throw std::invalid_argument("trajectory time breaks must be finite");
    }
    if (index > 0U && t_breaks[index] <= t_breaks[index - 1U]) {
      throw std::invalid_argument(
          "trajectory time breaks must be strictly increasing");
    }
  }
}

int Trajectory::segment_index(Value time) const {
  if (t_breaks.size() < 2U) {
    throw std::logic_error("trajectory has fewer than two time breaks");
  }
  if (time < t_breaks.front() || time > t_breaks.back()) {
    throw std::out_of_range("time is outside the trajectory interval");
  }
  if (time <= t_breaks.front()) {
    return 0;
  }
  if (time >= t_breaks.back()) {
    return static_cast<int>(t_breaks.size() - 2U);
  }
  const auto upper = std::upper_bound(t_breaks.begin(), t_breaks.end(), time);
  return static_cast<int>(std::distance(t_breaks.begin(), upper) - 1);
}

Point Trajectory::position(Value time) const {
  const auto index = static_cast<Index>(segment_index(time));
  const Value duration = t_breaks[index + 1U] - t_breaks[index];
  const Value fraction = (time - t_breaks[index]) / duration;
  return waypoints[index] +
         (waypoints[index + 1U] - waypoints[index]) * fraction;
}

namespace {
constexpr Value kTimeEpsilon = 1e-9;

void validate_instance_shapes(const Instance& instance) {
  if (instance.n < 0 || instance.m < 0 ||
      static_cast<Index>(instance.n) != instance.trajectories.size() ||
      static_cast<Index>(instance.m) != instance.stations.size()) {
    throw std::invalid_argument("kinetic operation received inconsistent instance dimensions");
  }
}

void validate_station_support(const Instance& instance, int station_id,
                              int supporting_point) {
  if (station_id < 0 || station_id >= instance.m) {
    throw std::out_of_range("station id is outside the instance");
  }
  if (supporting_point < 0 || supporting_point >= instance.n) {
    throw std::out_of_range("supporting point is outside the instance");
  }
}

Point segment_velocity(const Trajectory& trajectory, Value time) {
  const auto segment =
      static_cast<Index>(trajectory.segment_index(time));
  const Value duration =
      trajectory.t_breaks[segment + 1U] - trajectory.t_breaks[segment];
  return (trajectory.waypoints[segment + 1U] -
          trajectory.waypoints[segment]) *
         (1.0 / duration);
}
}

std::vector<Value> KineticCore::solve_quadratic(
    Value a, Value b, Value c, Value eps_a, Value eps_b, Value eps_disc) {
  if (!std::isfinite(a) || !std::isfinite(b) || !std::isfinite(c) ||
      !std::isfinite(eps_a) || !std::isfinite(eps_b) ||
      !std::isfinite(eps_disc) || eps_a < 0.0 || eps_b < 0.0 ||
      eps_disc < 0.0) {
    return {};
  }
  if (std::abs(a) < eps_a) {
    if (std::abs(b) < eps_b) {
      return {};
    }
    return {-c / b};
  }

  const Value discriminant = b * b - 4.0 * a * c;
  if (!std::isfinite(discriminant) || discriminant < -eps_disc) {
    return {};
  }
  if (discriminant <= 0.0) {
    return {-b / (2.0 * a)};
  }

  const Value root_discriminant = std::sqrt(discriminant);
  const Value sign_b = b < 0.0 ? -1.0 : 1.0;
  const Value q = -0.5 * (b + sign_b * root_discriminant);
  Value first = q / a;
  Value second =
      std::abs(q) <= std::numeric_limits<Value>::epsilon() ? 0.0 : c / q;
  if (first > second) {
    std::swap(first, second);
  }
  return {first, second};
}

std::vector<SupportChangeEvent> KineticCore::find_support_changes(
    const Instance& instance, int station_id, int current_support,
    Value t_start, Value t_end, bool forward) {
  LOG_DEBUG("find_support_changes: station={}, support={}, direction={}",
            station_id, current_support, forward ? "forward" : "backward");
  validate_instance_shapes(instance);
  validate_station_support(instance, station_id, current_support);
  if (!std::isfinite(t_start) || !std::isfinite(t_end) ||
      t_start < 0.0 || t_start > instance.T_end || t_end < 0.0 ||
      t_end > instance.T_end) {
    throw std::out_of_range("support change interval is outside [0, T_end]");
  }
  if (forward && t_end < t_start) {
    throw std::invalid_argument("forward support interval has reversed bounds");
  }
  if (!forward && t_end > t_start) {
    throw std::invalid_argument("backward support interval has reversed bounds");
  }

  std::vector<SupportChangeEvent> events;
  const auto& support_trajectory =
      instance.trajectories[static_cast<Index>(current_support)];
  const Value interval_start = std::min(t_start, t_end);
  const Value interval_end = std::max(t_start, t_end);
  for (int other_support = 0; other_support < instance.n; ++other_support) {
    if (other_support == current_support) {
      continue;
    }
    const auto& other_trajectory =
        instance.trajectories[static_cast<Index>(other_support)];
    if (interval_start < support_trajectory.t_breaks.front() ||
        interval_end > support_trajectory.t_breaks.back() ||
        interval_start < other_trajectory.t_breaks.front() ||
        interval_end > other_trajectory.t_breaks.back()) {
      throw std::out_of_range(
          "support change interval exceeds a trajectory's time domain");
    }

    std::vector<Value> breaks{interval_start, interval_end};
    for (const Value time : support_trajectory.t_breaks) {
      if (time > interval_start && time < interval_end) {
        breaks.push_back(time);
      }
    }
    for (const Value time : other_trajectory.t_breaks) {
      if (time > interval_start && time < interval_end) {
        breaks.push_back(time);
      }
    }
    std::sort(breaks.begin(), breaks.end());
    breaks.erase(std::unique(breaks.begin(), breaks.end()), breaks.end());

    for (Index segment = 0; segment + 1U < breaks.size(); ++segment) {
      const Value segment_start = breaks[segment];
      const Value segment_end = breaks[segment + 1U];
      const Value duration = segment_end - segment_start;
      if (duration <= 0.0) {
        continue;
      }
      const Point p1_start = support_trajectory.position(segment_start);
      const Point p2_start = other_trajectory.position(segment_start);
      const Point p1_velocity =
          (support_trajectory.position(segment_end) - p1_start) *
          (1.0 / duration);
      const Point p2_velocity =
          (other_trajectory.position(segment_end) - p2_start) *
          (1.0 / duration);
      const Point station =
          instance.stations[static_cast<Index>(station_id)].pos;
      const Point d1 = station - p1_start;
      const Point d2 = station - p2_start;
      const Value a = p1_velocity.dot(p1_velocity) -
                      p2_velocity.dot(p2_velocity);
      const Value b = -2.0 * d1.dot(p1_velocity) +
                      2.0 * d2.dot(p2_velocity);
      const Value c = d1.dot(d1) - d2.dot(d2);
      for (const Value offset : solve_quadratic(a, b, c)) {
        if (offset < -kTimeEpsilon ||
            offset > duration + kTimeEpsilon) {
          continue;
        }
        const Value event_time =
            std::clamp(segment_start + offset, segment_start, segment_end);
        if ((forward && event_time > t_start + kTimeEpsilon &&
             event_time <= t_end + kTimeEpsilon) ||
            (!forward && event_time < t_start - kTimeEpsilon &&
             event_time >= t_end - kTimeEpsilon)) {
          events.push_back(
              {event_time, station_id, other_support, true});
        }
      }
    }
  }

  std::sort(events.begin(), events.end(),
            [forward](const SupportChangeEvent& lhs,
                      const SupportChangeEvent& rhs) {
              if (lhs.time != rhs.time) {
                return forward ? lhs.time < rhs.time : lhs.time > rhs.time;
              }
              return lhs.new_supporting_point < rhs.new_supporting_point;
            });
  events.erase(std::unique(events.begin(), events.end(),
                           [](const SupportChangeEvent& lhs,
                              const SupportChangeEvent& rhs) {
                             return lhs.new_supporting_point ==
                                        rhs.new_supporting_point &&
                                    std::abs(lhs.time - rhs.time) <=
                                        kTimeEpsilon;
                           }),
               events.end());
  return events;
}

SupportChangeEvent KineticCore::find_next_event(
    const Instance& instance, const std::vector<int>& current_supports,
    Value t_start, Value t_end, bool forward) {
  validate_instance_shapes(instance);
  if (current_supports.size() != static_cast<Index>(instance.m)) {
    throw std::invalid_argument(
        "current_supports size must match the number of stations");
  }
  SupportChangeEvent nearest;
  nearest.time = forward ? std::numeric_limits<Value>::infinity()
                         : -std::numeric_limits<Value>::infinity();
  for (int station_id = 0; station_id < instance.m; ++station_id) {
    const int support = current_supports[static_cast<Index>(station_id)];
    if (support < 0) {
      continue;
    }
    const auto events = find_support_changes(instance, station_id, support,
                                             t_start, t_end, forward);
    if (!events.empty() &&
        ((!nearest.valid && forward) ||
         (forward && events.front().time < nearest.time) ||
         (!forward && !nearest.valid) ||
         (!forward && events.front().time > nearest.time))) {
      nearest = events.front();
    }
  }
  if (!nearest.valid) {
    nearest.time = -1.0;
  }
  return nearest;
}

int KineticCore::resolve_degeneracy(const Instance& instance, int station_id,
                                    const std::vector<int>& candidates,
                                    Value time) {
  validate_instance_shapes(instance);
  if (station_id < 0 || station_id >= instance.m) {
    throw std::out_of_range("station id is outside the instance");
  }
  if (!std::isfinite(time) || time < 0.0 || time > instance.T_end) {
    throw std::out_of_range("degeneracy time is outside [0, T_end]");
  }
  const Point station =
      instance.stations[static_cast<Index>(station_id)].pos;
  int best_candidate = -1;
  Value best_derivative = -std::numeric_limits<Value>::infinity();
  for (const int candidate : candidates) {
    if (candidate < 0 || candidate >= instance.n) {
      throw std::out_of_range("candidate point is outside the instance");
    }
    const auto& trajectory =
        instance.trajectories[static_cast<Index>(candidate)];
    const Point position = trajectory.position(time);
    const Point velocity = segment_velocity(trajectory, time);
    const Value derivative = 2.0 * (position - station).dot(velocity);
    if (best_candidate < 0 || derivative > best_derivative) {
      best_candidate = candidate;
      best_derivative = derivative;
    }
  }
  return best_candidate;
}

int KineticCore::second_furthest_assigned(
    const Instance& instance, int station_id,
    const std::vector<int>& assigned_points, Value time) {
  validate_instance_shapes(instance);
  if (station_id < 0 || station_id >= instance.m) {
    throw std::out_of_range("station id is outside the instance");
  }
  if (!std::isfinite(time) || time < 0.0 || time > instance.T_end) {
    throw std::out_of_range("assignment time is outside [0, T_end]");
  }
  if (assigned_points.size() < 2U) {
    return -1;
  }

  const Point station =
      instance.stations[static_cast<Index>(station_id)].pos;
  std::vector<std::pair<Value, int>> distances;
  distances.reserve(assigned_points.size());
  for (const int point_id : assigned_points) {
    if (point_id < 0 || point_id >= instance.n) {
      throw std::out_of_range("assigned point is outside the instance");
    }
    const Point point =
        instance.trajectories[static_cast<Index>(point_id)].position(time);
    distances.emplace_back((station - point).norm(), point_id);
  }
  std::sort(distances.begin(), distances.end(),
            [](const auto& lhs, const auto& rhs) {
              if (lhs.first != rhs.first) {
                return lhs.first > rhs.first;
              }
              return lhs.second < rhs.second;
            });
  return distances[1].second;
}

std::vector<HandoverEvent> KineticCore::find_handovers(
    const Instance& instance, int station_from, int station_to,
    const std::vector<int>& current_supports, Value t_start, Value t_end,
    bool forward) {
  LOG_DEBUG("find_handovers: y1={}, y2={}", station_from, station_to);
  validate_instance_shapes(instance);
  if (station_from < 0 || station_from >= instance.m || station_to < 0 ||
      station_to >= instance.m || station_from == station_to) {
    throw std::out_of_range("handover station ids must be distinct and valid");
  }
  if (current_supports.size() != static_cast<Index>(instance.m)) {
    throw std::invalid_argument(
        "current_supports size must match the number of stations");
  }
  const int support_from =
      current_supports[static_cast<Index>(station_from)];
  const int support_to = current_supports[static_cast<Index>(station_to)];
  validate_station_support(instance, station_from, support_from);
  validate_station_support(instance, station_to, support_to);

  const Point from_position =
      instance.stations[static_cast<Index>(station_from)].pos;
  const Point current_from_point =
      instance.trajectories[static_cast<Index>(support_from)].position(t_start);
  const Value current_radius = (from_position - current_from_point).norm();
  std::vector<int> assigned_from;
  assigned_from.reserve(static_cast<Index>(instance.n));
  for (int point_id = 0; point_id < instance.n; ++point_id) {
    const Point point =
        instance.trajectories[static_cast<Index>(point_id)].position(t_start);
    if ((from_position - point).norm() <= current_radius + kTimeEpsilon) {
      assigned_from.push_back(point_id);
    }
  }
  const int second_support = second_furthest_assigned(
      instance, station_from, assigned_from, t_start);
  if (second_support == -1) {
    return {};
  }

  const auto support_events = find_support_changes(
      instance, station_from, support_from, t_start, t_end, forward);
  std::vector<HandoverEvent> handovers;
  for (const auto& event : support_events) {
    if (event.new_supporting_point == second_support) {
      handovers.push_back({event.time, station_from, station_to, support_to,
                           second_support, support_to, true});
    }
  }
  return handovers;
}

HandoverEvent KineticCore::find_next_handover(
    const Instance& instance, const std::vector<int>& current_supports,
    Value t_start, Value t_end, bool forward) {
  validate_instance_shapes(instance);
  if (current_supports.size() != static_cast<Index>(instance.m)) {
    throw std::invalid_argument(
        "current_supports size must match the number of stations");
  }
  HandoverEvent nearest;
  for (int station_from = 0; station_from < instance.m; ++station_from) {
    if (current_supports[static_cast<Index>(station_from)] < 0) {
      continue;
    }
    for (int station_to = 0; station_to < instance.m; ++station_to) {
      if (station_from == station_to ||
          current_supports[static_cast<Index>(station_to)] < 0) {
        continue;
      }
      const auto events = find_handovers(
          instance, station_from, station_to, current_supports, t_start,
          t_end, forward);
      if (!events.empty() &&
          (!nearest.valid ||
           (forward && events.front().time < nearest.time) ||
           (!forward && events.front().time > nearest.time))) {
        nearest = events.front();
      }
    }
  }
  return nearest;
}
}
