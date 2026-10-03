#include "kdc/kinetic.hpp"

#include "kdc/candidate.hpp"
#include "kdc/logging.hpp"
#include "kdc/profiling.hpp"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <limits>
#include <numeric>
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
  KDC_PROFILE_PHASE(ProfilePhase::TRAJECTORY_POSITION);
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

std::vector<SupportChangeEvent> support_changes_impl(
    const Instance& instance, const InstancePrecompute& precompute,
    int station_id, int current_support, Value t_start, Value t_end,
    bool forward, SolverBudget* budget, bool nearest_only = false,
    int candidate_filter = -1) {
  std::vector<SupportChangeEvent> events;
  SupportChangeEvent nearest;
  const auto& support_trajectory =
      precompute.trajectories[static_cast<Index>(current_support)];
  const Value interval_start = std::min(t_start, t_end);
  const Value interval_end = std::max(t_start, t_end);
  const Point station =
      precompute.stations[static_cast<Index>(station_id)].pos;
  for (int other_support = 0; other_support < instance.n; ++other_support) {
    if (budget != nullptr) {
      budget->checkpoint();
    }
    if (other_support == current_support) {
      continue;
    }
    const auto& other_trajectory =
        precompute.trajectories[static_cast<Index>(other_support)];
    if (interval_start < support_trajectory.t_breaks.front() ||
        interval_end > support_trajectory.t_breaks.back() ||
        interval_start < other_trajectory.t_breaks.front() ||
        interval_end > other_trajectory.t_breaks.back()) {
      throw std::out_of_range(
          "support change interval exceeds a trajectory's time domain");
    }

    std::vector<Value> breaks{interval_start};
    breaks.reserve(support_trajectory.t_breaks.size() +
                   other_trajectory.t_breaks.size() + 2U);
    auto support_break =
        std::upper_bound(support_trajectory.t_breaks.begin(),
                         support_trajectory.t_breaks.end(), interval_start);
    const auto support_end =
        std::lower_bound(support_break, support_trajectory.t_breaks.end(),
                         interval_end);
    auto other_break =
        std::upper_bound(other_trajectory.t_breaks.begin(),
                         other_trajectory.t_breaks.end(), interval_start);
    const auto other_end =
        std::lower_bound(other_break, other_trajectory.t_breaks.end(),
                         interval_end);
    while (support_break != support_end || other_break != other_end) {
      if (budget != nullptr) {
        budget->checkpoint();
      }
      Value next_break = 0.0;
      if (other_break == other_end ||
          (support_break != support_end &&
           *support_break <= *other_break)) {
        next_break = *support_break++;
      } else {
        next_break = *other_break++;
      }
      if (next_break != breaks.back()) {
        breaks.push_back(next_break);
      }
    }
    if (interval_end != breaks.back()) {
      breaks.push_back(interval_end);
    }

    for (Index segment = 0; segment + 1U < breaks.size(); ++segment) {
      if (budget != nullptr) {
        budget->checkpoint();
      }
      const Value segment_start = breaks[segment];
      const Value segment_end = breaks[segment + 1U];
      const Value duration = segment_end - segment_start;
      if (duration <= 0.0) {
        continue;
      }
      const Value midpoint = segment_start + duration / 2.0;
      const Index support_segment = precompute.segment_index(
          static_cast<Index>(current_support), midpoint);
      const Index other_segment = precompute.segment_index(
          static_cast<Index>(other_support), midpoint);
      const Point p1_start =
          precompute.position(static_cast<Index>(current_support),
                              segment_start);
      const Point p2_start =
          precompute.position(static_cast<Index>(other_support),
                              segment_start);
      const Point p1_velocity =
          precompute.trajectory_segments[static_cast<Index>(current_support)]
              [support_segment]
                  .velocity;
      const Point p2_velocity =
          precompute.trajectory_segments[static_cast<Index>(other_support)]
              [other_segment]
                  .velocity;
      const Point d1 = station - p1_start;
      const Point d2 = station - p2_start;
      const Value a = p1_velocity.dot(p1_velocity) -
                      p2_velocity.dot(p2_velocity);
      const Value b = -2.0 * d1.dot(p1_velocity) +
                      2.0 * d2.dot(p2_velocity);
      const Value c = d1.dot(d1) - d2.dot(d2);
      for (const Value offset : KineticCore::solve_quadratic(a, b, c)) {
        if (budget != nullptr) {
          budget->checkpoint();
        }
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
          if (candidate_filter >= 0 && candidate_filter != other_support) {
            continue;
          }
          const SupportChangeEvent candidate{
              event_time, station_id, other_support, true};
          if (!nearest_only) {
            events.push_back(candidate);
          } else if (!nearest.valid ||
                     (forward && candidate.time < nearest.time) ||
                     (!forward && candidate.time > nearest.time) ||
                     (candidate.time == nearest.time &&
                      candidate.new_supporting_point <
                          nearest.new_supporting_point)) {
            nearest = candidate;
          }
        }
      }
    }
  }

  if (nearest_only) {
    if (nearest.valid) {
      events.push_back(nearest);
    }
    return events;
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

std::vector<int> points_owned_by(const std::vector<int>& owners,
                                 int station_id) {
  std::vector<int> points;
  for (Index point = 0; point < owners.size(); ++point) {
    if (owners[point] == station_id) {
      points.push_back(static_cast<int>(point));
    }
  }
  return points;
}

void validate_owners(const Instance& instance,
                     const std::vector<int>& owners) {
  if (owners.size() != static_cast<Index>(instance.n)) {
    throw std::invalid_argument(
        "assigned_points size must match the number of points");
  }
  for (const int owner : owners) {
    if (owner < 0 || owner >= instance.m) {
      throw std::out_of_range("assigned point owner is outside the instance");
    }
  }
}

bool enters_receiving_disk(const InstancePrecompute& precompute,
                           int station_id, int candidate, int support,
                           Value time, bool forward) {
  const Point station = precompute.stations[static_cast<Index>(station_id)].pos;
  const Point point =
      precompute.position(static_cast<Index>(candidate), time);
  const Point support_position =
      precompute.position(static_cast<Index>(support), time);
  const Value candidate_rate =
      2.0 * (point - station)
                .dot(precompute.velocity(static_cast<Index>(candidate), time));
  const Value support_rate =
      2.0 * (support_position - station)
                .dot(precompute.velocity(static_cast<Index>(support), time));
  const Value derivative = candidate_rate - support_rate;
  const Value tolerance =
      64.0 * std::numeric_limits<Value>::epsilon() *
      std::max(std::abs(candidate_rate), std::abs(support_rate));
  return (forward ? 1.0 : -1.0) * derivative < -tolerance;
}

HandoverEvent make_nonincreasing_handover(
    const Instance& instance, const InstancePrecompute& precompute,
    const std::vector<int>& owners, int station_from, int station_to,
    int point_id, Value time, bool forward) {
  if (point_id < 0 || point_id >= instance.n || station_from < 0 ||
      station_from >= instance.m || station_to < 0 ||
      station_to >= instance.m || station_from == station_to ||
      owners.size() != static_cast<Index>(instance.n) ||
      owners[static_cast<Index>(point_id)] != station_from) {
    return {};
  }
  std::vector<Value> before_radii_squared(static_cast<Index>(instance.m), 0.0);
  std::vector<Value> after_radii_squared(static_cast<Index>(instance.m), 0.0);
  std::vector<int> before_supports(static_cast<Index>(instance.m), -1);
  std::vector<int> after_supports(static_cast<Index>(instance.m), -1);
  std::vector<Value> before_tie_breakers(static_cast<Index>(instance.m), 0.0);
  std::vector<Value> after_tie_breakers(static_cast<Index>(instance.m), 0.0);
  const Value direction = forward ? 1.0 : -1.0;

  for (int current_point = 0; current_point < instance.n; ++current_point) {
    const int owner = owners[static_cast<Index>(current_point)];
    const Index owner_index = static_cast<Index>(owner);
    const Point position =
        precompute.position(static_cast<Index>(current_point), time);
    const Value before_distance_squared =
        (position - instance.stations[owner_index].pos).norm2();
    const Value before_rate =
        2.0 * (position - instance.stations[owner_index].pos)
                  .dot(precompute.velocity(static_cast<Index>(current_point),
                                           time, !forward));
    const Value before_tie_breaker = -direction * before_rate;
    const Value before_tolerance =
        64.0 * std::numeric_limits<Value>::epsilon() *
        std::max({1.0, before_distance_squared,
                  before_radii_squared[owner_index]});
    if (before_supports[owner_index] < 0 ||
        before_distance_squared >
            before_radii_squared[owner_index] + before_tolerance ||
        (std::abs(before_distance_squared -
                  before_radii_squared[owner_index]) <= before_tolerance &&
         before_tie_breaker > before_tie_breakers[owner_index])) {
      before_radii_squared[owner_index] = before_distance_squared;
      before_supports[owner_index] = current_point;
      before_tie_breakers[owner_index] = before_tie_breaker;
    }

    const int new_owner =
        current_point == point_id ? station_to : owner;
    const Index new_owner_index = static_cast<Index>(new_owner);
    const Value after_distance_squared =
        (position - instance.stations[new_owner_index].pos).norm2();
    const Value after_rate =
        2.0 * (position - instance.stations[new_owner_index].pos)
                  .dot(precompute.velocity(static_cast<Index>(current_point),
                                           time, forward));
    const Value after_tie_breaker = direction * after_rate;
    const Value after_tolerance =
        64.0 * std::numeric_limits<Value>::epsilon() *
        std::max({1.0, after_distance_squared,
                  after_radii_squared[new_owner_index]});
    if (after_supports[new_owner_index] < 0 ||
        after_distance_squared >
            after_radii_squared[new_owner_index] + after_tolerance ||
        (std::abs(after_distance_squared -
                  after_radii_squared[new_owner_index]) <= after_tolerance &&
         after_tie_breaker > after_tie_breakers[new_owner_index])) {
      after_radii_squared[new_owner_index] = after_distance_squared;
      after_supports[new_owner_index] = current_point;
      after_tie_breakers[new_owner_index] = after_tie_breaker;
    }
  }
  if (before_supports[static_cast<Index>(station_from)] != point_id ||
      after_supports[static_cast<Index>(station_from)] < 0 ||
      after_supports[static_cast<Index>(station_to)] < 0) {
    return {};
  }

  const Value pi = std::acos(-1.0);
  const Value before_area =
      std::accumulate(before_radii_squared.begin(),
                      before_radii_squared.end(), 0.0) *
      pi;
  const Value after_area =
      std::accumulate(after_radii_squared.begin(), after_radii_squared.end(),
                      0.0) *
      pi;
  const Value tolerance =
      1e-9 * std::max({1.0, std::abs(before_area), std::abs(after_area)});
  if (after_area > before_area + tolerance) {
    return {};
  }
  return {time, station_from, station_to, point_id,
          after_supports[static_cast<Index>(station_from)],
          after_supports[static_cast<Index>(station_to)], true};
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
    Value t_start, Value t_end, bool forward, SolverBudget* budget) {
  KDC_PROFILE_PHASE(ProfilePhase::SUPPORT_EVENT_DETECTION);
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

  const auto precomputed = CandidateSet::precompute(instance, budget);
  return support_changes_impl(instance, *precomputed, station_id,
                              current_support, t_start, t_end, forward,
                              budget);
}

SupportChangeEvent KineticCore::find_next_event(
    const Instance& instance, const std::vector<int>& current_supports,
    Value t_start, Value t_end, bool forward, SolverBudget* budget) {
  KDC_PROFILE_PHASE(ProfilePhase::SUPPORT_EVENT_DETECTION);
  validate_instance_shapes(instance);
  if (current_supports.size() != static_cast<Index>(instance.m)) {
    throw std::invalid_argument(
        "current_supports size must match the number of stations");
  }
  SupportChangeEvent nearest;
  nearest.time = forward ? std::numeric_limits<Value>::infinity()
                         : -std::numeric_limits<Value>::infinity();
  const auto precomputed = CandidateSet::precompute(instance, budget);
  for (int station_id = 0; station_id < instance.m; ++station_id) {
    if (budget != nullptr) {
      budget->checkpoint();
    }
    const int support = current_supports[static_cast<Index>(station_id)];
    if (support < 0) {
      continue;
    }
    const auto events = support_changes_impl(
        instance, *precomputed, station_id, support, t_start, t_end, forward,
        budget, true);
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
                                    Value time, SolverBudget* budget) {
  validate_instance_shapes(instance);
  if (station_id < 0 || station_id >= instance.m) {
    throw std::out_of_range("station id is outside the instance");
  }
  if (!std::isfinite(time) || time < 0.0 || time > instance.T_end) {
    throw std::out_of_range("degeneracy time is outside [0, T_end]");
  }
  const Point station =
      instance.stations[static_cast<Index>(station_id)].pos;
  const auto precomputed = CandidateSet::precompute(instance, budget);
  int best_candidate = -1;
  Value best_derivative = -std::numeric_limits<Value>::infinity();
  for (const int candidate : candidates) {
    if (budget != nullptr) {
      budget->checkpoint();
    }
    if (candidate < 0 || candidate >= instance.n) {
      throw std::out_of_range("candidate point is outside the instance");
    }
    const Point position =
        precomputed->position(static_cast<Index>(candidate), time);
    const Point velocity =
        precomputed->velocity(static_cast<Index>(candidate), time);
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
    const std::vector<int>& assigned_points, Value time,
    SolverBudget* budget) {
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
  const auto precomputed = CandidateSet::precompute(instance, budget);
  std::vector<std::pair<Value, int>> distances;
  distances.reserve(assigned_points.size());
  for (const int point_id : assigned_points) {
    if (budget != nullptr) {
      budget->checkpoint();
    }
    if (point_id < 0 || point_id >= instance.n) {
      throw std::out_of_range("assigned point is outside the instance");
    }
    const Point point =
        precomputed->position(static_cast<Index>(point_id), time);
    distances.emplace_back((station - point).norm2(), point_id);
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
    const std::vector<int>& current_supports,
    const std::vector<int>& assigned_points, Value t_start, Value t_end,
    bool forward, SolverBudget* budget) {
  KDC_PROFILE_PHASE(ProfilePhase::HANDOVER_DETECTION);
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
  validate_owners(instance, assigned_points);
  const int support_from =
      current_supports[static_cast<Index>(station_from)];
  const int support_to =
      current_supports[static_cast<Index>(station_to)];
  validate_station_support(instance, station_from, support_from);
  validate_station_support(instance, station_to, support_to);

  if (assigned_points[static_cast<Index>(support_from)] != station_from ||
      assigned_points[static_cast<Index>(support_to)] != station_to) {
    throw std::invalid_argument(
        "handover supports must belong to their respective stations");
  }
  const auto source_points = points_owned_by(assigned_points, station_from);
  if (source_points.size() < 2U) {
    return {};
  }

  const auto precomputed = CandidateSet::precompute(instance, budget);
  const auto support_events = support_changes_impl(
      instance, *precomputed, station_to, support_to, t_start, t_end,
      forward, budget, false, support_from);
  std::vector<HandoverEvent> handovers;
  for (const auto& event : support_events) {
    if (budget != nullptr) {
      budget->checkpoint();
    }
    if (event.new_supporting_point == support_from &&
        enters_receiving_disk(*precomputed, station_to, support_from,
                              support_to, event.time, forward)) {
      const auto handover = make_nonincreasing_handover(
          instance, *precomputed, assigned_points, station_from, station_to,
          support_from, event.time, forward);
      if (handover.valid) {
        handovers.push_back(handover);
      }
    }
  }
  return handovers;
}

std::vector<HandoverEvent> KineticCore::find_handovers_from(
    const Instance& instance, int station_from,
    const std::vector<int>& current_supports,
    const std::vector<int>& assigned_points, Value t_start, Value t_end,
    bool forward, SolverBudget* budget) {
  KDC_PROFILE_PHASE(ProfilePhase::HANDOVER_DETECTION);
  validate_instance_shapes(instance);
  if (station_from < 0 || station_from >= instance.m) {
    throw std::out_of_range("handover source station is invalid");
  }
  if (current_supports.size() != static_cast<Index>(instance.m)) {
    throw std::invalid_argument(
        "current_supports size must match the number of stations");
  }
  validate_owners(instance, assigned_points);
  const int support_from =
      current_supports[static_cast<Index>(station_from)];
  validate_station_support(instance, station_from, support_from);
  if (assigned_points[static_cast<Index>(support_from)] != station_from) {
    throw std::invalid_argument(
        "handover source support must belong to its station");
  }
  const auto source_points = points_owned_by(assigned_points, station_from);
  if (source_points.size() < 2U) {
    return {};
  }
  const auto precomputed = CandidateSet::precompute(instance, budget);
  std::vector<HandoverEvent> handovers;
  for (int station_to = 0; station_to < instance.m; ++station_to) {
    if (budget != nullptr) {
      budget->checkpoint();
    }
    if (station_to == station_from ||
        current_supports[static_cast<Index>(station_to)] < 0) {
      continue;
    }
    const int support_to =
        current_supports[static_cast<Index>(station_to)];
    validate_station_support(instance, station_to, support_to);
    if (assigned_points[static_cast<Index>(support_to)] != station_to) {
      throw std::invalid_argument(
          "handover receiver support must belong to its station");
    }
    const auto support_events = support_changes_impl(
        instance, *precomputed, station_to, support_to, t_start, t_end,
        forward, budget, false, support_from);
    for (const auto& event : support_events) {
      if (budget != nullptr) {
        budget->checkpoint();
      }
      if (event.new_supporting_point == support_from &&
          enters_receiving_disk(*precomputed, station_to, support_from,
                                support_to, event.time, forward)) {
        const auto handover = make_nonincreasing_handover(
            instance, *precomputed, assigned_points, station_from, station_to,
            support_from, event.time, forward);
        if (handover.valid) {
          handovers.push_back(handover);
        }
      }
    }
  }
  return handovers;
}

HandoverEvent KineticCore::find_next_handover(
    const Instance& instance, const std::vector<int>& current_supports,
    const std::vector<int>& assigned_points, Value t_start, Value t_end,
    bool forward, SolverBudget* budget) {
  KDC_PROFILE_PHASE(ProfilePhase::HANDOVER_DETECTION);
  validate_instance_shapes(instance);
  if (current_supports.size() != static_cast<Index>(instance.m)) {
    throw std::invalid_argument(
        "current_supports size must match the number of stations");
  }
  validate_owners(instance, assigned_points);
  HandoverEvent nearest;
  const auto precomputed = CandidateSet::precompute(instance, budget);
  for (int station_from = 0; station_from < instance.m; ++station_from) {
    if (budget != nullptr) {
      budget->checkpoint();
    }
    if (current_supports[static_cast<Index>(station_from)] < 0) {
      continue;
    }
    const int support_from =
        current_supports[static_cast<Index>(station_from)];
    validate_station_support(instance, station_from, support_from);
    if (assigned_points[static_cast<Index>(support_from)] != station_from) {
      throw std::invalid_argument(
          "handover source support must belong to its station");
    }
    const auto source_points = points_owned_by(assigned_points, station_from);
    if (source_points.size() < 2U) {
      continue;
    }
    for (int station_to = 0; station_to < instance.m; ++station_to) {
      if (budget != nullptr) {
        budget->checkpoint();
      }
      if (station_from == station_to ||
          current_supports[static_cast<Index>(station_to)] < 0) {
        continue;
      }
      const int support_to =
          current_supports[static_cast<Index>(station_to)];
      validate_station_support(instance, station_to, support_to);
      if (assigned_points[static_cast<Index>(support_to)] != station_to) {
        throw std::invalid_argument(
            "handover receiver support must belong to its station");
      }
      const auto support_events = support_changes_impl(
          instance, *precomputed, station_to, support_to, t_start, t_end,
          forward, budget, false, support_from);
      for (const auto& event : support_events) {
        if (!enters_receiving_disk(*precomputed, station_to, support_from,
                                   support_to, event.time, forward)) {
          continue;
        }
        const auto handover = make_nonincreasing_handover(
            instance, *precomputed, assigned_points, station_from, station_to,
            support_from, event.time, forward);
        if (budget != nullptr) {
          budget->checkpoint();
        }
        if (handover.valid &&
            (!nearest.valid ||
             (forward && handover.time < nearest.time) ||
             (!forward && handover.time > nearest.time))) {
          nearest = handover;
        }
      }
    }
  }
  return nearest;
}
}
