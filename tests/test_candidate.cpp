#include "test_utils.hpp"

#include "kdc/candidate.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
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
