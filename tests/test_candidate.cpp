#include "test_utils.hpp"

#include "kdc/candidate.hpp"
#include "kdc/mock_ilp_solver.hpp"
#include "kdc/stationary.hpp"

#include <catch2/catch_test_macros.hpp>
#include <Eigen/Core>
#include <Eigen/SparseCore>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <random>
#include <utility>
#include <vector>

namespace {
bool all_points_covered(const kdc::CandidateSet::CoverageMatrix& coverage) {
  if (coverage.row_ptr.size() !=
          static_cast<kdc::Index>(coverage.num_points) + 1U ||
      coverage.row_ptr.empty() || coverage.row_ptr.front() != 0 ||
      coverage.row_ptr.back() !=
          static_cast<int>(coverage.col_idx.size())) {
    return false;
  }
  for (int point = 0; point < coverage.num_points; ++point) {
    if (coverage.row_ptr[static_cast<kdc::Index>(point)] ==
        coverage.row_ptr[static_cast<kdc::Index>(point) + 1U]) {
      return false;
    }
  }
  return true;
}

kdc::CandidateSet::CoverageMatrix reference_coverage(
    const kdc::Instance& instance,
    const std::vector<kdc::CandidateDisk>& disks, double time) {
  std::vector<kdc::Point> points;
  for (const auto& trajectory : instance.trajectories) {
    points.push_back(trajectory.position(time));
  }
  std::vector<std::vector<int>> covered_by_point(
      static_cast<kdc::Index>(instance.n));
  for (kdc::Index disk_index = 0; disk_index < disks.size(); ++disk_index) {
    const auto& disk = disks[disk_index];
    const auto& station =
        instance.stations[static_cast<kdc::Index>(disk.station_id)].pos;
    const double radius =
        (station - points[static_cast<kdc::Index>(disk.supporting_point)]).norm();
    for (kdc::Index point = 0; point < points.size(); ++point) {
      if ((station - points[point]).norm() <= radius + 1e-9) {
        covered_by_point[point].push_back(static_cast<int>(disk_index));
      }
    }
  }
  kdc::CandidateSet::CoverageMatrix result;
  result.num_disks = static_cast<int>(disks.size());
  result.num_points = instance.n;
  result.row_ptr.push_back(0);
  for (const auto& row : covered_by_point) {
    result.col_idx.insert(result.col_idx.end(), row.begin(), row.end());
    result.row_ptr.push_back(static_cast<int>(result.col_idx.size()));
  }
  return result;
}

void require_same_coverage(const kdc::CandidateSet::CoverageMatrix& lhs,
                           const kdc::CandidateSet::CoverageMatrix& rhs) {
  REQUIRE(lhs.num_disks == rhs.num_disks);
  REQUIRE(lhs.num_points == rhs.num_points);
  REQUIRE(lhs.row_ptr == rhs.row_ptr);
  REQUIRE(lhs.col_idx == rhs.col_idx);
}
}

TEST_CASE("CandidateSet builds sorted station-point disks") {
  const auto instance = kdc::test::make_dummy_instance(10, 5, 12U);
  const auto disks = kdc::CandidateSet::build(instance);

  SECTION("size") {
    REQUIRE(disks.size() ==
            static_cast<kdc::Index>(instance.n * instance.m));
  }

  SECTION("ordering") {
    std::vector<double> distances;
    for (const auto& disk : disks) {
      if (disk.station_id == 0) {
        const auto station = instance.stations[0].pos;
        const auto point = instance.trajectories[
            static_cast<kdc::Index>(disk.supporting_point)].position(0.0);
        distances.push_back((station - point).norm());
      }
    }
    REQUIRE(distances.size() == static_cast<kdc::Index>(instance.n));
    for (kdc::Index index = 1; index < distances.size(); ++index) {
      REQUIRE(distances[index - 1U] <= distances[index]);
    }

  }

  SECTION("coverage nonempty") {
    const auto coverage =
        kdc::CandidateSet::build_coverage(instance, disks, 0.0);
    REQUIRE(all_points_covered(coverage));
  }

  SECTION("CSR consistency") {
    const auto coverage =
        kdc::CandidateSet::build_coverage(instance, disks, 0.0);
    REQUIRE(coverage.row_ptr.front() == 0);
    REQUIRE(coverage.row_ptr.back() ==
            static_cast<int>(coverage.col_idx.size()));
    REQUIRE(std::is_sorted(coverage.row_ptr.begin(), coverage.row_ptr.end()));
    REQUIRE(coverage.num_disks == static_cast<int>(disks.size()));
    REQUIRE(coverage.num_points == instance.n);
  }

  SECTION("coverage at t=0.5") {
    const auto coverage =
        kdc::CandidateSet::build_coverage(instance, disks, 0.5);
    REQUIRE(all_points_covered(coverage));
  }
}

TEST_CASE("CandidateSet builds large candidate sets efficiently") {
  const auto instance = kdc::test::make_dummy_instance(500, 25, 73U);
  const auto start = std::chrono::steady_clock::now();
  const auto disks = kdc::CandidateSet::build(instance);
  const auto elapsed = std::chrono::steady_clock::now() - start;

  REQUIRE(disks.size() == 12500U);
  REQUIRE(elapsed < std::chrono::seconds(1));
}

TEST_CASE("Instance precompute preserves candidate identity and is scoped") {
  auto instance = kdc::test::make_instance_linear(
      {{{1.0, 0.0}, {1.0, 0.0}},
       {{-1.0, 0.0}, {-1.0, 0.0}},
       {{0.0, 1.0}, {0.0, 1.0}},
       {{0.0, -1.0}, {0.0, -1.0}}},
      {{0.0, 0.0}});

  const auto first = kdc::CandidateSet::precompute(instance);
  const auto second = kdc::CandidateSet::precompute(instance);
  REQUIRE(first == second);
  REQUIRE(first->candidates.size() == 4U);
  for (int point = 0; point < 4; ++point) {
    REQUIRE(first->candidates[static_cast<kdc::Index>(point)].station_id == 0);
    REQUIRE(first->candidates[static_cast<kdc::Index>(point)].supporting_point ==
            point);
  }

  instance.stations[0].pos = {0.5, 0.0};
  const auto updated = kdc::CandidateSet::precompute(instance);
  REQUIRE(updated != first);
  REQUIRE(updated->matches(instance));
}

TEST_CASE("Optimized coverage matches distance-based reference on random inputs") {
  for (unsigned seed = 0; seed < 12U; ++seed) {
    const auto instance = kdc::test::make_dummy_instance(9, 4, seed);
    const auto precomputed = kdc::CandidateSet::precompute(instance);
    const auto& disks = precomputed->candidates;
    for (const double time : {0.0, 0.17, 0.5, 0.91, 1.0}) {
      const auto geometry =
          kdc::CandidateSet::build_geometry(instance, *precomputed, time);
      const auto optimized =
          kdc::CandidateSet::build_coverage(instance, disks, geometry);
      const auto reference = reference_coverage(instance, disks, time);
      require_same_coverage(optimized, reference);
    }
  }
}

TEST_CASE("Optimized coverage handles ties, collinearity, and zero radii") {
  const auto instance = kdc::test::make_instance_linear(
      {{{0.0, 0.0}, {0.0, 0.0}},
       {{1.0, 0.0}, {1.0, 0.0}},
       {{2.0, 0.0}, {2.0, 0.0}},
       {{1.0, 0.0}, {1.0, 0.0}}},
      {{0.0, 0.0}, {2.0, 0.0}});
  const auto precomputed = kdc::CandidateSet::precompute(instance);
  const auto geometry =
      kdc::CandidateSet::build_geometry(instance, *precomputed, 0.4);
  const auto optimized = kdc::CandidateSet::build_coverage(
      instance, precomputed->candidates, geometry);
  const auto reference =
      reference_coverage(instance, precomputed->candidates, 0.4);
  require_same_coverage(optimized, reference);
}

TEST_CASE("Squared coverage preserves the distance-space tolerance") {
  const auto instance = kdc::test::make_instance_linear(
      {{{1.0, 0.0}, {1.0, 0.0}},
       {{1.0 + 0.5e-9, 0.0}, {1.0 + 0.5e-9, 0.0}},
       {{1.0 + 2e-9, 0.0}, {1.0 + 2e-9, 0.0}}},
      {{0.0, 0.0}});
  const std::vector<kdc::CandidateDisk> disks{{0, 0}};
  const auto precomputed = kdc::CandidateSet::precompute(instance);
  const auto geometry =
      kdc::CandidateSet::build_geometry(instance, *precomputed, 0.0);
  const auto optimized =
      kdc::CandidateSet::build_coverage(instance, disks, geometry);
  const auto reference = reference_coverage(instance, disks, 0.0);
  require_same_coverage(optimized, reference);
  REQUIRE(optimized.row_ptr[1] == 1);
  REQUIRE(optimized.row_ptr[2] == 2);
  REQUIRE(optimized.row_ptr[3] == 2);
}

TEST_CASE("Static IP objective matches reference coverage construction") {
  const auto instance = kdc::test::make_instance_linear(
      {{{0.0, 0.0}, {0.0, 0.0}},
       {{1.0, 0.0}, {1.0, 0.0}},
       {{2.0, 0.0}, {2.0, 0.0}}},
      {{0.0, 0.0}, {2.0, 0.0}});
  const auto disks = kdc::CandidateSet::build(instance);
  const auto coverage = reference_coverage(instance, disks, 0.0);
  Eigen::VectorXd costs(static_cast<Eigen::Index>(disks.size()));
  Eigen::SparseMatrix<double> constraints(instance.n,
                                           static_cast<int>(disks.size()));
  std::vector<Eigen::Triplet<double>> entries;
  for (int point = 0; point < coverage.num_points; ++point) {
    for (int offset = coverage.row_ptr[static_cast<kdc::Index>(point)];
         offset < coverage.row_ptr[static_cast<kdc::Index>(point) + 1U];
         ++offset) {
      entries.emplace_back(
          point, coverage.col_idx[static_cast<kdc::Index>(offset)], 1.0);
    }
  }
  constraints.setFromTriplets(entries.begin(), entries.end());
  const double pi = std::acos(-1.0);
  std::vector<kdc::Point> points;
  for (const auto& trajectory : instance.trajectories) {
    points.push_back(trajectory.position(0.0));
  }
  for (kdc::Index disk = 0; disk < disks.size(); ++disk) {
    const auto& candidate = disks[disk];
    const double radius =
        (instance.stations[static_cast<kdc::Index>(candidate.station_id)].pos -
         points[static_cast<kdc::Index>(candidate.supporting_point)])
            .norm();
    costs[static_cast<Eigen::Index>(disk)] = pi * radius * radius;
  }
  const Eigen::VectorXd rhs = Eigen::VectorXd::Ones(instance.n);
  std::vector<int> integer_variables;
  for (kdc::Index disk = 0; disk < disks.size(); ++disk) {
    integer_variables.push_back(static_cast<int>(disk));
  }
  kdc::MockILPSolver reference_solver;
  const auto reference_result =
      reference_solver.solve(costs, constraints, rhs, integer_variables, 60.0,
                             1e-4);

  kdc::MockILPSolver optimized_solver;
  const auto optimized_assignment = kdc::StationarySolver::solve_ip(
      instance, 0.0, optimized_solver, 60.0, 1e-4);
  double reference_assignment_cost = 0.0;
  for (kdc::Index station = 0;
       station < static_cast<kdc::Index>(instance.m); ++station) {
    double largest_radius = 0.0;
    for (kdc::Index disk = 0; disk < disks.size(); ++disk) {
      if (reference_result.x[disk] > 0.5 &&
          disks[disk].station_id == static_cast<int>(station)) {
        largest_radius = std::max(
            largest_radius,
            (instance.stations[station].pos -
             points[static_cast<kdc::Index>(disks[disk].supporting_point)])
                .norm());
      }
    }
    reference_assignment_cost += pi * largest_radius * largest_radius;
  }
  REQUIRE(optimized_assignment.feasible);
  REQUIRE(kdc::test::near(optimized_assignment.cost, reference_assignment_cost,
                          1e-10));
}
