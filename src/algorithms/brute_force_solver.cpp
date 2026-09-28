#include "kdc/algorithms/brute_force_solver.hpp"

#include "kdc/logging.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace kdc {
BruteForceSolver::BruteForceSolver() : BruteForceSolver(Config{}) {}

BruteForceSolver::BruteForceSolver(Config config) : config_(config) {
  if (!std::isfinite(config_.time_limit_sec) ||
      config_.time_limit_sec <= 0.0 || config_.max_assignments == 0U) {
    throw std::invalid_argument("brute-force solver configuration is invalid");
  }
}

void BruteForceSolver::set_time_limit(double time_limit_sec) {
  if (!std::isfinite(time_limit_sec) || time_limit_sec <= 0.0) {
    throw std::invalid_argument("brute-force time limit must be positive");
  }
  config_.time_limit_sec = time_limit_sec;
}

void BruteForceSolver::decode_assignment(std::uint64_t code,
                                         int station_count,
                                         int point_count,
                                         std::vector<int>& output) const {
  output.resize(static_cast<Index>(point_count));
  for (int point = 0; point < point_count; ++point) {
    output[static_cast<Index>(point)] =
        static_cast<int>(code % static_cast<std::uint64_t>(station_count));
    code /= static_cast<std::uint64_t>(station_count);
  }
}

double BruteForceSolver::evaluate_assignment(
    const std::vector<int>& assignment,
    const std::vector<std::vector<double>>& distances,
    int station_count) const {
  std::vector<double> radii(static_cast<Index>(station_count), 0.0);
  for (Index point = 0; point < assignment.size(); ++point) {
    const int station_id = assignment[point];
    radii[static_cast<Index>(station_id)] =
        std::max(radii[static_cast<Index>(station_id)],
                 distances[point][static_cast<Index>(station_id)]);
  }
  double squared_radius_sum = 0.0;
  for (const double radius : radii) {
    squared_radius_sum += radius * radius;
  }
  return std::acos(-1.0) * squared_radius_sum;
}

StaticSolution BruteForceSolver::solve(const Instance& instance,
                                       double time) {
  if (!std::isfinite(time) || !std::isfinite(instance.T_end) ||
      instance.T_end <= 0.0 || time < 0.0 || time > instance.T_end) {
    throw std::out_of_range("brute-force solve time is outside [0, T_end]");
  }
  if (instance.n < 0 || instance.m < 0 ||
      static_cast<Index>(instance.n) != instance.trajectories.size() ||
      static_cast<Index>(instance.m) != instance.stations.size()) {
    throw std::invalid_argument(
        "brute-force solver received inconsistent instance dimensions");
  }
  LOG_DEBUG("BruteForce::solve t={:.6f} n={} m={}", time, instance.n,
            instance.m);

  StaticSolution solution;
  solution.solver_name = name();
  solution.supporting_point.assign(static_cast<Index>(instance.m), -1);
  solution.radius.assign(static_cast<Index>(instance.m), 0.0);
  if (instance.n == 0) {
    solution.feasible = true;
    solution.lower_bound = 0.0;
    solution.upper_bound = 0.0;
    return solution;
  }
  if (instance.m == 0) {
    solution.feasible = false;
    solution.lower_bound = 0.0;
    solution.upper_bound = std::numeric_limits<double>::infinity();
    return solution;
  }

  std::uint64_t total_assignments = 1U;
  for (int point = 0; point < instance.n; ++point) {
    if (total_assignments >
        config_.max_assignments / static_cast<std::uint64_t>(instance.m)) {
      LOG_WARN("BruteForce: assignment count exceeds configured cap {}",
               config_.max_assignments);
      solution.feasible = false;
      solution.lower_bound = 0.0;
      solution.upper_bound = std::numeric_limits<double>::infinity();
      return solution;
    }
    total_assignments *= static_cast<std::uint64_t>(instance.m);
  }

  std::vector<Point> points;
  points.reserve(static_cast<Index>(instance.n));
  for (const auto& trajectory : instance.trajectories) {
    points.push_back(trajectory.position(time));
  }
  std::vector<std::vector<double>> distances(
      static_cast<Index>(instance.n),
      std::vector<double>(static_cast<Index>(instance.m), 0.0));
  for (int point = 0; point < instance.n; ++point) {
    for (int station_id = 0; station_id < instance.m; ++station_id) {
      distances[static_cast<Index>(point)][static_cast<Index>(station_id)] =
          (points[static_cast<Index>(point)] -
           instance.stations[static_cast<Index>(station_id)].pos)
              .norm();
    }
  }

  const auto start = std::chrono::steady_clock::now();
  std::vector<int> assignment;
  std::vector<int> best_assignment;
  double best_cost = std::numeric_limits<double>::infinity();
  std::uint64_t best_code = 0U;
  bool completed = true;
  for (std::uint64_t code = 0U; code < total_assignments; ++code) {
    const double elapsed =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - start)
            .count();
    if (elapsed > config_.time_limit_sec) {
      LOG_WARN("BruteForce: time limit {:.4f}s reached", elapsed);
      completed = false;
      break;
    }
    decode_assignment(code, instance.m, instance.n, assignment);
    const double cost =
        evaluate_assignment(assignment, distances, instance.m);
    if (cost < best_cost) {
      best_cost = cost;
      best_code = code;
      best_assignment = assignment;
    }
  }

  solution.solve_time_sec =
      std::chrono::duration<double>(std::chrono::steady_clock::now() - start)
          .count();
  if (!std::isfinite(best_cost)) {
    solution.feasible = false;
    solution.lower_bound = 0.0;
    solution.upper_bound = std::numeric_limits<double>::infinity();
    return solution;
  }
  if (best_assignment.empty()) {
    decode_assignment(best_code, instance.m, instance.n, best_assignment);
  }
  std::vector<int> best_support(static_cast<Index>(instance.m), -1);
  for (int point = 0; point < instance.n; ++point) {
    const int station_id = best_assignment[static_cast<Index>(point)];
    const Index station_index = static_cast<Index>(station_id);
    const Index point_index = static_cast<Index>(point);
    if (best_support[station_index] == -1 ||
        distances[point_index][station_index] >
            distances[static_cast<Index>(best_support[station_index])]
                     [station_index]) {
      best_support[station_index] = point;
    }
  }

  solution.supporting_point = std::move(best_support);
  const double pi = std::acos(-1.0);
  for (int station_id = 0; station_id < instance.m; ++station_id) {
    const int support =
        solution.supporting_point[static_cast<Index>(station_id)];
    if (support >= 0) {
      solution.radius[static_cast<Index>(station_id)] =
          distances[static_cast<Index>(support)]
                   [static_cast<Index>(station_id)];
    }
    solution.cost += pi * solution.radius[static_cast<Index>(station_id)] *
                     solution.radius[static_cast<Index>(station_id)];
  }
  solution.feasible = true;
  solution.upper_bound = solution.cost;
  solution.lower_bound = completed ? solution.cost : 0.0;
  LOG_INFO("BruteForce: cost={:.9f} total={} t={:.4f}s", solution.cost,
           total_assignments, solution.solve_time_sec);
  return solution;
}
}
