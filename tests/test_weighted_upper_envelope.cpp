#include "kdc/weighted_upper_envelope.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <vector>

namespace {
kdc::Instance make_upper_instance(
    const std::vector<kdc::Trajectory>& trajectories,
    int station_count) {
  kdc::Instance instance;
  instance.n = static_cast<int>(trajectories.size());
  instance.m = station_count;
  instance.T_end = 1.0;
  for (int station = 0; station < station_count; ++station) {
    instance.stations.push_back(
        {station,
         kdc::Point(static_cast<kdc::Value>(station), 0.0)});
  }
  instance.trajectories = trajectories;
  return instance;
}

bool close_weight(kdc::Weight lhs, kdc::Weight rhs,
                  kdc::Weight tolerance = 1e-12) {
  return std::abs(lhs - rhs) <=
         tolerance * std::max(kdc::Weight{1.0},
                              std::abs(rhs));
}
}

TEST_CASE("Upper envelope handles identical and constant polynomials") {
  kdc::Trajectory first(
      {0.0, 1.0},
      {kdc::Point(0.0, 0.0),
       kdc::Point(0.0, 0.0)});
  kdc::Trajectory second(
      {0.0, 1.0},
      {kdc::Point(0.0, 0.0),
       kdc::Point(0.0, 0.0)});
  kdc::Instance instance =
      make_upper_instance({first, second}, 1);
  kdc::WeightedUpperEnvelope envelope(instance);

  const auto result =
      envelope.solve(0.0, 1.0, {0, 0});
  REQUIRE(result.direct_valid);
  REQUIRE(result.status ==
          kdc::UpperEnvelopeStatusV1::FAST_COMPLETE);
  REQUIRE(close_weight(result.direct_integral, 0.0));
  REQUIRE(result.d_plus_fallback_valid);
  REQUIRE(result.upper_bound == 0.0);
}

TEST_CASE("Upper envelope handles root at interval boundary") {
  kdc::Trajectory stationary(
      {0.0, 1.0},
      {kdc::Point(0.0, 0.0),
       kdc::Point(0.0, 0.0)});
  kdc::Trajectory linear(
      {0.0, 1.0},
      {kdc::Point(0.0, 0.0),
       kdc::Point(1.0, 0.0)});
  kdc::Instance instance =
      make_upper_instance({stationary, linear}, 1);
  kdc::WeightedUpperEnvelope envelope(instance);

  const auto result =
      envelope.solve(0.0, 1.0, {0, 0});
  REQUIRE(result.direct_valid);
  REQUIRE(close_weight(result.direct_integral,
                        std::acos(-1.0) / 3.0));
}

TEST_CASE("Upper envelope handles repeated and three-function equality") {
  kdc::Trajectory zero(
      {0.0, 1.0},
      {kdc::Point(0.0, 0.0),
       kdc::Point(0.0, 0.0)});
  kdc::Trajectory repeated(
      {0.0, 1.0},
      {kdc::Point(-0.5, 0.0),
       kdc::Point(0.5, 0.0)});
  kdc::Instance repeated_instance =
      make_upper_instance({zero, repeated}, 1);
  kdc::WeightedUpperEnvelope repeated_envelope(repeated_instance);
  const auto repeated_result =
      repeated_envelope.solve(0.0, 1.0, {0, 0});
  REQUIRE(repeated_result.direct_valid);
  REQUIRE(close_weight(repeated_result.direct_integral,
                        std::acos(-1.0) / 12.0));

  kdc::Trajectory positive(
      {0.0, 1.0},
      {kdc::Point(0.0, 0.0),
       kdc::Point(1.0, 0.0)});
  kdc::Trajectory negative(
      {0.0, 1.0},
      {kdc::Point(0.0, 0.0),
       kdc::Point(-1.0, 0.0)});
  kdc::Instance triple_instance =
      make_upper_instance({zero, positive, negative}, 1);
  kdc::WeightedUpperEnvelope triple_envelope(triple_instance);
  const auto triple_result =
      triple_envelope.solve(0.0, 1.0, {0, 0, 0});
  REQUIRE(triple_result.direct_valid);
  REQUIRE(close_weight(triple_result.direct_integral,
                        std::acos(-1.0) / 3.0));
}

TEST_CASE("Upper envelope handles empty station and affine/constant cases") {
  kdc::Trajectory stationary(
      {0.0, 1.0},
      {kdc::Point(0.0, 0.0),
       kdc::Point(0.0, 0.0)});
  kdc::Instance instance =
      make_upper_instance({stationary}, 2);
  kdc::WeightedUpperEnvelope envelope(instance);

  const auto result =
      envelope.solve(0.0, 1.0, {0});
  REQUIRE(result.direct_valid);
  REQUIRE(close_weight(result.direct_integral, 0.0));
  REQUIRE(result.d_plus_fallback_valid);
  REQUIRE(result.upper_bound == 0.0);
}

TEST_CASE("Upper envelope D-plus fallback remains available") {
  kdc::Trajectory moving(
      {0.0, 1.0},
      {kdc::Point(0.0, 0.0),
       kdc::Point(1.0, 0.0)});
  kdc::Instance instance =
      make_upper_instance({moving}, 1);
  kdc::WeightedUpperEnvelope envelope(instance);

  const auto result =
      envelope.solve(0.0, 1.0, {0});
  REQUIRE(result.direct_valid);
  REQUIRE(result.d_plus_fallback_valid);
  REQUIRE(close_weight(result.direct_integral,
                        std::acos(-1.0) / 3.0));
  REQUIRE(result.d_plus_fallback_integral >=
          result.direct_integral);
  REQUIRE(result.upper_bound <=
          result.d_plus_fallback_integral);
}

TEST_CASE("Validated upper envelope is unavailable") {
  kdc::Trajectory moving(
      {0.0, 1.0},
      {kdc::Point(0.0, 0.0),
       kdc::Point(1.0, 0.0)});
  kdc::Instance instance =
      make_upper_instance({moving}, 1);
  kdc::WeightedUpperEnvelope envelope(instance);

  const auto result =
      envelope.solve(
          0.0, 1.0, {0},
          kdc::UpperEnvelopeModeV1::VALIDATED);
  REQUIRE(result.status ==
          kdc::UpperEnvelopeStatusV1::VALIDATED_UNAVAILABLE);
  REQUIRE_FALSE(result.direct_valid);
  REQUIRE_FALSE(result.d_plus_fallback_valid);
  REQUIRE(result.upper_bound ==
          std::numeric_limits<kdc::Weight>::infinity());
}
