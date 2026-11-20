#include "kdc/temporal_partition.hpp"

#include <catch2/catch_test_macros.hpp>

#include <map>
#include <set>

namespace {
kdc::TemporalBoundRecordV1 bound(double value,
                                 kdc::TemporalBoundValidityV1 validity =
                                     kdc::TemporalBoundValidityV1::VALID) {
  kdc::TemporalBoundRecordV1 result;
  result.value = value;
  result.validity = validity;
  result.method = "test";
  return result;
}

kdc::IntervalRecordV1 make_leaf(kdc::TemporalLeafId id,
                                double start,
                                double end) {
  kdc::IntervalRecordV1 leaf;
  leaf.leaf_id = id;
  leaf.start = start;
  leaf.end = end;
  leaf.midpoint = start + (end - start) / 2.0;
  leaf.delta = end - start;
  leaf.depth = 0;
  leaf.status = kdc::TemporalLeafStatusV1::OPEN;
  leaf.lb = bound(0.0);
  leaf.ub = bound(1.0);
  return leaf;
}
}

TEST_CASE("Temporal partition constructs one P/Q leaf and covers domain") {
  kdc::TemporalPartition partition(0.0, 1.0);

  REQUIRE(partition.partition().size() == 1U);
  REQUIRE(partition.queue().size() == 1U);
  REQUIRE(partition.queue().count(1U) == 1U);
  REQUIRE(partition.validate().valid);

  const auto& leaf = partition.leaf(1U);
  REQUIRE(leaf.start == 0.0);
  REQUIRE(leaf.end == 1.0);
  REQUIRE(leaf.midpoint == 0.5);
  REQUIRE(leaf.delta == 1.0);
  REQUIRE(leaf.depth == 0U);
  REQUIRE(partition.boundary_ownership() ==
          kdc::TemporalBoundaryOwnershipV1::LOWER_INTERVAL);
}

TEST_CASE("Structural split replaces OPEN leaf and updates P/Q") {
  kdc::TemporalPartition partition(0.0, 1.0);
  const auto split = partition.split_leaf(1U, 0.25);

  REQUIRE(partition.partition().size() == 2U);
  REQUIRE(partition.queue().size() == 2U);
  REQUIRE(partition.queue().count(split.first) == 1U);
  REQUIRE(partition.queue().count(split.second) == 1U);

  const auto& left = partition.leaf(split.first);
  const auto& right = partition.leaf(split.second);
  REQUIRE(left.start == 0.0);
  REQUIRE(left.end == 0.25);
  REQUIRE(right.start == 0.25);
  REQUIRE(right.end == 1.0);
  REQUIRE(left.parent_id == 1U);
  REQUIRE(right.parent_id == 1U);
  REQUIRE(left.midpoint == 0.125);
  REQUIRE(right.midpoint == 0.625);
  REQUIRE(partition.history().at(1U).status ==
          kdc::TemporalLeafStatusV1::FROZEN);
  REQUIRE(partition.history().at(1U).freeze_reason == "split");
  REQUIRE(partition.validate().valid);
}

TEST_CASE("Temporal split rejects endpoints and non-OPEN leaves") {
  kdc::TemporalPartition partition(0.0, 1.0);
  REQUIRE_THROWS_AS(partition.split_leaf(1U, 0.0),
                    std::invalid_argument);
  REQUIRE_THROWS_AS(partition.split_leaf(1U, 1.0),
                    std::invalid_argument);
  REQUIRE_THROWS_AS(partition.split_leaf(999U, 0.5),
                    std::out_of_range);

  partition.freeze_leaf(1U, "test freeze");
  REQUIRE_THROWS_AS(partition.split_leaf(1U, 0.5),
                    std::invalid_argument);
}

TEST_CASE("FROZEN leaves remain P and leave Q") {
  kdc::TemporalPartition partition(0.0, 1.0);
  partition.freeze_leaf(1U, "oracle cap");

  REQUIRE(partition.partition().size() == 1U);
  REQUIRE(partition.queue().empty());
  REQUIRE(partition.leaf(1U).status ==
          kdc::TemporalLeafStatusV1::FROZEN);
  REQUIRE(partition.leaf(1U).freeze_reason == "oracle cap");
  REQUIRE(partition.validate().valid);
}

TEST_CASE("FATHOMED requires finite valid equal LB and UB") {
  kdc::TemporalPartition partition(0.0, 1.0);
  kdc::TemporalBoundRecordV1 lb = bound(2.0);
  kdc::TemporalBoundRecordV1 ub = bound(3.0);
  partition.set_leaf_bounds(1U, lb, ub, std::nullopt);
  REQUIRE_THROWS_AS(partition.fathom_leaf(1U),
                    std::invalid_argument);

  ub = bound(2.0);
  partition.set_leaf_bounds(1U, lb, ub, std::nullopt);
  partition.fathom_leaf(1U);
  REQUIRE(partition.partition().size() == 1U);
  REQUIRE(partition.queue().empty());
  REQUIRE(partition.leaf(1U).status ==
          kdc::TemporalLeafStatusV1::FATHOMED);
  REQUIRE(partition.validate().valid);
}

TEST_CASE("Static validator rejects empty invalid and inconsistent P") {
  std::map<kdc::TemporalLeafId, kdc::IntervalRecordV1> empty;
  std::set<kdc::TemporalLeafId> empty_queue;
  REQUIRE_FALSE(kdc::TemporalPartition::validate_partition(
                    0.0, 1.0, empty, empty_queue).valid);

  auto invalid_length =
      make_leaf(1U, 0.5, 0.5);
  std::map<kdc::TemporalLeafId, kdc::IntervalRecordV1> invalid_p{
      {1U, invalid_length}};
  REQUIRE_FALSE(kdc::TemporalPartition::validate_partition(
                    0.0, 1.0, invalid_p, empty_queue).valid);

  auto left = make_leaf(1U, 0.0, 0.4);
  auto right = make_leaf(2U, 0.5, 1.0);
  std::map<kdc::TemporalLeafId, kdc::IntervalRecordV1> gap_p{
      {1U, left}, {2U, right}};
  std::set<kdc::TemporalLeafId> gap_q{1U, 2U};
  REQUIRE_FALSE(kdc::TemporalPartition::validate_partition(
                    0.0, 1.0, gap_p, gap_q).valid);

  auto overlap_right = make_leaf(2U, 0.3, 1.0);
  std::map<kdc::TemporalLeafId, kdc::IntervalRecordV1> overlap_p{
      {1U, left}, {2U, overlap_right}};
  std::set<kdc::TemporalLeafId> overlap_q{1U, 2U};
  REQUIRE_FALSE(kdc::TemporalPartition::validate_partition(
                    0.0, 1.0, overlap_p, overlap_q).valid);
}

TEST_CASE("Q is always a subset of P and OPEN") {
  kdc::TemporalPartition partition(0.0, 1.0);
  REQUIRE(partition.validate().valid);

  std::map<kdc::TemporalLeafId, kdc::IntervalRecordV1> empty_p;
  std::set<kdc::TemporalLeafId> stale_q{999U};
  REQUIRE_FALSE(kdc::TemporalPartition::validate_partition(
                    0.0, 1.0, empty_p, stale_q).valid);
}
