#include "kdc/algorithms/greedy_set_cover_solver.hpp"

#include "kdc/logging.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

namespace kdc {
namespace {
using Clock = std::chrono::steady_clock;
}

GreedySetCoverSolver::GreedySetCoverSolver() : GreedySetCoverSolver(Config{}) {}

GreedySetCoverSolver::GreedySetCoverSolver(Config config) : config_(config) {}

StaticSolution GreedySetCoverSolver::solve(const Instance& instance,
                                           double time) {
  if (!std::isfinite(time) || !std::isfinite(instance.T_end) ||
      instance.T_end <= 0.0 || time < 0.0 || time > instance.T_end) {
    throw std::out_of_range("greedy solve time is outside [0, T_end]");
  }
  if (instance.n < 0 || instance.m < 0 ||
      static_cast<Index>(instance.n) != instance.trajectories.size() ||
      static_cast<Index>(instance.m) != instance.stations.size()) {
    throw std::invalid_argument(
        "greedy solver received inconsistent instance dimensions");
  }
  const auto start = Clock::now();
  StaticSolution solution;
  solution.solver_name = name();
  solution.supporting_point.assign(static_cast<Index>(instance.m), -1);
  solution.radius.assign(static_cast<Index>(instance.m), 0.0);
  if (instance.n == 0) {
    solution.feasible = true;
    solution.lower_bound = 0.0;
    solution.upper_bound = 0.0;
    solution.solve_time_sec =
        std::chrono::duration<double>(Clock::now() - start).count();
    return solution;
  }
  if (instance.m == 0) {
    solution.lower_bound = 0.0;
    solution.upper_bound = std::numeric_limits<double>::infinity();
    solution.solve_time_sec =
        std::chrono::duration<double>(Clock::now() - start).count();
    return solution;
  }

  const Index point_count = static_cast<Index>(instance.n);
  const Index station_count = static_cast<Index>(instance.m);
  if (point_count > std::numeric_limits<Index>::max() / station_count ||
      point_count * station_count >
          static_cast<Index>(std::numeric_limits<int>::max())) {
    throw std::length_error("greedy candidate count exceeds supported range");
  }
  const double pi = std::acos(-1.0);
  const Index disk_count = point_count * station_count;
  std::vector<Point> positions;
  positions.reserve(point_count);
  for (const auto& trajectory : instance.trajectories) {
    positions.push_back(trajectory.position(time));
  }
  std::vector<double> distances_squared(point_count * station_count, 0.0);
  std::vector<double> nearest_distance_squared(
      point_count, std::numeric_limits<double>::infinity());
  std::vector<std::vector<int>> points_by_distance(station_count);
  for (Index station = 0; station < station_count; ++station) {
    auto& ordering = points_by_distance[station];
    ordering.resize(point_count);
    for (Index point = 0; point < point_count; ++point) {
      const double distance_squared =
          (instance.stations[station].pos - positions[point]).norm2();
      distances_squared[point * station_count + station] = distance_squared;
      nearest_distance_squared[point] =
          std::min(nearest_distance_squared[point], distance_squared);
      ordering[point] = static_cast<int>(point);
    }
    std::sort(ordering.begin(), ordering.end(),
              [&distances_squared, station, station_count](int lhs, int rhs) {
                const double lhs_distance =
                    distances_squared[static_cast<Index>(lhs) * station_count +
                                     station];
                const double rhs_distance =
                    distances_squared[static_cast<Index>(rhs) * station_count +
                                     station];
                return lhs_distance == rhs_distance ? lhs < rhs
                                                    : lhs_distance < rhs_distance;
              });
  }

  std::vector<Index> covered_prefix_end(disk_count, 0U);
  for (Index station = 0; station < station_count; ++station) {
    const auto& ordering = points_by_distance[station];
    for (Index support = 0; support < point_count; ++support) {
      const double support_distance_squared =
          distances_squared[support * station_count + station];
      const double radius = std::sqrt(support_distance_squared) + 1e-9;
      const double threshold_squared = radius * radius;
      const auto end = std::upper_bound(
          ordering.begin(), ordering.end(), threshold_squared,
          [&distances_squared, station, station_count](double threshold,
                                                       int point) {
            return threshold <
                   distances_squared[static_cast<Index>(point) *
                                         station_count +
                                     station];
          });
      covered_prefix_end[station * point_count + support] =
          static_cast<Index>(end - ordering.begin());
    }
  }

  std::vector<bool> uncovered(point_count, true);
  int num_uncovered = instance.n;
  std::vector<std::vector<int>> uncovered_prefix(
      station_count, std::vector<int>(point_count + 1U, 0));
  std::vector<Index> selected;
  selected.reserve(point_count);
  while (num_uncovered > 0) {
    for (Index station = 0; station < station_count; ++station) {
      const auto& ordering = points_by_distance[station];
      auto& prefix = uncovered_prefix[station];
      prefix[0] = 0;
      for (Index rank = 0; rank < point_count; ++rank) {
        prefix[rank + 1U] =
            prefix[rank] +
            (uncovered[static_cast<Index>(ordering[rank])] ? 1 : 0);
      }
    }
    Index best_disk = disk_count;
    double best_key = -std::numeric_limits<double>::infinity();
    for (Index station = 0; station < station_count; ++station) {
      for (Index support = 0; support < point_count; ++support) {
        const Index disk_id = station * point_count + support;
        const int new_count =
            uncovered_prefix[station][covered_prefix_end[disk_id]];
        if (new_count == 0) {
          continue;
        }
        double key = 0.0;
        const double cost =
            pi * distances_squared[support * station_count + station];
        switch (config_.tie_break) {
          case TieBreak::DENSITY:
            key = static_cast<double>(new_count) / (cost + 1e-12);
            break;
          case TieBreak::MAX_NEW_POINTS:
            key = static_cast<double>(new_count);
            break;
          case TieBreak::MIN_COST:
            key = -cost;
            break;
        }
        if (key > best_key) {
          best_key = key;
          best_disk = disk_id;
        }
      }
    }
    if (best_disk == disk_count) {
      LOG_ERROR("Greedy: no candidate disk covers the remaining points");
      break;
    }
    selected.push_back(best_disk);
    const Index station = best_disk / point_count;
    const auto& ordering = points_by_distance[station];
    const Index end = covered_prefix_end[best_disk];
    for (Index rank = 0; rank < end; ++rank) {
      const Index point = static_cast<Index>(ordering[rank]);
      if (uncovered[point]) {
        uncovered[point] = false;
        --num_uncovered;
      }
    }
  }

  std::vector<int> largest_support(station_count, -1);
  for (const Index selected_id : selected) {
    const Index station = selected_id / point_count;
    const Index support = selected_id % point_count;
    const int previous = largest_support[station];
    if (previous < 0 ||
        distances_squared[support * station_count + station] >
            distances_squared[static_cast<Index>(previous) * station_count +
                              station]) {
      largest_support[station] = static_cast<int>(support);
    }
  }
  for (int station = 0; station < instance.m; ++station) {
    const int support = largest_support[static_cast<Index>(station)];
    if (support < 0) {
      continue;
    }
    solution.supporting_point[static_cast<Index>(station)] =
        support;
    const double radius_squared =
        distances_squared[static_cast<Index>(support) * station_count +
                          static_cast<Index>(station)];
    solution.radius[static_cast<Index>(station)] =
        std::sqrt(radius_squared);
    solution.cost += pi * radius_squared;
  }

  double lower_bound = 0.0;
  for (const double distance_squared : nearest_distance_squared) {
    lower_bound = std::max(lower_bound, pi * distance_squared);
  }
  solution.feasible = num_uncovered == 0;
  solution.lower_bound = lower_bound;
  solution.upper_bound = solution.feasible
                             ? solution.cost
                             : std::numeric_limits<double>::infinity();
  solution.solve_time_sec =
      std::chrono::duration<double>(Clock::now() - start).count();
  LOG_INFO("Greedy: cost={:.9f} LB={:.9f} ratio={:.4f}", solution.cost,
           lower_bound,
           solution.cost / std::max(lower_bound, 1e-12));
  return solution;
}
}
