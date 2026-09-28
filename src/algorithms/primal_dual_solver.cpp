#include "kdc/algorithms/primal_dual_solver.hpp"

#include "kdc/candidate.hpp"
#include "kdc/logging.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <vector>

namespace kdc {
namespace {
using Clock = std::chrono::steady_clock;
}

PrimalDualSolver::PrimalDualSolver() : PrimalDualSolver(Config{}) {}

PrimalDualSolver::PrimalDualSolver(Config config) : config_(config) {
  if (!std::isfinite(config_.epsilon) || config_.epsilon <= 0.0) {
    throw std::invalid_argument("primal-dual solver epsilon must be positive");
  }
}

StaticSolution PrimalDualSolver::solve(const Instance& instance, double time) {
  if (!std::isfinite(time) || !std::isfinite(instance.T_end) ||
      instance.T_end <= 0.0 || time < 0.0 || time > instance.T_end) {
    throw std::out_of_range("primal-dual solve time is outside [0, T_end]");
  }
  if (instance.n < 0 || instance.m < 0 ||
      static_cast<Index>(instance.n) != instance.trajectories.size() ||
      static_cast<Index>(instance.m) != instance.stations.size()) {
    throw std::invalid_argument(
        "primal-dual solver received inconsistent instance dimensions");
  }
  LOG_DEBUG("PrimalDual::solve t={:.6f}", time);
  const auto started = Clock::now();
  StaticSolution solution;
  solution.solver_name = name();
  solution.supporting_point.assign(static_cast<Index>(instance.m), -1);
  solution.radius.assign(static_cast<Index>(instance.m), 0.0);
  if (instance.n == 0) {
    solution.feasible = true;
    solution.lower_bound = 0.0;
    solution.upper_bound = 0.0;
    solution.solve_time_sec =
        std::chrono::duration<double>(Clock::now() - started).count();
    return solution;
  }
  if (instance.m == 0) {
    solution.lower_bound = 0.0;
    solution.upper_bound = std::numeric_limits<double>::infinity();
    solution.solve_time_sec =
        std::chrono::duration<double>(Clock::now() - started).count();
    return solution;
  }

  const std::vector<CandidateDisk> disks = CandidateSet::build(instance);
  const CandidateSet::CoverageMatrix coverage =
      CandidateSet::build_coverage(instance, disks, time);
  const Index disk_count = disks.size();
  const Index point_count = static_cast<Index>(instance.n);
  const double pi = std::acos(-1.0);
  std::vector<std::vector<int>> points_of_disk(disk_count);
  std::vector<std::vector<int>> disks_of_point(point_count);
  for (Index point = 0; point < point_count; ++point) {
    const int begin = coverage.row_ptr[point];
    const int end = coverage.row_ptr[point + 1U];
    for (int offset = begin; offset < end; ++offset) {
      const int disk = coverage.col_idx[static_cast<Index>(offset)];
      points_of_disk[static_cast<Index>(disk)].push_back(
          static_cast<int>(point));
      disks_of_point[point].push_back(disk);
    }
  }
  std::vector<double> costs(disk_count, 0.0);
  for (Index disk_id = 0; disk_id < disk_count; ++disk_id) {
    const CandidateDisk& disk = disks[disk_id];
    const Point position =
        instance.trajectories[static_cast<Index>(disk.supporting_point)]
            .position(time);
    const double radius =
        (instance.stations[static_cast<Index>(disk.station_id)].pos - position)
            .norm();
    costs[disk_id] = pi * radius * radius;
  }

  std::vector<double> dual(point_count, 0.0);
  std::vector<bool> selected(disk_count, false);
  std::vector<int> selected_disks;
  selected_disks.reserve(disk_count);
  std::vector<bool> point_covered(point_count, false);
  int uncovered_count = instance.n;
  while (uncovered_count > 0) {
    Index point = point_count;
    for (Index candidate = 0; candidate < point_count; ++candidate) {
      if (!point_covered[candidate]) {
        point = candidate;
        break;
      }
    }
    if (point == point_count) {
      break;
    }

    double delta = std::numeric_limits<double>::infinity();
    for (const int disk_value : disks_of_point[point]) {
      const Index disk_id = static_cast<Index>(disk_value);
      if (selected[disk_id]) {
        continue;
      }
      double dual_sum = 0.0;
      for (const int covered_point : points_of_disk[disk_id]) {
        dual_sum += dual[static_cast<Index>(covered_point)];
      }
      delta = std::min(delta, std::max(0.0, costs[disk_id] - dual_sum));
    }
    if (!std::isfinite(delta)) {
      break;
    }
    dual[point] += delta;
    for (const int disk_value : disks_of_point[point]) {
      const Index disk_id = static_cast<Index>(disk_value);
      if (selected[disk_id]) {
        continue;
      }
      double dual_sum = 0.0;
      for (const int covered_point : points_of_disk[disk_id]) {
        dual_sum += dual[static_cast<Index>(covered_point)];
      }
      if (dual_sum >= costs[disk_id] - config_.epsilon) {
        selected[disk_id] = true;
        selected_disks.push_back(static_cast<int>(disk_id));
        for (const int covered_point : points_of_disk[disk_id]) {
          const Index covered_index = static_cast<Index>(covered_point);
          if (!point_covered[covered_index]) {
            point_covered[covered_index] = true;
            --uncovered_count;
          }
        }
      }
    }
  }

  if (uncovered_count == 0 && config_.use_reverse_delete) {
    std::vector<int> coverage_count(point_count, 0);
    for (const int disk_value : selected_disks) {
      for (const int point_value : points_of_disk[static_cast<Index>(disk_value)]) {
        ++coverage_count[static_cast<Index>(point_value)];
      }
    }
    std::vector<bool> removed(disk_count, false);
    for (auto disk_iter = selected_disks.rbegin();
         disk_iter != selected_disks.rend(); ++disk_iter) {
      const Index disk_id = static_cast<Index>(*disk_iter);
      bool can_remove = true;
      for (const int point_value : points_of_disk[disk_id]) {
        if (coverage_count[static_cast<Index>(point_value)] <= 1) {
          can_remove = false;
          break;
        }
      }
      if (can_remove) {
        removed[disk_id] = true;
        for (const int point_value : points_of_disk[disk_id]) {
          --coverage_count[static_cast<Index>(point_value)];
        }
      }
    }
    selected_disks.erase(
        std::remove_if(selected_disks.begin(), selected_disks.end(),
                       [&removed](int disk_id) {
                         return removed[static_cast<Index>(disk_id)];
                       }),
        selected_disks.end());
  }

  std::vector<int> largest_disk(static_cast<Index>(instance.m), -1);
  for (const int disk_value : selected_disks) {
    const Index disk_id = static_cast<Index>(disk_value);
    const Index station =
        static_cast<Index>(disks[disk_id].station_id);
    const int previous = largest_disk[station];
    if (previous < 0 || costs[disk_id] >
                            costs[static_cast<Index>(previous)]) {
      largest_disk[station] = disk_value;
    }
  }
  for (int station = 0; station < instance.m; ++station) {
    const int disk_value = largest_disk[static_cast<Index>(station)];
    if (disk_value < 0) {
      continue;
    }
    const Index disk_id = static_cast<Index>(disk_value);
    const CandidateDisk& disk = disks[disk_id];
    solution.supporting_point[static_cast<Index>(station)] =
        disk.supporting_point;
    solution.radius[static_cast<Index>(station)] =
        std::sqrt(costs[disk_id] / pi);
    solution.cost += costs[disk_id];
  }

  double dual_sum = 0.0;
  for (const double value : dual) {
    dual_sum += value;
  }
  const double lower_bound = dual_sum / static_cast<double>(instance.m);
  solution.feasible = uncovered_count == 0;
  solution.lower_bound = lower_bound;
  solution.upper_bound = solution.feasible
                             ? solution.cost
                             : std::numeric_limits<double>::infinity();
  solution.solve_time_sec =
      std::chrono::duration<double>(Clock::now() - started).count();
  LOG_INFO("PrimalDual: cost={:.9f} LB={:.9f} ratio={:.4f} |D|={}",
           solution.cost, solution.lower_bound,
           solution.cost / std::max(solution.lower_bound, 1e-12),
           selected_disks.size());
  return solution;
}
}
