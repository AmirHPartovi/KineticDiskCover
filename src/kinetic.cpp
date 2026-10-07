#include "kdc/kinetic.hpp"

#include "kdc/candidate.hpp"
#include "kdc/logging.hpp"
#include "kdc/profiling.hpp"

#include <algorithm>
#include <array>
#include <chrono>
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
constexpr Value kSupportComparisonUlps = 64.0;

Value comparison_tolerance(Value first, Value second) {
  return kSupportComparisonUlps * std::numeric_limits<Value>::epsilon() *
         std::max({1.0, std::abs(first), std::abs(second)});
}

int compare_numeric(Value first, Value second) {
  const Value tolerance = comparison_tolerance(first, second);
  if (first > second + tolerance) {
    return 1;
  }
  if (second > first + tolerance) {
    return -1;
  }
  return 0;
}

class DiagnosticsTimer {
 public:
  DiagnosticsTimer(KineticEventDiagnostics* diagnostics,
                   std::uint64_t KineticEventDiagnostics::*field) noexcept
      : diagnostics_(diagnostics), field_(field) {
    if (diagnostics_ != nullptr) {
      start_ = std::chrono::steady_clock::now();
    }
  }

  ~DiagnosticsTimer() {
    if (diagnostics_ != nullptr) {
      const auto elapsed =
          std::chrono::duration_cast<std::chrono::nanoseconds>(
              std::chrono::steady_clock::now() - start_)
              .count();
      diagnostics_->*field_ += static_cast<std::uint64_t>(elapsed);
    }
  }

 private:
  KineticEventDiagnostics* diagnostics_;
  std::uint64_t KineticEventDiagnostics::*field_;
  std::chrono::steady_clock::time_point start_{};
};

bool is_supported_event_engine(KineticEventEngine engine) {
  return engine == KineticEventEngine::REFERENCE_EXHAUSTIVE ||
         engine == KineticEventEngine::KINETIC_TOURNAMENT;
}

void require_supported_event_engine(KineticEventEngine engine) {
  if (!is_supported_event_engine(engine)) {
    throw std::invalid_argument("unsupported kinetic event engine");
  }
}

void require_reference_engine(KineticEventEngine engine) {
  if (engine != KineticEventEngine::REFERENCE_EXHAUSTIVE &&
      engine != KineticEventEngine::KINETIC_TOURNAMENT) {
    throw std::invalid_argument("unsupported kinetic event engine");
  }
}

void validate_instance_shapes(const Instance& instance) {
  if (instance.n < 0 || instance.m < 0 ||
      static_cast<Index>(instance.n) != instance.trajectories.size() ||
      static_cast<Index>(instance.m) != instance.stations.size()) {
    throw std::invalid_argument("kinetic operation received inconsistent instance dimensions");
  }
}

int exact_support_winner(const Instance& instance, int station_id,
                        const std::vector<int>& assigned_points, double time,
                        SolverBudget* budget) {
  return KineticCore::resolve_support_at_time(instance, station_id,
                                              assigned_points, time, budget);
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
    int candidate_filter = -1,
    KineticEventEngine engine =
        KineticEventEngine::REFERENCE_EXHAUSTIVE,
    KineticEventDiagnostics* diagnostics = nullptr) {
  require_supported_event_engine(engine);
  DiagnosticsTimer timer(
      diagnostics,
      &KineticEventDiagnostics::support_event_detection_nanoseconds);
  if (diagnostics != nullptr) {
    ++diagnostics->station_support_event_searches;
  }
  std::vector<SupportChangeEvent> events;
  SupportChangeEvent nearest;
  const auto& support_trajectory =
      precompute.trajectories[static_cast<Index>(current_support)];
  const Value interval_start = std::min(t_start, t_end);
  const Value interval_end = std::max(t_start, t_end);
  const Point station =
      precompute.stations[static_cast<Index>(station_id)].pos;
  const int first_candidate =
      candidate_filter >= 0 ? candidate_filter : 0;
  const int candidate_end =
      candidate_filter >= 0 ? candidate_filter + 1 : instance.n;
  for (int other_support = first_candidate; other_support < candidate_end;
       ++other_support) {
    if (budget != nullptr) {
      budget->checkpoint();
    }
    if (other_support == current_support) {
      continue;
    }
    if (diagnostics != nullptr) {
      ++diagnostics->point_vs_support_comparisons;
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

    std::vector<Value> event_breakpoints{interval_start};
    event_breakpoints.reserve(support_trajectory.t_breaks.size() +
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
      if (next_break != event_breakpoints.back()) {
        event_breakpoints.push_back(next_break);
      }
    }
    if (interval_end != event_breakpoints.back()) {
      event_breakpoints.push_back(interval_end);
    }

    for (Index segment = 0; segment + 1U < event_breakpoints.size();
         ++segment) {
      if (budget != nullptr) {
        budget->checkpoint();
      }
      const Value segment_start = event_breakpoints[segment];
      const Value segment_end = event_breakpoints[segment + 1U];
      const Value duration = segment_end - segment_start;
      if (duration <= 0.0) {
        continue;
      }
      if (diagnostics != nullptr) {
        ++diagnostics->trajectory_segment_pair_examinations;
        ++diagnostics->quadratic_equations_solved;
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
      const auto roots = KineticCore::solve_quadratic(a, b, c);
      if (diagnostics != nullptr) {
        diagnostics->real_roots_found +=
            static_cast<std::uint64_t>(roots.size());
      }
      for (const Value offset : roots) {
        if (budget != nullptr) {
          budget->checkpoint();
        }
        const Value root_time_tolerance =
            64.0 * std::numeric_limits<Value>::epsilon() *
            std::max({1.0, std::abs(segment_start), std::abs(segment_end)});
        if (offset < -root_time_tolerance ||
            offset > duration + root_time_tolerance) {
          if (diagnostics != nullptr) {
            ++diagnostics->candidate_roots_rejected;
          }
          continue;
        }
        const Value event_time =
            std::clamp(segment_start + offset, segment_start, segment_end);
        if ((forward && event_time > t_start && event_time <= t_end) ||
            (!forward && event_time < t_start && event_time >= t_end)) {
          if (candidate_filter >= 0 && candidate_filter != other_support) {
            if (diagnostics != nullptr) {
              ++diagnostics->candidate_roots_rejected;
            }
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
        } else if (diagnostics != nullptr) {
          ++diagnostics->candidate_roots_rejected;
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
                                    event_times_simultaneous(lhs.time,
                                                             rhs.time);
                           }),
               events.end());
  return events;
}

std::vector<std::vector<int>> points_grouped_by_owner(
    const std::vector<int>& owners, int station_count) {
  std::vector<std::vector<int>> grouped(static_cast<Index>(station_count));
  for (Index point = 0; point < owners.size(); ++point) {
    grouped[static_cast<Index>(owners[point])].push_back(
        static_cast<int>(point));
  }
  return grouped;
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

bool enters_receiving_disk(const Instance& instance, int station_id,
                           int candidate, int support,
                           Value time, bool forward) {
  return KineticCore::compare_support_directional_limit(
             instance, station_id, candidate, support, time, forward) < 0;
}

struct StationSupportAtTime {
  int point{-1};
  Value distance_squared{0.0};
};

StationSupportAtTime station_support_at_time(
    const Instance& instance, const InstancePrecompute& precompute,
    int station_id, const std::vector<int>& station_points, Value time,
    bool forward, int excluded_point, int included_point,
    SolverBudget* budget, KineticEventDiagnostics* diagnostics) {
  std::vector<int> candidates;
  candidates.reserve(station_points.size() + (included_point >= 0 ? 1U : 0U));
  for (const int point_id : station_points) {
    if (point_id == excluded_point) {
      continue;
    }
    candidates.push_back(point_id);
  }
  if (included_point >= 0 &&
      std::find(candidates.begin(), candidates.end(), included_point) ==
          candidates.end()) {
    candidates.push_back(included_point);
  }
  if (diagnostics != nullptr) {
    diagnostics->handover_local_support_points_inspected +=
        static_cast<std::uint64_t>(candidates.size());
  }
  StationSupportAtTime result;
  result.point = KineticCore::resolve_support_directional_limit(
      instance, station_id, candidates, time, forward, budget);
  if (result.point >= 0) {
    const Point station = instance.stations[static_cast<Index>(station_id)].pos;
    const Point position =
        precompute.position(static_cast<Index>(result.point), time);
    result.distance_squared = (position - station).norm2();
  }
  return result;
}

HandoverEvent make_nonincreasing_handover_global(
    const Instance& instance, const InstancePrecompute& precompute,
    const std::vector<int>& owners, int station_from, int station_to,
    int point_id, Value time, bool forward,
    KineticEventDiagnostics* diagnostics = nullptr) {
  if (point_id < 0 || point_id >= instance.n || station_from < 0 ||
      station_from >= instance.m || station_to < 0 ||
      station_to >= instance.m || station_from == station_to ||
      owners.size() != static_cast<Index>(instance.n) ||
      owners[static_cast<Index>(point_id)] != station_from) {
    return {};
  }
  std::vector<std::vector<int>> before_points(static_cast<Index>(instance.m));
  std::vector<std::vector<int>> after_points(static_cast<Index>(instance.m));
  for (int current_point = 0; current_point < instance.n; ++current_point) {
    if (diagnostics != nullptr) {
      ++diagnostics->handover_global_point_scans;
    }
    const int owner = owners[static_cast<Index>(current_point)];
    const int new_owner =
        current_point == point_id ? station_to : owner;
    before_points[static_cast<Index>(owner)].push_back(current_point);
    after_points[static_cast<Index>(new_owner)].push_back(current_point);
  }

  std::vector<Value> before_radii_squared(static_cast<Index>(instance.m), 0.0);
  std::vector<Value> after_radii_squared(static_cast<Index>(instance.m), 0.0);
  std::vector<int> before_supports(static_cast<Index>(instance.m), -1);
  std::vector<int> after_supports(static_cast<Index>(instance.m), -1);
  for (int station_id = 0; station_id < instance.m; ++station_id) {
    const Index station_index = static_cast<Index>(station_id);
    before_supports[station_index] =
        KineticCore::resolve_support_directional_limit(
            instance, station_id, before_points[station_index], time,
            !forward);
    after_supports[station_index] =
        KineticCore::resolve_support_directional_limit(
            instance, station_id, after_points[station_index], time, forward);
    const Point station = instance.stations[station_index].pos;
    if (before_supports[station_index] >= 0) {
      const Point position = precompute.position(
          static_cast<Index>(before_supports[station_index]), time);
      before_radii_squared[station_index] = (position - station).norm2();
    }
    if (after_supports[station_index] >= 0) {
      const Point position = precompute.position(
          static_cast<Index>(after_supports[station_index]), time);
      after_radii_squared[station_index] = (position - station).norm2();
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

HandoverEvent make_nonincreasing_handover(
    const Instance& instance, const InstancePrecompute& precompute,
    const std::vector<int>& owners,
    const std::vector<std::vector<int>>& station_points, int station_from,
    int station_to, int point_id, Value time, bool forward,
    SolverBudget* budget, KineticEventDiagnostics* diagnostics,
    HandoverEvaluation evaluation) {
  if (evaluation == HandoverEvaluation::REFERENCE_GLOBAL) {
    return make_nonincreasing_handover_global(
        instance, precompute, owners, station_from, station_to, point_id, time,
        forward, diagnostics);
  }
  if (evaluation != HandoverEvaluation::LOCAL_EXACT) {
    throw std::invalid_argument("unsupported handover evaluation mode");
  }
  if (point_id < 0 || point_id >= instance.n || station_from < 0 ||
      station_from >= instance.m || station_to < 0 ||
      station_to >= instance.m || station_from == station_to ||
      owners.size() != static_cast<Index>(instance.n) ||
      station_points.size() != static_cast<Index>(instance.m) ||
      owners[static_cast<Index>(point_id)] != station_from) {
    return {};
  }

  const auto before_from = station_support_at_time(
      instance, precompute, station_from,
      station_points[static_cast<Index>(station_from)], time, !forward, -1, -1,
      budget, diagnostics);
  const auto before_to = station_support_at_time(
      instance, precompute, station_to,
      station_points[static_cast<Index>(station_to)], time, !forward, -1, -1,
      budget, diagnostics);
  const auto after_from = station_support_at_time(
      instance, precompute, station_from,
      station_points[static_cast<Index>(station_from)], time, forward, point_id,
      -1, budget, diagnostics);
  const auto after_to = station_support_at_time(
      instance, precompute, station_to,
      station_points[static_cast<Index>(station_to)], time, forward, -1,
      point_id, budget, diagnostics);
  if (before_from.point != point_id || after_from.point < 0 ||
      after_to.point < 0) {
    return {};
  }

  const Value before_local =
      before_from.distance_squared + before_to.distance_squared;
  const Value after_local =
      after_from.distance_squared + after_to.distance_squared;
  if (after_local <= before_local) {
    if (diagnostics != nullptr) {
      ++diagnostics->handover_local_acceptances;
    }
    return {time, station_from, station_to, point_id, after_from.point,
            after_to.point, true};
  }

  if (diagnostics != nullptr) {
    ++diagnostics->handover_global_fallbacks;
  }
  return make_nonincreasing_handover_global(
      instance, precompute, owners, station_from, station_to, point_id, time,
      forward, diagnostics);
}

void record_handover_trace(KineticEventDiagnostics* diagnostics,
                           const HandoverEvent& handover) {
  if (diagnostics == nullptr || !handover.valid) {
    return;
  }
  diagnostics->trace.push_back(
      {handover.time, KineticEventType::HANDOVER, -1, -1, -1,
       handover.from_station, handover.to_station, handover.point_id,
       "station-pair ties use ascending ids; support-root ties use point id",
       handover.new_support_from, handover.new_support_to});
}

int compare_support_at_time_impl(
    const Instance& instance, const InstancePrecompute& precompute,
    int station_id, int first_point, int second_point, Value time) {
  if (first_point == second_point) {
    return 0;
  }
  const Point station = instance.stations[static_cast<Index>(station_id)].pos;
  const Point first_position =
      precompute.position(static_cast<Index>(first_point), time);
  const Point second_position =
      precompute.position(static_cast<Index>(second_point), time);
  const int distance_order =
      compare_numeric((first_position - station).norm2(),
                      (second_position - station).norm2());
  if (distance_order != 0) {
    return distance_order;
  }
  return first_point < second_point ? 1 : -1;
}

int compare_support_directional_limit_impl(
    const Instance& instance, const InstancePrecompute& precompute,
    int station_id, int first_point, int second_point, Value time,
    bool forward) {
  if (first_point == second_point) {
    return 0;
  }
  const Point station = instance.stations[static_cast<Index>(station_id)].pos;
  const Point first_position =
      precompute.position(static_cast<Index>(first_point), time);
  const Point second_position =
      precompute.position(static_cast<Index>(second_point), time);
  const int distance_order =
      compare_numeric((first_position - station).norm2(),
                      (second_position - station).norm2());
  if (distance_order != 0) {
    return distance_order;
  }

  const Point first_velocity = precompute.velocity(
      static_cast<Index>(first_point), time, forward);
  const Point second_velocity = precompute.velocity(
      static_cast<Index>(second_point), time, forward);
  const Value direction = forward ? 1.0 : -1.0;
  const Value first_rate =
      direction * 2.0 * (first_position - station).dot(first_velocity);
  const Value second_rate =
      direction * 2.0 * (second_position - station).dot(second_velocity);
  const int rate_order = compare_numeric(first_rate, second_rate);
  if (rate_order != 0) {
    return rate_order;
  }

  const int acceleration_order =
      compare_numeric(first_velocity.dot(first_velocity),
                      second_velocity.dot(second_velocity));
  if (acceleration_order != 0) {
    return acceleration_order;
  }
  return first_point < second_point ? 1 : -1;
}

}  // namespace

bool event_times_simultaneous(double first, double second) noexcept {
  if (!std::isfinite(first) || !std::isfinite(second)) {
    return first == second;
  }
  const Value scale = std::max({1.0, std::abs(first), std::abs(second)});
  const Value tolerance =
      kTimeEpsilon + 64.0 * std::numeric_limits<Value>::epsilon() * scale;
  return std::abs(first - second) <= tolerance;
}

KineticFarthestTournament::Node::Node(int point_id)
    : point_id(point_id), winner(point_id) {}

void KineticFarthestTournament::rebuild_tree(double time,
                                             SolverBudget* budget) {
  if (instance_ == nullptr) {
    root_.reset();
    winner_ = -1;
    second_winner_ = -1;
    return;
  }
  std::vector<int> ordered = points_;
  std::sort(ordered.begin(), ordered.end());
  ordered.erase(std::unique(ordered.begin(), ordered.end()), ordered.end());

  const auto build = [&](auto& self, std::size_t begin, std::size_t end)
      -> std::unique_ptr<Node> {
    if (begin >= end) {
      return nullptr;
    }
    const std::size_t middle = begin + (end - begin) / 2U;
    auto node = std::make_unique<Node>(ordered[middle]);
    node->left = self(self, begin, middle);
    node->right = self(self, middle + 1U, end);
    recompute_node(node.get(), time, budget);
    return node;
  };
  root_ = build(build, 0U, ordered.size());
  winner_ = root_ ? root_->winner : -1;
  second_winner_ = root_ ? root_->second_winner : -1;
}

void KineticFarthestTournament::recompute_node(Node* node, double time,
                                               SolverBudget* budget) {
  if (node == nullptr) {
    return;
  }
  if (node->left != nullptr) {
    recompute_node(node->left.get(), time, budget);
  }
  if (node->right != nullptr) {
    recompute_node(node->right.get(), time, budget);
  }

  std::array<int, 5> candidates{
      node->point_id,
      node->left ? node->left->winner : -1,
      node->left ? node->left->second_winner : -1,
      node->right ? node->right->winner : -1,
      node->right ? node->right->second_winner : -1};
  int best = -1;
  int runner_up = -1;
  for (const int candidate : candidates) {
    if (candidate < 0 || candidate == best || candidate == runner_up) {
      continue;
    }
    if (best < 0 ||
        KineticCore::compare_support_at_time(
            *instance_, station_id_, candidate, best, time, budget) > 0) {
      runner_up = best;
      best = candidate;
    } else if (runner_up < 0 ||
               KineticCore::compare_support_at_time(
                   *instance_, station_id_, candidate, runner_up, time,
                   budget) > 0) {
      runner_up = candidate;
    }
  }
  node->winner = best;
  node->second_winner = runner_up;
}

void KineticFarthestTournament::initialize(
    const Instance& instance, int station_id,
    const std::vector<int>& assigned_points, double time,
    SolverBudget* budget) {
  validate_instance_shapes(instance);
  if (station_id < 0 || station_id >= instance.m) {
    throw std::out_of_range("station id is outside the instance");
  }
  if (!std::isfinite(time) || time < 0.0 || time > instance.T_end) {
    throw std::out_of_range("tournament time is outside [0, T_end]");
  }
  instance_ = &instance;
  station_id_ = station_id;
  points_.clear();
  points_.reserve(assigned_points.size());
  for (const int point_id : assigned_points) {
    if (point_id < 0 || point_id >= instance.n) {
      throw std::out_of_range("assigned point is outside the instance");
    }
    if (std::find(points_.begin(), points_.end(), point_id) == points_.end()) {
      points_.push_back(point_id);
    }
  }
  rebuild_tree(time, budget);
  winner_ = root_ ? root_->winner : -1;
  second_winner_ = root_ ? root_->second_winner : -1;
}

void KineticFarthestTournament::insert(int point_id, double time,
                                       SolverBudget* budget) {
  if (instance_ == nullptr) {
    throw std::logic_error("tournament is not initialized");
  }
  if (point_id < 0 || point_id >= instance_->n) {
    throw std::out_of_range("assigned point is outside the instance");
  }
  if (std::find(points_.begin(), points_.end(), point_id) == points_.end()) {
    points_.push_back(point_id);
  }
  rebuild_tree(time, budget);
  winner_ = root_ ? root_->winner : -1;
  second_winner_ = root_ ? root_->second_winner : -1;
}

void KineticFarthestTournament::erase(int point_id, double time,
                                      SolverBudget* budget) {
  if (instance_ == nullptr) {
    throw std::logic_error("tournament is not initialized");
  }
  const auto found = std::find(points_.begin(), points_.end(), point_id);
  if (found != points_.end()) {
    points_.erase(found);
  }
  rebuild_tree(time, budget);
  winner_ = root_ ? root_->winner : -1;
  second_winner_ = root_ ? root_->second_winner : -1;
}

void KineticFarthestTournament::update_motion(int point_id, double time,
                                              SolverBudget* budget) {
  if (instance_ == nullptr) {
    throw std::logic_error("tournament is not initialized");
  }
  if (point_id < 0 || point_id >= instance_->n) {
    throw std::out_of_range("assigned point is outside the instance");
  }
  if (std::find(points_.begin(), points_.end(), point_id) == points_.end()) {
    return;
  }
  rebuild_tree(time, budget);
  winner_ = root_ ? root_->winner : -1;
  second_winner_ = root_ ? root_->second_winner : -1;
}

int KineticFarthestTournament::current_winner() const { return winner_; }

int KineticFarthestTournament::second_winner() const {
  return second_winner_;
}

int KineticFarthestTournament::best_except(int point_id) const {
  if (winner_ != point_id) {
    return winner_;
  }
  return second_winner_;
}

double KineticFarthestTournament::next_event_time(double time, bool forward,
                                                  SolverBudget* budget) const {
  if (instance_ == nullptr || points_.empty()) {
    return -1.0;
  }
  const auto precomputed = CandidateSet::precompute(*instance_, budget);
  const Point station =
      instance_->stations[static_cast<Index>(station_id_)].pos;
  double next_time = forward ? std::numeric_limits<double>::infinity()
                             : -std::numeric_limits<double>::infinity();
  for (Index i = 0; i < points_.size(); ++i) {
    for (Index j = i + 1; j < points_.size(); ++j) {
      const int point_i = points_[i];
      const int point_j = points_[j];
      const auto& traj_i =
          precomputed->trajectories[static_cast<Index>(point_i)];
      const auto& traj_j =
          precomputed->trajectories[static_cast<Index>(point_j)];
      std::vector<double> breaks{time};
      breaks.insert(breaks.end(), traj_i.t_breaks.begin(), traj_i.t_breaks.end());
      breaks.insert(breaks.end(), traj_j.t_breaks.begin(), traj_j.t_breaks.end());
      std::sort(breaks.begin(), breaks.end());
      breaks.erase(std::unique(breaks.begin(), breaks.end()), breaks.end());
      for (Index k = 0; k + 1U < breaks.size(); ++k) {
        const double start = breaks[k];
        const double end = breaks[k + 1U];
        const double duration = end - start;
        if (duration <= 0.0) {
          continue;
        }
        const auto p_i_start =
            precomputed->position(static_cast<Index>(point_i), start);
        const auto p_j_start =
            precomputed->position(static_cast<Index>(point_j), start);
        const auto v_i = precomputed->velocity(static_cast<Index>(point_i), start);
        const auto v_j = precomputed->velocity(static_cast<Index>(point_j), start);
        const auto d_i = station - p_i_start;
        const auto d_j = station - p_j_start;
        const double a = v_i.dot(v_i) - v_j.dot(v_j);
        const double b = -2.0 * d_i.dot(v_i) + 2.0 * d_j.dot(v_j);
        const double c = d_i.dot(d_i) - d_j.dot(d_j);
        for (const double offset : KineticCore::solve_quadratic(a, b, c)) {
          const double root_time_tolerance =
              64.0 * std::numeric_limits<double>::epsilon() *
              std::max({1.0, std::abs(start), std::abs(end)});
          if (offset < -root_time_tolerance ||
              offset > duration + root_time_tolerance) {
            continue;
          }
          const double candidate = std::clamp(start + offset, start, end);
          if ((forward && candidate > time) ||
              (!forward && candidate < time)) {
            next_time = forward ? std::min(next_time, candidate)
                               : std::max(next_time, candidate);
          }
        }
      }
    }
  }
  if (!std::isfinite(next_time)) {
    return -1.0;
  }
  return next_time;
}

bool KineticFarthestTournament::process_until(double time, SolverBudget* budget) {
  if (instance_ == nullptr) {
    throw std::logic_error("tournament is not initialized");
  }
  const int previous = winner_;
  rebuild_tree(time, budget);
  winner_ = root_ ? root_->winner : -1;
  second_winner_ = root_ ? root_->second_winner : -1;
  return winner_ != previous;
}

bool KineticFarthestTournament::validate(double time,
                                          SolverBudget* budget) const {
  if (instance_ == nullptr || points_.empty()) {
    return winner_ == -1;
  }
  const int expected = exact_support_winner(*instance_, station_id_, points_, time,
                                           budget);
  return expected == winner_;
}

int KineticCore::compare_support_at_time(
    const Instance& instance, int station_id, int first_point, int second_point,
    Value time, SolverBudget* budget) {
  validate_instance_shapes(instance);
  validate_station_support(instance, station_id, first_point);
  validate_station_support(instance, station_id, second_point);
  if (!std::isfinite(time) || time < 0.0 || time > instance.T_end) {
    throw std::out_of_range("support comparison time is outside [0, T_end]");
  }
  if (first_point == second_point) {
    return 0;
  }
  const auto precomputed = CandidateSet::precompute(instance, budget);
  return compare_support_at_time_impl(instance, *precomputed, station_id,
                                      first_point, second_point, time);
}

int KineticCore::compare_support_directional_limit(
    const Instance& instance, int station_id, int first_point, int second_point,
    Value time, bool forward, SolverBudget* budget) {
  validate_instance_shapes(instance);
  validate_station_support(instance, station_id, first_point);
  validate_station_support(instance, station_id, second_point);
  if (!std::isfinite(time) || time < 0.0 || time > instance.T_end) {
    throw std::out_of_range("support comparison time is outside [0, T_end]");
  }
  if (first_point == second_point) {
    return 0;
  }
  const auto precomputed = CandidateSet::precompute(instance, budget);
  return compare_support_directional_limit_impl(
      instance, *precomputed, station_id, first_point, second_point, time,
      forward);
}

int KineticCore::resolve_support_at_time(
    const Instance& instance, int station_id,
    const std::vector<int>& candidates, Value time, SolverBudget* budget) {
  validate_instance_shapes(instance);
  if (station_id < 0 || station_id >= instance.m || !std::isfinite(time) ||
      time < 0.0 || time > instance.T_end) {
    throw std::out_of_range("exact support resolution is outside its domain");
  }
  const auto precomputed = CandidateSet::precompute(instance, budget);
  int winner = -1;
  for (const int candidate : candidates) {
    if (budget != nullptr) {
      budget->checkpoint();
    }
    validate_station_support(instance, station_id, candidate);
    if (winner < 0 ||
        compare_support_at_time_impl(instance, *precomputed, station_id,
                                     candidate, winner, time) > 0) {
      winner = candidate;
    }
  }
  return winner;
}

int KineticCore::resolve_support_directional_limit(
    const Instance& instance, int station_id,
    const std::vector<int>& candidates, Value time, bool forward,
    SolverBudget* budget) {
  validate_instance_shapes(instance);
  if (station_id < 0 || station_id >= instance.m || !std::isfinite(time) ||
      time < 0.0 || time > instance.T_end) {
    throw std::out_of_range(
        "directional support resolution is outside its domain");
  }
  const auto precomputed = CandidateSet::precompute(instance, budget);
  int winner = -1;
  for (const int candidate : candidates) {
    if (budget != nullptr) {
      budget->checkpoint();
    }
    validate_station_support(instance, station_id, candidate);
    if (winner < 0 ||
        compare_support_directional_limit_impl(
            instance, *precomputed, station_id, candidate, winner, time,
            forward) > 0) {
      winner = candidate;
    }
  }
  return winner;
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
    Value t_start, Value t_end, bool forward, SolverBudget* budget,
    KineticEventEngine engine, KineticEventDiagnostics* diagnostics) {
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
                              budget, false, -1, engine, diagnostics);
}

SupportChangeEvent KineticCore::find_external_challenge(
    const Instance& instance, int receiver_station, int receiver_support,
    int challenger, Value t_start, Value t_end, bool forward,
    SolverBudget* budget, KineticEventEngine engine,
    KineticEventDiagnostics* diagnostics) {
  require_supported_event_engine(engine);
  validate_instance_shapes(instance);
  validate_station_support(instance, receiver_station, receiver_support);
  validate_station_support(instance, receiver_station, challenger);
  if (!std::isfinite(t_start) || !std::isfinite(t_end) ||
      t_start < 0.0 || t_start > instance.T_end || t_end < 0.0 ||
      t_end > instance.T_end ||
      (forward && t_end < t_start) || (!forward && t_end > t_start)) {
    throw std::out_of_range("external challenge interval is invalid");
  }
  if (diagnostics != nullptr) {
    ++diagnostics->external_challenge_certificates;
    ++diagnostics->external_challenge_updates;
  }
  const auto precomputed = CandidateSet::precompute(instance, budget);
  const auto roots = support_changes_impl(
      instance, *precomputed, receiver_station, receiver_support, t_start,
      t_end, forward, budget, false, challenger, engine, diagnostics);
  for (const auto& root : roots) {
    if (budget != nullptr) {
      budget->checkpoint();
    }
    if (compare_support_directional_limit_impl(
            instance, *precomputed, receiver_station, challenger,
            receiver_support, root.time, forward) < 0) {
      return root;
    }
  }
  return {};
}

HandoverEvent KineticCore::evaluate_handover_candidate(
    const Instance& instance, int station_from, int station_to,
    int point_id, int source_support_before, int receiver_support_before,
    int source_support_after_removal, int receiver_support_without_point,
    const std::vector<int>& assigned_points, Value time, bool forward,
    SolverBudget* budget, KineticEventDiagnostics* diagnostics,
    HandoverEvaluation evaluation) {
  validate_instance_shapes(instance);
  validate_owners(instance, assigned_points);
  validate_station_support(instance, station_from, point_id);
  validate_station_support(instance, station_to, point_id);
  validate_station_support(instance, station_from, source_support_before);
  validate_station_support(instance, station_to, receiver_support_before);
  validate_station_support(instance, station_from, source_support_after_removal);
  validate_station_support(instance, station_to,
                           receiver_support_without_point);
  if (station_from == station_to || !std::isfinite(time) || time < 0.0 ||
      time > instance.T_end ||
      assigned_points[static_cast<Index>(point_id)] != station_from ||
      source_support_before != point_id ||
      source_support_after_removal == point_id ||
      assigned_points[static_cast<Index>(source_support_after_removal)] !=
          station_from ||
      assigned_points[static_cast<Index>(receiver_support_before)] !=
          station_to ||
      assigned_points[static_cast<Index>(receiver_support_without_point)] !=
          station_to) {
    return {};
  }
  if (diagnostics != nullptr) {
    ++diagnostics->local_support_queries;
  }
  const int receiver_order = compare_support_directional_limit(
      instance, station_to, point_id, receiver_support_without_point, time,
      forward, budget);
  if (receiver_order >= 0) {
    return {};
  }
  if (evaluation != HandoverEvaluation::LOCAL_EXACT &&
      evaluation != HandoverEvaluation::REFERENCE_GLOBAL) {
    throw std::invalid_argument("unsupported handover evaluation mode");
  }
  if (evaluation == HandoverEvaluation::REFERENCE_GLOBAL) {
    const auto precomputed = CandidateSet::precompute(instance, budget);
    return make_nonincreasing_handover_global(
        instance, *precomputed, assigned_points, station_from, station_to,
        point_id, time, forward, diagnostics);
  }

  const int receiver_support_after =
      receiver_order > 0 ? point_id : receiver_support_without_point;
  const Point station_from_position =
      instance.stations[static_cast<Index>(station_from)].pos;
  const Point station_to_position =
      instance.stations[static_cast<Index>(station_to)].pos;
  const auto squared_radius = [&](int support, const Point& station) {
    const Point displacement =
        instance.trajectories[static_cast<Index>(support)].position(time) -
        station;
    return displacement.norm2();
  };
  const Value before_local =
      squared_radius(source_support_before, station_from_position) +
      squared_radius(receiver_support_before, station_to_position);
  const Value after_local =
      squared_radius(source_support_after_removal, station_from_position) +
      squared_radius(receiver_support_after, station_to_position);
  if (after_local <= before_local) {
    if (diagnostics != nullptr) {
      ++diagnostics->handover_local_acceptances;
    }
    return {time, station_from, station_to, point_id,
            source_support_after_removal, receiver_support_after, true};
  }

  if (diagnostics != nullptr) {
    ++diagnostics->handover_global_fallbacks;
  }
  const auto precomputed = CandidateSet::precompute(instance, budget);
  return make_nonincreasing_handover_global(
      instance, *precomputed, assigned_points, station_from, station_to,
      point_id, time, forward, diagnostics);
}

SupportChangeEvent KineticCore::find_next_event(
    const Instance& instance, const std::vector<int>& current_supports,
    Value t_start, Value t_end, bool forward, SolverBudget* budget,
    KineticEventEngine engine, KineticEventDiagnostics* diagnostics) {
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
        budget, true, -1, engine, diagnostics);
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
  } else if (diagnostics != nullptr) {
    ++diagnostics->selected_support_events;
    diagnostics->trace.push_back(
        {nearest.time, KineticEventType::SUPPORT_CHANGE, nearest.station_id,
         current_supports[static_cast<Index>(nearest.station_id)],
         nearest.new_supporting_point, -1, -1,
         nearest.new_supporting_point,
         "earliest time; station and equal-time candidate ids ordered ascending"});
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
  return resolve_support_directional_limit(instance, station_id, candidates,
                                           time, true, budget);
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
  for (const int point_id : assigned_points) {
    if (budget != nullptr) {
      budget->checkpoint();
    }
    validate_station_support(instance, station_id, point_id);
  }
  KineticFarthestTournament tournament;
  tournament.initialize(instance, station_id, assigned_points, time, budget);
  return tournament.second_winner();
}

std::vector<HandoverEvent> KineticCore::find_handovers(
    const Instance& instance, int station_from, int station_to,
    const std::vector<int>& current_supports,
    const std::vector<int>& assigned_points, Value t_start, Value t_end,
    bool forward, SolverBudget* budget, KineticEventEngine engine,
    KineticEventDiagnostics* diagnostics, HandoverEvaluation evaluation) {
  KDC_PROFILE_PHASE(ProfilePhase::HANDOVER_DETECTION);
  DiagnosticsTimer timer(
      diagnostics, &KineticEventDiagnostics::handover_detection_nanoseconds);
  require_reference_engine(engine);
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
  const auto station_points =
      points_grouped_by_owner(assigned_points, instance.m);
  const auto& source_points =
      station_points[static_cast<Index>(station_from)];
  if (source_points.size() < 2U) {
    return {};
  }

  const auto precomputed = CandidateSet::precompute(instance, budget);
  const auto support_events = support_changes_impl(
      instance, *precomputed, station_to, support_to, t_start, t_end,
      forward, budget, false, support_from, engine, diagnostics);
  std::vector<HandoverEvent> handovers;
  for (const auto& event : support_events) {
    if (diagnostics != nullptr) {
      ++diagnostics->handover_event_checks;
    }
    if (budget != nullptr) {
      budget->checkpoint();
    }
    if (event.new_supporting_point == support_from &&
        enters_receiving_disk(instance, station_to, support_from,
                              support_to, event.time, forward)) {
      const auto handover = make_nonincreasing_handover(
          instance, *precomputed, assigned_points, station_points,
          station_from, station_to, support_from, event.time, forward, budget,
          diagnostics, evaluation);
      if (handover.valid) {
        handovers.push_back(handover);
        record_handover_trace(diagnostics, handover);
      }
    }
  }
  return handovers;
}

std::vector<HandoverEvent> KineticCore::find_handovers_from(
    const Instance& instance, int station_from,
    const std::vector<int>& current_supports,
    const std::vector<int>& assigned_points, Value t_start, Value t_end,
    bool forward, SolverBudget* budget, KineticEventEngine engine,
    KineticEventDiagnostics* diagnostics, HandoverEvaluation evaluation) {
  KDC_PROFILE_PHASE(ProfilePhase::HANDOVER_DETECTION);
  DiagnosticsTimer timer(
      diagnostics, &KineticEventDiagnostics::handover_detection_nanoseconds);
  require_reference_engine(engine);
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
  const auto station_points =
      points_grouped_by_owner(assigned_points, instance.m);
  const auto& source_points =
      station_points[static_cast<Index>(station_from)];
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
        forward, budget, false, support_from, engine, diagnostics);
    for (const auto& event : support_events) {
      if (diagnostics != nullptr) {
        ++diagnostics->handover_event_checks;
      }
      if (budget != nullptr) {
        budget->checkpoint();
      }
      if (event.new_supporting_point == support_from &&
          enters_receiving_disk(instance, station_to, support_from,
                                support_to, event.time, forward)) {
        const auto handover = make_nonincreasing_handover(
            instance, *precomputed, assigned_points, station_points,
            station_from, station_to, support_from, event.time, forward, budget,
            diagnostics, evaluation);
        if (handover.valid) {
          handovers.push_back(handover);
          record_handover_trace(diagnostics, handover);
        }
      }
    }
  }
  return handovers;
}

HandoverEvent KineticCore::find_next_handover(
    const Instance& instance, const std::vector<int>& current_supports,
    const std::vector<int>& assigned_points, Value t_start, Value t_end,
    bool forward, SolverBudget* budget, KineticEventEngine engine,
    KineticEventDiagnostics* diagnostics, HandoverEvaluation evaluation) {
  KDC_PROFILE_PHASE(ProfilePhase::HANDOVER_DETECTION);
  DiagnosticsTimer timer(
      diagnostics, &KineticEventDiagnostics::handover_detection_nanoseconds);
  require_reference_engine(engine);
  validate_instance_shapes(instance);
  if (current_supports.size() != static_cast<Index>(instance.m)) {
    throw std::invalid_argument(
        "current_supports size must match the number of stations");
  }
  validate_owners(instance, assigned_points);
  const auto station_points =
      points_grouped_by_owner(assigned_points, instance.m);
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
    const auto& source_points =
        station_points[static_cast<Index>(station_from)];
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
          forward, budget, false, support_from, engine, diagnostics);
      for (const auto& event : support_events) {
        if (diagnostics != nullptr) {
          ++diagnostics->handover_event_checks;
        }
        if (!enters_receiving_disk(instance, station_to, support_from,
                                   support_to, event.time, forward)) {
          continue;
        }
        const auto handover = make_nonincreasing_handover(
            instance, *precomputed, assigned_points, station_points,
            station_from, station_to, support_from, event.time, forward,
            budget, diagnostics, evaluation);
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
  record_handover_trace(diagnostics, nearest);
  return nearest;
}
}
