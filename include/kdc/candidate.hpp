#pragma once

#include "kdc/solver_budget.hpp"
#include "kdc/types.hpp"

#include <vector>

namespace kdc {
struct CandidateDisk {
  int station_id{-1};
  int supporting_point{-1};
};

struct TrajectorySegmentMetadata {
  double start_time{0.0};
  double end_time{0.0};
  double duration{0.0};
  Point start_position{};
  Point velocity{};
  Point affine_origin{};
};

struct InstancePrecompute {
  int n{0};
  int m{0};
  double T_end{0.0};
  std::vector<Station> stations;
  std::vector<Trajectory> trajectories;
  std::vector<CandidateDisk> candidates;
  std::vector<Point> initial_positions;
  std::vector<std::vector<TrajectorySegmentMetadata>> trajectory_segments;

  bool matches(const Instance& instance,
               SolverBudget* budget = nullptr) const;
  Index segment_index(Index point, double time) const;
  Index directional_segment_index(Index point, double time,
                                  bool forward) const;
  Point position(Index point, double time) const;
  Point velocity(Index point, double time, bool forward = true) const;
};

struct StaticGeometry {
  std::vector<Point> point_positions;
  std::vector<double> station_point_distance_squared;

  double distance_squared(Index station, Index point,
                         Index point_count) const noexcept {
    return station_point_distance_squared[station * point_count + point];
  }
};

class CandidateSet {
 public:
  static std::shared_ptr<const InstancePrecompute> precompute(
      const Instance& instance, SolverBudget* budget = nullptr);
  static StaticGeometry build_geometry(
      const Instance& instance, const InstancePrecompute& precompute,
      double time, SolverBudget* budget = nullptr);

  static std::vector<CandidateDisk> build(const Instance& instance,
                                          SolverBudget* budget = nullptr);

  struct CoverageMatrix {
    int num_disks{0};
    int num_points{0};
    std::vector<int> row_ptr;
    std::vector<int> col_idx;
  };

  static CoverageMatrix build_coverage(
      const Instance& instance, const std::vector<CandidateDisk>& disks,
      double time = 0.0, SolverBudget* budget = nullptr);
  static CoverageMatrix build_coverage(
      const Instance& instance, const std::vector<CandidateDisk>& disks,
      const StaticGeometry& geometry, SolverBudget* budget = nullptr);

  static void dump(const std::vector<CandidateDisk>& disks);
};
}
