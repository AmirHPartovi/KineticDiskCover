#include "kdc/solution.hpp"

#include "kdc/candidate.hpp"
#include "kdc/kinetic.hpp"
#include "kdc/logging.hpp"
#include "kdc/profiling.hpp"
#include "kdc/stationary.hpp"

#include <algorithm>
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

void append_clipped_solution(KineticSolution& destination,
                             const KineticSolution& source, Value start,
                             Value end) {
  for (const auto& interval : source.intervals) {
    append_interval(destination, interval, std::max(start, interval.t_start),
                    std::min(end, interval.t_end));
  }
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
    ObjectiveType objective_type, SolverBudget* budget) {
  KDC_PROFILE_PHASE(ProfilePhase::KINETIC_EXTENSION);
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
      instance.n < 0 || instance.m < 0 ||
      instance.trajectories.size() != static_cast<Index>(instance.n) ||
      instance.stations.size() != static_cast<Index>(instance.m)) {
    throw std::invalid_argument("extension requires a feasible assignment");
  }

  std::vector<int> supports = init_assignment.supporting_point;
  for (const int support : supports) {
    if (budget != nullptr) {
      budget->checkpoint();
    }
    if (support < -1 || support >= instance.n) {
      throw std::out_of_range("initial supporting point is invalid");
    }
  }
  std::vector<int> owners(static_cast<Index>(instance.n), -1);
  std::vector<Value> owner_radii(static_cast<Index>(instance.n),
                                 std::numeric_limits<Value>::infinity());
  const auto precomputed = CandidateSet::precompute(instance, budget);
  const auto initial_geometry =
      CandidateSet::build_geometry(instance, *precomputed, t_start, budget);
  for (int station_id = 0; station_id < instance.m; ++station_id) {
    if (budget != nullptr) {
      budget->checkpoint();
    }
    const Index station_index = static_cast<Index>(station_id);
    if (!std::isfinite(init_assignment.radius[station_index]) ||
        init_assignment.radius[station_index] < 0.0) {
      throw std::invalid_argument("initial assignment has an invalid radius");
    }
    for (int point_id = 0; point_id < instance.n; ++point_id) {
      if (budget != nullptr) {
        budget->checkpoint();
      }
      const Index point_index = static_cast<Index>(point_id);
      const Value radius =
          init_assignment.radius[station_index] + kIntervalTolerance;
      const Value distance_squared = initial_geometry.distance_squared(
          station_index, point_index, static_cast<Index>(instance.n));
      if (distance_squared <= radius * radius &&
          init_assignment.radius[station_index] < owner_radii[point_index]) {
        owner_radii[point_index] = init_assignment.radius[station_index];
        owners[point_index] = station_id;
      }
    }
  }
  for (const int owner : owners) {
    if (budget != nullptr) {
      budget->checkpoint();
    }
    if (owner == -1) {
      throw std::invalid_argument(
          "initial assignment does not cover every point at t_start");
    }
  }
  KineticSolution solution;
  solution.objective = objective_type;
  Value current_time = t_start;
  const Value direction = forward ? 1.0 : -1.0;
  std::vector<Value> breakpoints;
  for (const auto& trajectory : precomputed->trajectories) {
    for (const Value breakpoint : trajectory.t_breaks) {
      if (budget != nullptr) {
        budget->checkpoint();
      }
      breakpoints.push_back(breakpoint);
    }
  }
  std::sort(breakpoints.begin(), breakpoints.end());
  breakpoints.erase(std::unique(breakpoints.begin(), breakpoints.end()),
                    breakpoints.end());
  std::size_t event_count = 0U;
  while (direction * (t_end - current_time) > kIntervalTolerance) {
    if (budget != nullptr) {
      budget->checkpoint();
    }
    const Value remaining = std::abs(t_end - current_time);
    const Value probe_time =
        std::clamp(current_time + direction * std::min(1e-8, remaining / 4.0),
                   0.0, instance.T_end);
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

    Value next_time = t_end;
    if (forward) {
      const auto breakpoint = std::upper_bound(
          breakpoints.begin(), breakpoints.end(),
          current_time + kIntervalTolerance);
      if (breakpoint != breakpoints.end() && *breakpoint < next_time) {
        next_time = *breakpoint;
      }
    } else {
      const auto breakpoint = std::lower_bound(
          breakpoints.begin(), breakpoints.end(),
          current_time - kIntervalTolerance);
      if (breakpoint != breakpoints.begin()) {
        const Value previous = *std::prev(breakpoint);
        if (previous > next_time) {
          next_time = previous;
        }
      }
    }
    for (int station_id = 0; station_id < instance.m; ++station_id) {
      const int support = supports[static_cast<Index>(station_id)];
      if (support < 0) {
        continue;
      }
      const auto events = KineticCore::find_support_changes(
          instance, station_id, support, current_time, t_end, forward,
          budget);
      for (const auto& event : events) {
        if (budget != nullptr) {
          budget->checkpoint();
        }
        if (owners[static_cast<Index>(event.new_supporting_point)] !=
            station_id) {
          continue;
        }
        const Value derivative_change =
            distance_derivative(instance, *precomputed, station_id,
                                event.new_supporting_point, event.time,
                                forward) -
            distance_derivative(instance, *precomputed, station_id, support,
                                event.time, forward);
        if (direction * derivative_change <= 1e-12) {
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
    if (use_handovers) {
      handover_event = KineticCore::find_next_handover(
          instance, supports, current_time, t_end, forward, budget);
      if (handover_event.valid &&
          direction * (handover_event.time - current_time) > 0.0 &&
          direction * (handover_event.time - next_time) < 0.0) {
        next_time = handover_event.time;
      }
    }

    const Value interval_start = std::min(current_time, next_time);
    const Value interval_end = std::max(current_time, next_time);
    SolutionInterval interval;
    interval.t_start = interval_start;
    interval.t_end = interval_end;
    interval.assigned_points = owners;
    compute_quadratic_coeffs_precomputed(instance, *precomputed, supports,
                                         interval, budget);
    solution.intervals.push_back(std::move(interval));

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
    SolutionInterval interval;
    interval.t_start = std::min(t_start, t_end);
    interval.t_end = std::max(t_start, t_end);
    interval.assigned_points = owners;
    compute_quadratic_coeffs_precomputed(instance, *precomputed, supports,
                                         interval, budget);
    solution.intervals.push_back(std::move(interval));
  }
  std::sort(solution.intervals.begin(), solution.intervals.end(),
            [](const SolutionInterval& lhs, const SolutionInterval& rhs) {
              return lhs.t_start < rhs.t_start;
            });
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
    if (objective_type == ObjectiveType::MIN_SUM) {
      const auto integral = [](const SolutionInterval& interval, Value lo,
                               Value hi) {
        return (interval.a / 3.0) * (hi * hi * hi - lo * lo * lo) +
               (interval.b / 2.0) * (hi * hi - lo * lo) +
               interval.c * (hi - lo);
      };
      const auto& selected =
          integral(interval1, start, end) <= integral(interval2, start, end)
              ? interval1
              : interval2;
      append_interval(result, selected, start, end);
      continue;
    }

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
  if (end <= start) {
    return result;
  }

  if (objective_type == ObjectiveType::MIN_SUM) {
    std::vector<Value> breakpoints{start, end};
    const auto add_overlap_breakpoints =
        [&breakpoints, start, end](const KineticSolution& solution) {
          for (const auto& interval : solution.intervals) {
            if (interval.t_start > start && interval.t_start < end) {
              breakpoints.push_back(interval.t_start);
            }
            if (interval.t_end > start && interval.t_end < end) {
              breakpoints.push_back(interval.t_end);
            }
          }
        };
    add_overlap_breakpoints(new_solution);
    add_overlap_breakpoints(current);
    std::sort(breakpoints.begin(), breakpoints.end());
    breakpoints.erase(
        std::unique(breakpoints.begin(), breakpoints.end(),
                    [](Value lhs, Value rhs) {
                      return std::abs(lhs - rhs) <= kIntervalTolerance;
                    }),
        breakpoints.end());
    for (Index index = 0; index + 1U < breakpoints.size(); ++index) {
      if (budget != nullptr) {
        budget->checkpoint();
      }
      const Value lo = breakpoints[index];
      const Value hi = breakpoints[index + 1U];
      const Value midpoint = lo + (hi - lo) / 2.0;
      const auto& new_interval = interval_at(new_solution, midpoint);
      const auto& current_interval = interval_at(current, midpoint);
      const auto integrate = [](const SolutionInterval& interval, Value from,
                                Value to) {
        return (interval.a / 3.0) * (to * to * to - from * from * from) +
               (interval.b / 2.0) * (to * to - from * from) +
               interval.c * (to - from);
      };
      if (integrate(new_interval, lo, hi) >
          integrate(current_interval, lo, hi) + kIntervalTolerance) {
        break;
      }
      append_interval(result, new_interval, lo, hi);
    }
    result.remove_duplicates();
    return result;
  }

  const auto cumulative_difference = [&new_solution, &current, start](
                                         Value time) {
    return new_solution.integral_on(start, time) -
           current.integral_on(start, time);
  };
  std::vector<Value> breakpoints;
  for (const auto& interval : new_solution.intervals) {
    if (interval.t_start > start && interval.t_start < end) {
      breakpoints.push_back(interval.t_start);
    }
    if (interval.t_end > start && interval.t_end < end) {
      breakpoints.push_back(interval.t_end);
    }
  }
  for (const auto& interval : current.intervals) {
    if (interval.t_start > start && interval.t_start < end) {
      breakpoints.push_back(interval.t_start);
    }
    if (interval.t_end > start && interval.t_end < end) {
      breakpoints.push_back(interval.t_end);
    }
  }
  breakpoints.push_back(start);
  breakpoints.push_back(end);
  std::sort(breakpoints.begin(), breakpoints.end());
  breakpoints.erase(
      std::unique(breakpoints.begin(), breakpoints.end(),
                  [](Value lhs, Value rhs) {
                    return std::abs(lhs - rhs) <= kIntervalTolerance;
                  }),
      breakpoints.end());

  Value intersection = end;
  bool found_intersection = false;
  for (Index index = 0; index + 1U < breakpoints.size() &&
                         !found_intersection;
       ++index) {
    if (budget != nullptr) {
      budget->checkpoint();
    }
    const Value segment_start = breakpoints[index];
    const Value segment_end = breakpoints[index + 1U];
    const Value midpoint =
        segment_start + (segment_end - segment_start) / 2.0;
    const auto& new_interval = interval_at(new_solution, midpoint);
    const auto& current_interval = interval_at(current, midpoint);
    std::vector<Value> monotonic_points{segment_start, segment_end};
    const auto critical_points = KineticCore::solve_quadratic(
        new_interval.a - current_interval.a,
        new_interval.b - current_interval.b,
        new_interval.c - current_interval.c);
    for (const Value critical : critical_points) {
      if (critical > segment_start + kIntervalTolerance &&
          critical < segment_end - kIntervalTolerance) {
        monotonic_points.push_back(critical);
      }
    }
    std::sort(monotonic_points.begin(), monotonic_points.end());
    for (Index point = 0; point + 1U < monotonic_points.size();
         ++point) {
      Value low = monotonic_points[point];
      Value high = monotonic_points[point + 1U];
      Value low_difference = cumulative_difference(low);
      Value high_difference = cumulative_difference(high);
      if (high > start + kIntervalTolerance &&
          std::abs(high_difference) <= 1e-12) {
        intersection = high;
        found_intersection = true;
        break;
      }
      if (low <= start + kIntervalTolerance ||
          low_difference == 0.0 ||
          (low_difference < 0.0) == (high_difference < 0.0)) {
        continue;
      }
      const bool low_negative = low_difference < 0.0;
      for (int iteration = 0; iteration < 80; ++iteration) {
        if (budget != nullptr) {
          budget->checkpoint();
        }
        const Value middle = low + (high - low) / 2.0;
        const Value difference = cumulative_difference(middle);
        if ((difference < 0.0) == low_negative) {
          low = middle;
          low_difference = difference;
        } else {
          high = middle;
          high_difference = difference;
        }
      }
      (void)low_difference;
      (void)high_difference;
      intersection = low + (high - low) / 2.0;
      found_intersection = true;
      break;
    }
  }

  if (!found_intersection && cumulative_difference(end) > 0.0) {
    return result;
  }
  append_clipped_solution(result, new_solution, start, intersection);
  result.remove_duplicates();
  return result;
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
