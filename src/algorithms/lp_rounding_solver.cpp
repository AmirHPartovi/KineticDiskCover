#include "kdc/algorithms/lp_rounding_solver.hpp"

#include "kdc/candidate.hpp"
#include "kdc/logging.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <random>
#include <stdexcept>
#include <vector>

namespace kdc {
namespace {
using Clock = std::chrono::steady_clock;

StaticSolution empty_solution(int station_count, const std::string& solver_name,
                              bool feasible) {
  StaticSolution solution;
  solution.supporting_point.assign(static_cast<Index>(station_count), -1);
  solution.radius.assign(static_cast<Index>(station_count), 0.0);
  solution.feasible = feasible;
  solution.lower_bound = 0.0;
  solution.upper_bound =
      feasible ? 0.0 : std::numeric_limits<double>::infinity();
  solution.solver_name = solver_name;
  return solution;
}
}

LPRoundingSolver::LPRoundingSolver(ILPSolver* ilp)
    : LPRoundingSolver(ilp, Config{}) {}

LPRoundingSolver::LPRoundingSolver(ILPSolver* ilp, Config config)
    : ilp_(ilp), config_(config) {
  if (config_.num_trials <= 0 ||
      !std::isfinite(config_.lp_time_limit_sec) ||
      config_.lp_time_limit_sec <= 0.0) {
    throw std::invalid_argument("LP-rounding solver configuration is invalid");
  }
}

void LPRoundingSolver::set_time_limit(double time_limit_sec) {
  if (!std::isfinite(time_limit_sec) || time_limit_sec <= 0.0) {
    throw std::invalid_argument("LP-rounding time limit must be positive");
  }
  config_.lp_time_limit_sec = time_limit_sec;
}

StaticSolution LPRoundingSolver::solve(const Instance& instance, double time) {
  if (ilp_ == nullptr) {
    throw std::invalid_argument("LP-rounding solver requires an ILP solver");
  }
  if (!std::isfinite(time) || !std::isfinite(instance.T_end) ||
      instance.T_end <= 0.0 || time < 0.0 || time > instance.T_end) {
    throw std::out_of_range("LP-rounding solve time is outside [0, T_end]");
  }
  if (instance.n < 0 || instance.m < 0 ||
      static_cast<Index>(instance.n) != instance.trajectories.size() ||
      static_cast<Index>(instance.m) != instance.stations.size()) {
    throw std::invalid_argument(
        "LP-rounding solver received inconsistent instance dimensions");
  }
  LOG_DEBUG("LPRounding::solve t={:.6f} trials={}", time, config_.num_trials);
  const auto started = Clock::now();
  if (instance.n == 0) {
    StaticSolution result = empty_solution(instance.m, name(), true);
    result.solve_time_sec =
        std::chrono::duration<double>(Clock::now() - started).count();
    return result;
  }
  if (instance.m == 0) {
    StaticSolution result = empty_solution(instance.m, name(), false);
    result.solve_time_sec =
        std::chrono::duration<double>(Clock::now() - started).count();
    return result;
  }

  const std::vector<CandidateDisk> disks = CandidateSet::build(instance);
  const CandidateSet::CoverageMatrix coverage =
      CandidateSet::build_coverage(instance, disks, time);
  const Index disk_count = disks.size();
  const Index point_count = static_cast<Index>(instance.n);
  const double pi = std::acos(-1.0);
  Eigen::VectorXd costs(static_cast<Eigen::Index>(disk_count));
  std::vector<double> radii(disk_count, 0.0);
  std::vector<std::vector<int>> points_of_disk(disk_count);
  for (Index disk_id = 0; disk_id < disk_count; ++disk_id) {
    const CandidateDisk& disk = disks[disk_id];
    const Point position =
        instance.trajectories[static_cast<Index>(disk.supporting_point)]
            .position(time);
    const double radius =
        (instance.stations[static_cast<Index>(disk.station_id)].pos - position)
            .norm();
    radii[disk_id] = radius;
    costs[static_cast<Eigen::Index>(disk_id)] = pi * radius * radius;
  }
  for (Index point = 0; point < point_count; ++point) {
    const int begin = coverage.row_ptr[point];
    const int end = coverage.row_ptr[point + 1U];
    for (int offset = begin; offset < end; ++offset) {
      const int disk_id = coverage.col_idx[static_cast<Index>(offset)];
      points_of_disk[static_cast<Index>(disk_id)].push_back(
          static_cast<int>(point));
    }
  }

  Eigen::SparseMatrix<double> matrix(instance.n,
                                     static_cast<int>(disk_count));
  std::vector<Eigen::Triplet<double>> entries;
  entries.reserve(coverage.col_idx.size());
  for (int point = 0; point < instance.n; ++point) {
    const int begin = coverage.row_ptr[static_cast<Index>(point)];
    const int end = coverage.row_ptr[static_cast<Index>(point) + 1U];
    for (int offset = begin; offset < end; ++offset) {
      entries.emplace_back(
          point, coverage.col_idx[static_cast<Index>(offset)], 1.0);
    }
  }
  matrix.setFromTriplets(entries.begin(), entries.end());
  const Eigen::VectorXd rhs = Eigen::VectorXd::Ones(instance.n);
  const ILPResult lp =
      ilp_->solve(costs, matrix, rhs, {}, config_.lp_time_limit_sec, 0.0);
  if (lp.status != ILPResult::Status::OPTIMAL &&
      lp.status != ILPResult::Status::FEASIBLE) {
    LOG_WARN("LPRounding: LP solve returned unusable status {}",
             static_cast<int>(lp.status));
    StaticSolution result = empty_solution(instance.m, name(), false);
    result.solve_time_sec =
        std::chrono::duration<double>(Clock::now() - started).count();
    return result;
  }
  if (lp.x.size() != disk_count ||
      !std::isfinite(lp.lower_bound) || lp.lower_bound < 0.0) {
    throw std::runtime_error(
        "LP-rounding received an invalid LP solution or lower bound");
  }
  std::vector<double> probabilities(disk_count, 0.0);
  for (Index disk_id = 0; disk_id < disk_count; ++disk_id) {
    const double value = lp.x[disk_id];
    if (!std::isfinite(value) || value < -1e-8 || value > 1.0 + 1e-8) {
      throw std::runtime_error(
          "LP-rounding received an invalid LP variable value");
    }
    probabilities[disk_id] = std::clamp(value, 0.0, 1.0);
  }
  const double lp_lower_bound = lp.lower_bound;

  std::mt19937 random(config_.seed);
  std::uniform_real_distribution<double> uniform(0.0, 1.0);
  double best_cost = std::numeric_limits<double>::infinity();
  std::vector<int> best_support(static_cast<Index>(instance.m), -1);
  std::vector<double> best_radius(static_cast<Index>(instance.m), 0.0);

  for (int trial = 0; trial < config_.num_trials; ++trial) {
    std::vector<bool> uncovered(point_count, true);
    int num_uncovered = instance.n;
    std::vector<bool> selected(disk_count, false);
    for (Index disk_id = 0; disk_id < disk_count; ++disk_id) {
      if (uniform(random) < probabilities[disk_id]) {
        selected[disk_id] = true;
        for (const int point : points_of_disk[disk_id]) {
          const Index point_index = static_cast<Index>(point);
          if (uncovered[point_index]) {
            uncovered[point_index] = false;
            --num_uncovered;
          }
        }
      }
    }

    while (num_uncovered > 0) {
      Index best_disk = disk_count;
      double best_key = -std::numeric_limits<double>::infinity();
      for (Index disk_id = 0; disk_id < disk_count; ++disk_id) {
        if (selected[disk_id]) {
          continue;
        }
        int new_count = 0;
        for (const int point : points_of_disk[disk_id]) {
          if (uncovered[static_cast<Index>(point)]) {
            ++new_count;
          }
        }
        if (new_count == 0) {
          continue;
        }
        const double key =
            static_cast<double>(new_count) /
            (costs[static_cast<Eigen::Index>(disk_id)] + 1e-12);
        if (key > best_key) {
          best_key = key;
          best_disk = disk_id;
        }
      }
      if (best_disk == disk_count) {
        break;
      }
      selected[best_disk] = true;
      for (const int point : points_of_disk[best_disk]) {
        const Index point_index = static_cast<Index>(point);
        if (uncovered[point_index]) {
          uncovered[point_index] = false;
          --num_uncovered;
        }
      }
    }

    if (num_uncovered != 0) {
      continue;
    }
    std::vector<int> trial_support(static_cast<Index>(instance.m), -1);
    std::vector<double> trial_radius(static_cast<Index>(instance.m), 0.0);
    for (Index disk_id = 0; disk_id < disk_count; ++disk_id) {
      if (!selected[disk_id]) {
        continue;
      }
      const Index station =
          static_cast<Index>(disks[disk_id].station_id);
      if (trial_support[station] < 0 ||
          radii[disk_id] > trial_radius[station]) {
        trial_support[station] = disks[disk_id].supporting_point;
        trial_radius[station] = radii[disk_id];
      }
    }
    double trial_cost = 0.0;
    for (const double radius : trial_radius) {
      trial_cost += pi * radius * radius;
    }
    if (trial_cost < best_cost) {
      best_cost = trial_cost;
      best_support = std::move(trial_support);
      best_radius = std::move(trial_radius);
    }
  }

  if (!std::isfinite(best_cost)) {
    LOG_ERROR("LPRounding: randomized rounding and repair found no cover");
    StaticSolution result = empty_solution(instance.m, name(), false);
    result.lower_bound = lp_lower_bound;
    result.solve_time_sec =
        std::chrono::duration<double>(Clock::now() - started).count();
    return result;
  }
  StaticSolution result;
  result.supporting_point = std::move(best_support);
  result.radius = std::move(best_radius);
  result.cost = best_cost;
  result.feasible = true;
  result.lower_bound = std::min(lp_lower_bound, result.cost);
  result.upper_bound = result.cost;
  result.solve_time_sec =
      std::chrono::duration<double>(Clock::now() - started).count();
  result.solver_name = name();
  LOG_INFO("LPRounding: cost={:.9f} LB={:.9f} ratio={:.4f}", result.cost,
           result.lower_bound,
           result.cost / std::max(result.lower_bound, 1e-12));
  return result;
}
}
