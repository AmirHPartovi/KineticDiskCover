#include "kdc/minsum.hpp"

#include "kdc/algorithms/ip_static_solver.hpp"
#include "kdc/logging.hpp"
#include "kdc/profiling.hpp"
#include "kdc/stationary.hpp"
#include "kdc/verify.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace kdc {
namespace {
constexpr double kMinSumTolerance = 1e-9;

double interval_integral(const SolutionInterval& interval, double start,
                         double end) {
  return (interval.a / 3.0) * (end * end * end - start * start * start) +
         (interval.b / 2.0) * (end * end - start * start) +
         interval.c * (end - start);
}

std::pair<double, double> largest_integral_interval(
    const KineticSolution& solution) {
  if (!solution.is_well_formed()) {
    throw std::invalid_argument(
        "selecting an adaptive interval requires a valid solution");
  }
  double largest_integral = -std::numeric_limits<double>::infinity();
  std::pair<double, double> selected{
      solution.intervals.front().t_start, solution.intervals.front().t_end};
  for (const auto& interval : solution.intervals) {
    const double contribution =
        solution.integral_on(interval.t_start, interval.t_end);
    if (contribution > largest_integral) {
      largest_integral = contribution;
      selected = {interval.t_start, interval.t_end};
    }
  }
  return selected;
}

double interpolate_lower_bound(
    const std::vector<std::pair<double, double>>& samples, double time) {
  if (samples.empty()) {
    throw std::invalid_argument("lower-bound samples must not be empty");
  }
  auto upper = std::lower_bound(
      samples.begin(), samples.end(), time,
      [](const auto& sample, double value) { return sample.first < value; });
  if (upper == samples.begin()) {
    return upper->second;
  }
  if (upper == samples.end()) {
    return samples.back().second;
  }
  if (std::abs(upper->first - time) <= kMinSumTolerance) {
    return upper->second;
  }
  const auto lower = std::prev(upper);
  const double fraction =
      (time - lower->first) / (upper->first - lower->first);
  return lower->second + fraction * (upper->second - lower->second);
}

double trapezoid_integral(
    const std::vector<std::pair<double, double>>& samples) {
  double result = 0.0;
  for (Index index = 0; index + 1U < samples.size(); ++index) {
    result += 0.5 * (samples[index].second + samples[index + 1U].second) *
              (samples[index + 1U].first - samples[index].first);
  }
  return result;
}

bool sampled_feasible(const Instance& instance,
                      const KineticSolution& solution,
                      SolverBudget* budget) {
  if (!solution.is_well_formed()) {
    return false;
  }
  for (const auto& interval : solution.intervals) {
    for (int sample = 0; sample <= 100; ++sample) {
      if (budget != nullptr) {
        budget->checkpoint();
      }
      const double fraction = static_cast<double>(sample) / 100.0;
      const double time =
          interval.t_start + fraction * (interval.t_end - interval.t_start);
      if (!Verifier::check_coverage(instance, solution, time, 1e-6, nullptr,
                                    budget) ||
          !Verifier::check_supporting_points(instance, solution, time, 1e-6,
                                             budget)) {
        return false;
      }
    }
  }
  return true;
}

double relative_gap(double value, double lower_bound) {
  return std::max(0.0, value - lower_bound) /
         std::max(1.0, std::abs(lower_bound));
}

void validate_config(const MinSumSolver::Config& config) {
  if (!std::isfinite(config.initial_ip_gap) ||
      !std::isfinite(config.final_ip_gap) ||
      !std::isfinite(config.time_limit_per_ip) ||
      !std::isfinite(config.gap_target) ||
      !std::isfinite(config.global_time_limit_sec) ||
      config.initial_ip_gap < 0.0 || config.final_ip_gap < 0.0 ||
      config.time_limit_per_ip <= 0.0 || config.gap_target < 0.0 ||
      config.global_time_limit_sec < 0.0 || config.lb_num_samples <= 0 ||
      config.max_iterations <= 0 || config.stagnation_patience <= 0 ||
      !std::isfinite(config.improvement_tolerance) ||
      config.improvement_tolerance < 0.0 ||
      config.verify_every_n_iters < 0) {
    throw std::invalid_argument("MinSum configuration is invalid");
  }
  (void)minsum_refinement_policy_to_string(config.refinement_policy);
}

KineticSolution join_directions(const KineticSolution& backward,
                                const KineticSolution& forward,
                                double join_time) {
  KineticSolution joined;
  joined.objective = ObjectiveType::MIN_SUM;
  for (const auto& interval : backward.intervals) {
    if (interval.t_end <= join_time + kMinSumTolerance) {
      joined.intervals.push_back(interval);
    }
  }
  for (const auto& interval : forward.intervals) {
    if (interval.t_start >= join_time - kMinSumTolerance) {
      joined.intervals.push_back(interval);
    }
  }
  std::sort(joined.intervals.begin(), joined.intervals.end(),
            [](const SolutionInterval& lhs, const SolutionInterval& rhs) {
              return lhs.t_start < rhs.t_start;
            });
  joined.remove_duplicates();
  if (!joined.is_well_formed()) {
    throw std::runtime_error(
        "MinSum forward and backward extensions did not join continuously");
  }
  return joined;
}

void insert_lower_bound_sample(
    std::vector<std::pair<double, double>>& samples, double time,
    double lower_bound) {
  const auto position = std::lower_bound(
      samples.begin(), samples.end(), time,
      [](const auto& sample, double value) { return sample.first < value; });
  if (position != samples.end() &&
      std::abs(position->first - time) <= kMinSumTolerance) {
    position->second = std::max(position->second, lower_bound);
  } else if (position != samples.begin() &&
             std::abs(std::prev(position)->first - time) <=
                 kMinSumTolerance) {
    auto existing = std::prev(position);
    existing->second = std::max(existing->second, lower_bound);
  } else {
    samples.insert(position, {time, lower_bound});
  }
}
}

void apply_integral_handovers(const Instance& instance,
                              KineticSolution& solution,
                              SolverBudget* budget) {
  LOG_DEBUG("apply_integral_handovers: {} intervals",
            solution.intervals.size());
  if (instance.n < 0 || instance.m < 0 ||
      static_cast<Index>(instance.n) != instance.trajectories.size() ||
      static_cast<Index>(instance.m) != instance.stations.size() ||
      !std::isfinite(instance.T_end) || instance.T_end <= 0.0 ||
      !solution.is_well_formed() ||
      solution.intervals.front().t_start < -kMinSumTolerance ||
      solution.intervals.back().t_end > instance.T_end + kMinSumTolerance) {
    throw std::invalid_argument(
        "apply_integral_handovers requires a valid instance and solution");
  }
  for (const auto& interval : solution.intervals) {
    if (interval.supporting_point.size() !=
            static_cast<Index>(instance.m) ||
        interval.assigned_points.size() !=
            static_cast<Index>(instance.n)) {
      throw std::invalid_argument(
          "apply_integral_handovers solution dimensions do not match instance");
    }
    for (const int support : interval.supporting_point) {
      if (support < -1 || support >= instance.n) {
        throw std::invalid_argument(
            "apply_integral_handovers found an invalid supporting point");
      }
    }
  }

  constexpr int maximum_passes = 10;
  int total_applied = 0;
  for (int pass = 0; pass < maximum_passes; ++pass) {
    if (budget != nullptr) {
      budget->checkpoint();
    }
    bool applied_this_pass = false;
    for (Index index = 0; index < solution.intervals.size(); ++index) {
      bool improved = true;
      while (improved) {
        if (budget != nullptr) {
          budget->checkpoint();
        }
        improved = false;
        const std::vector<int> current_supports =
            solution.intervals[index].supporting_point;
        const SolutionInterval current = solution.intervals[index];
        const double old_integral =
            interval_integral(current, current.t_start, current.t_end);

        for (int from = 0; from < instance.m && !improved; ++from) {
          if (budget != nullptr) {
            budget->checkpoint();
          }
          if (current_supports[static_cast<Index>(from)] < 0) {
            continue;
          }
          const auto events = KineticCore::find_handovers_from(
              instance, from, current_supports, current.assigned_points,
              current.t_start, current.t_end, true, budget);
          for (const auto& event : events) {
            if (budget != nullptr) {
              budget->checkpoint();
            }
            const int to = event.to_station;
            if (!event.valid || event.from_station != from || to < 0 ||
                to >= instance.m ||
                current_supports[static_cast<Index>(to)] < 0 ||
                event.point_id < 0 || event.point_id >= instance.n ||
                current.assigned_points[
                    static_cast<Index>(event.point_id)] != from ||
                event.time <= current.t_start + kMinSumTolerance ||
                event.time >= current.t_end - kMinSumTolerance ||
                event.new_support_from < 0 ||
                event.new_support_from >= instance.n ||
                event.new_support_to < -1 ||
                event.new_support_to >= instance.n) {
              continue;
            }

            SolutionInterval left = current;
            left.t_end = event.time;
            SolutionInterval right = current;
            right.t_start = event.time;
            right.supporting_point[static_cast<Index>(from)] =
                event.new_support_from;
            right.supporting_point[static_cast<Index>(to)] =
                event.new_support_to;
            right.assigned_points[static_cast<Index>(event.point_id)] = to;
            KineticSolution::compute_quadratic_coeffs(
                instance, left.supporting_point, left, budget);
            KineticSolution::compute_quadratic_coeffs(
                instance, right.supporting_point, right, budget);
            const double new_integral =
                interval_integral(left, left.t_start, left.t_end) +
                interval_integral(right, right.t_start, right.t_end);
            if (new_integral < old_integral - kMinSumTolerance) {
              solution.intervals[index] = std::move(left);
              solution.intervals.insert(
                  solution.intervals.begin() +
                      static_cast<std::ptrdiff_t>(index + 1U),
                  std::move(right));
              improved = true;
              applied_this_pass = true;
              ++total_applied;
              break;
            }
          }
        }
      }
    }
    if (!applied_this_pass) {
      break;
    }
  }
  solution.remove_duplicates();
  if (!solution.is_well_formed()) {
    throw std::runtime_error(
        "apply_integral_handovers produced a malformed solution");
  }
  LOG_INFO("apply_integral_handovers: applied {} handovers", total_applied);
}

std::pair<double, double> MinSumSolver::find_max_contribution_interval(
    const KineticSolution& solution,
    const std::vector<std::pair<double, double>>& lower_bound_samples) {
  if (!solution.is_well_formed() || lower_bound_samples.size() < 2U) {
    throw std::invalid_argument(
        "finding a MinSum contribution interval requires valid inputs");
  }
  double largest_waste = -std::numeric_limits<double>::infinity();
  std::pair<double, double> worst{
      solution.intervals.front().t_start, solution.intervals.back().t_end};
  for (const auto& interval : solution.intervals) {
    const double lower = interpolate_lower_bound(lower_bound_samples,
                                                  interval.t_start);
    const double upper = interpolate_lower_bound(lower_bound_samples,
                                                  interval.t_end);
    const double lower_integral =
        0.5 * (lower + upper) * (interval.t_end - interval.t_start);
    const double waste =
        solution.integral_on(interval.t_start, interval.t_end) -
        lower_integral;
    if (waste > largest_waste) {
      largest_waste = waste;
      worst = {interval.t_start, interval.t_end};
    }
  }
  return worst;
}

double MinSumSolver::compute_integral_lower_bound(
    const Instance& instance, IStaticSolver& static_solver, int num_samples,
    const Config& config,
    std::vector<std::pair<double, double>>& output_samples,
    SolverBudget* budget, bool* time_limited) {
  (void)config;
  if (num_samples <= 0 || !std::isfinite(instance.T_end) ||
      instance.T_end <= 0.0) {
    throw std::invalid_argument("invalid MinSum lower-bound sampling domain");
  }
  output_samples.clear();
  output_samples.reserve(static_cast<Index>(num_samples) + 1U);
  if (time_limited != nullptr) {
    *time_limited = false;
  }
  for (int sample = 0; sample <= num_samples; ++sample) {
    if (budget != nullptr) {
      budget->checkpoint();
    }
    const double time =
        instance.T_end * static_cast<double>(sample) /
        static_cast<double>(num_samples);
    const StaticSolution assignment =
        budget == nullptr
            ? static_solver.solve(instance, time)
            : static_solver.solve_with_budget(instance, time, *budget,
                                              config.time_limit_per_ip);
    if (!assignment.feasible) {
      if (assignment.time_limited ||
          assignment.optimality_status == OptimalityStatus::TIME_LIMIT) {
        if (time_limited != nullptr) {
          *time_limited = true;
        }
        break;
      }
      throw std::runtime_error(
          "MinSum lower-bound IP solve did not produce a feasible assignment");
    }
    const double lower_bound =
        assignment.bound_status == BoundStatus::NONE ? 0.0
                                                     : assignment.lower_bound;
    output_samples.emplace_back(time, lower_bound);
    if (assignment.time_limited) {
      if (time_limited != nullptr) {
        *time_limited = true;
      }
      break;
    }
  }
  return trapezoid_integral(output_samples);
}

KineticSolution MinSumSolver::combine_integral(
    const KineticSolution& first, const KineticSolution& second) {
  return KineticSolution::combine(first, second, ObjectiveType::MIN_SUM);
}

void MinSumSolver::local_improvement_integral(const Instance& instance,
                                              KineticSolution& solution,
                                              SolverBudget* budget) {
  KDC_PROFILE_PHASE(ProfilePhase::LOCAL_IMPROVEMENT);
  bool improved = true;
  while (improved) {
    if (budget != nullptr) {
      budget->checkpoint();
    }
    improved = false;
    for (int station_id = 0; station_id < instance.m; ++station_id) {
      if (budget != nullptr) {
        budget->checkpoint();
      }
      KineticSolution candidate = solution;
      bool has_support = false;
      for (auto& interval : candidate.intervals) {
        if (interval.supporting_point[static_cast<Index>(station_id)] >= 0) {
          has_support = true;
          interval.supporting_point[static_cast<Index>(station_id)] = -1;
          KineticSolution::compute_quadratic_coeffs(
              instance, interval.supporting_point, interval, budget);
        }
      }
      if (!has_support) {
        continue;
      }

      bool remains_feasible = true;
      for (const auto& interval : candidate.intervals) {
        if (budget != nullptr) {
          budget->checkpoint();
        }
        for (int sample = 0; sample <= 100 && remains_feasible; ++sample) {
          if (budget != nullptr) {
            budget->checkpoint();
          }
          const double fraction =
              static_cast<double>(sample) / 100.0;
          const double time =
              interval.t_start + fraction * (interval.t_end - interval.t_start);
          for (const auto& trajectory : instance.trajectories) {
            const Point point = trajectory.position(time);
            bool covered = false;
            for (int other_station = 0; other_station < instance.m;
                 ++other_station) {
              const int support = interval.supporting_point[
                  static_cast<Index>(other_station)];
              if (support < 0) {
                continue;
              }
              const Point support_position =
                  instance.trajectories[static_cast<Index>(support)]
                      .position(time);
              const double radius =
                  (support_position -
                   instance.stations[static_cast<Index>(other_station)].pos)
                      .norm();
              if ((point -
                   instance.stations[static_cast<Index>(other_station)].pos)
                      .norm() <= radius + 1e-9) {
                covered = true;
                break;
              }
            }
            if (!covered) {
              remains_feasible = false;
              break;
            }
          }
        }
        if (!remains_feasible) {
          break;
        }
      }
      if (remains_feasible &&
          candidate.total_integral() <
              solution.total_integral() - kMinSumTolerance) {
        solution = std::move(candidate);
        improved = true;
        break;
      }
    }
  }
  solution.remove_duplicates();
}

MinSumSolver::Result MinSumSolver::solve(const Instance& instance,
                                         ILPSolver& solver,
                                         const Config& config) {
  const double limit = config.global_time_limit_sec > 0.0
                           ? config.global_time_limit_sec
                           : 600.0;
  SolverBudget budget(limit);
  return solve(instance, solver, config, budget);
}

MinSumSolver::Result MinSumSolver::solve(const Instance& instance,
                                         ILPSolver& solver,
                                         const Config& config,
                                         SolverBudget& budget) {
  IPStaticSolver::Config static_config;
  static_config.time_limit_sec = config.time_limit_per_ip;
  static_config.gap_target = config.initial_ip_gap;
  IPStaticSolver static_solver(&solver, static_config);
  return solve(instance, static_solver, config, budget);
}

MinSumSolver::Result MinSumSolver::solve(const Instance& instance,
                                         IStaticSolver& static_solver,
                                         const Config& config) {
  const double limit = config.global_time_limit_sec > 0.0
                           ? config.global_time_limit_sec
                           : (static_solver.is_exact() ? 600.0 : 30.0);
  SolverBudget budget(limit);
  return solve(instance, static_solver, config, budget);
}

MinSumSolver::Result MinSumSolver::solve(const Instance& instance,
                                         IStaticSolver& static_solver,
                                         const Config& config,
                                         SolverBudget& budget) {
  validate_config(config);
  if (instance.n < 0 || instance.m < 0 ||
      static_cast<Index>(instance.n) != instance.trajectories.size() ||
      static_cast<Index>(instance.m) != instance.stations.size() ||
      !std::isfinite(instance.T_end) || instance.T_end <= 0.0) {
    throw std::invalid_argument("MinSum received an invalid instance");
  }
  LOG_INFO("MinSum: start n={}, m={}", instance.n, instance.m);
  const auto start = std::chrono::steady_clock::now();
  Result result;
  result.refinement_policy = config.refinement_policy;

  StaticSolution initial_assignment;
  try {
    initial_assignment = static_solver.solve_with_budget(
        instance, 0.0, budget, config.time_limit_per_ip);
  } catch (const SolverBudgetExpired&) {
    result.exact_solver = static_solver.is_exact();
    result.time_limited = true;
    result.optimality_status = OptimalityStatus::TIME_LIMIT;
    result.total_time_sec =
        std::chrono::duration<double>(
            std::chrono::steady_clock::now() - start)
            .count();
    return result;
  }
  ++result.num_ip_solves;
  if (!initial_assignment.feasible) {
    result.exact_solver = static_solver.is_exact();
    result.time_limited =
        initial_assignment.time_limited ||
        initial_assignment.optimality_status == OptimalityStatus::TIME_LIMIT;
    result.optimality_status =
        result.time_limited
            ? OptimalityStatus::TIME_LIMIT
            : initial_assignment.optimality_status;
    result.bound_status = initial_assignment.bound_status;
    result.lower_bound = initial_assignment.lower_bound;
    result.total_time_sec =
        std::chrono::duration<double>(
            std::chrono::steady_clock::now() - start)
            .count();
    return result;
  }
  result.time_limited = initial_assignment.time_limited;
  KineticSolution solution;
  try {
    solution = KineticSolution::extend(
        instance,
        StaticAssignment{initial_assignment.supporting_point,
                         initial_assignment.radius, initial_assignment.cost,
                         initial_assignment.feasible,
                         initial_assignment.assigned_points},
        0.0, instance.T_end, true,
        config.use_handovers, ObjectiveType::MIN_SUM, &budget);
  } catch (const SolverBudgetExpired&) {
    result.exact_solver = static_solver.is_exact();
    result.time_limited = true;
    result.optimality_status = OptimalityStatus::TIME_LIMIT;
    result.total_time_sec =
        std::chrono::duration<double>(
            std::chrono::steady_clock::now() - start)
            .count();
    return result;
  }
  if (!solution.is_well_formed()) {
    throw std::runtime_error("MinSum initial extension is malformed");
  }

  std::vector<std::pair<double, double>> lower_bound_samples;
  double current_integral = solution.total_integral();
  double lower_bound_integral = 0.0;
  try {
  if (config.refinement_policy ==
          MinSumRefinementPolicy::CERTIFIED_BOUND &&
      !result.time_limited) {
    budget.checkpoint();
    bool sampling_time_limited = false;
    lower_bound_integral = compute_integral_lower_bound(
        instance, static_solver, config.lb_num_samples, config,
        lower_bound_samples, &budget, &sampling_time_limited);
    result.num_ip_solves += static_cast<int>(lower_bound_samples.size());
    result.time_limited = result.time_limited || sampling_time_limited;
  }
  lower_bound_integral = std::min(lower_bound_integral, current_integral);
  result.certified_lower_bound_integral = 0.0;
  result.heuristic_lower_bound_integral = lower_bound_integral;
  result.lower_bound = 0.0;
  // Nonnegative disk-area costs give an independent certified integral lower
  // bound of zero. Sampled pointwise bounds remain heuristic estimates.
  result.bound_status = BoundStatus::CERTIFIED;
  double current_gap = relative_gap(current_integral, lower_bound_integral);
  result.gap_trace.push_back(current_gap);
  const auto append_trace = [&](int iteration, double time) {
    IterTrace row;
    row.iter = iteration;
    row.t_max = time;
    row.objective_value = current_integral;
    row.lower_bound = lower_bound_integral;
    row.gap = relative_gap(current_integral, lower_bound_integral);
    row.wall_time_sec =
        std::chrono::duration<double>(
            std::chrono::steady_clock::now() - start)
            .count();
    row.num_ip_solves = result.num_ip_solves;
    result.trace.push_back(row);
  };
  append_trace(0, 0.0);
  int stagnant_iterations = 0;
  for (int iteration = 1;
       iteration <= config.max_iterations && !result.time_limited;
       ++iteration) {
    budget.checkpoint();
    result.num_iterations = iteration;
    const bool has_sampled_estimate =
        config.refinement_policy ==
            MinSumRefinementPolicy::CERTIFIED_BOUND &&
        lower_bound_samples.size() >= 2U;
    const auto contribution =
        has_sampled_estimate
            ? find_max_contribution_interval(solution, lower_bound_samples)
            : largest_integral_interval(solution);
    const double midpoint = contribution.first +
                            (contribution.second - contribution.first) / 2.0;
    current_gap = relative_gap(current_integral, lower_bound_integral);
    LOG_INFO("MinSum iter {}: [t_a,t_b]=[{:.4f},{:.4f}], t_mid={:.4f}, "
             "int={:.6f}, LB={:.6f}, gap={:.4f}",
             iteration, contribution.first, contribution.second, midpoint,
             current_integral, lower_bound_integral, current_gap);
    if (current_gap < config.gap_target) {
      break;
    }
    if (contribution.second - contribution.first <= kMinSumTolerance) {
      break;
    }

    const StaticSolution assignment = static_solver.solve_with_budget(
        instance, midpoint, budget, config.time_limit_per_ip);
    ++result.num_ip_solves;
    if (!assignment.feasible) {
      if (assignment.time_limited ||
          assignment.optimality_status == OptimalityStatus::TIME_LIMIT) {
        result.time_limited = true;
        break;
      }
      throw std::runtime_error(
          "MinSum iteration static solve did not produce a feasible assignment");
    }
    if (assignment.time_limited ||
        assignment.optimality_status == OptimalityStatus::TIME_LIMIT) {
      result.time_limited = true;
      break;
    }
    if (config.refinement_policy ==
          MinSumRefinementPolicy::CERTIFIED_BOUND &&
      assignment.bound_status != BoundStatus::NONE) {
      insert_lower_bound_sample(lower_bound_samples, midpoint,
                              assignment.lower_bound);
      lower_bound_integral = std::max(
          lower_bound_integral,
          std::min(trapezoid_integral(lower_bound_samples), current_integral));
    }
    result.time_limited = result.time_limited || assignment.time_limited;
    result.heuristic_lower_bound_integral = lower_bound_integral;

    if (assignment.cost >= solution.cost_at(midpoint) -
                              config.improvement_tolerance *
                                  std::max(1.0, std::abs(solution.cost_at(midpoint)))) {
      ++stagnant_iterations;
      const double updated_gap =
          relative_gap(current_integral, lower_bound_integral);
      if (updated_gap <= result.gap_trace.back() + 1e-10) {
        result.gap_trace.push_back(updated_gap);
        append_trace(iteration, midpoint);
      }
      LOG_INFO("MinSum: no improvement at selected time ({}/{})",
               stagnant_iterations, config.stagnation_patience);
      if (stagnant_iterations >= config.stagnation_patience) {
        break;
      }
      continue;
    }

    const KineticSolution forward = KineticSolution::extend(
        instance,
        StaticAssignment{assignment.supporting_point, assignment.radius,
                         assignment.cost, assignment.feasible,
                         assignment.assigned_points},
        midpoint, instance.T_end, true,
        config.use_handovers, ObjectiveType::MIN_SUM, &budget);
    const KineticSolution backward = KineticSolution::extend(
        instance,
        StaticAssignment{assignment.supporting_point, assignment.radius,
                         assignment.cost, assignment.feasible,
                         assignment.assigned_points},
        midpoint, 0.0, false, config.use_handovers,
        ObjectiveType::MIN_SUM, &budget);
    KineticSolution candidate = join_directions(backward, forward, midpoint);
    if (config.use_no_dup) {
      candidate.remove_duplicates();
    }
    if (config.use_partial_ext) {
      candidate = KineticSolution::partial_extend(
          candidate, solution, ObjectiveType::MIN_SUM, &budget);
    }
    if (candidate.intervals.empty()) {
      ++stagnant_iterations;
      LOG_INFO("MinSum: no candidate extension ({}/{})", stagnant_iterations,
               config.stagnation_patience);
      const double updated_gap =
          relative_gap(current_integral, lower_bound_integral);
      if (updated_gap <= result.gap_trace.back() + 1e-10) {
        result.gap_trace.push_back(updated_gap);
        append_trace(iteration, midpoint);
      }
      if (stagnant_iterations >= config.stagnation_patience) {
        break;
      }
      continue;
    }
    if (config.use_handovers) {
      candidate.remove_duplicates();
    }

    KineticSolution combined = KineticSolution::combine(
        solution, candidate, ObjectiveType::MIN_SUM, &budget);
    local_improvement_integral(instance, combined, &budget);
    if (config.verify_after) {
      const auto filter_start = std::chrono::steady_clock::now();
      bool passes_sample_filter = false;
      try {
        passes_sample_filter = sampled_feasible(instance, combined, &budget);
      } catch (const SolverBudgetExpired&) {
        result.verification_time_sec +=
            std::chrono::duration<double>(
                std::chrono::steady_clock::now() - filter_start)
                .count();
        throw;
      }
      result.verification_time_sec +=
          std::chrono::duration<double>(std::chrono::steady_clock::now() -
                                        filter_start)
              .count();
      if (!passes_sample_filter) {
        ++stagnant_iterations;
        LOG_WARN("MinSum: rejected an unverified refinement at iter {} "
                 "({}/{})",
                 iteration, stagnant_iterations,
                 config.stagnation_patience);
        if (stagnant_iterations >= config.stagnation_patience) {
          break;
        }
        continue;
      }
    }
    const auto candidate_verification_start = std::chrono::steady_clock::now();
    VerificationReport candidate_report;
    try {
      candidate_report =
          Verifier::verify_continuous(instance, combined, 1e-6, &budget);
    } catch (const SolverBudgetExpired&) {
      result.verification_time_sec +=
          std::chrono::duration<double>(
              std::chrono::steady_clock::now() -
              candidate_verification_start)
              .count();
      throw;
    }
    result.verification_time_sec +=
        std::chrono::duration<double>(std::chrono::steady_clock::now() -
                                      candidate_verification_start)
            .count();
    result.verification_kind = candidate_report.kind;
    if (!candidate_report.all_ok()) {
      ++stagnant_iterations;
      LOG_WARN("MinSum: rejected a refinement that failed continuous "
               "verification at iter {} ({}/{})",
               iteration, stagnant_iterations,
               config.stagnation_patience);
      if (stagnant_iterations >= config.stagnation_patience) {
        break;
      }
      continue;
    }
    const double new_integral = combined.total_integral();
    const double required_improvement =
        config.improvement_tolerance *
        std::max(1.0, std::abs(current_integral));
    if (current_integral - new_integral <= required_improvement) {
      ++stagnant_iterations;
      const double updated_gap =
          relative_gap(current_integral, lower_bound_integral);
      if (updated_gap <= result.gap_trace.back() + 1e-10) {
        result.gap_trace.push_back(updated_gap);
        append_trace(iteration, midpoint);
      }
      LOG_INFO("MinSum: negligible integral improvement ({}/{})",
               stagnant_iterations, config.stagnation_patience);
      if (stagnant_iterations >= config.stagnation_patience) {
        break;
      }
      continue;
    }
    stagnant_iterations = 0;
    solution = std::move(combined);
    current_integral = new_integral;
    lower_bound_integral =
        std::min(lower_bound_integral, current_integral);
    const double new_gap =
        relative_gap(current_integral, lower_bound_integral);
    if (new_gap > result.gap_trace.back() + 1e-10) {
      throw std::runtime_error("MinSum relative gap increased");
    }
    result.gap_trace.push_back(new_gap);
    append_trace(iteration, midpoint);
  }
  } catch (const SolverBudgetExpired&) {
    result.time_limited = true;
    LOG_WARN("MinSum: global solver budget expired");
  }

  const auto finish = std::chrono::steady_clock::now();
  result.solution = std::move(solution);
  result.feasible = result.solution.is_well_formed();
  result.exact_solver = static_solver.is_exact();
  result.total_integral = result.solution.total_integral();
  result.lower_bound_integral = lower_bound_integral;
  result.lower_bound = result.certified_lower_bound_integral;
  result.bound_status =
      result.feasible ? BoundStatus::CERTIFIED : BoundStatus::NONE;
  result.upper_bound = result.feasible
                           ? result.total_integral
                           : std::numeric_limits<double>::infinity();
  result.certified_lower_bound_integral = 0.0;
  result.heuristic_lower_bound_integral = lower_bound_integral;
  result.gap = relative_gap(result.total_integral, lower_bound_integral);
  result.certified_gap.reset();
  result.total_time_sec =
      std::max(0.0, std::chrono::duration<double>(finish - start).count() -
                        result.verification_time_sec);
  if (config.verify_after) {
    const auto verification_start = std::chrono::steady_clock::now();
    try {
      const VerificationReport report =
          Verifier::verify_continuous(instance, result.solution, 1e-6, &budget);
      result.verification_time_sec +=
          std::chrono::duration<double>(std::chrono::steady_clock::now() -
                                        verification_start)
              .count();
      result.verification_kind = report.kind;
      if (!report.all_ok()) {
        throw std::runtime_error("MinSum final verification failed: " +
                                 report.errors.front());
      }
      result.verified = true;
    } catch (const SolverBudgetExpired&) {
      result.verification_time_sec +=
          std::chrono::duration<double>(std::chrono::steady_clock::now() -
                                        verification_start)
              .count();
      result.time_limited = true;
    }
  }
  result.time_limited = result.time_limited || budget.expired();
  result.optimality_status =
      result.time_limited
          ? OptimalityStatus::TIME_LIMIT
          : (!result.feasible
                 ? OptimalityStatus::FAILED
                 : (result.exact_solver && result.certified_gap.has_value() &&
                            *result.certified_gap <= 1e-12
                        ? OptimalityStatus::OPTIMAL
                        : OptimalityStatus::FEASIBLE));
  LOG_INFO("MinSum: done. int={:.6f}, LB={:.6f}, gap={:.4f}, iters={}, "
           "t={:.3f}s",
           result.total_integral, result.lower_bound_integral, result.gap,
           result.num_iterations, result.total_time_sec);
  return result;
}
}
