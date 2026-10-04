#include "kdc/stationary.hpp"

#include "kdc/candidate.hpp"
#include "kdc/logging.hpp"
#include "kdc/profiling.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <vector>

namespace kdc {
StaticAssignment StationarySolver::assign_points_to_disks(
    const Instance& instance, double time,
    const std::vector<int>& supporting_points,
    const std::vector<double>& radii, SolverBudget* budget) {
  if (!std::isfinite(time) || time < 0.0 || time > instance.T_end ||
      instance.n < 0 || instance.m < 0 ||
      static_cast<Index>(instance.n) != instance.trajectories.size() ||
      static_cast<Index>(instance.m) != instance.stations.size() ||
      supporting_points.size() != static_cast<Index>(instance.m) ||
      radii.size() != static_cast<Index>(instance.m)) {
    throw std::invalid_argument("static ownership received invalid dimensions");
  }

  const auto precomputed = CandidateSet::precompute(instance, budget);
  const StaticGeometry geometry =
      CandidateSet::build_geometry(instance, *precomputed, time, budget);
  const Index point_count = static_cast<Index>(instance.n);
  const Index station_count = static_cast<Index>(instance.m);
  StaticAssignment assignment;
  assignment.supporting_point.assign(station_count, -1);
  assignment.radius.assign(station_count, 0.0);
  assignment.assigned_points.assign(point_count, -1);
  std::vector<Value> disk_radius_squared(station_count, 0.0);
  std::vector<Value> owned_radius_squared(station_count, 0.0);

  for (Index station = 0; station < station_count; ++station) {
    if (budget != nullptr) {
      budget->checkpoint();
    }
    const int support = supporting_points[station];
    const Value radius = radii[station];
    if (!std::isfinite(radius) || radius < 0.0 || support < -1 ||
        support >= instance.n) {
      throw std::invalid_argument("static ownership received invalid disks");
    }
    if (support < 0) {
      continue;
    }
    const Index point = static_cast<Index>(support);
    disk_radius_squared[station] = radius * radius;
    const Value distance_squared =
        geometry.distance_squared(station, point, point_count);
    if (distance_squared >
        disk_radius_squared[station] + 2e-9 * radius + 1e-18) {
      throw std::invalid_argument(
          "static supporting point lies outside its disk");
    }
    if (assignment.assigned_points[point] < 0) {
      assignment.assigned_points[point] = static_cast<int>(station);
      owned_radius_squared[station] = distance_squared;
    }
  }

  for (Index point = 0; point < point_count; ++point) {
    if (budget != nullptr) {
      budget->checkpoint();
    }
    if (assignment.assigned_points[point] >= 0) {
      continue;
    }
    int best_station = -1;
    Value best_increment = std::numeric_limits<Value>::infinity();
    Value best_distance_squared = 0.0;
    for (Index station = 0; station < station_count; ++station) {
      if (budget != nullptr) {
        budget->checkpoint();
      }
      if (supporting_points[station] < 0) {
        continue;
      }
      const Value distance_squared =
          geometry.distance_squared(station, point, point_count);
      const Value radius = radii[station];
      if (distance_squared >
          disk_radius_squared[station] + 2e-9 * radius + 1e-18) {
        continue;
      }
      const Value increment =
          std::max(owned_radius_squared[station], distance_squared) -
          owned_radius_squared[station];
      if (increment < best_increment ||
          (increment == best_increment &&
           (best_station < 0 ||
            station < static_cast<Index>(best_station)))) {
        best_station = static_cast<int>(station);
        best_increment = increment;
        best_distance_squared = distance_squared;
      }
    }
    if (best_station >= 0) {
      assignment.assigned_points[point] = best_station;
      owned_radius_squared[static_cast<Index>(best_station)] =
          std::max(owned_radius_squared[static_cast<Index>(best_station)],
                   best_distance_squared);
    }
  }

  assignment.feasible = true;
  for (Index station = 0; station < station_count; ++station) {
    int support = -1;
    Value farthest_squared = -1.0;
    for (Index point = 0; point < point_count; ++point) {
      if (budget != nullptr) {
        budget->checkpoint();
      }
      if (assignment.assigned_points[point] != static_cast<int>(station)) {
        continue;
      }
      const Value distance_squared =
          geometry.distance_squared(station, point, point_count);
      if (distance_squared > farthest_squared ||
          (distance_squared == farthest_squared &&
           (support < 0 || static_cast<int>(point) < support))) {
        support = static_cast<int>(point);
        farthest_squared = distance_squared;
      }
    }
    if (support >= 0) {
      assignment.supporting_point[station] = support;
      assignment.radius[station] = std::sqrt(farthest_squared);
    }
  }
  assignment.feasible = true;
  for (Index point = 0; point < point_count; ++point) {
    if (budget != nullptr) {
      budget->checkpoint();
    }
    const int owner = assignment.assigned_points[point];
    if (owner < 0 || owner >= instance.m) {
      assignment.feasible = false;
      continue;
    }
    const Index station = static_cast<Index>(owner);
    const Value distance_squared =
        geometry.distance_squared(station, point, point_count);
    const Value radius = assignment.radius[station];
    if (distance_squared > radius * radius + 2e-9 * radius + 1e-18) {
      assignment.feasible = false;
    }
  }
  const Value pi = std::acos(-1.0);
  for (const Value radius : assignment.radius) {
    assignment.cost += pi * radius * radius;
  }
  return assignment;
}

StaticAssignment StationarySolver::solve_nn(const Instance& instance,
                                            double time,
                                            SolverBudget* budget) {
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

  const auto precomputed = CandidateSet::precompute(instance, budget);
  const StaticGeometry geometry =
      CandidateSet::build_geometry(instance, *precomputed, time, budget);
  const auto point_count = static_cast<Index>(instance.n);
  const auto station_count = static_cast<Index>(instance.m);

  std::vector<int> nearest_station(point_count, -1);
  std::vector<Value> nearest_distance_squared(point_count, 0.0);
  for (Index point_index = 0; point_index < point_count; ++point_index) {
    if (budget != nullptr) {
      budget->checkpoint();
    }
    Value best_distance_squared = std::numeric_limits<Value>::infinity();
    int best_station = -1;
    for (Index station_index = 0; station_index < station_count;
         ++station_index) {
      if (budget != nullptr) {
        budget->checkpoint();
      }
      const Value distance_squared =
          geometry.distance_squared(station_index, point_index, point_count);
      if (distance_squared < best_distance_squared) {
        best_distance_squared = distance_squared;
        best_station = static_cast<int>(station_index);
      }
    }
    nearest_station[point_index] = best_station;
    nearest_distance_squared[point_index] =
        best_station < 0 ? 0.0 : best_distance_squared;
  }

  std::vector<Index> order(point_count);
  std::iota(order.begin(), order.end(), 0U);
  std::sort(order.begin(), order.end(),
            [&nearest_distance_squared](Index lhs, Index rhs) {
              if (nearest_distance_squared[lhs] !=
                  nearest_distance_squared[rhs]) {
                return nearest_distance_squared[lhs] >
                       nearest_distance_squared[rhs];
              }
              return lhs < rhs;
            });

  std::vector<int> supporting_points(station_count, -1);
  std::vector<Value> radii(station_count, 0.0);
  std::vector<bool> covered(point_count, false);
  for (const Index point_index : order) {
    if (budget != nullptr) {
      budget->checkpoint();
    }
    if (covered[point_index]) {
      continue;
    }
    const int nearest = nearest_station[point_index];
    if (nearest < 0) {
      continue;
    }
    const auto station_index = static_cast<Index>(nearest);
    const Value distance_squared = nearest_distance_squared[point_index];
    if (distance_squared >= radii[station_index] * radii[station_index]) {
      supporting_points[station_index] = static_cast<int>(point_index);
      radii[station_index] = std::sqrt(distance_squared);
    }
    const Value coverage_radius = radii[station_index];
    const Value radius_squared = coverage_radius * coverage_radius;
    const Value tolerance = 2e-9 * coverage_radius + 1e-18;
    for (Index other_point = 0; other_point < point_count; ++other_point) {
      if (budget != nullptr) {
        budget->checkpoint();
      }
      if (geometry.distance_squared(station_index, other_point, point_count) <=
          radius_squared + tolerance) {
        covered[other_point] = true;
      }
    }
  }

  StaticAssignment assignment = assign_points_to_disks(
      instance, time, supporting_points, radii, budget);
  assignment.feasible =
      assignment.feasible &&
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
    ILPResult::Status* out_status, SolverBudget* budget) {
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
  ScopedPhaseTimer model_timer(ProfilePhase::MODEL_BUILD);
  ScopedPhaseTimer build_timer(ProfilePhase::LP_ILP_BUILD);
  const auto precomputed = CandidateSet::precompute(instance, budget);
  const auto& disks = precomputed->candidates;
  const StaticGeometry geometry =
      CandidateSet::build_geometry(instance, *precomputed, time, budget);
  if (instance.n > 0 && instance.m > 0) {
    const auto coverage =
        CandidateSet::build_coverage(instance, disks, geometry, budget);
    costs.resize(static_cast<Eigen::Index>(disks.size()));
    constraints.resize(instance.n, static_cast<int>(disks.size()));
    integer_vars.reserve(disks.size());

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
      const Value radius_squared = geometry.distance_squared(
          static_cast<Index>(disk.station_id),
          static_cast<Index>(disk.supporting_point),
          static_cast<Index>(instance.n));
      costs[static_cast<Eigen::Index>(disk_index)] = pi * radius_squared;
      integer_vars.push_back(static_cast<int>(disk_index));
    }
  } else {
    costs.resize(0);
    constraints.resize(instance.n, 0);
  }

  if (budget != nullptr) {
    budget->checkpoint();
  }
  build_timer.stop();
  model_timer.stop();
  const double effective_limit =
      budget == nullptr ? time_limit_sec
                        : budget->limit_seconds(time_limit_sec);
  if (effective_limit <= 0.0) {
    throw SolverBudgetExpired();
  }
  const ILPResult result = [&]() {
    KDC_PROFILE_PHASE(ProfilePhase::LP_ILP_SOLVE);
    return solver.solve(costs, constraints, rhs, integer_vars, effective_limit,
                        gap_target);
  }();
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
    if (result.status == ILPResult::Status::TIME_LIMIT) {
      StaticAssignment interrupted;
      interrupted.supporting_point.assign(static_cast<Index>(instance.m), -1);
      interrupted.radius.assign(static_cast<Index>(instance.m), 0.0);
      interrupted.feasible = false;
      return interrupted;
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
      const Value radius = std::sqrt(geometry.distance_squared(
          station_index, static_cast<Index>(support),
          static_cast<Index>(instance.n)));
      if (radius > largest_radius) {
        largest_radius = radius;
        assignment.supporting_point[station_index] = support;
        assignment.radius[station_index] = radius;
      }
    }
  }

  assignment = assign_points_to_disks(instance, time,
                                      assignment.supporting_point,
                                      assignment.radius, budget);
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
