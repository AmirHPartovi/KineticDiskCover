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

double MinMaxSolver::find_peak_time(const KineticSolution& solution) {
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
  const auto start = std::chrono::steady_clock::now();
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
  result.bound_status = initial_assignment.bound_status;
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
  result.initial_peak_cost = solution.peak_cost();
  double current_gap =
      relative_gap(result.initial_peak_cost, result.certified_lower_bound);
  result.gap_trace.push_back(current_gap);

  try {
    for (int iteration = 1;
         iteration <= config.max_iterations && !result.time_limited;
         ++iteration) {
      budget.checkpoint();
      result.num_iterations = iteration;
      const double peak = solution.peak_cost();
      const double previous_peak = peak;
      const double maximum_time = find_peak_time(solution);
      current_gap = relative_gap(peak, result.certified_lower_bound);
      IterTrace row;
      row.iter = iteration;
      row.t_max = maximum_time;
      row.objective_value = peak;
      row.lower_bound = result.certified_lower_bound;
      row.gap = current_gap;
      row.certified_gap =
          result.bound_status == BoundStatus::CERTIFIED ? current_gap : 0.0;
      row.has_certified_gap =
          result.bound_status == BoundStatus::CERTIFIED;
      row.heuristic_gap =
          relative_gap(peak, result.heuristic_lower_bound);
      row.combined_peak = peak;
      row.wall_time_sec =
          std::chrono::duration<double>(std::chrono::steady_clock::now() -
                                        solve_start)
              .count();
      row.num_ip_solves = result.num_ip_solves;
      result.trace.push_back(row);
      LOG_INFO("MinMax iter {}: t_max={:.6f}, peak={:.6f}, LB={:.6f}, "
               "gap={:.4f}",
               iteration, maximum_time, peak, result.certified_lower_bound,
               current_gap);
      if (result.bound_status == BoundStatus::CERTIFIED &&
          current_gap < config.gap_target) {
        result.trace.back().stop_reason =
            "gap_target_reached_with_certified_bound";
        break;
      }

      const StaticSolution assignment = static_solver.solve_with_budget(
          instance, maximum_time, budget, config.time_limit_per_ip);
      ++result.num_ip_solves;
      result.trace.back().num_ip_solves = result.num_ip_solves;
      result.trace.back().static_solver_status =
          optimality_status_to_string(assignment.optimality_status);
      result.trace.back().static_solver_exact = assignment.exact_solver;
      result.trace.back().static_cost_at_peak_time = assignment.cost;
      result.trace.back().static_lower_bound = assignment.lower_bound;
      result.trace.back().static_upper_bound = assignment.upper_bound;
      if (!assignment.feasible) {
        if (assignment.time_limited ||
            assignment.optimality_status == OptimalityStatus::TIME_LIMIT) {
          result.time_limited = true;
          result.trace.back().stop_reason = "global_time_limit";
          break;
        }
        result.trace.back().stop_reason = "static_solve_failed";
        throw std::runtime_error("MinMax iteration static solve is infeasible");
      }
      if (assignment.bound_status == BoundStatus::CERTIFIED) {
        result.certified_lower_bound =
            std::max(result.certified_lower_bound, assignment.lower_bound);
        result.bound_status = BoundStatus::CERTIFIED;
      } else if (assignment.bound_status == BoundStatus::HEURISTIC) {
        result.heuristic_lower_bound =
            std::max(result.heuristic_lower_bound, assignment.lower_bound);
        if (result.bound_status != BoundStatus::CERTIFIED) {
          result.bound_status = BoundStatus::HEURISTIC;
        }
      }
      result.trace.back().lower_bound = result.certified_lower_bound;
      result.trace.back().has_certified_gap =
          result.bound_status == BoundStatus::CERTIFIED;
      result.trace.back().certified_gap =
          result.trace.back().has_certified_gap
              ? relative_gap(peak, result.certified_lower_bound)
              : 0.0;
      result.trace.back().heuristic_gap =
          relative_gap(peak, result.heuristic_lower_bound);
      result.time_limited = result.time_limited || assignment.time_limited;
      const bool static_optimum_proven =
          assignment.exact_solver && !assignment.time_limited &&
          assignment.optimality_status == OptimalityStatus::OPTIMAL;
      if (static_optimum_proven && assignment.cost >= peak - 1e-9) {
        LOG_INFO("MinMax: no improvement at t_max, stopping");
        result.trace.back().stop_reason =
            "static_optimum_not_better_than_peak";
        const double updated_gap =
            relative_gap(peak, result.certified_lower_bound);
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
        result.trace.back().stop_reason = "candidate_empty";
        const double updated_gap =
            relative_gap(peak, result.certified_lower_bound);
        if (updated_gap < current_gap - 1e-12) {
          result.gap_trace.push_back(updated_gap);
        }
        break;
      }

      const double candidate_domain_tolerance =
          1e-9 * std::max(1.0, instance.T_end);
      if (!candidate.is_well_formed() ||
          std::abs(candidate.intervals.front().t_start) >
              candidate_domain_tolerance ||
          std::abs(candidate.intervals.back().t_end - instance.T_end) >
              candidate_domain_tolerance) {
        result.trace.back().stop_reason = "candidate_domain_invalid";
        throw std::runtime_error(
            "MinMax candidate does not cover the complete time domain");
      }
      const auto candidate_verification_start =
          std::chrono::steady_clock::now();
      VerificationReport candidate_report;
      try {
        candidate_report =
            Verifier::verify_continuous(instance, candidate, 1e-6, &budget);
      } catch (const SolverBudgetExpired&) {
        result.verification_time_sec +=
            std::chrono::duration<double>(
                std::chrono::steady_clock::now() -
                candidate_verification_start)
                .count();
        throw;
      }
      result.verification_time_sec += std::chrono::duration<double>(
                                          std::chrono::steady_clock::now() -
                                          candidate_verification_start)
                                          .count();
      result.verification_kind = candidate_report.kind;
      if (!candidate_report.all_ok()) {
        result.trace.back().stop_reason = "verification_failure";
        throw std::runtime_error(
            "MinMax candidate verification failed: " +
            candidate_report.errors.front());
      }
      result.trace.back().candidate_peak = candidate.peak_cost();
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
          result.trace.back().stop_reason = "verification_failure";
          LOG_ERROR("MinMax: verification failed at iter {}", iteration);
          for (const auto& error : report.errors) {
            LOG_ERROR("  {}", error);
          }
          throw std::runtime_error("MinMax solution verification failed");
        }
      }

      const double new_peak = solution.peak_cost();
      if (new_peak > previous_peak + 1e-9 * std::max(1.0, std::abs(previous_peak))) {
        throw std::runtime_error("MinMax combination increased the peak");
      }
      result.trace.back().combined_peak = new_peak;
      result.trace.back().peak_improvement = previous_peak - new_peak;
      result.trace.back().candidate_accepted = true;
      const double new_gap =
          relative_gap(new_peak, result.certified_lower_bound);
      if (new_gap > current_gap + 1e-10) {
        throw std::runtime_error("MinMax relative gap increased");
      }
      result.gap_trace.push_back(new_gap);
      current_gap = new_gap;
      if (new_peak >= previous_peak - 1e-9) {
        LOG_INFO("MinMax: combined solution did not improve peak, stopping");
        result.trace.back().stop_reason =
            static_optimum_proven ? "no_peak_improvement_after_exact_candidate"
                                  : "heuristic_no_improvement";
        break;
      }
      result.trace.back().stop_reason = "candidate_accepted";
    }
  } catch (const SolverBudgetExpired&) {
    result.time_limited = true;
    if (!result.trace.empty()) {
      result.trace.back().stop_reason = "global_time_limit";
    }
    LOG_WARN("MinMax: global solver budget expired");
  }
  if (!result.trace.empty() && result.trace.back().stop_reason == "pending") {
    result.trace.back().stop_reason = "iteration_limit";
  }

  const auto finish = std::chrono::steady_clock::now();
  result.solution = std::move(solution);
  result.feasible = result.solution.is_well_formed();
  result.exact_solver = static_solver.is_exact();
  result.peak_cost = result.solution.peak_cost();
  result.peak_time = result.solution.peak_time();
  result.peak_consistent = Verifier::check_peak_consistency(
      result.solution, result.peak_cost, result.peak_time);
  if (!result.peak_consistent) {
    throw std::runtime_error(
        "MinMax reported peak is inconsistent with the solution");
  }
  result.lower_bound = result.certified_lower_bound;
  result.upper_bound = result.feasible
                           ? result.peak_cost
                           : std::numeric_limits<double>::infinity();
  result.gap = relative_gap(result.peak_cost, result.certified_lower_bound);
  result.heuristic_gap =
      relative_gap(result.peak_cost, result.heuristic_lower_bound);
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
  LOG_INFO("MinMax: done. peak={:.6f}, certified_LB={:.6f}, gap={:.4f}, "
           "iters={}, t={:.3f}s",
           result.peak_cost, result.certified_lower_bound, result.gap,
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
