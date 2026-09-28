#include "kdc/stationary.hpp"

#include "kdc/candidate.hpp"
#include "kdc/logging.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <vector>

namespace kdc {
StaticAssignment StationarySolver::solve_nn(const Instance& instance,
                                            double time) {
  LOG_DEBUG("NN: t={}", time);
  if (!std::isfinite(time) || time < 0.0 || time > instance.T_end) {
    throw std::out_of_range("stationary solve time is outside [0, T_end]");
  }
  if (instance.n < 0 || instance.m < 0 ||
      static_cast<Index>(instance.n) != instance.trajectories.size() ||
      static_cast<Index>(instance.m) != instance.stations.size()) {
    throw std::invalid_argument(
        "stationary solve received inconsistent instance dimensions");
  }

  const auto point_count = static_cast<Index>(instance.n);
  const auto station_count = static_cast<Index>(instance.m);
  std::vector<Point> points;
  points.reserve(point_count);
  for (const auto& trajectory : instance.trajectories) {
    points.push_back(trajectory.position(time));
  }

  std::vector<int> nearest_station(point_count, -1);
  std::vector<Value> nearest_distance(point_count, 0.0);
  for (Index point_index = 0; point_index < point_count; ++point_index) {
    Value best_distance = std::numeric_limits<Value>::infinity();
    int best_station = -1;
    for (Index station_index = 0; station_index < station_count;
         ++station_index) {
      const Value distance =
          (points[point_index] - instance.stations[station_index].pos).norm();
      if (distance < best_distance) {
        best_distance = distance;
        best_station = static_cast<int>(station_index);
      }
    }
    nearest_station[point_index] = best_station;
    nearest_distance[point_index] =
        best_station < 0 ? 0.0 : best_distance;
  }

  std::vector<Index> order(point_count);
  std::iota(order.begin(), order.end(), 0U);
  std::sort(order.begin(), order.end(),
            [&nearest_distance](Index lhs, Index rhs) {
              if (nearest_distance[lhs] != nearest_distance[rhs]) {
                return nearest_distance[lhs] > nearest_distance[rhs];
              }
              return lhs < rhs;
            });

  StaticAssignment assignment;
  assignment.supporting_point.assign(station_count, -1);
  assignment.radius.assign(station_count, 0.0);
  std::vector<bool> covered(point_count, false);
  for (const Index point_index : order) {
    if (covered[point_index]) {
      continue;
    }
    const int nearest = nearest_station[point_index];
    if (nearest < 0) {
      continue;
    }
    const auto station_index = static_cast<Index>(nearest);
    if (nearest_distance[point_index] >= assignment.radius[station_index]) {
      assignment.supporting_point[station_index] =
          static_cast<int>(point_index);
      assignment.radius[station_index] = nearest_distance[point_index];
    }
    const Value coverage_radius = assignment.radius[station_index];
    const Point station = instance.stations[station_index].pos;
    for (Index other_point = 0; other_point < point_count; ++other_point) {
      if ((points[other_point] - station).norm() <= coverage_radius + 1e-9) {
        covered[other_point] = true;
      }
    }
  }

  const Value squared_radius_sum =
      std::inner_product(assignment.radius.begin(), assignment.radius.end(),
                         assignment.radius.begin(), 0.0);
  assignment.cost = std::acos(-1.0) * squared_radius_sum;
  assignment.feasible =
      std::all_of(covered.begin(), covered.end(), [](bool value) {
        return value;
      });
  const auto active_stations = static_cast<int>(std::count_if(
      assignment.supporting_point.begin(), assignment.supporting_point.end(),
      [](int point) { return point != -1; }));
  LOG_INFO("NN: cost={:.6f}, feasible={}, active_stations={}",
           assignment.cost, assignment.feasible, active_stations);
  return assignment;
}

StaticAssignment StationarySolver::solve_ip(
    const Instance& instance, double time, ILPSolver& solver,
    double time_limit_sec, double gap_target, double* out_lower_bound,
    ILPResult::Status* out_status) {
  LOG_DEBUG("IP: t={}, time_limit={}, gap={}", time, time_limit_sec,
            gap_target);
  if (!std::isfinite(time) || time < 0.0 || time > instance.T_end) {
    throw std::out_of_range("stationary solve time is outside [0, T_end]");
  }
  if (instance.n < 0 || instance.m < 0 ||
      static_cast<Index>(instance.n) != instance.trajectories.size() ||
      static_cast<Index>(instance.m) != instance.stations.size()) {
    throw std::invalid_argument(
        "stationary solve received inconsistent instance dimensions");
  }

  Eigen::VectorXd costs;
  Eigen::SparseMatrix<double> constraints;
  Eigen::VectorXd rhs = Eigen::VectorXd::Ones(instance.n);
  std::vector<int> integer_vars;
  std::vector<CandidateDisk> disks;
  std::vector<Point> points;
  if (instance.n > 0 && instance.m > 0) {
    disks = CandidateSet::build(instance);
    const auto coverage =
        CandidateSet::build_coverage(instance, disks, time);
    costs.resize(static_cast<Eigen::Index>(disks.size()));
    constraints.resize(instance.n, static_cast<int>(disks.size()));
    integer_vars.reserve(disks.size());
    points.reserve(static_cast<Index>(instance.n));
    for (const auto& trajectory : instance.trajectories) {
      points.push_back(trajectory.position(time));
    }

    std::vector<Eigen::Triplet<double>> entries;
    entries.reserve(coverage.col_idx.size());
    for (int point_index = 0; point_index < coverage.num_points;
         ++point_index) {
      const int row_start =
          coverage.row_ptr[static_cast<Index>(point_index)];
      const int row_end =
          coverage.row_ptr[static_cast<Index>(point_index) + 1U];
      for (int offset = row_start; offset < row_end; ++offset) {
        entries.emplace_back(
            point_index,
            coverage.col_idx[static_cast<Index>(offset)], 1.0);
      }
    }
    constraints.setFromTriplets(entries.begin(), entries.end());
    const Value pi = std::acos(-1.0);
    for (Index disk_index = 0; disk_index < disks.size(); ++disk_index) {
      const auto& disk = disks[disk_index];
      const Point station =
          instance.stations[static_cast<Index>(disk.station_id)].pos;
      const Value radius =
          (station - points[static_cast<Index>(disk.supporting_point)]).norm();
      costs[static_cast<Eigen::Index>(disk_index)] = pi * radius * radius;
      integer_vars.push_back(static_cast<int>(disk_index));
    }
  } else {
    costs.resize(0);
    constraints.resize(instance.n, 0);
  }

  const ILPResult result = solver.solve(costs, constraints, rhs, integer_vars,
                                        time_limit_sec, gap_target);
  if (out_status != nullptr) {
    *out_status = result.status;
  }
  if (out_lower_bound != nullptr) {
    *out_lower_bound = result.lower_bound;
  }
  if (result.status == ILPResult::Status::INFEASIBLE) {
    LOG_ERROR("IP: infeasible at t={}", time);
    StaticAssignment infeasible;
    infeasible.supporting_point.assign(static_cast<Index>(instance.m), -1);
    infeasible.radius.assign(static_cast<Index>(instance.m), 0.0);
    infeasible.feasible = false;
    return infeasible;
  }
  if (result.status != ILPResult::Status::OPTIMAL &&
      result.status != ILPResult::Status::FEASIBLE &&
      result.status != ILPResult::Status::TIME_LIMIT) {
    throw std::runtime_error("IP solver failed: " + result.solver_message);
  }
  if (result.x.size() != disks.size()) {
    if (instance.n == 0 && result.x.empty()) {
      StaticAssignment empty;
      empty.supporting_point.assign(static_cast<Index>(instance.m), -1);
      empty.radius.assign(static_cast<Index>(instance.m), 0.0);
      empty.feasible = true;
      return empty;
    }
    throw std::runtime_error(
        "IP solver returned no valid incumbent solution vector");
  }

  StaticAssignment assignment;
  assignment.supporting_point.assign(static_cast<Index>(instance.m), -1);
  assignment.radius.assign(static_cast<Index>(instance.m), 0.0);
  for (Index station_index = 0;
       station_index < static_cast<Index>(instance.m); ++station_index) {
    const Index begin = station_index * static_cast<Index>(instance.n);
    const Index end = begin + static_cast<Index>(instance.n);
    Value largest_radius = -1.0;
    for (Index disk_index = begin; disk_index < end; ++disk_index) {
      if (result.x[disk_index] <= 0.5) {
        continue;
      }
      const int support = disks[disk_index].supporting_point;
      const Value radius =
          (instance.stations[station_index].pos -
           points[static_cast<Index>(support)])
              .norm();
      if (radius > largest_radius) {
        largest_radius = radius;
        assignment.supporting_point[station_index] = support;
        assignment.radius[station_index] = radius;
      }
    }
  }

  const Value pi = std::acos(-1.0);
  for (const Value radius : assignment.radius) {
    assignment.cost += pi * radius * radius;
  }
  assignment.feasible = true;
  for (const Point& point : points) {
    bool covered = false;
    for (Index station_index = 0;
         station_index < static_cast<Index>(instance.m); ++station_index) {
      if ((point - instance.stations[station_index].pos).norm() <=
          assignment.radius[station_index] + 1e-9) {
        covered = true;
        break;
      }
    }
    if (!covered) {
      assignment.feasible = false;
      break;
    }
  }
  if (!assignment.feasible) {
    throw std::runtime_error(
        "IP solver returned a solution that does not cover all points");
  }
  const auto active_stations = static_cast<int>(std::count_if(
      assignment.supporting_point.begin(), assignment.supporting_point.end(),
      [](int point) { return point != -1; }));
  LOG_INFO("IP: cost={:.6f}, active={}, time={:.3f}s", assignment.cost,
           active_stations, result.solve_time_sec);
  return assignment;
}
}
