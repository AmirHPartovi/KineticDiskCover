#include "kdc/candidate.hpp"

#include "kdc/logging.hpp"

#include <algorithm>
#include <limits>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <utility>

namespace kdc {
namespace {
std::mutex& candidate_cache_mutex() {
  static std::mutex value;
  return value;
}

std::string instance_cache_key(const Instance& instance) {
  std::ostringstream key;
  key << "id=" << instance.id << ";name=" << instance.name << ";n=" << instance.n
      << ";m=" << instance.m << ";T=" << instance.T_end << ";";
  for (const auto& station : instance.stations) {
    key << "station(" << station.id << "," << station.pos.x << ","
        << station.pos.y << ");";
  }
  for (const auto& trajectory : instance.trajectories) {
    key << "traj(" << trajectory.t_breaks.size() << ",";
    for (const auto& time : trajectory.t_breaks) {
      key << time << ";";
    }
    for (const auto& point : trajectory.waypoints) {
      key << point.x << "," << point.y << ";";
    }
    key << ");";
  }
  return key.str();
}

std::string coverage_cache_key(const Instance& instance,
                              const std::vector<CandidateDisk>& disks,
                              double time) {
  std::ostringstream key;
  key << instance_cache_key(instance) << ";time=" << time << ";disks=";
  for (const auto& disk : disks) {
    key << disk.station_id << ":" << disk.supporting_point << ";";
  }
  return key.str();
}
}

std::vector<CandidateDisk> CandidateSet::build(const Instance& instance) {
  LOG_DEBUG("CandidateSet::build: n={}, m={}", instance.n, instance.m);
  if (instance.n <= 0 || instance.m <= 0) {
    LOG_ERROR("CandidateSet::build requires n > 0 and m > 0 (n={}, m={})",
              instance.n, instance.m);
    throw std::invalid_argument("CandidateSet::build requires n > 0 and m > 0");
  }
  if (static_cast<Index>(instance.n) != instance.trajectories.size() ||
      static_cast<Index>(instance.m) != instance.stations.size()) {
    LOG_ERROR("CandidateSet::build received inconsistent instance dimensions");
    throw std::invalid_argument(
        "CandidateSet::build received inconsistent instance dimensions");
  }
  const auto n = static_cast<Index>(instance.n);
  const auto m = static_cast<Index>(instance.m);
  if (n > std::numeric_limits<Index>::max() / m ||
      n * m > static_cast<Index>(std::numeric_limits<int>::max())) {
    throw std::length_error("candidate count exceeds supported index range");
  }

  static std::unordered_map<std::string, std::vector<CandidateDisk>> cache;
  const std::string key = instance_cache_key(instance);
  {
    std::lock_guard<std::mutex> lock(candidate_cache_mutex());
    const auto cached = cache.find(key);
    if (cached != cache.end()) {
      return cached->second;
    }
  }

  std::vector<CandidateDisk> disks;
  disks.reserve(n * m);
  std::vector<std::pair<Value, int>> distances;
  distances.reserve(n);
  for (Index station_index = 0; station_index < m; ++station_index) {
    distances.clear();
    const Point station = instance.stations[station_index].pos;
    for (Index point_index = 0; point_index < n; ++point_index) {
      const Value distance =
          (station - instance.trajectories[point_index].position(0.0)).norm();
      distances.emplace_back(distance, static_cast<int>(point_index));
    }
    std::sort(distances.begin(), distances.end());
    for (const auto& distance : distances) {
      disks.push_back(
          {static_cast<int>(station_index), distance.second});
    }
  }
  {
    std::lock_guard<std::mutex> lock(candidate_cache_mutex());
    cache[key] = disks;
  }
  LOG_INFO("CandidateSet::build: built {} disks", disks.size());
  return disks;
}

CandidateSet::CoverageMatrix CandidateSet::build_coverage(
    const Instance& instance, const std::vector<CandidateDisk>& disks,
    double time) {
  LOG_DEBUG("build_coverage: t={}", time);
  if (instance.n < 0 || instance.m < 0 ||
      static_cast<Index>(instance.n) != instance.trajectories.size() ||
      static_cast<Index>(instance.m) != instance.stations.size()) {
    throw std::invalid_argument(
        "build_coverage received inconsistent instance dimensions");
  }
  if (disks.size() >
      static_cast<Index>(std::numeric_limits<int>::max())) {
    throw std::length_error("disk count exceeds supported index range");
  }

  static std::unordered_map<std::string, CoverageMatrix> coverage_cache;
  const std::string key = coverage_cache_key(instance, disks, time);
  {
    std::lock_guard<std::mutex> lock(candidate_cache_mutex());
    const auto cached = coverage_cache.find(key);
    if (cached != coverage_cache.end()) {
      return cached->second;
    }
  }

  const auto point_count = static_cast<Index>(instance.n);
  const auto disk_count = disks.size();
  std::vector<Point> points;
  points.reserve(point_count);
  for (const auto& trajectory : instance.trajectories) {
    points.push_back(trajectory.position(time));
  }

  std::vector<Point> centers;
  std::vector<Value> radii;
  centers.reserve(disk_count);
  radii.reserve(disk_count);
  for (const auto& disk : disks) {
    if (disk.station_id < 0 || disk.station_id >= instance.m ||
        disk.supporting_point < 0 || disk.supporting_point >= instance.n) {
      throw std::invalid_argument("candidate disk index is out of range");
    }
    const Point center = instance.stations[static_cast<Index>(disk.station_id)].pos;
    centers.push_back(center);
    radii.push_back(
        (center - points[static_cast<Index>(disk.supporting_point)]).norm());
  }

  std::vector<int> counts(point_count, 0);
  for (Index disk_index = 0; disk_index < disk_count; ++disk_index) {
    for (Index point_index = 0; point_index < point_count; ++point_index) {
      const Value distance = (centers[disk_index] - points[point_index]).norm();
      if (distance <= radii[disk_index] + 1e-9) {
        if (counts[point_index] == std::numeric_limits<int>::max()) {
          throw std::length_error("coverage row exceeds supported index range");
        }
        ++counts[point_index];
      }
    }
  }

  CoverageMatrix coverage;
  coverage.num_disks = static_cast<int>(disk_count);
  coverage.num_points = instance.n;
  coverage.row_ptr.resize(point_count + 1U, 0);
  for (Index point_index = 0; point_index < point_count; ++point_index) {
    if (counts[point_index] >
        std::numeric_limits<int>::max() - coverage.row_ptr[point_index]) {
      throw std::length_error("coverage matrix exceeds supported index range");
    }
    coverage.row_ptr[point_index + 1U] =
        coverage.row_ptr[point_index] + counts[point_index];
  }
  coverage.col_idx.resize(
      static_cast<Index>(coverage.row_ptr.back()));
  std::vector<int> next_row = coverage.row_ptr;
  for (Index disk_index = 0; disk_index < disk_count; ++disk_index) {
    for (Index point_index = 0; point_index < point_count; ++point_index) {
      const Value distance = (centers[disk_index] - points[point_index]).norm();
      if (distance <= radii[disk_index] + 1e-9) {
        const int slot = next_row[point_index]++;
        coverage.col_idx[static_cast<Index>(slot)] =
            static_cast<int>(disk_index);
      }
    }
  }
  {
    std::lock_guard<std::mutex> lock(candidate_cache_mutex());
    coverage_cache[key] = coverage;
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
