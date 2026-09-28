#include "test_utils.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <utility>
#include <vector>

using kdc::Point;
using kdc::Trajectory;

TEST_CASE("Point operations follow two-dimensional vector arithmetic") {
  SECTION("Point arithmetic") {
    REQUIRE(Point(1, 2) + Point(3, 4) == Point(4, 6));
    REQUIRE(Point(3, 4) - Point(1, 2) == Point(2, 2));
    REQUIRE(Point(1, 2) * 2.0 == Point(2, 4));
    REQUIRE(kdc::test::near(Point(3, 4).dot(Point(1, 2)), 11.0));
    REQUIRE(kdc::test::near(Point(3, 4).cross(Point(1, 2)), 2.0));
    REQUIRE(kdc::test::near(Point(3, 4).norm2(), 25.0));
    REQUIRE(kdc::test::near(Point(3, 4).norm(), 5.0));
  }
}

TEST_CASE("Trajectory interpolates positions on its time breaks") {
  const Trajectory trajectory({0.0, 1.0},
                              {Point(0, 0), Point(10, 20)});

  SECTION("Trajectory position at t=0 and t=1") {
    REQUIRE(trajectory.position(0.0) == Point(0, 0));
    REQUIRE(trajectory.position(1.0) == Point(10, 20));
  }

  SECTION("Trajectory position at t=0.5") {
    REQUIRE(trajectory.position(0.5) == Point(5, 10));
  }
}

TEST_CASE("Trajectory selects the interval containing a time") {
  const Trajectory trajectory({0.0, 0.3, 1.0},
                              {Point(0, 0), Point(3, 6), Point(10, 20)});

  SECTION("Trajectory segment_index") {
    REQUIRE(trajectory.segment_index(0.15) == 0U);
    REQUIRE(trajectory.segment_index(0.5) == 1U);
    REQUIRE(trajectory.segment_index(1.0) == 1U);
  }
}

TEST_CASE("Dummy instance generation scales to repeated medium instances") {
  const auto start = std::chrono::steady_clock::now();
  std::size_t trajectory_count = 0U;
  std::size_t station_count = 0U;
  for (unsigned seed = 0U; seed < 1000U; ++seed) {
    const auto instance = kdc::test::make_dummy_instance(100, 10, seed);
    trajectory_count += instance.trajectories.size();
    station_count += instance.stations.size();
  }
  const auto elapsed = std::chrono::steady_clock::now() - start;

  REQUIRE(trajectory_count == 100000U);
  REQUIRE(station_count == 10000U);
  REQUIRE(elapsed < std::chrono::seconds(5));
}

TEST_CASE("Test utilities construct linear instances and compare solutions") {
  const auto instance = kdc::test::make_instance_linear(
      {{Point(0, 0), Point(10, 20)}}, {Point(3, 4)}, 2.0);
  REQUIRE(instance.trajectories.size() == 1U);
  REQUIRE(instance.stations.size() == 1U);
  REQUIRE(instance.trajectories.front().position(1.0) == Point(5, 10));

  kdc::KineticSolution first;
  first.intervals = {{0.0, 0.5, {0}, {0}, 0.0, 0.0, 3.0},
                     {0.5, 1.0, {0}, {0}, 0.0, 0.0, 3.0}};
  kdc::KineticSolution equivalent;
  equivalent.intervals = {{0.0, 1.0, {0}, {0}, 0.0, 0.0, 3.0}};
  REQUIRE(kdc::test::solutions_equal(first, equivalent));

  equivalent.intervals.front().c += 1.0;
  REQUIRE_FALSE(kdc::test::solutions_equal(first, equivalent));
}
