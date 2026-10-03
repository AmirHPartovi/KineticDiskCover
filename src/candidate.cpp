#include "kdc/candidate.hpp"

#include "kdc/logging.hpp"
#include "kdc/profiling.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

namespace kdc {
namespace {
constexpr double kCoverageTolerance = 1e-9;

void validate_instance(const Instance& instance) {
  if (instance.n < 0 || instance.m < 0 ||
      static_cast<Index>(instance.n) != instance.trajectories.size() ||
      static_cast<Index>(instance.m) != instance.stations.size()) {
    throw std::invalid_argument(
        "candidate geometry received inconsistent instance dimensions");
  }
}

double squared_distance_tolerance(double radius_squared) {
  const double radius = std::sqrt(std::max(0.0, radius_squared));
  return 2.0 * kCoverageTolerance * radius +
         kCoverageTolerance * kCoverageTolerance;
}
}

bool InstancePrecompute::matches(const Instance& instance,
                                 SolverBudget* budget) const {
  if (n != instance.n || m != instance.m || T_end != instance.T_end ||
      stations.size() != instance.stations.size() ||
      trajectories.size() != instance.trajectories.size()) {
    return false;
  }
  for (Index i = 0; i < stations.size(); ++i) {
    if (budget != nullptr) {
      budget->checkpoint();
    }
    if (stations[i].id != instance.stations[i].id ||
        stations[i].pos != instance.stations[i].pos) {
      return false;
    }
  }
  for (Index i = 0; i < trajectories.size(); ++i) {
    if (budget != nullptr) {
      budget->checkpoint();
    }
    if (trajectories[i].t_breaks != instance.trajectories[i].t_breaks ||
        trajectories[i].waypoints != instance.trajectories[i].waypoints) {
      return false;
    }
  }
  return true;
}

Index InstancePrecompute::segment_index(Index point, double time) const {
  if (point >= trajectory_segments.size() ||
      trajectory_segments[point].empty() || !std::isfinite(time)) {
    throw std::out_of_range("trajectory segment lookup is invalid");
  }
  const auto& segments = trajectory_segments[point];
  if (time < segments.front().start_time ||
      time > segments.back().end_time) {
    throw std::out_of_range("time is outside the trajectory interval");
  }
  if (time == segments.front().start_time) {
    return 0U;
  }
  if (time >= segments.back().end_time) {
    return segments.size() - 1U;
  }
  const auto upper = std::upper_bound(
      segments.begin(), segments.end(), time,
      [](double value, const TrajectorySegmentMetadata& segment) {
        return value < segment.start_time;
      });
  return static_cast<Index>(std::distance(segments.begin(), upper) - 1);
}

Index InstancePrecompute::directional_segment_index(Index point, double time,
                                                     bool forward) const {
  if (!forward && point < trajectory_segments.size() &&
      !trajectory_segments[point].empty()) {
    const auto& segments = trajectory_segments[point];
    const auto breakpoint = std::lower_bound(
        segments.begin(), segments.end(), time,
        [](const TrajectorySegmentMetadata& segment, double value) {
          return segment.start_time < value;
        });
    if (breakpoint != segments.end() &&
        breakpoint->start_time == time && breakpoint != segments.begin()) {
      return static_cast<Index>(std::distance(segments.begin(), breakpoint) - 1);
    }
  }
  return segment_index(point, time);
}

Point InstancePrecompute::position(Index point, double time) const {
  KDC_PROFILE_PHASE(ProfilePhase::TRAJECTORY_POSITION);
  const auto& segment = trajectory_segments.at(point).at(segment_index(point, time));
  return segment.start_position + segment.velocity * (time - segment.start_time);
}

Point InstancePrecompute::velocity(Index point, double time,
                                   bool forward) const {
  return trajectory_segments.at(point).at(
      directional_segment_index(point, time, forward)).velocity;
}

std::shared_ptr<const InstancePrecompute> CandidateSet::precompute(
    const Instance& instance, SolverBudget* budget) {
  KDC_PROFILE_PHASE(ProfilePhase::CANDIDATE_BUILD);
  validate_instance(instance);
  if (!instance.precompute_cache) {
    instance.precompute_cache =
        std::make_shared<InstancePrecomputeCache>();
  }
  auto& cache = *instance.precompute_cache;
  std::lock_guard<std::mutex> lock(cache.mutex);
  if (cache.value && cache.value->matches(instance, budget)) {
    return cache.value;
  }

  auto result = std::make_shared<InstancePrecompute>();
  result->n = instance.n;
  result->m = instance.m;
  result->T_end = instance.T_end;
  result->stations = instance.stations;
  result->trajectories = instance.trajectories;
  result->initial_positions.reserve(instance.trajectories.size());
  result->trajectory_segments.resize(instance.trajectories.size());
  for (Index point = 0; point < instance.trajectories.size(); ++point) {
    if (budget != nullptr) {
      budget->checkpoint();
    }
    const auto& trajectory = instance.trajectories[point];
    result->initial_positions.push_back(trajectory.position(0.0));
    auto& segments = result->trajectory_segments[point];
    if (trajectory.t_breaks.size() > 1U) {
      segments.reserve(trajectory.t_breaks.size() - 1U);
    }
    for (Index segment = 0; segment + 1U < trajectory.t_breaks.size();
         ++segment) {
      if (budget != nullptr) {
        budget->checkpoint();
      }
      const double start_time = trajectory.t_breaks[segment];
      const double end_time = trajectory.t_breaks[segment + 1U];
      const double duration = end_time - start_time;
      const Point start = trajectory.waypoints[segment];
      const Point velocity =
          duration == 0.0
              ? Point{}
              : (trajectory.waypoints[segment + 1U] - start) *
                    (1.0 / duration);
      segments.push_back(
          {start_time, end_time, duration, start, velocity,
           start - velocity * start_time});
    }
  }

  const auto n = static_cast<Index>(instance.n);
  const auto m = static_cast<Index>(instance.m);
  if (n != 0U && m > std::numeric_limits<Index>::max() / n) {
    throw std::length_error("candidate count exceeds supported index range");
  }
  if (n * m > static_cast<Index>(std::numeric_limits<int>::max())) {
    throw std::length_error("candidate count exceeds supported index range");
  }
  result->candidates.reserve(n * m);
  std::vector<std::pair<Value, int>> distances;
  distances.reserve(n);
  for (Index station = 0; station < m; ++station) {
    if (budget != nullptr) {
      budget->checkpoint();
    }
    distances.clear();
    const Point station_position = result->stations[station].pos;
    for (Index point = 0; point < n; ++point) {
      if (budget != nullptr) {
        budget->checkpoint();
      }
      distances.emplace_back(
          (station_position - result->initial_positions[point]).norm2(),
          static_cast<int>(point));
    }
    std::sort(distances.begin(), distances.end());
    for (const auto& distance : distances) {
      result->candidates.push_back(
          {static_cast<int>(station), distance.second});
    }
  }

  cache.value = result;
  LOG_INFO("InstancePrecompute: built {} candidates", result->candidates.size());
  return result;
}

StaticGeometry CandidateSet::build_geometry(
    const Instance& instance, const InstancePrecompute& precompute,
    double time, SolverBudget* budget) {
  validate_instance(instance);
  if (!precompute.matches(instance, budget)) {
    throw std::invalid_argument(
        "static geometry precompute does not match the instance");
  }
  if (!std::isfinite(time) || time < 0.0 || time > instance.T_end) {
    throw std::out_of_range("static geometry time is outside [0, T_end]");
  }
  StaticGeometry geometry;
  const auto point_count = static_cast<Index>(instance.n);
  const auto station_count = static_cast<Index>(instance.m);
  if (point_count != 0U &&
      station_count >
          std::numeric_limits<Index>::max() / point_count) {
    throw std::length_error("static geometry exceeds supported index range");
  }
  geometry.point_positions.reserve(point_count);
  for (const auto& trajectory : instance.trajectories) {
    if (budget != nullptr) {
      budget->checkpoint();
    }
    geometry.point_positions.push_back(trajectory.position(time));
  }
  KDC_PROFILE_PHASE(ProfilePhase::DISTANCE_MATRIX);
  geometry.station_point_distance_squared.resize(point_count * station_count);
  for (Index station = 0; station < station_count; ++station) {
    for (Index point = 0; point < point_count; ++point) {
      if (budget != nullptr) {
        budget->checkpoint();
      }
      geometry.station_point_distance_squared[station * point_count + point] =
          (precompute.stations[station].pos -
           geometry.point_positions[point])
              .norm2();
    }
  }
  return geometry;
}

std::vector<CandidateDisk> CandidateSet::build(const Instance& instance,
                                                SolverBudget* budget) {
  LOG_DEBUG("CandidateSet::build: n={}, m={}", instance.n, instance.m);
  if (instance.n <= 0 || instance.m <= 0) {
    LOG_ERROR("CandidateSet::build requires n > 0 and m > 0 (n={}, m={})",
              instance.n, instance.m);
    throw std::invalid_argument("CandidateSet::build requires n > 0 and m > 0");
  }
  return precompute(instance, budget)->candidates;
}

CandidateSet::CoverageMatrix CandidateSet::build_coverage(
    const Instance& instance, const std::vector<CandidateDisk>& disks,
    double time, SolverBudget* budget) {
  const auto precomputed = precompute(instance, budget);
  const auto geometry =
      build_geometry(instance, *precomputed, time, budget);
  return build_coverage(instance, disks, geometry, budget);
}

CandidateSet::CoverageMatrix CandidateSet::build_coverage(
    const Instance& instance, const std::vector<CandidateDisk>& disks,
    const StaticGeometry& geometry, SolverBudget* budget) {
  KDC_PROFILE_PHASE(ProfilePhase::COVERAGE_MATRIX);
  validate_instance(instance);
  if (disks.size() >
      static_cast<Index>(std::numeric_limits<int>::max())) {
    throw std::length_error("disk count exceeds supported index range");
  }
  const auto point_count = static_cast<Index>(instance.n);
  const auto station_count = static_cast<Index>(instance.m);
  if (geometry.point_positions.size() != point_count ||
      geometry.station_point_distance_squared.size() !=
          point_count * station_count) {
    throw std::invalid_argument("static geometry dimensions are inconsistent");
  }

  std::vector<std::vector<int>> points_by_disk(disks.size());
  std::vector<int> counts(point_count, 0);
  for (Index disk_index = 0; disk_index < disks.size(); ++disk_index) {
    if (budget != nullptr) {
      budget->checkpoint();
    }
    const auto& disk = disks[disk_index];
    if (disk.station_id < 0 || disk.station_id >= instance.m ||
        disk.supporting_point < 0 || disk.supporting_point >= instance.n) {
      throw std::invalid_argument("candidate disk index is out of range");
    }
    const Index station = static_cast<Index>(disk.station_id);
    const Index support = static_cast<Index>(disk.supporting_point);
    const double radius_squared =
        geometry.distance_squared(station, support, point_count);
    const double threshold =
        radius_squared + squared_distance_tolerance(radius_squared);
    auto& covered_points = points_by_disk[disk_index];
    for (Index point = 0; point < point_count; ++point) {
      if (budget != nullptr) {
        budget->checkpoint();
      }
      if (geometry.distance_squared(station, point, point_count) <= threshold) {
        if (counts[point] == std::numeric_limits<int>::max()) {
          throw std::length_error("coverage row exceeds supported index range");
        }
        ++counts[point];
        covered_points.push_back(static_cast<int>(point));
      }
    }
  }

  CoverageMatrix coverage;
  coverage.num_disks = static_cast<int>(disks.size());
  coverage.num_points = instance.n;
  coverage.row_ptr.resize(point_count + 1U, 0);
  for (Index point = 0; point < point_count; ++point) {
    if (budget != nullptr) {
      budget->checkpoint();
    }
    if (counts[point] >
        std::numeric_limits<int>::max() - coverage.row_ptr[point]) {
      throw std::length_error("coverage matrix exceeds supported index range");
    }
    coverage.row_ptr[point + 1U] =
        coverage.row_ptr[point] + counts[point];
  }
  coverage.col_idx.resize(static_cast<Index>(coverage.row_ptr.back()));
  std::vector<int> next_row = coverage.row_ptr;
  for (Index disk_index = 0; disk_index < points_by_disk.size(); ++disk_index) {
    if (budget != nullptr) {
      budget->checkpoint();
    }
    for (const int point : points_by_disk[disk_index]) {
      if (budget != nullptr) {
        budget->checkpoint();
      }
      const Index point_index = static_cast<Index>(point);
      const int slot = next_row[point_index]++;
      coverage.col_idx[static_cast<Index>(slot)] =
          static_cast<int>(disk_index);
    }
  }
  LOG_INFO("build_coverage: nnz={}", coverage.col_idx.size());
  return coverage;
}

void CandidateSet::dump(const std::vector<CandidateDisk>& disks) {
#ifndef NDEBUG
  for (const auto& disk : disks) {
    static_cast<void>(disk);
    LOG_DEBUG("candidate station_id={} supporting_point={}", disk.station_id,
              disk.supporting_point);
  }
#else
  static_cast<void>(disks);
#endif
}
}
