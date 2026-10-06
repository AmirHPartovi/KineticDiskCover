#include "kdc/minmaxsum.hpp"

#include "kdc/verify.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <stdexcept>

namespace kdc {
namespace {
using Clock = SolverBudget::Clock;

bool within_tolerance(double lhs, double rhs, const MinMaxSumSolver::Config& config) {
  return lhs <= rhs + config.absolute_tolerance +
                    config.relative_tolerance *
                        std::max({1.0, std::abs(lhs), std::abs(rhs)});
}

bool covers_horizon(const KineticSolution& solution, double end,
                    const MinMaxSumSolver::Config& config) {
  if (!solution.is_well_formed()) {
    return false;
  }
  const double tolerance =
      config.absolute_tolerance +
      config.relative_tolerance * std::max(1.0, std::abs(end));
  return std::abs(solution.intervals.front().t_start) <= tolerance &&
         std::abs(solution.intervals.back().t_end - end) <= tolerance;
}

void validate_config(const MinMaxSumSolver::Config& config) {
  if (!std::isfinite(config.global_time_limit_sec) ||
      config.global_time_limit_sec <= 0.0 ||
      !std::isfinite(config.minmax_budget_fraction) ||
      !std::isfinite(config.minsum_budget_fraction) ||
      config.minmax_budget_fraction <= 0.0 ||
      config.minsum_budget_fraction <= 0.0 ||
      std::abs(config.minmax_budget_fraction +
               config.minsum_budget_fraction - 1.0) > 1e-9 ||
      !std::isfinite(config.absolute_tolerance) ||
      !std::isfinite(config.relative_tolerance) ||
      config.absolute_tolerance < 0.0 ||
      config.relative_tolerance < 0.0) {
    throw std::invalid_argument(
        "MinMaxSum requires positive component budget fractions summing to "
        "one, a positive global budget, and nonnegative finite tolerances");
  }
}

std::string source_id(const std::string& configured, const Instance& instance,
                     const char* component, const IStaticSolver& solver,
                     unsigned seed) {
  if (!configured.empty()) {
    return configured;
  }
  const std::string instance_id =
      instance.name.empty() ? std::to_string(instance.id) : instance.name;
  return instance_id + ":" + component + ":" + solver.name() + ":seed-" +
         std::to_string(seed);
}

OptimalityStatus certified_or_reported_status(OptimalityStatus status,
                                               double objective_value) {
  if (status == OptimalityStatus::TIME_LIMIT ||
      status == OptimalityStatus::FAILED ||
      status == OptimalityStatus::INFEASIBLE) {
    return status;
  }
  // Disk-area costs are nonnegative, so an exactly zero objective is a
  // global certificate independent of the static solver used to find it.
  return objective_value == 0.0 ? OptimalityStatus::OPTIMAL : status;
}

bool usable_component(const MinMaxSolver::Result& component, double horizon,
                      const MinMaxSumSolver::Config& config) {
  if (!component.feasible || !component.verified ||
      !covers_horizon(component.solution, horizon, config)) {
    return false;
  }
  const double peak = component.solution.peak_cost();
  if (!within_tolerance(component.peak_cost, peak, config) ||
      !within_tolerance(peak, component.peak_cost, config)) {
    return false;
  }
  return Verifier::check_peak_consistency(
      component.solution, peak, component.solution.peak_time());
}

bool usable_component(const MinSumSolver::Result& component, double horizon,
                      const MinMaxSumSolver::Config& config) {
  if (!component.feasible || !component.verified ||
      !covers_horizon(component.solution, horizon, config)) {
    return false;
  }
  const double peak = component.solution.peak_cost();
  const double integral = component.solution.total_integral();
  if (!std::isfinite(integral) ||
      !within_tolerance(component.total_integral, integral, config) ||
      !within_tolerance(integral, component.total_integral, config)) {
    return false;
  }
  return Verifier::check_peak_consistency(
      component.solution, peak, component.solution.peak_time());
}

void validate_instance(const Instance& instance) {
  if (instance.n < 0 || instance.m < 0 ||
      static_cast<Index>(instance.n) != instance.trajectories.size() ||
      static_cast<Index>(instance.m) != instance.stations.size() ||
      !std::isfinite(instance.T_end) || instance.T_end <= 0.0) {
    throw std::invalid_argument("MinMaxSum received an invalid instance");
  }
}
}  // namespace

MinMaxSumSolver::Result MinMaxSumSolver::solve(
    const Instance& instance, IStaticSolver& static_solver,
    const Config& config) {
  validate_config(config);
  SolverBudget budget(config.global_time_limit_sec);
  return solve(instance, static_solver, config, budget);
}

MinMaxSumSolver::Result MinMaxSumSolver::solve(
    const Instance& instance, IStaticSolver& static_solver,
    const Config& config, SolverBudget& budget) {
  validate_config(config);
  validate_instance(instance);

  Result result;
  result.seed = config.seed;
  result.minmax_source_run =
      source_id(config.minmax_source_run, instance, "minmax", static_solver,
                config.seed);
  result.minsum_source_run =
      source_id(config.minsum_source_run, instance, "minsum", static_solver,
                config.seed + 1U);

  const auto overall_start = Clock::now();
  const double initial_remaining = budget.remaining_seconds();
  if (initial_remaining <= 0.0) {
    result.time_limited = true;
    result.minmax_component_status = MinMaxSumComponentStatus::TIME_LIMIT;
    result.minsum_component_status = MinMaxSumComponentStatus::TIME_LIMIT;
    result.minmax_component_optimality = OptimalityStatus::TIME_LIMIT;
    result.minsum_component_optimality = OptimalityStatus::TIME_LIMIT;
    result.joint_optimality_status = OptimalityStatus::TIME_LIMIT;
    return result;
  }

  const auto minmax_deadline = std::min(
      budget.deadline(),
      overall_start +
          std::chrono::duration_cast<Clock::duration>(
              std::chrono::duration<double>(
                  initial_remaining * config.minmax_budget_fraction)));
  const auto joint_deadline = std::min(
      budget.deadline(),
      overall_start +
          std::chrono::duration_cast<Clock::duration>(
              std::chrono::duration<double>(
                  initial_remaining *
                  (config.minmax_budget_fraction +
                   config.minsum_budget_fraction))));
  SolverBudget minmax_budget(minmax_deadline);
  SolverBudget minsum_budget(joint_deadline);

  std::optional<MinMaxSolver::Result> minmax;
  std::optional<MinSumSolver::Result> minsum;

  auto phase_start = Clock::now();
  try {
    static_solver.set_seed(config.seed);
    auto component_config = config.minmax_config;
    component_config.verify_after = true;
    minmax = MinMaxSolver::solve(instance, static_solver, component_config,
                                 minmax_budget);
    result.minmax_component_status =
        minmax->time_limited ? MinMaxSumComponentStatus::TIME_LIMIT
                             : MinMaxSumComponentStatus::COMPLETED;
    result.minmax_component_optimality = minmax->optimality_status;
    result.minmax_component_certified_gap = minmax->certified_gap;
  } catch (const SolverBudgetExpired& error) {
    result.minmax_component_status = MinMaxSumComponentStatus::TIME_LIMIT;
    result.minmax_component_optimality = OptimalityStatus::TIME_LIMIT;
    result.minmax_error = error.what();
  } catch (const std::exception& error) {
    result.minmax_component_status = MinMaxSumComponentStatus::FAILED;
    result.minmax_component_optimality = OptimalityStatus::FAILED;
    result.minmax_error = error.what();
  }
  result.minmax_time_sec =
      std::chrono::duration<double>(Clock::now() - phase_start).count();

  phase_start = Clock::now();
  try {
    static_solver.set_seed(config.seed + 1U);
    auto component_config = config.minsum_config;
    component_config.verify_after = true;
    minsum = MinSumSolver::solve(instance, static_solver, component_config,
                                 minsum_budget);
    result.minsum_component_status =
        minsum->time_limited ? MinMaxSumComponentStatus::TIME_LIMIT
                             : MinMaxSumComponentStatus::COMPLETED;
    result.minsum_component_optimality = minsum->optimality_status;
    result.minsum_component_certified_gap = minsum->certified_gap;
  } catch (const SolverBudgetExpired& error) {
    result.minsum_component_status = MinMaxSumComponentStatus::TIME_LIMIT;
    result.minsum_component_optimality = OptimalityStatus::TIME_LIMIT;
    result.minsum_error = error.what();
  } catch (const std::exception& error) {
    result.minsum_component_status = MinMaxSumComponentStatus::FAILED;
    result.minsum_component_optimality = OptimalityStatus::FAILED;
    result.minsum_error = error.what();
  }
  result.minsum_time_sec =
      std::chrono::duration<double>(Clock::now() - phase_start).count();

  const bool has_minmax =
      minmax.has_value() && usable_component(*minmax, instance.T_end, config);
  const bool has_minsum =
      minsum.has_value() && usable_component(*minsum, instance.T_end, config);
  if (has_minmax) {
    result.minmax_component_peak = minmax->solution.peak_cost();
    result.minmax_component_integral = minmax->solution.total_integral();
    result.minmax_component_optimality = certified_or_reported_status(
        minmax->optimality_status, *result.minmax_component_peak);
  } else if (minmax.has_value()) {
    result.minmax_component_status = minmax->time_limited
                                         ? MinMaxSumComponentStatus::TIME_LIMIT
                                         : MinMaxSumComponentStatus::FAILED;
    result.minmax_component_optimality =
        minmax->time_limited ? OptimalityStatus::TIME_LIMIT
                             : OptimalityStatus::FAILED;
    if (result.minmax_error.empty()) {
      result.minmax_error =
          "MinMax component did not return a verified full-horizon solution";
    }
  }
  if (has_minsum) {
    result.minsum_component_peak = minsum->solution.peak_cost();
    result.minsum_component_integral = minsum->solution.total_integral();
    result.minsum_component_optimality = certified_or_reported_status(
        minsum->optimality_status, *result.minsum_component_integral);
  } else if (minsum.has_value()) {
    result.minsum_component_status = minsum->time_limited
                                         ? MinMaxSumComponentStatus::TIME_LIMIT
                                         : MinMaxSumComponentStatus::FAILED;
    result.minsum_component_optimality =
        minsum->time_limited ? OptimalityStatus::TIME_LIMIT
                             : OptimalityStatus::FAILED;
    if (result.minsum_error.empty()) {
      result.minsum_error =
          "MinSum component did not return a verified full-horizon solution";
    }
  }

  if (!has_minmax && !has_minsum) {
    result.time_limited =
        result.minmax_component_status == MinMaxSumComponentStatus::TIME_LIMIT ||
        result.minsum_component_status == MinMaxSumComponentStatus::TIME_LIMIT ||
        budget.expired();
    result.joint_optimality_status =
        result.time_limited ? OptimalityStatus::TIME_LIMIT
                            : OptimalityStatus::FAILED;
    result.total_time_sec =
        std::chrono::duration<double>(Clock::now() - overall_start).count();
    return result;
  }

  try {
    if (has_minmax && has_minsum) {
      result.solution = KineticSolution::combine(
          minmax->solution, minsum->solution, ObjectiveType::MIN_MAX_SUM,
          &budget);
    } else if (has_minmax) {
      result.solution = minmax->solution;
      result.solution.objective = ObjectiveType::MIN_MAX_SUM;
    } else {
      result.solution = minsum->solution;
      result.solution.objective = ObjectiveType::MIN_MAX_SUM;
    }
  } catch (const SolverBudgetExpired&) {
    result.time_limited = true;
    result.joint_optimality_status = OptimalityStatus::TIME_LIMIT;
    result.total_time_sec =
        std::chrono::duration<double>(Clock::now() - overall_start).count();
    return result;
  }

  if (!covers_horizon(result.solution, instance.T_end, config)) {
    throw std::runtime_error(
        "MinMaxSum combination does not cover the full time horizon");
  }
  const auto verification_start = Clock::now();
  VerificationReport verification;
  try {
    verification =
        Verifier::verify_continuous(instance, result.solution, 1e-6, &budget);
  } catch (const SolverBudgetExpired&) {
    result.time_limited = true;
    result.joint_optimality_status = OptimalityStatus::TIME_LIMIT;
    result.verification_time_sec =
        std::chrono::duration<double>(Clock::now() - verification_start).count();
    result.total_time_sec =
        std::chrono::duration<double>(Clock::now() - overall_start).count();
    return result;
  }
  result.verification_time_sec =
      std::chrono::duration<double>(Clock::now() - verification_start).count();
  result.peak_cost = result.solution.peak_cost();
  result.integral_cost = result.solution.total_integral();
  result.peak_consistent = Verifier::check_peak_consistency(
      result.solution, result.peak_cost, result.solution.peak_time());
  result.integral_consistent = verification.integral_consistent_ok;
  result.coverage_ok = verification.coverage_ok;
  result.supporting_points_ok = verification.supporting_points_ok;
  result.assignment_ok = verification.assignment_consistent_ok;
  result.cost_consistent = verification.cost_consistent_ok;
  if (!verification.all_ok() || !result.peak_consistent ||
      !result.integral_consistent || !std::isfinite(result.peak_cost) ||
      !std::isfinite(result.integral_cost)) {
    throw std::runtime_error(
        "MinMaxSum envelope failed continuous or objective consistency "
        "verification");
  }
  result.verified = true;
  result.feasible = true;

  const auto dominates = [&](double peak, double integral) {
    return within_tolerance(result.peak_cost, peak, config) &&
           within_tolerance(result.integral_cost, integral, config);
  };
  if (has_minmax) {
    result.dominates_minmax =
        dominates(*result.minmax_component_peak,
                  *result.minmax_component_integral);
    if (!result.dominates_minmax) {
      throw std::runtime_error(
          "MinMaxSum envelope does not dominate its MinMax component");
    }
  }
  if (has_minsum) {
    result.dominates_minsum =
        dominates(*result.minsum_component_peak,
                  *result.minsum_component_integral);
    if (!result.dominates_minsum) {
      throw std::runtime_error(
          "MinMaxSum envelope does not dominate its MinSum component");
    }
  }
  result.dominance_invariants_ok =
      (!has_minmax || result.dominates_minmax) &&
      (!has_minsum || result.dominates_minsum);

  const bool component_time_limited =
      result.minmax_component_status == MinMaxSumComponentStatus::TIME_LIMIT ||
      result.minsum_component_status == MinMaxSumComponentStatus::TIME_LIMIT;
  result.time_limited = component_time_limited || budget.expired();
  const bool both_components_optimal =
      has_minmax && has_minsum &&
      result.minmax_component_optimality == OptimalityStatus::OPTIMAL &&
      result.minsum_component_optimality == OptimalityStatus::OPTIMAL;
  result.joint_optimality_status =
      result.time_limited
          ? OptimalityStatus::TIME_LIMIT
          : (both_components_optimal ? OptimalityStatus::OPTIMAL
                                     : OptimalityStatus::FEASIBLE);
  result.total_time_sec =
      std::chrono::duration<double>(Clock::now() - overall_start).count();
  return result;
}

}  // namespace kdc
