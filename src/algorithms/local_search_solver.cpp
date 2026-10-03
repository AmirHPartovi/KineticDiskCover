#include "kdc/algorithms/local_search_solver.hpp"

#include "kdc/logging.hpp"
#include "kdc/stationary.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace kdc {
namespace {
using Clock = std::chrono::steady_clock;
constexpr double kImprovementTolerance = 1e-9;
}

LocalSearchSolver::LocalSearchSolver() : LocalSearchSolver(Config{}) {}

LocalSearchSolver::LocalSearchSolver(Config config) : config_(config) {
  if (config_.max_iterations < 0 || config_.max_restarts < 0) {
    throw std::invalid_argument(
        "local-search iteration and restart limits must be nonnegative");
  }
}

std::vector<LocalSearchSolver::Move> LocalSearchSolver::enumerate_moves(
    const State& state,
    const std::vector<std::vector<double>>& distances, int point_count,
    int station_count) const {
  const Index m = static_cast<Index>(station_count);
  std::vector<double> largest(m, 0.0);
  std::vector<double> second_largest(m, 0.0);
  std::vector<int> largest_point(m, -1);
  std::vector<int> assigned_count(m, 0);
  for (int point = 0; point < point_count; ++point) {
    const int station = state.assignment[static_cast<Index>(point)];
    const double distance =
        distances[static_cast<Index>(point)][static_cast<Index>(station)];
    const Index station_index = static_cast<Index>(station);
    ++assigned_count[station_index];
    if (distance > largest[station_index]) {
      second_largest[station_index] = largest[station_index];
      largest[station_index] = distance;
      largest_point[station_index] = point;
    } else if (distance > second_largest[station_index]) {
      second_largest[station_index] = distance;
    }
  }

  const double pi = std::acos(-1.0);
  std::vector<Move> moves;
  moves.reserve(static_cast<Index>(point_count) *
                static_cast<Index>(std::max(station_count - 1, 0)));
  for (int point = 0; point < point_count; ++point) {
    const int from = state.assignment[static_cast<Index>(point)];
    const Index from_index = static_cast<Index>(from);
    const double new_from_radius =
        assigned_count[from_index] <= 1
            ? 0.0
            : (largest_point[from_index] == point
                   ? second_largest[from_index]
                   : largest[from_index]);
    for (int to = 0; to < station_count; ++to) {
      if (to == from) {
        continue;
      }
      const double new_to_radius =
          std::max(state.radius[static_cast<Index>(to)],
                   distances[static_cast<Index>(point)]
                            [static_cast<Index>(to)]);
      const double old_cost =
          state.radius[from_index] * state.radius[from_index] +
          state.radius[static_cast<Index>(to)] *
              state.radius[static_cast<Index>(to)];
      const double new_cost =
          new_from_radius * new_from_radius + new_to_radius * new_to_radius;
      moves.push_back(
          {point, from, to, pi * (new_cost - old_cost)});
    }
  }
  return moves;
}

void LocalSearchSolver::apply_move(
    State& state, const Move& move,
    const std::vector<std::vector<double>>& distances,
    int station_count) const {
  state.assignment[static_cast<Index>(move.point_id)] = move.to_station;
  recompute_radius(state, distances,
                   static_cast<int>(state.assignment.size()), station_count);
}

void LocalSearchSolver::recompute_radius(
    State& state, const std::vector<std::vector<double>>& distances,
    int point_count, int station_count) const {
  state.radius.assign(static_cast<Index>(station_count), 0.0);
  for (int point = 0; point < point_count; ++point) {
    const int station = state.assignment[static_cast<Index>(point)];
    state.radius[static_cast<Index>(station)] =
        std::max(state.radius[static_cast<Index>(station)],
                 distances[static_cast<Index>(point)]
                          [static_cast<Index>(station)]);
  }
  state.cost = 0.0;
  const double pi = std::acos(-1.0);
  for (const double radius : state.radius) {
    state.cost += pi * radius * radius;
  }
}

LocalSearchSolver::State LocalSearchSolver::perturb(
    const State& best, const std::vector<std::vector<double>>& distances,
    int point_count, int station_count, std::mt19937& random) const {
  State result = best;
  if (point_count == 0 || station_count == 0) {
    return result;
  }
  const int perturbation_size = std::max(1, point_count / 20);
  std::uniform_int_distribution<int> point_distribution(0, point_count - 1);
  std::uniform_int_distribution<int> station_distribution(0, station_count - 1);
  for (int index = 0; index < perturbation_size; ++index) {
    const int point = point_distribution(random);
    result.assignment[static_cast<Index>(point)] =
        station_distribution(random);
  }
  recompute_radius(result, distances, point_count, station_count);
  return result;
}

StaticSolution LocalSearchSolver::solve(const Instance& instance, double time) {
  if (!std::isfinite(time) || !std::isfinite(instance.T_end) ||
      instance.T_end <= 0.0 || time < 0.0 || time > instance.T_end) {
    throw std::out_of_range("local-search solve time is outside [0, T_end]");
  }
  if (instance.n < 0 || instance.m < 0 ||
      static_cast<Index>(instance.n) != instance.trajectories.size() ||
      static_cast<Index>(instance.m) != instance.stations.size()) {
    throw std::invalid_argument(
        "local-search solver received inconsistent instance dimensions");
  }
  LOG_DEBUG("LocalSearch::solve t={:.6f} mode={}", time,
            static_cast<int>(config_.mode));
  const auto started = Clock::now();
  StaticSolution result;
  result.solver_name = name();
  result.supporting_point.assign(static_cast<Index>(instance.m), -1);
  result.radius.assign(static_cast<Index>(instance.m), 0.0);
  if (instance.n == 0) {
    result.feasible = true;
    result.lower_bound = 0.0;
    result.upper_bound = 0.0;
    result.solve_time_sec =
        std::chrono::duration<double>(Clock::now() - started).count();
    set_static_result_status(result, BoundStatus::CERTIFIED,
                             OptimalityStatus::FEASIBLE, false);
    return result;
  }
  if (instance.m == 0) {
    result.lower_bound = 0.0;
    result.upper_bound = std::numeric_limits<double>::infinity();
    result.solve_time_sec =
        std::chrono::duration<double>(Clock::now() - started).count();
    set_static_result_status(result, BoundStatus::NONE,
                             OptimalityStatus::INFEASIBLE, false);
    return result;
  }

  std::vector<std::vector<double>> distances(
      static_cast<Index>(instance.n),
      std::vector<double>(static_cast<Index>(instance.m), 0.0));
  for (int point = 0; point < instance.n; ++point) {
    const Point position =
        instance.trajectories[static_cast<Index>(point)].position(time);
    for (int station = 0; station < instance.m; ++station) {
      distances[static_cast<Index>(point)][static_cast<Index>(station)] =
          (instance.stations[static_cast<Index>(station)].pos - position).norm();
    }
  }

  const StaticAssignment nearest =
      StationarySolver::solve_nn(instance, time, active_budget());
  State current;
  current.assignment.assign(static_cast<Index>(instance.n), -1);
  for (int point = 0; point < instance.n; ++point) {
    const Point position =
        instance.trajectories[static_cast<Index>(point)].position(time);
    for (int station = 0; station < instance.m; ++station) {
      const Index station_index = static_cast<Index>(station);
      if (nearest.supporting_point[station_index] < 0) {
        continue;
      }
      if ((instance.stations[station_index].pos - position).norm() <=
          nearest.radius[station_index] + 1e-9) {
        current.assignment[static_cast<Index>(point)] = station;
        break;
      }
    }
    if (current.assignment[static_cast<Index>(point)] < 0) {
      throw std::runtime_error("nearest-neighbor solution leaves a point uncovered");
    }
  }
  recompute_radius(current, distances, instance.n, instance.m);
  State best = current;
  std::mt19937 random(config_.seed);
  int iterations = 0;
  int restarts = 0;
  while (iterations < config_.max_iterations &&
         restarts < config_.max_restarts) {
    check_budget();
    const std::vector<Move> moves =
        enumerate_moves(current, distances, instance.n, instance.m);
    std::vector<Move> improving;
    improving.reserve(moves.size());
    for (const Move& move : moves) {
      if (move.delta_cost < -kImprovementTolerance) {
        improving.push_back(move);
      }
    }
    if (improving.empty()) {
      if (!config_.use_perturbation || restarts + 1 >= config_.max_restarts) {
        break;
      }
      current = perturb(best, distances, instance.n, instance.m, random);
      ++restarts;
      continue;
    }

    Move chosen = improving.front();
    if (config_.mode == Mode::FIRST_IMPROVEMENT) {
      std::uniform_int_distribution<Index> choice(0U, improving.size() - 1U);
      chosen = improving[choice(random)];
    } else {
      chosen = *std::min_element(
          improving.begin(), improving.end(),
          [](const Move& lhs, const Move& rhs) {
            return lhs.delta_cost < rhs.delta_cost;
          });
    }
    apply_move(current, chosen, distances, instance.m);
    ++iterations;
    if (current.cost < best.cost - kImprovementTolerance) {
      best = current;
      restarts = 0;
    }
  }

  std::vector<int> support(static_cast<Index>(instance.m), -1);
  for (int point = 0; point < instance.n; ++point) {
    const int station = best.assignment[static_cast<Index>(point)];
    const Index station_index = static_cast<Index>(station);
    if (support[station_index] < 0 ||
        distances[static_cast<Index>(point)][station_index] >
            distances[static_cast<Index>(support[station_index])]
                     [station_index]) {
      support[station_index] = point;
    }
  }
  result.supporting_point = std::move(support);
  result.radius = best.radius;
  result.cost = best.cost;
  double lower_bound = 0.0;
  const double pi = std::acos(-1.0);
  for (int point = 0; point < instance.n; ++point) {
    double nearest_distance = std::numeric_limits<double>::infinity();
    for (int station = 0; station < instance.m; ++station) {
      nearest_distance =
          std::min(nearest_distance,
                   distances[static_cast<Index>(point)]
                            [static_cast<Index>(station)]);
    }
    lower_bound =
        std::max(lower_bound, pi * nearest_distance * nearest_distance);
  }
  result.feasible = true;
  result.lower_bound = lower_bound;
  result.upper_bound = result.cost;
  result.solve_time_sec =
      std::chrono::duration<double>(Clock::now() - started).count();
  set_static_result_status(result, BoundStatus::CERTIFIED,
                           OptimalityStatus::FEASIBLE, false);
  LOG_INFO("LocalSearch: cost={:.9f} LB={:.9f} iters={} restarts={}",
           result.cost, result.lower_bound, iterations, restarts);
  return result;
}
}
