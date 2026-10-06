#include "kdc/solution.hpp"

#include "kdc/candidate.hpp"
#include "kdc/kinetic.hpp"
#include "kdc/logging.hpp"
#include "kdc/profiling.hpp"
#include "kdc/stationary.hpp"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

namespace kdc {
namespace {
Value evaluate(const SolutionInterval& interval, Value time) {
  return (interval.a * time + interval.b) * time + interval.c;
}

void validate_interval_request(const KineticSolution& solution, Value start,
                               Value end) {
  if (solution.intervals.empty()) {
    throw std::runtime_error("kinetic solution has no intervals");
  }
  if (!std::isfinite(start) || !std::isfinite(end) || end < start) {
    throw std::invalid_argument("integration bounds must be finite and ordered");
  }
  if (start < solution.intervals.front().t_start ||
      end > solution.intervals.back().t_end) {
    throw std::out_of_range("requested time range is outside the solution");
  }
}

constexpr Value kIntervalTolerance = 1e-9;

class ExtensionDiagnosticsTimer {
 public:
  explicit ExtensionDiagnosticsTimer(
      KineticEventDiagnostics* diagnostics) noexcept
      : diagnostics_(diagnostics) {
    if (diagnostics_ != nullptr) {
      start_ = std::chrono::steady_clock::now();
    }
  }

  ~ExtensionDiagnosticsTimer() {
    if (diagnostics_ != nullptr) {
      const auto elapsed =
          std::chrono::duration_cast<std::chrono::nanoseconds>(
              std::chrono::steady_clock::now() - start_)
              .count();
      diagnostics_->total_extension_nanoseconds +=
          static_cast<std::uint64_t>(elapsed);
    }
  }

 private:
  KineticEventDiagnostics* diagnostics_;
  std::chrono::steady_clock::time_point start_{};
};

class IntervalConstructionTimer {
 public:
  explicit IntervalConstructionTimer(
      KineticEventDiagnostics* diagnostics) noexcept
      : diagnostics_(diagnostics) {
    if (diagnostics_ != nullptr) {
      start_ = std::chrono::steady_clock::now();
    }
  }

  ~IntervalConstructionTimer() {
    stop();
  }

  void stop() noexcept {
    if (diagnostics_ != nullptr) {
      const auto elapsed =
          std::chrono::duration_cast<std::chrono::nanoseconds>(
              std::chrono::steady_clock::now() - start_)
              .count();
      diagnostics_->interval_construction_nanoseconds +=
          static_cast<std::uint64_t>(elapsed);
      diagnostics_ = nullptr;
    }
  }

 private:
  KineticEventDiagnostics* diagnostics_;
  std::chrono::steady_clock::time_point start_{};
};

bool same_coefficients(const SolutionInterval& lhs,
                       const SolutionInterval& rhs) {
  return lhs.supporting_point == rhs.supporting_point &&
         lhs.assigned_points == rhs.assigned_points &&
         std::abs(lhs.a - rhs.a) <= kIntervalTolerance &&
         std::abs(lhs.b - rhs.b) <= kIntervalTolerance &&
         std::abs(lhs.c - rhs.c) <= kIntervalTolerance;
}

const SolutionInterval& interval_at(const KineticSolution& solution,
                                   Value time) {
  auto upper = std::upper_bound(
      solution.intervals.begin(), solution.intervals.end(), time,
      [](Value value, const SolutionInterval& interval) {
        return value < interval.t_start;
      });
  if (upper == solution.intervals.begin()) {
    return solution.intervals.front();
  }
  --upper;
  if (time > upper->t_end + kIntervalTolerance) {
    throw std::invalid_argument("solution intervals do not cover a breakpoint");
  }
  return *upper;
}

void append_interval(KineticSolution& solution,
                     const SolutionInterval& source, Value start, Value end) {
  if (end <= start) {
    return;
  }
  SolutionInterval interval = source;
  interval.t_start = start;
  interval.t_end = end;
  solution.intervals.push_back(std::move(interval));
}

std::vector<Value> common_breakpoints(const KineticSolution& first,
                                      const KineticSolution& second) {
  if (!first.is_well_formed() || !second.is_well_formed()) {
    throw std::invalid_argument("combine requires well-formed solutions");
  }
  std::vector<Value> breakpoints;
  breakpoints.reserve(first.intervals.size() + second.intervals.size() + 2U);
  for (const auto& interval : first.intervals) {
    breakpoints.push_back(interval.t_start);
    breakpoints.push_back(interval.t_end);
  }
  for (const auto& interval : second.intervals) {
    breakpoints.push_back(interval.t_start);
    breakpoints.push_back(interval.t_end);
  }
  std::sort(breakpoints.begin(), breakpoints.end());
  std::vector<Value> unique_breakpoints;
  unique_breakpoints.reserve(breakpoints.size());
  for (const Value breakpoint : breakpoints) {
    if (unique_breakpoints.empty() ||
        std::abs(breakpoint - unique_breakpoints.back()) >
            kIntervalTolerance) {
      unique_breakpoints.push_back(breakpoint);
    }
  }
  if (unique_breakpoints.size() < 2U) {
    throw std::invalid_argument("solutions have no positive duration");
  }
  return unique_breakpoints;
}

Value distance_derivative(const Instance& instance,
                          const InstancePrecompute& precompute,
                          int station_id, int point_id, Value time,
                          bool forward) {
  const Point station =
      instance.stations[static_cast<Index>(station_id)].pos;
  const Point position =
      precompute.position(static_cast<Index>(point_id), time);
  return 2.0 * (position - station)
                   .dot(precompute.velocity(static_cast<Index>(point_id), time,
                                            forward));
}

void compute_quadratic_coeffs_precomputed(
    const Instance& instance, const InstancePrecompute& precompute,
    const std::vector<int>& supporting_points, SolutionInterval& output,
    SolverBudget* budget) {
  Value a = 0.0;
  Value b = 0.0;
  Value c = 0.0;
  const Value pi = std::acos(-1.0);
  const Value midpoint =
      output.t_start + (output.t_end - output.t_start) / 2.0;
  for (Index station_index = 0; station_index < supporting_points.size();
       ++station_index) {
    if (budget != nullptr) {
      budget->checkpoint();
    }
    const int point_index = supporting_points[station_index];
    if (point_index == -1) {
      continue;
    }
    if (point_index < 0 || point_index >= instance.n) {
      throw std::out_of_range("supporting point is outside the instance");
    }
    const auto& segment =
        precompute.trajectory_segments[static_cast<Index>(point_index)]
                                      [precompute.segment_index(
                                          static_cast<Index>(point_index),
                                          midpoint)];
    const Point displacement =
        instance.stations[station_index].pos - segment.affine_origin;
    a += pi * segment.velocity.dot(segment.velocity);
    b += pi * (-2.0 * displacement.dot(segment.velocity));
    c += pi * displacement.dot(displacement);
  }
  output.supporting_point = supporting_points;
  output.a = a;
  output.b = b;
  output.c = c;
}
}

void KineticSolution::compute_quadratic_coeffs(
    const Instance& instance, const std::vector<int>& supporting_points,
    SolutionInterval& output, SolverBudget* budget) {
  LOG_DEBUG("compute_quadratic_coeffs: m={}", supporting_points.size());
  if (instance.n < 0 || instance.m < 0 ||
      static_cast<Index>(instance.n) != instance.trajectories.size() ||
      static_cast<Index>(instance.m) != instance.stations.size()) {
    throw std::invalid_argument(
        "quadratic coefficient calculation received inconsistent dimensions");
  }
  if (supporting_points.size() != static_cast<Index>(instance.m)) {
    throw std::invalid_argument(
        "supporting_points size must match the number of stations");
  }
  if (!std::isfinite(instance.T_end) || instance.T_end <= 0.0) {
    throw std::invalid_argument(
        "quadratic coefficients require a positive finite T_end");
  }
  if (!std::isfinite(output.t_start) || !std::isfinite(output.t_end) ||
      output.t_start < 0.0 || output.t_end > instance.T_end ||
      output.t_start >= output.t_end) {
    throw std::invalid_argument(
        "quadratic coefficients require a positive interval within [0, T_end]");
  }

  const auto precomputed = CandidateSet::precompute(instance, budget);
  compute_quadratic_coeffs_precomputed(instance, *precomputed,
                                       supporting_points, output, budget);
}

KineticSolution KineticSolution::extend(
    const Instance& instance, const StaticAssignment& init_assignment,
    double t_start, double t_end, bool forward, bool use_handovers,
    ObjectiveType objective_type, SolverBudget* budget,
    KineticEventEngine event_engine,
    KineticEventDiagnostics* diagnostics,
    KineticIntervalEmission interval_emission,
    HandoverEvaluation handover_evaluation) {
  KDC_PROFILE_PHASE(ProfilePhase::KINETIC_EXTENSION);
  ExtensionDiagnosticsTimer diagnostics_timer(diagnostics);
  if (event_engine != KineticEventEngine::REFERENCE_EXHAUSTIVE) {
    throw std::invalid_argument("unsupported kinetic event engine");
  }
  if (interval_emission !=
          KineticIntervalEmission::REFERENCE_ALL_TRAJECTORY_BREAKPOINTS &&
      interval_emission !=
          KineticIntervalEmission::EXACT_RELEVANT_BOUNDARIES) {
    throw std::invalid_argument("unsupported kinetic interval emission mode");
  }
  LOG_DEBUG("extend: t_start={}, t_end={}, forward={}", t_start, t_end,
            forward);
  if (!std::isfinite(t_start) || !std::isfinite(t_end) ||
      t_start < 0.0 || t_start > instance.T_end || t_end < 0.0 ||
      t_end > instance.T_end) {
    throw std::out_of_range("extension bounds are outside [0, T_end]");
  }
  if ((forward && t_end < t_start) || (!forward && t_end > t_start)) {
    throw std::invalid_argument("extension bounds disagree with direction");
  }
  if (!init_assignment.feasible ||
      init_assignment.supporting_point.size() !=
          static_cast<Index>(instance.m) ||
      init_assignment.radius.size() != static_cast<Index>(instance.m) ||
      init_assignment.assigned_points.size() !=
          static_cast<Index>(instance.n) ||
      instance.n < 0 || instance.m < 0 ||
      instance.trajectories.size() != static_cast<Index>(instance.n) ||
      instance.stations.size() != static_cast<Index>(instance.m)) {
    throw std::invalid_argument("extension requires a feasible assignment");
  }

  std::vector<int> supports = init_assignment.supporting_point;
  std::vector<int> owners = init_assignment.assigned_points;
  for (const int support : supports) {
    if (budget != nullptr) {
      budget->checkpoint();
    }
    if (support < -1 || support >= instance.n) {
      throw std::out_of_range("initial supporting point is invalid");
    }
  }
  const auto precomputed = CandidateSet::precompute(instance, budget);
  const auto initial_geometry =
      CandidateSet::build_geometry(instance, *precomputed, t_start, budget);
  std::vector<bool> station_has_points(static_cast<Index>(instance.m), false);
  for (int point_id = 0; point_id < instance.n; ++point_id) {
    if (budget != nullptr) {
      budget->checkpoint();
    }
    const int owner = owners[static_cast<Index>(point_id)];
    if (owner < 0 || owner >= instance.m) {
      throw std::invalid_argument("initial assignment has an invalid owner");
    }
    station_has_points[static_cast<Index>(owner)] = true;
  }
  for (int station_id = 0; station_id < instance.m; ++station_id) {
    if (budget != nullptr) {
      budget->checkpoint();
    }
    const Index station_index = static_cast<Index>(station_id);
    if (!std::isfinite(init_assignment.radius[station_index]) ||
        init_assignment.radius[station_index] < 0.0) {
      throw std::invalid_argument("initial assignment has an invalid radius");
    }
    const int support = supports[station_index];
    if ((support < 0 && station_has_points[station_index]) ||
        (support >= 0 &&
         owners[static_cast<Index>(support)] != station_id)) {
      throw std::invalid_argument(
          "initial support and explicit ownership do not agree");
    }
    for (int point_id = 0; point_id < instance.n; ++point_id) {
      if (owners[static_cast<Index>(point_id)] != station_id) {
        continue;
      }
      const Value distance_squared = initial_geometry.distance_squared(
          station_index, static_cast<Index>(point_id),
          static_cast<Index>(instance.n));
      const Value radius = init_assignment.radius[station_index];
      if (distance_squared >
          radius * radius + 2e-9 * radius + 1e-18) {
        throw std::invalid_argument(
            "initial ownership assigns a point outside its disk");
      }
    }
  }
  KineticSolution solution;
  solution.objective = objective_type;
  Value current_time = t_start;
  const Value direction = forward ? 1.0 : -1.0;
  std::vector<Value> event_breakpoints;
  for (const auto& trajectory : precomputed->trajectories) {
    if (budget != nullptr) {
      budget->checkpoint();
    }
    if (diagnostics != nullptr) {
      diagnostics->raw_trajectory_breakpoints +=
          static_cast<std::uint64_t>(trajectory.t_breaks.size());
    }
    event_breakpoints.insert(event_breakpoints.end(),
                             trajectory.t_breaks.begin(),
                             trajectory.t_breaks.end());
  }
  std::sort(event_breakpoints.begin(), event_breakpoints.end());
  event_breakpoints.erase(
      std::unique(event_breakpoints.begin(), event_breakpoints.end()),
      event_breakpoints.end());
  std::size_t event_count = 0U;
  bool have_previous_supports = diagnostics != nullptr;
  while (direction * (t_end - current_time) > kIntervalTolerance) {
    if (budget != nullptr) {
      budget->checkpoint();
    }
    const Value remaining = std::abs(t_end - current_time);
    const Value probe_time =
        std::clamp(current_time + direction * std::min(1e-8, remaining / 4.0),
                   0.0, instance.T_end);
    std::vector<int> previous_supports;
    if (diagnostics != nullptr) {
      previous_supports = supports;
    }
    for (int& support : supports) {
      if (budget != nullptr) {
        budget->checkpoint();
      }
      support = -1;
    }
    std::vector<Value> farthest_distance(static_cast<Index>(instance.m), -1.0);
    for (int point_id = 0; point_id < instance.n; ++point_id) {
      if (budget != nullptr) {
        budget->checkpoint();
      }
      const int station_id = owners[static_cast<Index>(point_id)];
      const Index station_index = static_cast<Index>(station_id);
      const Value distance_squared =
          (precomputed->position(static_cast<Index>(point_id), probe_time) -
           instance.stations[station_index].pos)
              .norm2();
      if (distance_squared > farthest_distance[station_index]) {
        farthest_distance[station_index] = distance_squared;
        supports[station_index] = point_id;
      }
    }
    if (have_previous_supports && diagnostics != nullptr) {
      for (Index station = 0; station < supports.size(); ++station) {
        if (supports[station] != previous_supports[station]) {
          ++diagnostics->selected_support_events;
          diagnostics->trace.push_back(
              {current_time, KineticEventType::SUPPORT_CHANGE,
               static_cast<int>(station), previous_supports[station],
               supports[station], -1, -1, supports[station],
               "equal distances retain the lowest point id in ascending scan"});
        }
      }
    }
    have_previous_supports = diagnostics != nullptr;
    if (use_handovers) {
      bool transferred = false;
      for (int station_from = 0; station_from < instance.m && !transferred;
           ++station_from) {
        if (budget != nullptr) {
          budget->checkpoint();
        }
        const int support_from =
            supports[static_cast<Index>(station_from)];
        if (support_from < 0) {
          continue;
        }
        std::vector<int> source_points;
        for (int point_id = 0; point_id < instance.n; ++point_id) {
          if (owners[static_cast<Index>(point_id)] == station_from) {
            source_points.push_back(point_id);
          }
        }
        const int second_support = KineticCore::second_furthest_assigned(
            instance, station_from, source_points, current_time, budget);
        if (second_support < 0) {
          continue;
        }
        const Point source_station =
            instance.stations[static_cast<Index>(station_from)].pos;
        const Value source_radius_squared =
            (precomputed->position(static_cast<Index>(support_from),
                                   current_time) -
             source_station)
                .norm2();
        const Value second_radius_squared =
            (precomputed->position(static_cast<Index>(second_support),
                                   current_time) -
             source_station)
                .norm2();
        const Value source_tolerance =
            64.0 * std::numeric_limits<Value>::epsilon() *
            std::max(source_radius_squared, second_radius_squared);
        if (second_radius_squared >=
            source_radius_squared - source_tolerance) {
          continue;
        }
        for (int station_to = 0; station_to < instance.m; ++station_to) {
          if (budget != nullptr) {
            budget->checkpoint();
          }
          const int support_to = supports[static_cast<Index>(station_to)];
          if (station_to == station_from || support_to < 0) {
            continue;
          }
          const Point receiver_station =
              instance.stations[static_cast<Index>(station_to)].pos;
          const Value receiving_radius_squared =
              (precomputed->position(static_cast<Index>(support_to),
                                     current_time) -
               receiver_station)
                  .norm2();
          const Value transferred_distance_squared =
              (precomputed->position(static_cast<Index>(support_from),
                                     current_time) -
               receiver_station)
                  .norm2();
          const Value receiver_tolerance =
              64.0 * std::numeric_limits<Value>::epsilon() *
              std::max(transferred_distance_squared,
                       receiving_radius_squared);
          bool receiver_accepts =
              transferred_distance_squared <
              receiving_radius_squared - receiver_tolerance;
          if (!receiver_accepts &&
              std::abs(transferred_distance_squared -
                       receiving_radius_squared) <= receiver_tolerance) {
            const Value relative_derivative =
                distance_derivative(instance, *precomputed, station_to,
                                    support_from, current_time, forward) -
                distance_derivative(instance, *precomputed, station_to,
                                    support_to, current_time, forward);
            const Value derivative_tolerance =
                64.0 * std::numeric_limits<Value>::epsilon() *
                std::max(1.0, std::abs(relative_derivative));
            receiver_accepts =
                direction * relative_derivative < -derivative_tolerance;
          }
          if (receiver_accepts) {
            owners[static_cast<Index>(support_from)] = station_to;
            if (diagnostics != nullptr) {
              diagnostics->trace.push_back(
                  {current_time, KineticEventType::HANDOVER, -1, -1, -1,
                   station_from, station_to, support_from,
                   "source stations then receiver stations scanned in ascending id order",
                   second_support, support_to});
            }
            transferred = true;
            break;
          }
        }
      }
      if (transferred) {
        if (++event_count > 100000U) {
          throw std::runtime_error("kinetic extension exceeded event limit");
        }
        continue;
      }
    }

    Value next_time = t_end;
    const auto select_previous_breakpoint = [&](const auto& breaks) {
      if (forward) {
        const auto breakpoint = std::upper_bound(
            breaks.begin(), breaks.end(), current_time + kIntervalTolerance);
        if (breakpoint != breaks.end() &&
            *breakpoint < next_time) {
          next_time = *breakpoint;
        }
      } else {
        const auto breakpoint = std::lower_bound(
            breaks.begin(), breaks.end(), current_time - kIntervalTolerance);
        if (breakpoint != breaks.begin()) {
          const Value previous = *std::prev(breakpoint);
          if (previous > next_time) {
            next_time = previous;
          }
        }
      }
    };
    if (interval_emission ==
        KineticIntervalEmission::REFERENCE_ALL_TRAJECTORY_BREAKPOINTS) {
      select_previous_breakpoint(event_breakpoints);
    } else {
      for (Index station = 0; station < supports.size(); ++station) {
        const int support = supports[station];
        if (support < 0) {
          continue;
        }
        select_previous_breakpoint(
            precomputed
                ->trajectories[static_cast<Index>(support)]
                .t_breaks);
      }
    }
    const auto event_detection_start =
        diagnostics == nullptr ? std::chrono::steady_clock::time_point{}
                               : std::chrono::steady_clock::now();
    for (int station_id = 0; station_id < instance.m; ++station_id) {
      const int support = supports[static_cast<Index>(station_id)];
      if (support < 0) {
        continue;
      }
      const auto events = KineticCore::find_support_changes(
          instance, station_id, support, current_time, t_end, forward,
          budget, event_engine, diagnostics);
      for (const auto& event : events) {
        if (budget != nullptr) {
          budget->checkpoint();
        }
        if (owners[static_cast<Index>(event.new_supporting_point)] !=
            station_id) {
          if (diagnostics != nullptr) {
            ++diagnostics->candidate_roots_rejected;
          }
          continue;
        }
        const Value derivative_change =
            distance_derivative(instance, *precomputed, station_id,
                                event.new_supporting_point, event.time,
                                forward) -
            distance_derivative(instance, *precomputed, station_id, support,
                                event.time, forward);
        if (direction * derivative_change <= 1e-12) {
          if (diagnostics != nullptr) {
            ++diagnostics->candidate_roots_rejected;
          }
          continue;
        }
        if (direction * (event.time - current_time) > 0.0 &&
            direction * (event.time - next_time) < 0.0) {
          next_time = event.time;
        }
        break;
      }
    }
    HandoverEvent handover_event;
    const Index handover_trace_start =
        diagnostics == nullptr ? 0U : diagnostics->trace.size();
    if (use_handovers) {
      handover_event = KineticCore::find_next_handover(
          instance, supports, owners, current_time, t_end, forward, budget,
          event_engine, diagnostics, handover_evaluation);
      if (handover_event.valid &&
          direction * (handover_event.time - current_time) > 0.0 &&
          direction * (handover_event.time - next_time) < 0.0) {
        next_time = handover_event.time;
      }
    }
    const bool handover_selected =
        handover_event.valid &&
        std::abs(handover_event.time - next_time) <= kIntervalTolerance &&
        handover_event.point_id >= 0 &&
        handover_event.point_id < instance.n &&
        owners[static_cast<Index>(handover_event.point_id)] ==
            handover_event.from_station;
    if (diagnostics != nullptr && handover_event.valid &&
        !handover_selected) {
      diagnostics->trace.erase(
          diagnostics->trace.begin() +
              static_cast<std::vector<KineticEventTraceEntry>::difference_type>(
                  handover_trace_start),
          diagnostics->trace.end());
    }
    if (diagnostics != nullptr) {
      diagnostics->event_detection_nanoseconds +=
          static_cast<std::uint64_t>(
              std::chrono::duration_cast<std::chrono::nanoseconds>(
                  std::chrono::steady_clock::now() - event_detection_start)
                  .count());
    }

    const Value interval_start = std::min(current_time, next_time);
    const Value interval_end = std::max(current_time, next_time);
    IntervalConstructionTimer interval_timer(diagnostics);
    SolutionInterval interval;
    interval.t_start = interval_start;
    interval.t_end = interval_end;
    interval.assigned_points = owners;
    compute_quadratic_coeffs_precomputed(instance, *precomputed, supports,
                                         interval, budget);
    assert(interval.t_start < interval.t_end);
    assert(std::isfinite(interval.a) && std::isfinite(interval.b) &&
           std::isfinite(interval.c));
    solution.intervals.push_back(std::move(interval));
    interval_timer.stop();
    if (diagnostics != nullptr) {
      ++diagnostics->solution_intervals_generated;
      if (direction * (next_time - t_end) < -kIntervalTolerance) {
        for (Index station = 0; station < supports.size(); ++station) {
          const int support = supports[station];
          if (support < 0) {
            continue;
          }
          const auto& trajectory =
              precomputed->trajectories[static_cast<Index>(support)];
          if (std::find(trajectory.t_breaks.begin(),
                        trajectory.t_breaks.end(), next_time) !=
              trajectory.t_breaks.end()) {
            diagnostics->trace.push_back(
                {next_time,
                 KineticEventType::ACTIVE_SUPPORT_MOTION_BREAKPOINT,
                 static_cast<int>(station), support, support, -1, -1, support,
                 "active support trajectory changes linear segment"});
          }
        }
      }
    }

    if (handover_event.valid &&
        std::abs(handover_event.time - next_time) <= kIntervalTolerance &&
        handover_event.point_id >= 0 &&
        handover_event.point_id < instance.n &&
        owners[static_cast<Index>(handover_event.point_id)] ==
            handover_event.from_station) {
      owners[static_cast<Index>(handover_event.point_id)] =
          handover_event.to_station;
    }
    if (next_time == current_time) {
      throw std::runtime_error("kinetic extension failed to advance time");
    }
    current_time = next_time;
    if (++event_count > 100000U) {
      throw std::runtime_error("kinetic extension exceeded event limit");
    }
  }
  if (solution.intervals.empty() && t_start != t_end) {
    IntervalConstructionTimer interval_timer(diagnostics);
    SolutionInterval interval;
    interval.t_start = std::min(t_start, t_end);
    interval.t_end = std::max(t_start, t_end);
    interval.assigned_points = owners;
    compute_quadratic_coeffs_precomputed(instance, *precomputed, supports,
                                         interval, budget);
    assert(interval.t_start < interval.t_end);
    assert(std::isfinite(interval.a) && std::isfinite(interval.b) &&
           std::isfinite(interval.c));
    solution.intervals.push_back(std::move(interval));
    interval_timer.stop();
    if (diagnostics != nullptr) {
      ++diagnostics->solution_intervals_generated;
    }
  }
  std::sort(solution.intervals.begin(), solution.intervals.end(),
            [](const SolutionInterval& lhs, const SolutionInterval& rhs) {
              return lhs.t_start < rhs.t_start;
            });
  if (diagnostics != nullptr) {
    std::stable_sort(
        diagnostics->trace.begin(), diagnostics->trace.end(),
        [](const KineticEventTraceEntry& lhs,
           const KineticEventTraceEntry& rhs) {
          if (lhs.time != rhs.time) {
            return lhs.time < rhs.time;
          }
          if (lhs.type != rhs.type) {
            return lhs.type < rhs.type;
          }
          if (lhs.station_id != rhs.station_id) {
            return lhs.station_id < rhs.station_id;
          }
          if (lhs.from_station != rhs.from_station) {
            return lhs.from_station < rhs.from_station;
          }
          if (lhs.to_station != rhs.to_station) {
            return lhs.to_station < rhs.to_station;
          }
          return lhs.affected_point < rhs.affected_point;
        });
  }
  LOG_INFO("extend: {} intervals over [{}, {}]", solution.intervals.size(),
           std::min(t_start, t_end), std::max(t_start, t_end));
  return solution;
}

KineticSolution KineticSolution::combine(const KineticSolution& s1,
                                         const KineticSolution& s2,
                                         ObjectiveType objective_type,
                                         SolverBudget* budget) {
  KDC_PROFILE_PHASE(ProfilePhase::COMBINATION);
  LOG_DEBUG("combine: {} + {} intervals", s1.intervals.size(),
            s2.intervals.size());
  const auto breakpoints = common_breakpoints(s1, s2);
  KineticSolution result;
  result.objective = objective_type;
  for (Index index = 0; index + 1U < breakpoints.size(); ++index) {
    if (budget != nullptr) {
      budget->checkpoint();
    }
    const Value start = breakpoints[index];
    const Value end = breakpoints[index + 1U];
    if (end <= start) {
      continue;
    }
    const Value midpoint = start + (end - start) / 2.0;
    const bool active1 =
        midpoint >= s1.intervals.front().t_start - kIntervalTolerance &&
        midpoint <= s1.intervals.back().t_end + kIntervalTolerance;
    const bool active2 =
        midpoint >= s2.intervals.front().t_start - kIntervalTolerance &&
        midpoint <= s2.intervals.back().t_end + kIntervalTolerance;
    if (!active1 && !active2) {
      throw std::invalid_argument("combined solutions leave a time gap");
    }
    if (!active1 || !active2) {
      const auto& active_solution = active1 ? s1 : s2;
      append_interval(result, interval_at(active_solution, midpoint), start,
                      end);
      continue;
    }
    const auto& interval1 = interval_at(s1, midpoint);
    const auto& interval2 = interval_at(s2, midpoint);
    std::vector<Value> partitions{start, end};
    const auto roots = KineticCore::solve_quadratic(
        interval1.a - interval2.a, interval1.b - interval2.b,
        interval1.c - interval2.c);
    for (const Value root : roots) {
      if (root > start + kIntervalTolerance &&
          root < end - kIntervalTolerance) {
        partitions.push_back(root);
      }
    }
    std::sort(partitions.begin(), partitions.end());
    partitions.erase(
        std::unique(partitions.begin(), partitions.end(),
                    [](Value lhs, Value rhs) {
                      return std::abs(lhs - rhs) <= kIntervalTolerance;
                    }),
        partitions.end());
    for (Index partition = 0; partition + 1U < partitions.size();
         ++partition) {
      const Value sub_start = partitions[partition];
      const Value sub_end = partitions[partition + 1U];
      const Value sample = sub_start + (sub_end - sub_start) / 2.0;
      const auto& selected =
          evaluate(interval1, sample) <= evaluate(interval2, sample)
              ? interval1
              : interval2;
      append_interval(result, selected, sub_start, sub_end);
    }
  }
  result.remove_duplicates();
  LOG_INFO("combine: {} intervals", result.intervals.size());
  return result;
}

void KineticSolution::remove_duplicates() {
  if (intervals.empty()) {
    return;
  }
  std::vector<SolutionInterval> merged;
  merged.reserve(intervals.size());
  for (const auto& interval : intervals) {
    if (!merged.empty() &&
        std::abs(merged.back().t_end - interval.t_start) <=
            kIntervalTolerance &&
        same_coefficients(merged.back(), interval)) {
      merged.back().t_end = interval.t_end;
    } else {
      merged.push_back(interval);
    }
  }
  intervals = std::move(merged);
}

KineticSolution KineticSolution::partial_extend(
    const KineticSolution& new_solution, const KineticSolution& current,
    ObjectiveType objective_type, SolverBudget* budget) {
  if (!new_solution.is_well_formed() || !current.is_well_formed()) {
    throw std::invalid_argument("partial_extend requires well-formed solutions");
  }
  const Value start = std::max(new_solution.intervals.front().t_start,
                               current.intervals.front().t_start);
  const Value end = std::min(new_solution.intervals.back().t_end,
                             current.intervals.back().t_end);
  KineticSolution result;
  result.objective = objective_type;
  if (objective_type == ObjectiveType::MIN_MAX ||
      objective_type == ObjectiveType::MIN_MAX_SUM) {
    // MinMax refinement is peak-based: the actual decision is made later by
    // `combine`, which constructs the pointwise lower envelope.
    // Carrying the whole candidate through the common domain preserves the
    // correct MinMax semantics and avoids rejecting a candidate solely because
    // its cumulative integral is larger than the incumbent's.
    result = new_solution;
    result.objective = objective_type;
    if (start > result.intervals.front().t_start ||
        end < result.intervals.back().t_end) {
      KineticSolution clipped;
      clipped.objective = objective_type;
      for (const auto& interval : result.intervals) {
        if (budget != nullptr) {
          budget->checkpoint();
        }
        const Value interval_start = std::max(start, interval.t_start);
        const Value interval_end = std::min(end, interval.t_end);
        if (interval_end <= interval_start) {
          continue;
        }
        append_interval(clipped, interval, interval_start, interval_end);
      }
      clipped.remove_duplicates();
      return clipped;
    }
    return result;
  }
  if (objective_type == ObjectiveType::MIN_SUM) {
    result = new_solution;
    result.objective = objective_type;
    return result;
  }
  throw std::invalid_argument("partial_extend received an unknown objective");
}

double KineticSolution::cost_at(double time) const {
  if (intervals.empty()) {
    throw std::runtime_error("kinetic solution has no intervals");
  }
  if (!std::isfinite(time) || time < intervals.front().t_start ||
      time > intervals.back().t_end) {
    throw std::out_of_range("requested time is outside the solution");
  }
  auto upper = std::upper_bound(
      intervals.begin(), intervals.end(), time,
      [](Value value, const SolutionInterval& interval) {
        return value < interval.t_start;
      });
  if (upper == intervals.begin()) {
    upper = intervals.begin();
  } else {
    --upper;
  }
  if (time > upper->t_end) {
    throw std::out_of_range("requested time is outside solution intervals");
  }
  return evaluate(*upper, time);
}

double KineticSolution::integral_on(double t_start, double t_end) const {
  validate_interval_request(*this, t_start, t_end);
  Value integral = 0.0;
  for (const auto& interval : intervals) {
    const Value lower = std::max(t_start, interval.t_start);
    const Value upper = std::min(t_end, interval.t_end);
    if (upper <= lower) {
      continue;
    }
    integral += (interval.a / 3.0) *
                    (upper * upper * upper - lower * lower * lower) +
                (interval.b / 2.0) *
                    (upper * upper - lower * lower) +
                interval.c * (upper - lower);
  }
  return integral;
}

double KineticSolution::total_integral() const {
  if (intervals.empty()) {
    throw std::runtime_error("kinetic solution has no intervals");
  }
  return integral_on(intervals.front().t_start, intervals.back().t_end);
}

double KineticSolution::peak_cost() const {
  if (intervals.empty()) {
    throw std::runtime_error("kinetic solution has no intervals");
  }
  Value maximum = -std::numeric_limits<Value>::infinity();
  for (const auto& interval : intervals) {
    maximum = std::max(maximum, evaluate(interval, interval.t_start));
    maximum = std::max(maximum, evaluate(interval, interval.t_end));
    if (interval.a < 0.0) {
      const Value vertex = -interval.b / (2.0 * interval.a);
      if (vertex > interval.t_start && vertex < interval.t_end) {
        maximum = std::max(maximum, evaluate(interval, vertex));
      }
    }
  }
  return maximum;
}

double KineticSolution::peak_time() const {
  if (intervals.empty()) {
    throw std::runtime_error("kinetic solution has no intervals");
  }
  Value maximum = -std::numeric_limits<Value>::infinity();
  Value time_of_maximum = intervals.front().t_start;
  const auto consider = [&maximum, &time_of_maximum](
                            const SolutionInterval& interval, Value time) {
    const Value cost = evaluate(interval, time);
    if (cost > maximum) {
      maximum = cost;
      time_of_maximum = time;
    }
  };
  for (const auto& interval : intervals) {
    consider(interval, interval.t_start);
    consider(interval, interval.t_end);
    if (interval.a < 0.0) {
      const Value vertex = -interval.b / (2.0 * interval.a);
      if (vertex > interval.t_start && vertex < interval.t_end) {
        consider(interval, vertex);
      }
    }
  }
  return time_of_maximum;
}

bool KineticSolution::is_well_formed() const {
  if (intervals.empty()) {
    return false;
  }
  const Index station_count = intervals.front().supporting_point.size();
  const Index point_count = intervals.front().assigned_points.size();
  for (Index index = 0; index < intervals.size(); ++index) {
    const auto& interval = intervals[index];
    if (!std::isfinite(interval.t_start) || !std::isfinite(interval.t_end) ||
        interval.t_start >= interval.t_end ||
        interval.supporting_point.size() != station_count ||
        interval.assigned_points.size() != point_count ||
        !std::isfinite(interval.a) || !std::isfinite(interval.b) ||
        !std::isfinite(interval.c)) {
      return false;
    }
    if (index > 0U &&
        intervals[index - 1U].t_end != interval.t_start) {
      return false;
    }
  }
  return true;
}

void KineticSolution::dump() const {
  for (const auto& interval : intervals) {
    LOG_INFO(
        "solution interval [{:.9g}, {:.9g}]: a={:.9g}, b={:.9g}, c={:.9g}",
        interval.t_start, interval.t_end, interval.a, interval.b, interval.c);
  }
}
}
