#include "kdc/algorithms/simulated_annealing_solver.hpp"

#include "kdc/logging.hpp"
#include "kdc/stationary.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <set>
#include <random>
#include <stdexcept>
#include <vector>

namespace kdc {
namespace {
using Clock = std::chrono::steady_clock;

struct State {
  std::vector<int> assignment;
  std::vector<double> radius;
  std::vector<std::multiset<std::pair<double, int>>> station_points;
  double cost{0.0};
};

void recompute_state(State& state,
                    const std::vector<std::vector<double>>& distances,
                    int point_count, int station_count) {
  state.radius.assign(static_cast<Index>(station_count), 0.0);
  state.station_points.clear();
  state.station_points.resize(static_cast<Index>(station_count));
  for (int point = 0; point < point_count; ++point) {
    const int station = state.assignment[static_cast<Index>(point)];
    const double distance =
        distances[static_cast<Index>(point)][static_cast<Index>(station)];
    state.station_points[static_cast<Index>(station)].emplace(distance, point);
    state.radius[static_cast<Index>(station)] =
        std::max(state.radius[static_cast<Index>(station)], distance);
  }
  state.cost = 0.0;
  const double pi = std::acos(-1.0);
  for (const double radius : state.radius) {
    state.cost += pi * radius * radius;
  }
}

double move_delta(const State& state, int point, int from, int to,
                  const std::vector<std::vector<double>>& distances) {
  const auto& from_points =
      state.station_points[static_cast<Index>(from)];
  double from_radius = 0.0;
  if (from_points.size() > 1U) {
    auto largest = from_points.rbegin();
    if (largest->second == point) {
      ++largest;
    }
    from_radius = largest->first;
  }
  const double to_radius =
      std::max(state.radius[static_cast<Index>(to)],
               distances[static_cast<Index>(point)][static_cast<Index>(to)]);
  const double pi = std::acos(-1.0);
  const double old_cost =
      state.radius[static_cast<Index>(from)] *
          state.radius[static_cast<Index>(from)] +
      state.radius[static_cast<Index>(to)] *
          state.radius[static_cast<Index>(to)];
  const double new_cost = from_radius * from_radius + to_radius * to_radius;
  return pi * (new_cost - old_cost);
}

void apply_move(State& state, int point, int from, int to,
                const std::vector<std::vector<double>>& distances) {
  const Index from_index = static_cast<Index>(from);
  const Index to_index = static_cast<Index>(to);
  const double from_old_radius = state.radius[from_index];
  const double to_old_radius = state.radius[to_index];
  const double point_distance_from =
      distances[static_cast<Index>(point)][from_index];
  const double point_distance_to =
      distances[static_cast<Index>(point)][to_index];
  auto& from_points = state.station_points[from_index];
  const auto found = from_points.find({point_distance_from, point});
  if (found == from_points.end()) {
    throw std::logic_error("annealing move point is missing from its station");
  }
  from_points.erase(found);
  state.station_points[to_index].emplace(point_distance_to, point);
  state.assignment[static_cast<Index>(point)] = to;
  state.radius[from_index] =
      from_points.empty() ? 0.0 : from_points.rbegin()->first;
  state.radius[to_index] =
      state.station_points[to_index].rbegin()->first;
  const double pi = std::acos(-1.0);
  state.cost +=
      pi * (state.radius[from_index] * state.radius[from_index] +
            state.radius[to_index] * state.radius[to_index] -
            from_old_radius * from_old_radius -
            to_old_radius * to_old_radius);
}
}

SimulatedAnnealingSolver::SimulatedAnnealingSolver()
    : SimulatedAnnealingSolver(Config{}) {}

SimulatedAnnealingSolver::SimulatedAnnealingSolver(Config config)
    : config_(config) {
  if (!std::isfinite(config_.T_init) || config_.T_init <= 0.0 ||
      !std::isfinite(config_.T_min) || config_.T_min <= 0.0 ||
      !std::isfinite(config_.alpha) || config_.alpha <= 0.0 ||
      config_.alpha >= 1.0 || config_.iters_per_temp <= 0 ||
      config_.max_outer_iters < 0) {
    throw std::invalid_argument(
        "simulated-annealing configuration is invalid");
  }
}

StaticSolution SimulatedAnnealingSolver::solve(const Instance& instance,
                                               double time) {
  if (!std::isfinite(time) || !std::isfinite(instance.T_end) ||
      instance.T_end <= 0.0 || time < 0.0 || time > instance.T_end) {
    throw std::out_of_range("simulated-annealing time is outside [0, T_end]");
  }
  if (instance.n < 0 || instance.m < 0 ||
      static_cast<Index>(instance.n) != instance.trajectories.size() ||
      static_cast<Index>(instance.m) != instance.stations.size()) {
    throw std::invalid_argument(
        "simulated-annealing solver received inconsistent dimensions");
  }
  LOG_DEBUG("SA::solve t={:.6f} T_init={} alpha={}", time, config_.T_init,
            config_.alpha);
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
      throw std::runtime_error(
          "nearest-neighbor solution leaves a point uncovered");
    }
  }
  recompute_state(current, distances, instance.n, instance.m);
  State best = current;

  std::mt19937 random(config_.seed);
  std::uniform_real_distribution<double> uniform(0.0, 1.0);
  std::uniform_int_distribution<int> point_distribution(0, instance.n - 1);
  std::uniform_int_distribution<int> station_distribution(0, instance.m - 1);
  double temperature = config_.T_init;
  for (int outer = 0; outer < config_.max_outer_iters; ++outer) {
    check_budget();
    for (int inner = 0; inner < config_.iters_per_temp; ++inner) {
      check_budget();
      const int point = point_distribution(random);
      const int from = current.assignment[static_cast<Index>(point)];
      const int to = station_distribution(random);
      if (to == from) {
        continue;
      }
      const double delta =
          move_delta(current, point, from, to, distances);
      if (delta < 0.0 || uniform(random) < std::exp(-delta / temperature)) {
        apply_move(current, point, from, to, distances);
        if (current.cost < best.cost) {
          best = current;
        }
      }
    }
    temperature *= config_.alpha;
    if (temperature < config_.T_min) {
      break;
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
  LOG_INFO("SA: cost={:.9f} LB={:.9f} final_T={:.6e}", result.cost,
           result.lower_bound, temperature);
  return result;
}
}
