#include "kdc/minmax.hpp"

#include "kdc/algorithms/ip_static_solver.hpp"
#include "kdc/logging.hpp"
#include "kdc/stationary.hpp"
#include "kdc/trace.hpp"
#include "kdc/verify.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <stdexcept>

namespace kdc {
namespace {
KineticSolution join_directions(const KineticSolution& backward,
                                const KineticSolution& forward,
                                double join_time, ObjectiveType objective) {
  KineticSolution joined;
  joined.objective = objective;
  for (const auto& interval : backward.intervals) {
    if (interval.t_end <= join_time) {
      joined.intervals.push_back(interval);
    }
  }
  for (const auto& interval : forward.intervals) {
    if (interval.t_start >= join_time) {
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
        "forward and backward kinetic extensions did not join continuously");
  }
  return joined;
}

double relative_gap(double peak, double lower_bound) {
  return std::max(0.0, peak - lower_bound) /
         std::max(1.0, std::abs(lower_bound));
}

void validate_config(const MinMaxSolver::Config& config) {
  if (!std::isfinite(config.initial_ip_gap) ||
      !std::isfinite(config.final_ip_gap) ||
      !std::isfinite(config.time_limit_per_ip) ||
      !std::isfinite(config.gap_target) ||
      !std::isfinite(config.global_time_limit_sec) ||
      config.initial_ip_gap < 0.0 || config.final_ip_gap < 0.0 ||
      config.time_limit_per_ip <= 0.0 || config.gap_target < 0.0 ||
      config.global_time_limit_sec < 0.0 || config.max_iterations <= 0 ||
      config.verify_every_n_iters < 0) {
    throw std::invalid_argument("MinMax configuration is invalid");
  }
}
}

double MinMaxSolver::find_max_area_time(const KineticSolution& solution) {
  return solution.peak_time();
}

MinMaxSolver::Result MinMaxSolver::solve(const Instance& instance,
                                         ILPSolver& solver,
                                         const Config& config) {
  const double limit = config.global_time_limit_sec > 0.0
                           ? config.global_time_limit_sec
                           : 600.0;
  SolverBudget budget(limit);
  return solve(instance, solver, config, budget);
}

MinMaxSolver::Result MinMaxSolver::solve(const Instance& instance,
                                         ILPSolver& solver,
                                         const Config& config,
                                         SolverBudget& budget) {
  IPStaticSolver::Config static_config;
  static_config.time_limit_sec = config.time_limit_per_ip;
  static_config.gap_target = config.initial_ip_gap;
  IPStaticSolver static_solver(&solver, static_config);
  return solve(instance, static_solver, config, budget);
}

MinMaxSolver::Result MinMaxSolver::solve(const Instance& instance,
                                         IStaticSolver& static_solver,
                                         const Config& config) {
  const double limit = config.global_time_limit_sec > 0.0
                           ? config.global_time_limit_sec
                           : (static_solver.is_exact() ? 600.0 : 30.0);
  SolverBudget budget(limit);
  return solve(instance, static_solver, config, budget);
}

MinMaxSolver::Result MinMaxSolver::solve(const Instance& instance,
                                         IStaticSolver& static_solver,
                                         const Config& config,
                                         SolverBudget& budget) {
  validate_config(config);
  if (instance.n < 0 || instance.m < 0 ||
      static_cast<Index>(instance.n) != instance.trajectories.size() ||
      static_cast<Index>(instance.m) != instance.stations.size() ||
      !std::isfinite(instance.T_end) || instance.T_end <= 0.0) {
    throw std::invalid_argument("MinMax received an invalid instance");
  }
  LOG_INFO("MinMax: start n={}, m={}, T={}", instance.n, instance.m,
           instance.T_end);
  const auto start = std::chrono::high_resolution_clock::now();
  const auto solve_start = std::chrono::steady_clock::now();

  Result result;
  const StaticSolution initial_assignment = static_solver.solve_with_budget(
      instance, 0.0, budget, config.time_limit_per_ip);
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
    result.certified_lower_bound =
        initial_assignment.bound_status == BoundStatus::CERTIFIED
            ? initial_assignment.lower_bound
            : 0.0;
    result.heuristic_lower_bound =
        initial_assignment.bound_status == BoundStatus::HEURISTIC
            ? initial_assignment.lower_bound
            : 0.0;
    result.total_time_sec =
        std::chrono::duration<double>(
            std::chrono::steady_clock::now() - start)
            .count();
    return result;
  }
  double lower_bound = initial_assignment.bound_status != BoundStatus::NONE
                           ? initial_assignment.lower_bound
                           : 0.0;
  result.bound_status =
      initial_assignment.bound_status == BoundStatus::NONE
          ? BoundStatus::CERTIFIED
          : initial_assignment.bound_status;
  result.certified_lower_bound =
      initial_assignment.bound_status == BoundStatus::CERTIFIED
          ? initial_assignment.lower_bound
          : 0.0;
  result.heuristic_lower_bound =
      initial_assignment.bound_status == BoundStatus::HEURISTIC
          ? initial_assignment.lower_bound
          : 0.0;
  result.time_limited = initial_assignment.time_limited;

  KineticSolution solution = KineticSolution::extend(
      instance,
      StaticAssignment{initial_assignment.supporting_point,
                       initial_assignment.radius, initial_assignment.cost,
                       initial_assignment.feasible,
                       initial_assignment.assigned_points},
      0.0, instance.T_end, true,
      config.use_handovers, ObjectiveType::MIN_MAX, &budget);
  if (!solution.is_well_formed()) {
    throw std::runtime_error("MinMax initial extension is malformed");
  }
  double current_gap = relative_gap(solution.peak_cost(), lower_bound);
  result.gap_trace.push_back(current_gap);

  try {
    for (int iteration = 1;
         iteration <= config.max_iterations && !result.time_limited;
         ++iteration) {
      budget.checkpoint();
      result.num_iterations = iteration;
      const double peak = solution.peak_cost();
      const double previous_peak = peak;
      const double maximum_time = find_max_area_time(solution);
      current_gap = relative_gap(peak, lower_bound);
      IterTrace row;
      row.iter = iteration;
      row.t_max = maximum_time;
      row.objective_value = peak;
      row.lower_bound = lower_bound;
      row.gap = current_gap;
      row.wall_time_sec =
          std::chrono::duration<double>(std::chrono::steady_clock::now() -
                                        solve_start)
              .count();
      row.num_ip_solves = result.num_ip_solves;
      result.trace.push_back(row);
      LOG_INFO("MinMax iter {}: t_max={:.6f}, peak={:.6f}, LB={:.6f}, "
               "gap={:.4f}",
               iteration, maximum_time, peak, lower_bound, current_gap);
      if (current_gap < config.gap_target) {
        break;
      }

      const StaticSolution assignment = static_solver.solve_with_budget(
          instance, maximum_time, budget, config.time_limit_per_ip);
      ++result.num_ip_solves;
      if (!assignment.feasible) {
        if (assignment.time_limited ||
            assignment.optimality_status == OptimalityStatus::TIME_LIMIT) {
          result.time_limited = true;
          break;
        }
        throw std::runtime_error("MinMax iteration static solve is infeasible");
      }
      if (assignment.bound_status == BoundStatus::CERTIFIED) {
        result.certified_lower_bound =
            std::max(result.certified_lower_bound, assignment.lower_bound);
      } else if (assignment.bound_status == BoundStatus::HEURISTIC) {
        result.heuristic_lower_bound =
            std::max(result.heuristic_lower_bound, assignment.lower_bound);
      }
      if (assignment.bound_status != BoundStatus::NONE &&
          assignment.lower_bound > lower_bound) {
        lower_bound = assignment.lower_bound;
        result.bound_status = assignment.bound_status;
      }
      result.time_limited = result.time_limited || assignment.time_limited;
      if (assignment.cost >= peak - 1e-9) {
        LOG_INFO("MinMax: no improvement at t_max, stopping");
        const double updated_gap = relative_gap(peak, lower_bound);
        if (updated_gap < current_gap - 1e-12) {
          result.gap_trace.push_back(updated_gap);
        }
        break;
      }

      const KineticSolution forward = KineticSolution::extend(
          instance,
          StaticAssignment{assignment.supporting_point, assignment.radius,
                           assignment.cost, assignment.feasible,
                           assignment.assigned_points},
          maximum_time, instance.T_end, true,
          config.use_handovers, ObjectiveType::MIN_MAX, &budget);
      const KineticSolution backward = KineticSolution::extend(
          instance,
          StaticAssignment{assignment.supporting_point, assignment.radius,
                           assignment.cost, assignment.feasible,
                           assignment.assigned_points},
          maximum_time, 0.0, false,
          config.use_handovers, ObjectiveType::MIN_MAX, &budget);
      KineticSolution candidate = join_directions(
          backward, forward, maximum_time, ObjectiveType::MIN_MAX);
      if (config.use_no_dup) {
        candidate.remove_duplicates();
      }
      if (config.use_partial_ext) {
        candidate = KineticSolution::partial_extend(
            candidate, solution, ObjectiveType::MIN_MAX, &budget);
      }
      if (candidate.intervals.empty()) {
        LOG_INFO("MinMax: partial extension is empty, stopping");
        const double updated_gap = relative_gap(peak, lower_bound);
        if (updated_gap < current_gap - 1e-12) {
          result.gap_trace.push_back(updated_gap);
        }
        break;
      }

      solution = KineticSolution::combine(solution, candidate,
                                          ObjectiveType::MIN_MAX, &budget);
      if (config.verify_each_iteration ||
          (config.verify_after && config.verify_every_n_iters > 0 &&
           iteration % config.verify_every_n_iters == 0)) {
        const auto verification_start = std::chrono::steady_clock::now();
        VerificationReport report;
        try {
          report =
              Verifier::verify_continuous(instance, solution, 1e-6, &budget);
        } catch (const SolverBudgetExpired&) {
          result.verification_time_sec +=
              std::chrono::duration<double>(
                  std::chrono::steady_clock::now() - verification_start)
                  .count();
          throw;
        }
        result.verification_time_sec +=
            std::chrono::duration<double>(std::chrono::steady_clock::now() -
                                          verification_start)
                .count();
        result.verification_kind = report.kind;
        if (!report.all_ok()) {
          LOG_ERROR("MinMax: verification failed at iter {}", iteration);
          for (const auto& error : report.errors) {
            LOG_ERROR("  {}", error);
          }
          throw std::runtime_error("MinMax solution verification failed");
        }
      }

      const double new_peak = solution.peak_cost();
      const double new_gap = relative_gap(new_peak, lower_bound);
      if (new_gap > current_gap + 1e-10) {
        throw std::runtime_error("MinMax relative gap increased");
      }
      result.gap_trace.push_back(new_gap);
      current_gap = new_gap;
      if (new_peak >= previous_peak - 1e-9) {
        LOG_INFO("MinMax: combined solution did not improve peak, stopping");
        break;
      }
    }
  } catch (const SolverBudgetExpired&) {
    result.time_limited = true;
    LOG_WARN("MinMax: global solver budget expired");
  }

  const auto finish = std::chrono::high_resolution_clock::now();
  result.solution = std::move(solution);
  result.feasible = result.solution.is_well_formed();
  result.exact_solver = static_solver.is_exact();
  result.peak_cost = result.solution.peak_cost();
  result.lower_bound = result.certified_lower_bound;
  result.bound_status =
      result.feasible ? BoundStatus::CERTIFIED : BoundStatus::NONE;
  result.upper_bound = result.feasible
                           ? result.peak_cost
                           : std::numeric_limits<double>::infinity();
  result.gap = relative_gap(result.peak_cost, lower_bound);
  if (result.exact_solver && result.feasible &&
      result.bound_status == BoundStatus::CERTIFIED &&
      std::isfinite(result.upper_bound)) {
    result.certified_gap =
        relative_gap(result.upper_bound, result.certified_lower_bound);
  }
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
        throw std::runtime_error("MinMax final solution verification failed: " +
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
  LOG_INFO("MinMax: done. peak={:.6f}, LB={:.6f}, gap={:.4f}, iters={}, "
           "t={:.3f}s",
           result.peak_cost, result.lower_bound, result.gap,
           result.num_iterations, result.total_time_sec);
  if (!config.trace_csv_path.empty()) {
    try {
      TraceWriter::write_csv(result.trace, config.trace_csv_path);
    } catch (const std::exception& error) {
      LOG_ERROR("MinMax: failed to write trace CSV: {}", error.what());
    }
    TraceWriter::log_summary(result.trace);
  }
  return result;
}
}
