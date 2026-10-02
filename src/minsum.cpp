#include "kdc/minsum.hpp"

#include "kdc/algorithms/ip_static_solver.hpp"
#include "kdc/logging.hpp"
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
      config.global_time_limit_sec <= 0.0 || config.lb_num_samples <= 0 ||
      config.max_iterations <= 0 || config.verify_every_n_iters < 0) {
    throw std::invalid_argument("MinSum configuration is invalid");
  }
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
                              KineticSolution& solution) {
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
    bool applied_this_pass = false;
    for (Index index = 0; index < solution.intervals.size(); ++index) {
      bool improved = true;
      while (improved) {
        improved = false;
        const std::vector<int> current_supports =
            solution.intervals[index].supporting_point;
        const SolutionInterval current = solution.intervals[index];
        const double old_integral =
            interval_integral(current, current.t_start, current.t_end);

        for (int from = 0; from < instance.m && !improved; ++from) {
          for (int to = 0; to < instance.m && !improved; ++to) {
            if (from == to) {
              continue;
            }
            if (current_supports[static_cast<Index>(from)] < 0 ||
                current_supports[static_cast<Index>(to)] < 0) {
              continue;
            }
            const auto events = KineticCore::find_handovers(
                instance, from, to, current_supports, current.t_start,
                current.t_end, true);
            for (const auto& event : events) {
              if (!event.valid || event.from_station != from ||
                  event.to_station != to ||
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
              KineticSolution::compute_quadratic_coeffs(
                  instance, left.supporting_point, left);
              KineticSolution::compute_quadratic_coeffs(
                  instance, right.supporting_point, right);
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
    std::vector<std::pair<double, double>>& output_samples) {
  (void)config;
  if (num_samples <= 0 || !std::isfinite(instance.T_end) ||
      instance.T_end <= 0.0) {
    throw std::invalid_argument("invalid MinSum lower-bound sampling domain");
  }
  output_samples.clear();
  output_samples.reserve(static_cast<Index>(num_samples) + 1U);
  for (int sample = 0; sample <= num_samples; ++sample) {
    const double time =
        instance.T_end * static_cast<double>(sample) /
        static_cast<double>(num_samples);
    const StaticSolution assignment = static_solver.solve(instance, time);
    if (!assignment.feasible) {
      throw std::runtime_error(
          "MinSum lower-bound IP solve did not produce a feasible assignment");
    }
    const double lower_bound = static_solver.provides_lower_bound()
                                   ? assignment.lower_bound
                                   : 0.0;
    output_samples.emplace_back(time, lower_bound);
  }
  return trapezoid_integral(output_samples);
}

KineticSolution MinSumSolver::combine_integral(
    const KineticSolution& first, const KineticSolution& second) {
  return KineticSolution::combine(first, second, ObjectiveType::MIN_SUM);
}

void MinSumSolver::local_improvement_integral(const Instance& instance,
                                              KineticSolution& solution) {
  bool improved = true;
  while (improved) {
    improved = false;
    for (int station_id = 0; station_id < instance.m; ++station_id) {
      KineticSolution candidate = solution;
      bool has_support = false;
      for (auto& interval : candidate.intervals) {
        if (interval.supporting_point[static_cast<Index>(station_id)] >= 0) {
          has_support = true;
          interval.supporting_point[static_cast<Index>(station_id)] = -1;
          KineticSolution::compute_quadratic_coeffs(
              instance, interval.supporting_point, interval);
        }
      }
      if (!has_support) {
        continue;
      }

      bool remains_feasible = true;
      for (const auto& interval : candidate.intervals) {
        for (int sample = 0; sample <= 100 && remains_feasible; ++sample) {
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
  IPStaticSolver::Config static_config;
  static_config.time_limit_sec = config.time_limit_per_ip;
  static_config.gap_target = config.initial_ip_gap;
  IPStaticSolver static_solver(&solver, static_config);
  return solve(instance, static_solver, config);
}

MinSumSolver::Result MinSumSolver::solve(const Instance& instance,
                                         IStaticSolver& static_solver,
                                         const Config& config) {
  validate_config(config);
  if (instance.n < 0 || instance.m < 0 ||
      static_cast<Index>(instance.n) != instance.trajectories.size() ||
      static_cast<Index>(instance.m) != instance.stations.size() ||
      !std::isfinite(instance.T_end) || instance.T_end <= 0.0) {
    throw std::invalid_argument("MinSum received an invalid instance");
  }
  LOG_INFO("MinSum: start n={}, m={}", instance.n, instance.m);
  const auto start = std::chrono::high_resolution_clock::now();
  const auto global_deadline =
      std::chrono::steady_clock::now() +
      std::chrono::duration<double>(config.global_time_limit_sec);
  Result result;

  const StaticSolution initial_assignment = static_solver.solve(instance, 0.0);
  ++result.num_ip_solves;
  if (!initial_assignment.feasible) {
    throw std::runtime_error(
        "MinSum initial static solve did not produce a feasible assignment");
  }
  KineticSolution solution = KineticSolution::extend(
      instance,
      StaticAssignment{initial_assignment.supporting_point,
                       initial_assignment.radius, initial_assignment.cost,
                       initial_assignment.feasible},
      0.0, instance.T_end, true,
      config.use_handovers, ObjectiveType::MIN_SUM);
  if (!solution.is_well_formed()) {
    throw std::runtime_error("MinSum initial extension is malformed");
  }

  std::vector<std::pair<double, double>> lower_bound_samples;
  double lower_bound_integral = compute_integral_lower_bound(
      instance, static_solver, config.lb_num_samples, config,
      lower_bound_samples);
  result.num_ip_solves += config.lb_num_samples + 1;
  double current_integral = solution.total_integral();
  lower_bound_integral = std::min(lower_bound_integral, current_integral);
  result.certified_lower_bound_integral =
      static_solver.provides_lower_bound() ? lower_bound_integral : 0.0;
  result.heuristic_lower_bound_integral = lower_bound_integral;
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
            std::chrono::high_resolution_clock::now() - start)
            .count();
    row.num_ip_solves = result.num_ip_solves;
    result.trace.push_back(row);
  };
  append_trace(0, 0.0);
  for (int iteration = 1; iteration <= config.max_iterations; ++iteration) {
    if (std::chrono::steady_clock::now() >= global_deadline) {
      LOG_WARN("MinSum: global deadline reached after {} iterations",
               iteration - 1);
      break;
    }
    result.num_iterations = iteration;
    const auto contribution =
        find_max_contribution_interval(solution, lower_bound_samples);
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

    const StaticSolution assignment = static_solver.solve(instance, midpoint);
    ++result.num_ip_solves;
    if (!assignment.feasible) {
      throw std::runtime_error(
          "MinSum iteration static solve did not produce a feasible assignment");
    }
    if (static_solver.provides_lower_bound()) {
      insert_lower_bound_sample(lower_bound_samples, midpoint,
                                assignment.lower_bound);
      lower_bound_integral = std::max(
          lower_bound_integral,
          std::min(trapezoid_integral(lower_bound_samples), current_integral));
      result.certified_lower_bound_integral = lower_bound_integral;
    }
    result.heuristic_lower_bound_integral = lower_bound_integral;

    if (assignment.cost >= solution.cost_at(midpoint) - 1e-9) {
      const double updated_gap =
          relative_gap(current_integral, lower_bound_integral);
      if (updated_gap <= result.gap_trace.back() + 1e-10) {
        result.gap_trace.push_back(updated_gap);
        append_trace(iteration, midpoint);
      }
      LOG_INFO("MinSum: no improvement at contribution midpoint, stopping");
      break;
    }

    const KineticSolution forward = KineticSolution::extend(
        instance,
        StaticAssignment{assignment.supporting_point, assignment.radius,
                         assignment.cost, assignment.feasible},
        midpoint, instance.T_end, true,
        config.use_handovers, ObjectiveType::MIN_SUM);
    const KineticSolution backward = KineticSolution::extend(
        instance,
        StaticAssignment{assignment.supporting_point, assignment.radius,
                         assignment.cost, assignment.feasible},
        midpoint, 0.0, false, config.use_handovers,
        ObjectiveType::MIN_SUM);
    KineticSolution candidate = join_directions(backward, forward, midpoint);
    if (config.use_no_dup) {
      candidate.remove_duplicates();
    }
    if (config.use_partial_ext) {
      candidate = KineticSolution::partial_extend(
          candidate, solution, ObjectiveType::MIN_SUM);
    }
    if (candidate.intervals.empty()) {
      LOG_INFO("MinSum: partial extension is empty, stopping");
      const double updated_gap =
          relative_gap(current_integral, lower_bound_integral);
      if (updated_gap <= result.gap_trace.back() + 1e-10) {
        result.gap_trace.push_back(updated_gap);
        append_trace(iteration, midpoint);
      }
      break;
    }
    if (config.use_handovers) {
      candidate.remove_duplicates();
    }

    KineticSolution combined = combine_integral(solution, candidate);
    local_improvement_integral(instance, combined);
    if (config.verify_after &&
        (config.verify_every_n_iters > 0 &&
         iteration % config.verify_every_n_iters == 0)) {
      const VerificationReport report =
          Verifier::verify(instance, combined, 100, 1e-6);
      if (!report.all_ok()) {
        LOG_ERROR("MinSum: verification failed at iter {}", iteration);
        for (const auto& error : report.errors) {
          LOG_ERROR("  {}", error);
        }
        throw std::runtime_error("MinSum solution verification failed");
      }
    }

    const double new_integral = combined.total_integral();
    if (new_integral >= current_integral - 1e-9) {
      const double updated_gap =
          relative_gap(current_integral, lower_bound_integral);
      if (updated_gap <= result.gap_trace.back() + 1e-10) {
        result.gap_trace.push_back(updated_gap);
        append_trace(iteration, midpoint);
      }
      LOG_INFO("MinSum: no integral improvement, stopping");
      break;
    }
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

  const auto finish = std::chrono::high_resolution_clock::now();
  result.solution = std::move(solution);
  result.total_integral = result.solution.total_integral();
  result.lower_bound_integral = lower_bound_integral;
  result.certified_lower_bound_integral =
      static_solver.provides_lower_bound() ? lower_bound_integral : 0.0;
  result.heuristic_lower_bound_integral = lower_bound_integral;
  result.gap = relative_gap(result.total_integral, lower_bound_integral);
  result.total_time_sec =
      std::chrono::duration<double>(finish - start).count();
  if (config.verify_after) {
    const VerificationReport report =
        Verifier::verify(instance, result.solution, 100, 1e-6);
    if (!report.all_ok()) {
      throw std::runtime_error("MinSum final verification failed: " +
                               report.errors.front());
    }
    result.verified = true;
  }
  LOG_INFO("MinSum: done. int={:.6f}, LB={:.6f}, gap={:.4f}, iters={}, "
           "t={:.3f}s",
           result.total_integral, result.lower_bound_integral, result.gap,
           result.num_iterations, result.total_time_sec);
  return result;
}
}
