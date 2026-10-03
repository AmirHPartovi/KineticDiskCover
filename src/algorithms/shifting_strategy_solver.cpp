#include "kdc/algorithms/shifting_strategy_solver.hpp"

#include "kdc/algorithms/ip_static_solver.hpp"
#include "kdc/algorithms/nn_static_solver.hpp"
#include "kdc/logging.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <map>
#include <stdexcept>
#include <utility>
#include <vector>

namespace kdc {
namespace {
using Clock = std::chrono::steady_clock;

struct CellResult {
  std::vector<int> support;
  std::vector<double> radius;
  double cost{0.0};
  bool feasible{false};
};

CellResult solve_cell(const Instance& parent,
                      const std::vector<int>& point_ids, double time,
                      ILPSolver* ilp,
                      const ShiftingStrategySolver::Config& config,
                      SolverBudget* budget) {
  Instance subinstance;
  subinstance.id = parent.id;
  subinstance.name = parent.name;
  subinstance.n = static_cast<int>(point_ids.size());
  subinstance.m = parent.m;
  subinstance.T_end = parent.T_end;
  subinstance.stations = parent.stations;
  subinstance.trajectories.reserve(point_ids.size());
  for (const int point : point_ids) {
    subinstance.trajectories.push_back(
        parent.trajectories[static_cast<Index>(point)]);
  }

  StaticSolution local;
  if (config.use_ip_for_cells && ilp != nullptr) {
    IPStaticSolver::Config ip_config;
    ip_config.gap_target = config.cell_ip_gap;
    ip_config.time_limit_sec = config.cell_ip_time_limit_sec;
    IPStaticSolver solver(ilp, ip_config);
    local = budget == nullptr
                ? solver.solve(subinstance, time)
                : solver.solve_with_budget(
                      subinstance, time, *budget,
                      config.cell_ip_time_limit_sec);
  } else {
    NNStaticSolver solver;
    local = budget == nullptr
                ? solver.solve(subinstance, time)
                : solver.solve_with_budget(
                      subinstance, time, *budget,
                      config.cell_ip_time_limit_sec);
  }
  CellResult result;
  result.support = std::move(local.supporting_point);
  result.radius = std::move(local.radius);
  result.cost = local.cost;
  result.feasible = local.feasible;
  for (int& support : result.support) {
    if (support >= 0) {
      support = point_ids[static_cast<Index>(support)];
    }
  }
  return result;
}
}

ShiftingStrategySolver::ShiftingStrategySolver(ILPSolver* ilp)
    : ShiftingStrategySolver(ilp, Config{}) {}

ShiftingStrategySolver::ShiftingStrategySolver(ILPSolver* ilp, Config config)
    : ilp_(ilp), config_(config) {
  if (config_.l <= 0 || !std::isfinite(config_.cell_ip_gap) ||
      config_.cell_ip_gap < 0.0 ||
      !std::isfinite(config_.cell_ip_time_limit_sec) ||
      config_.cell_ip_time_limit_sec <= 0.0) {
    throw std::invalid_argument("shifting solver configuration is invalid");
  }
}

void ShiftingStrategySolver::set_time_limit(double time_limit_sec) {
  if (!std::isfinite(time_limit_sec) || time_limit_sec <= 0.0) {
    throw std::invalid_argument("shifting time limit must be positive");
  }
  config_.cell_ip_time_limit_sec = time_limit_sec;
}

StaticSolution ShiftingStrategySolver::solve(const Instance& instance,
                                             double time) {
  if (!std::isfinite(time) || !std::isfinite(instance.T_end) ||
      instance.T_end <= 0.0 || time < 0.0 || time > instance.T_end) {
    throw std::out_of_range("shifting solve time is outside [0, T_end]");
  }
  if (instance.n < 0 || instance.m < 0 ||
      static_cast<Index>(instance.n) != instance.trajectories.size() ||
      static_cast<Index>(instance.m) != instance.stations.size()) {
    throw std::invalid_argument(
        "shifting solver received inconsistent instance dimensions");
  }
  LOG_DEBUG("Shifting::solve t={:.6f} l={}", time, config_.l);
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

  std::vector<Point> points;
  points.reserve(static_cast<Index>(instance.n));
  for (const auto& trajectory : instance.trajectories) {
    points.push_back(trajectory.position(time));
  }
  double x_min = points.front().x;
  double x_max = x_min;
  double y_min = points.front().y;
  double y_max = y_min;
  for (const Point& point : points) {
    x_min = std::min(x_min, point.x);
    x_max = std::max(x_max, point.x);
    y_min = std::min(y_min, point.y);
    y_max = std::max(y_max, point.y);
  }
  const double width_x = x_max - x_min;
  const double width_y = y_max - y_min;

  std::vector<std::vector<double>> distances(
      static_cast<Index>(instance.n),
      std::vector<double>(static_cast<Index>(instance.m), 0.0));
  std::vector<double> nearest_distances(static_cast<Index>(instance.n),
                                        std::numeric_limits<double>::infinity());
  for (int point = 0; point < instance.n; ++point) {
    for (int station = 0; station < instance.m; ++station) {
      const double distance =
          (instance.stations[static_cast<Index>(station)].pos -
           points[static_cast<Index>(point)])
              .norm();
      distances[static_cast<Index>(point)][static_cast<Index>(station)] =
          distance;
      nearest_distances[static_cast<Index>(point)] =
          std::min(nearest_distances[static_cast<Index>(point)], distance);
    }
  }

  double best_cost = std::numeric_limits<double>::infinity();
  std::vector<int> best_support(static_cast<Index>(instance.m), -1);
  std::vector<double> best_radius(static_cast<Index>(instance.m), 0.0);

  // Keeping the best solution from every coarser resolution makes quality
  // monotone in l while still considering all shifts at the requested scale.
  for (int resolution = 1; resolution <= config_.l; ++resolution) {
    check_budget();
    const double cell_width_x =
        width_x > 0.0 ? width_x / static_cast<double>(resolution) : 1.0;
    const double cell_width_y =
        width_y > 0.0 ? width_y / static_cast<double>(resolution) : 1.0;
    for (int shift_x = 0; shift_x < resolution; ++shift_x) {
      check_budget();
      for (int shift_y = 0; shift_y < resolution; ++shift_y) {
        check_budget();
        std::map<std::pair<int, int>, std::vector<int>> cells;
        const double offset_x =
            static_cast<double>(shift_x) * cell_width_x /
            static_cast<double>(resolution);
        const double offset_y =
            static_cast<double>(shift_y) * cell_width_y /
            static_cast<double>(resolution);
        for (int point = 0; point < instance.n; ++point) {
          const Point& position = points[static_cast<Index>(point)];
          const int cell_x = static_cast<int>(
              std::floor((position.x - x_min + offset_x) / cell_width_x));
          const int cell_y = static_cast<int>(
              std::floor((position.y - y_min + offset_y) / cell_width_y));
          cells[{cell_x, cell_y}].push_back(point);
        }

        std::vector<int> selected_support(static_cast<Index>(instance.m), -1);
        std::vector<double> selected_radius(static_cast<Index>(instance.m), 0.0);
        bool feasible = true;
        for (const auto& cell : cells) {
          const CellResult local =
              solve_cell(instance, cell.second, time, ilp_, config_,
                         active_budget());
          if (!local.feasible) {
            feasible = false;
            break;
          }
          for (int station = 0; station < instance.m; ++station) {
            const Index station_index = static_cast<Index>(station);
            const int support = local.support[station_index];
            if (support >= 0 &&
                (selected_support[station_index] < 0 ||
                 local.radius[station_index] >
                     selected_radius[station_index])) {
              selected_support[station_index] = support;
              selected_radius[station_index] = local.radius[station_index];
            }
          }
        }
        if (!feasible) {
          continue;
        }
        double cost = 0.0;
        const double pi = std::acos(-1.0);
        for (const double radius : selected_radius) {
          cost += pi * radius * radius;
        }
        if (cost < best_cost) {
          best_cost = cost;
          best_support = std::move(selected_support);
          best_radius = std::move(selected_radius);
        }
      }
    }
  }

  if (!std::isfinite(best_cost)) {
    result.solve_time_sec =
        std::chrono::duration<double>(Clock::now() - started).count();
    set_static_result_status(result, BoundStatus::NONE,
                             OptimalityStatus::FAILED, false);
    return result;
  }
  double lower_bound = 0.0;
  const double pi = std::acos(-1.0);
  for (const double distance : nearest_distances) {
    lower_bound = std::max(lower_bound, pi * distance * distance);
  }
  result.supporting_point = std::move(best_support);
  result.radius = std::move(best_radius);
  result.cost = best_cost;
  result.feasible = true;
  result.lower_bound = lower_bound;
  result.upper_bound = best_cost;
  result.solve_time_sec =
      std::chrono::duration<double>(Clock::now() - started).count();
  LOG_INFO("Shifting: cost={:.9f} LB={:.9f} ratio={:.4f} l={}",
           result.cost, result.lower_bound,
           result.cost / std::max(result.lower_bound, 1e-12), config_.l);
  set_static_result_status(result, BoundStatus::CERTIFIED,
                           OptimalityStatus::FEASIBLE, false);
  return result;
}
}
