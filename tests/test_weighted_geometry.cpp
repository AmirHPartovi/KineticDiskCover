#include "kdc/weighted_geometry.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <vector>

namespace {
kdc::Instance make_two_station_instance(
    const std::vector<kdc::Trajectory>& trajectories) {
  kdc::Instance instance;
  instance.n = static_cast<int>(trajectories.size());
  instance.m = 2;
  instance.T_end = 1.0;
  instance.stations = {
      {10, kdc::Point(0.0, 0.0)},
      {20, kdc::Point(1.0, 0.0)}};
  instance.trajectories = trajectories;
  return instance;
}

bool double_close(kdc::Weight lhs, kdc::Weight rhs,
                  kdc::Weight tolerance = 1e-12) {
  return std::abs(lhs - rhs) <=
         tolerance * std::max(kdc::Weight{1.0},
                              std::abs(rhs));
}
}

TEST_CASE("WeightedGeometry extracts D(t) at stationary and moving times") {
  kdc::Trajectory stationary(
      {0.0, 1.0},
      {kdc::Point(0.0, 0.0),
       kdc::Point(0.0, 0.0)});
  kdc::Trajectory moving(
      {0.0, 1.0},
      {kdc::Point(0.0, 0.0),
       kdc::Point(1.0, 0.0)});
  kdc::Instance instance =
      make_two_station_instance({stationary, moving});
  kdc::WeightedGeometry geometry(instance);

  const auto at_zero = geometry.d_at(0.0);
  REQUIRE(at_zero.matrix.station_count() == 2U);
  REQUIRE(at_zero.matrix.point_count() == 2U);
  REQUIRE(at_zero.matrix.weight(0U, 0U) == 0.0);
  REQUIRE(at_zero.matrix.weight(1U, 0U) == 1.0);
  REQUIRE(at_zero.matrix.weight(0U, 1U) == 0.0);
  REQUIRE(at_zero.matrix.weight(1U, 1U) == 1.0);

  const auto at_half = geometry.d_at(0.5);
  REQUIRE(double_close(
      at_half.matrix.weight(0U, 1U), 0.25));
  REQUIRE(double_close(
      at_half.matrix.weight(1U, 1U), 0.25));
}

TEST_CASE("WeightedGeometry interval bounds contain D(t) samples") {
  kdc::Trajectory moving(
      {0.0, 1.0},
      {kdc::Point(0.0, 0.0),
       kdc::Point(1.0, 0.0)});
  kdc::Instance instance =
      make_two_station_instance({moving});
  kdc::WeightedGeometry geometry(instance);

  const auto bounds =
      geometry.bounds_on(0.0, 1.0);
  REQUIRE(bounds.validity ==
          kdc::GeometryBoundValidityV1::FAST_FLOAT_ONLY);
  REQUIRE(bounds.arithmetic ==
          kdc::GeometryArithmeticModeV1::FAST_DOUBLE);

  for (const double time : {0.0, 0.1, 0.25, 0.5, 0.75, 1.0}) {
    const auto dt = geometry.d_at(time);
    for (kdc::WeightIndex station = 0U;
         station < 2U;
         ++station) {
      for (kdc::WeightIndex point = 0U;
           point < 1U;
           ++point) {
        REQUIRE(bounds.lower.matrix.weight(station, point) <=
                dt.matrix.weight(station, point));
        REQUIRE(dt.matrix.weight(station, point) <=
                bounds.upper.matrix.weight(station, point));
      }
    }
  }

  REQUIRE(double_close(
      bounds.lower.matrix.weight(0U, 0U), 0.0));
  REQUIRE(double_close(
      bounds.upper.matrix.weight(0U, 0U), 1.0));
  REQUIRE(double_close(
      bounds.lower.matrix.weight(1U, 0U), 0.0));
  REQUIRE(double_close(
      bounds.upper.matrix.weight(1U, 0U), 1.0));
}

TEST_CASE("WeightedGeometry handles vertex inside and outside") {
  kdc::Trajectory inside(
      {0.0, 1.0},
      {kdc::Point(1.0, 0.0),
       kdc::Point(0.0, 0.0)});
  kdc::Instance inside_instance =
      make_two_station_instance({inside});
  kdc::WeightedGeometry inside_geometry(inside_instance);
  const auto inside_bounds =
      inside_geometry.bounds_on(0.25, 1.0);

  REQUIRE(double_close(
      inside_bounds.lower.matrix.weight(0U, 0U), 0.0));

  kdc::Trajectory outside(
      {0.0, 1.0},
      {kdc::Point(1.0, 0.0),
       kdc::Point(2.0, 0.0)});
  kdc::Instance outside_instance =
      make_two_station_instance({outside});
  kdc::WeightedGeometry outside_geometry(outside_instance);
  const auto outside_bounds =
      outside_geometry.bounds_on(0.25, 1.0);

  REQUIRE(double_close(
      outside_bounds.lower.matrix.weight(0U, 0U),
      1.5625));
  REQUIRE(double_close(
      outside_bounds.upper.matrix.weight(0U, 0U),
      4.0));
}

TEST_CASE("WeightedGeometry handles zero velocity and coefficient extraction") {
  kdc::Trajectory stationary(
      {0.0, 1.0},
      {kdc::Point(2.0, 0.0),
       kdc::Point(2.0, 0.0)});
  kdc::Instance instance =
      make_two_station_instance({stationary});
  kdc::WeightedGeometry geometry(instance);

  const auto bounds =
      geometry.bounds_on(0.0, 1.0);
  REQUIRE(double_close(
      bounds.lower.matrix.weight(0U, 0U), 4.0));
  REQUIRE(double_close(
      bounds.upper.matrix.weight(0U, 0U), 4.0));

  const auto coefficient =
      geometry.coefficients(0U, 0U, 0.0, 1.0);
  REQUIRE(coefficient.a == 0.0);
  REQUIRE(coefficient.b == 0.0);
  REQUIRE(double_close(coefficient.c, 4.0));
}

TEST_CASE("WeightedGeometry merges simultaneous breakpoint partition") {
  kdc::Trajectory first(
      {0.0, 0.5, 1.0},
      {kdc::Point(0.0, 0.0),
       kdc::Point(1.0, 0.0),
       kdc::Point(0.0, 0.0)});
  kdc::Trajectory second(
      {0.0, 0.5, 1.0},
      {kdc::Point(2.0, 0.0),
       kdc::Point(1.0, 0.0),
       kdc::Point(2.0, 0.0)});
  kdc::Instance instance =
      make_two_station_instance({first, second});
  kdc::WeightedGeometry geometry(instance);

  REQUIRE(geometry.common_partition().size() == 3U);
  REQUIRE(geometry.common_partition()[0] == 0.0);
  REQUIRE(geometry.common_partition()[1] == 0.5);
  REQUIRE(geometry.common_partition()[2] == 1.0);

  const auto bounds =
      geometry.bounds_on(0.0, 1.0);
  for (kdc::WeightIndex station = 0U;
       station < 2U;
       ++station) {
    for (kdc::WeightIndex point = 0U;
         point < 2U;
         ++point) {
      for (const double time : {0.0, 0.25, 0.5, 0.75, 1.0}) {
        const auto dt = geometry.d_at(time);
        REQUIRE(bounds.lower.matrix.weight(station, point) <=
                dt.matrix.weight(station, point));
        REQUIRE(dt.matrix.weight(station, point) <=
                bounds.upper.matrix.weight(station, point));
      }
    }
  }
}

TEST_CASE("WeightedGeometry handles breakpoint endpoints and one segment") {
  kdc::Trajectory trajectory(
      {0.0, 0.25, 1.0},
      {kdc::Point(0.0, 0.0),
       kdc::Point(0.25, 0.0),
       kdc::Point(1.0, 0.0)});
  kdc::Instance instance =
      make_two_station_instance({trajectory});
  kdc::WeightedGeometry geometry(instance);

  const auto first_segment =
      geometry.bounds_on(0.0, 0.25);
  REQUIRE(double_close(
      first_segment.lower.matrix.weight(0U, 0U), 0.0));
  REQUIRE(double_close(
      first_segment.upper.matrix.weight(0U, 0U),
      0.0625));

  const auto exact_second =
      geometry.bounds_on(0.25, 1.0);
  for (const double time : {0.25, 0.5, 0.75, 1.0}) {
    const auto dt = geometry.d_at(time);
    REQUIRE(exact_second.lower.matrix.weight(0U, 0U) <=
            dt.matrix.weight(0U, 0U));
    REQUIRE(dt.matrix.weight(0U, 0U) <=
            exact_second.upper.matrix.weight(0U, 0U));
  }
}

TEST_CASE("WeightedGeometry preserves duplicate centers and station ids") {
  kdc::Instance instance =
      make_two_station_instance(
          {kdc::Trajectory(
              {0.0, 1.0},
              {kdc::Point(0.0, 0.0),
               kdc::Point(0.0, 0.0)})});
  instance.stations[1].pos = kdc::Point(0.0, 0.0);
  instance.stations[1].id = 10;
  kdc::WeightedGeometry geometry(instance);

  REQUIRE(geometry.station_ids()[0] == 10);
  REQUIRE(geometry.station_ids()[1] == 10);
  const auto dt = geometry.d_at(0.5);
  REQUIRE(dt.matrix.weight(0U, 0U) == 0.0);
  REQUIRE(dt.matrix.weight(1U, 0U) == 0.0);
}

TEST_CASE("WeightedGeometry rejects inconsistent and discontinuous input") {
  kdc::Trajectory valid(
      {0.0, 1.0},
      {kdc::Point(0.0, 0.0),
       kdc::Point(1.0, 0.0)});
  kdc::Instance instance =
      make_two_station_instance({valid});
  instance.n = 2;
  REQUIRE_THROWS_AS(kdc::WeightedGeometry(instance),
                    std::invalid_argument);

  kdc::Trajectory wrong_end(
      {0.0, 0.75},
      {kdc::Point(0.0, 0.0),
       kdc::Point(1.0, 0.0)});
  kdc::Instance wrong_instance =
      make_two_station_instance({wrong_end});
  REQUIRE_THROWS_AS(kdc::WeightedGeometry(wrong_instance),
                    std::invalid_argument);

  REQUIRE_THROWS_AS(
      kdc::Trajectory(
          {0.0, 0.5, 0.5, 1.0},
          {kdc::Point(0.0, 0.0),
           kdc::Point(0.25, 0.0),
           kdc::Point(0.5, 0.0),
           kdc::Point(1.0, 0.0)}),
      std::invalid_argument);
}
