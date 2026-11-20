#include "kdc/global_bound_history.hpp"
#include "kdc/temporal_partition.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdio>
#include <limits>
#include <string>
#include <vector>

namespace {
kdc::TemporalBoundRecordV1 make_bound(
    double value,
    kdc::TemporalBoundValidityV1 validity =
        kdc::TemporalBoundValidityV1::VALID,
    kdc::TemporalBoundArithmeticV1 arithmetic =
        kdc::TemporalBoundArithmeticV1::CONSERVATIVE_INTERVAL,
    const std::string& certificate = "cert") {
  kdc::TemporalBoundRecordV1 bound;
  bound.value = value;
  bound.validity = validity;
  bound.method = "test";
  bound.certificate_id = certificate;
  bound.provenance = "test";
  bound.arithmetic = arithmetic;
  return bound;
}

kdc::TemporalPiecewiseProgramV1 make_program(
    double start,
    double end) {
  kdc::TemporalPiecewiseProgramV1 program;
  program.point_assignment = {0};
  kdc::TemporalRadiusPieceV1 piece;
  piece.start = start;
  piece.end = end;
  piece.station_radii = {1.0};
  program.pieces.push_back(piece);
  return program;
}
}

TEST_CASE("Global bound current marks invalid LB as zero and UB infinite") {
  kdc::TemporalPartition partition(0.0, 1.0);
  kdc::GlobalBoundIdentityV1 identity;
  identity.instance_hash = "instance-1";
  kdc::GlobalBoundHistory history(identity);

  const auto result =
      history.update(partition, 1U);
  REQUIRE_FALSE(result.lb_current.valid);
  REQUIRE(result.lb_current.value == 0.0);
  REQUIRE(result.lb_current.trivial);
  REQUIRE_FALSE(result.ub_current.valid);
  REQUIRE(result.ub_current.value ==
          std::numeric_limits<double>::infinity());
  REQUIRE_FALSE(result.lb_best.has_value());
  REQUIRE_FALSE(result.ub_best.has_value());
}

TEST_CASE("LB and UB history remain monotone for valid certificates") {
  kdc::GlobalBoundIdentityV1 identity;
  identity.instance_hash = "instance-monotone";
  kdc::GlobalBoundHistory history(identity);

  kdc::TemporalPartition partition(0.0, 1.0);
  kdc::TemporalBoundRecordV1 lb =
      make_bound(1.0, kdc::TemporalBoundValidityV1::VALID,
                 kdc::TemporalBoundArithmeticV1::CONSERVATIVE_INTERVAL,
                 "lb-a");
  kdc::TemporalBoundRecordV1 ub =
      make_bound(5.0, kdc::TemporalBoundValidityV1::VALID,
                 kdc::TemporalBoundArithmeticV1::CONSERVATIVE_INTERVAL,
                 "ub-a");
  partition.set_leaf_bounds(
      1U, lb, ub,
      make_program(0.0, 1.0));
  const auto first =
      history.update(partition, 1U);

  REQUIRE(first.lb_current.valid);
  REQUIRE(first.lb_best.has_value());
  REQUIRE(first.lb_best->value >= 1.0 - 1e-12);
  REQUIRE(first.ub_current.valid);
  REQUIRE(first.ub_best.has_value());
  REQUIRE(first.ub_best->value <= 5.0 + 1e-12);

  kdc::TemporalPartition refined(0.0, 1.0);
  const auto split = refined.split_leaf(1U, 0.5);
  kdc::TemporalBoundRecordV1 lb_left =
      make_bound(0.6, kdc::TemporalBoundValidityV1::VALID,
                 kdc::TemporalBoundArithmeticV1::CONSERVATIVE_INTERVAL,
                 "lb-b");
  kdc::TemporalBoundRecordV1 lb_right =
      make_bound(0.6, kdc::TemporalBoundValidityV1::VALID,
                 kdc::TemporalBoundArithmeticV1::CONSERVATIVE_INTERVAL,
                 "lb-b");
  kdc::TemporalBoundRecordV1 ub_left =
      make_bound(2.0, kdc::TemporalBoundValidityV1::VALID,
                 kdc::TemporalBoundArithmeticV1::CONSERVATIVE_INTERVAL,
                 "ub-b");
  kdc::TemporalBoundRecordV1 ub_right =
      make_bound(2.0, kdc::TemporalBoundValidityV1::VALID,
                 kdc::TemporalBoundArithmeticV1::CONSERVATIVE_INTERVAL,
                 "ub-b");
  refined.set_leaf_bounds(
      split.first, lb_left, ub_left,
      make_program(0.0, 0.5));
  refined.set_leaf_bounds(
      split.second, lb_right, ub_right,
      make_program(0.5, 1.0));
  const auto second =
      history.update(refined, 2U);

  REQUIRE(second.lb_best.has_value());
  REQUIRE(second.lb_best->value >= first.lb_best->value);
  REQUIRE(second.ub_best.has_value());
  REQUIRE(second.ub_best->value <= first.ub_best->value);
}

TEST_CASE("LB and UB best can come from different partitions") {
  kdc::GlobalBoundIdentityV1 identity;
  kdc::GlobalBoundHistory history(identity);

  kdc::TemporalPartition first(0.0, 1.0);
  kdc::TemporalBoundRecordV1 lb1 =
      make_bound(1.0, kdc::TemporalBoundValidityV1::VALID,
                 kdc::TemporalBoundArithmeticV1::CONSERVATIVE_INTERVAL,
                 "lb-p1");
  kdc::TemporalBoundRecordV1 ub1 =
      make_bound(5.0, kdc::TemporalBoundValidityV1::VALID,
                 kdc::TemporalBoundArithmeticV1::CONSERVATIVE_INTERVAL,
                 "ub-p1");
  first.set_leaf_bounds(
      1U, lb1, ub1,
      make_program(0.0, 1.0));
  history.update(first, 1U);

  kdc::TemporalPartition second(0.0, 1.0);
  kdc::TemporalBoundRecordV1 lb2 =
      make_bound(2.0, kdc::TemporalBoundValidityV1::VALID,
                 kdc::TemporalBoundArithmeticV1::CONSERVATIVE_INTERVAL,
                 "lb-p2");
  kdc::TemporalBoundRecordV1 ub2 =
      make_bound(6.0, kdc::TemporalBoundValidityV1::VALID,
                 kdc::TemporalBoundArithmeticV1::CONSERVATIVE_INTERVAL,
                 "ub-p2");
  second.set_leaf_bounds(
      1U, lb2, ub2,
      make_program(0.0, 1.0));
  const auto result =
      history.update(second, 2U);

  REQUIRE(result.lb_best.has_value());
  REQUIRE(result.lb_best->partition_version == 2U);
  REQUIRE(result.ub_best.has_value());
  REQUIRE(result.ub_best->partition_version == 1U);
}

TEST_CASE("Certificate invalidation removes LB and UB history values") {
  kdc::GlobalBoundIdentityV1 identity;
  kdc::GlobalBoundHistory history(identity);

  kdc::TemporalPartition partition(0.0, 1.0);
  kdc::TemporalBoundRecordV1 lb =
      make_bound(3.0, kdc::TemporalBoundValidityV1::VALID,
                 kdc::TemporalBoundArithmeticV1::CONSERVATIVE_INTERVAL,
                 "shared-cert");
  kdc::TemporalBoundRecordV1 ub =
      make_bound(4.0, kdc::TemporalBoundValidityV1::VALID,
                 kdc::TemporalBoundArithmeticV1::CONSERVATIVE_INTERVAL,
                 "shared-cert");
  partition.set_leaf_bounds(
      1U, lb, ub,
      make_program(0.0, 1.0));
  const auto before =
      history.update(partition, 1U);
  REQUIRE(before.lb_best.has_value());
  REQUIRE(before.ub_best.has_value());

  REQUIRE(history.invalidate_certificate("shared-cert"));
  REQUIRE_FALSE(history.lb_best().has_value());
  REQUIRE_FALSE(history.ub_best().has_value());
}

TEST_CASE("Infinite UB has no complete incumbent snapshot") {
  kdc::GlobalBoundIdentityV1 identity;
  kdc::GlobalBoundHistory history(identity);

  kdc::TemporalPartition partition(0.0, 1.0);
  kdc::TemporalBoundRecordV1 lb =
      make_bound(1.0);
  kdc::TemporalBoundRecordV1 ub =
      make_bound(
          0.0,
          kdc::TemporalBoundValidityV1::ABSENT,
          kdc::TemporalBoundArithmeticV1::UNKNOWN,
          "");
  partition.set_leaf_bounds(
      1U, lb, ub, std::nullopt);
  const auto result =
      history.update(partition, 1U);

  REQUIRE_FALSE(result.ub_current.valid);
  REQUIRE(result.ub_current.value ==
          std::numeric_limits<double>::infinity());
  REQUIRE_FALSE(result.ub_best.has_value());
}

TEST_CASE("UB snapshot serializes and deserializes") {
  kdc::GlobalBoundIdentityV1 identity;
  identity.instance_hash = "sha256-instance";
  identity.solver_identity = "test-solver";
  identity.config_identity = "test-config";
  identity.numeric_backend = "conservative-test";
  identity.verifier_state = "passed";
  kdc::GlobalBoundHistory history(identity);

  kdc::TemporalPartition partition(0.0, 1.0);
  kdc::TemporalBoundRecordV1 lb =
      make_bound(1.0);
  kdc::TemporalBoundRecordV1 ub =
      make_bound(2.0,
                 kdc::TemporalBoundValidityV1::VALID,
                 kdc::TemporalBoundArithmeticV1::CONSERVATIVE_INTERVAL,
                 "snapshot-cert");
  partition.set_leaf_bounds(
      1U, lb, ub,
      make_program(0.0, 1.0));
  const auto result =
      history.update(partition, 7U);
  REQUIRE(result.ub_best.has_value());

  const std::string path =
      "/tmp/kdc-global-bound-history-test.json";
  history.serialize(path);
  const auto restored =
      kdc::GlobalBoundHistory::deserialize(path);
  REQUIRE(restored.ub_best().has_value());
  REQUIRE(restored.ub_best()->partition_version == 7U);
  REQUIRE(restored.ub_best()->snapshot.complete);
  REQUIRE(restored.ub_best()->snapshot.identity.instance_hash ==
          "sha256-instance");
  REQUIRE(restored.ub_best()->snapshot.leaves.size() == 1U);
  std::remove(path.c_str());
}
