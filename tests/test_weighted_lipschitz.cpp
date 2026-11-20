#include "kdc/weighted_lipschitz.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <vector>

namespace {
bool lipschitz_close(kdc::Weight lhs, kdc::Weight rhs,
                     kdc::Weight tolerance = 1e-12) {
  return std::abs(lhs - rhs) <=
         tolerance * std::max(kdc::Weight{1.0},
                              std::abs(rhs));
}

kdc::Instance moving_one_station_instance() {
  kdc::Instance instance;
  instance.n = 1;
  instance.m = 1;
  instance.T_end = 1.0;
  instance.stations = {{10, kdc::Point(0.0, 0.0)}};
  instance.trajectories = {
      kdc::Trajectory(
          {0.0, 1.0},
          {kdc::Point(0.0, 0.0),
           kdc::Point(1.0, 0.0)})};
  return instance;
}
}

TEST_CASE("Phi Lipschitz cone handles K-zero, u-zero and Delta-zero") {
  REQUIRE(kdc::WeightedLipschitzBound::phi_fast(10.0, 0.0, 2.0) ==
          0.0);
  REQUIRE(kdc::WeightedLipschitzBound::phi_fast(0.0, 2.0, 2.0) ==
          0.0);
  REQUIRE(kdc::WeightedLipschitzBound::phi_fast(10.0, 2.0, 0.0) ==
          0.0);
  REQUIRE_THROWS_AS(kdc::WeightedLipschitzBound::phi_fast(
                        -1.0, 2.0, 1.0),
                    std::invalid_argument);
}

TEST_CASE("Phi Lipschitz cone branch boundary agrees") {
  const kdc::Weight K = 2.0;
  const kdc::Weight Delta = 1.0;
  const kdc::Weight u = K * Delta / 2.0;
  const kdc::Weight high =
      Delta * u - K * Delta * Delta / 4.0;
  const kdc::Weight low =
      u * u / K;
  const kdc::Weight result =
      kdc::WeightedLipschitzBound::phi_fast(
          u, K, Delta);

  REQUIRE(lipschitz_close(high, low));
  REQUIRE(lipschitz_close(result, high));
}

TEST_CASE("Phi Lipschitz cone matches direct symmetric integral") {
  // Delta=2, K=1, u=1.
  // Integral is exact 1.
  const kdc::Weight result =
      kdc::WeightedLipschitzBound::phi_fast(
          1.0, 1.0, 2.0);
  REQUIRE(lipschitz_close(result, 1.0));

  // Delta=2, K=1, u=3 uses the high branch:
  // 2*3-1*4/4=5.
  const kdc::Weight high =
      kdc::WeightedLipschitzBound::phi_fast(
          3.0, 1.0, 2.0);
  REQUIRE(lipschitz_close(high, 5.0));

  // Delta=2, K=2, u=0.5 uses the low branch:
  // u^2/K=0.125.
  const kdc::Weight low =
      kdc::WeightedLipschitzBound::phi_fast(
          0.5, 2.0, 2.0);
  REQUIRE(lipschitz_close(low, 0.125));
}

TEST_CASE("Lipschitz R and velocity enclosures are finite") {
  const kdc::Instance instance =
      moving_one_station_instance();
  kdc::WeightedLipschitzBound bound(instance);

  const auto pair =
      bound.pair_upper(0U, 0U, 0.0, 1.0);
  REQUIRE(pair.r_upper >= 1.0);
  REQUIRE(pair.velocity_norm_upper >= 1.0);
  REQUIRE(pair.l_upper >= 2.0);

  const auto lambda =
      bound.lambda_upper(0.0, 1.0);
  REQUIRE(lambda.lambda_upper >= 2.0 *
          std::acos(-1.0));
  REQUIRE(lambda.pairs.size() == 1U);
}

TEST_CASE("Lipschitz stationary velocity gives Lambda zero") {
  kdc::Instance instance =
      moving_one_station_instance();
  instance.trajectories[0] =
      kdc::Trajectory(
          {0.0, 1.0},
          {kdc::Point(1.0, 0.0),
           kdc::Point(1.0, 0.0)});
  kdc::WeightedLipschitzBound bound(instance);

  const auto pair =
      bound.pair_upper(0U, 0U, 0.0, 1.0);
  REQUIRE(lipschitz_close(pair.velocity_norm_upper, 0.0));
  REQUIRE(lipschitz_close(pair.l_upper, 0.0));
  const auto lambda =
      bound.lambda_upper(0.0, 1.0);
  REQUIRE(lipschitz_close(lambda.lambda_upper, 0.0));
}

TEST_CASE("Lipschitz interval rejects breakpoint crossing") {
  kdc::Instance instance =
      moving_one_station_instance();
  instance.trajectories[0] =
      kdc::Trajectory(
          {0.0, 0.5, 1.0},
          {kdc::Point(0.0, 0.0),
           kdc::Point(0.5, 0.0),
           kdc::Point(1.0, 0.0)});
  kdc::WeightedLipschitzBound bound(instance);

  REQUIRE_THROWS_AS(
      bound.lambda_upper(0.25, 0.75),
      std::invalid_argument);
}

TEST_CASE("Lipschitz local bound rejects heuristic midpoint sources") {
  kdc::Instance instance =
      moving_one_station_instance();
  kdc::WeightedLipschitzBound bound(instance);
  REQUIRE_THROWS_AS(
      kdc::WeightedLipschitzBound::make_validated_midpoint_bound(
          1.0,
          kdc::MidpointLowerBoundSourceV1::HEURISTIC),
      std::invalid_argument);

  const auto exact =
      kdc::WeightedLipschitzBound::make_validated_midpoint_bound(
          0.0,
          kdc::MidpointLowerBoundSourceV1::EXACT_STATIC);
  const auto local =
      bound.local_lower(0.0, 1.0, exact);
  REQUIRE(local.validity ==
          kdc::LipschitzValidityV1::FAST_FLOAT_ONLY);
  REQUIRE(local.lower_bound == 0.0);
}
