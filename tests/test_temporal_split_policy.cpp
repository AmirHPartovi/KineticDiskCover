#include "kdc/temporal_split_policy.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <limits>
#include <vector>

namespace {
kdc::WeightMatrix dummy_weight_matrix() {
  return kdc::WeightMatrix(1U, 1U, {0.0});
}

class ContradictoryExactMock final
    : public kdc::IWeightedExactOracle {
 public:
  kdc::WeightedExactOracleResultV1 solve(
      const kdc::WeightMatrix&,
      kdc::SolverBudget*) override {
    kdc::WeightedExactOracleResultV1 result;
    result.termination =
        kdc::WeightedOracleTerminationStatusV1::OPTIMAL;
    result.solver_reports_optimal = true;
    result.independently_validated_lower_bound = 10.0;
    result.independently_validated_lower_bound_validity =
        kdc::WeightedBoundValidityV1::VALID;
    result.optimality_certified = true;
    result.incumbent_feasible = true;
    result.optimality_claim_possible = true;
    return result;
  }
  std::string name() const override {
    return "contradictory-exact-mock";
  }
};

kdc::TemporalBoundRecordV1 test_bound(
    double value,
    kdc::TemporalBoundValidityV1 validity =
        kdc::TemporalBoundValidityV1::VALID) {
  kdc::TemporalBoundRecordV1 bound;
  bound.value = value;
  bound.validity = validity;
  bound.method = "test";
  bound.arithmetic =
      kdc::TemporalBoundArithmeticV1::CONSERVATIVE_INTERVAL;
  return bound;
}

kdc::Instance moving_instance(double end = 1.0) {
  kdc::Instance instance;
  instance.n = 1;
  instance.m = 1;
  instance.T_end = end;
  instance.stations = {
      {10, kdc::Point(0.0, 0.0)}};
  instance.trajectories = {
      kdc::Trajectory(
          {0.0, end},
          {kdc::Point(0.0, 0.0),
           kdc::Point(end, 0.0)})};
  return instance;
}

kdc::Instance shared_breakpoint_instance() {
  kdc::Instance instance;
  instance.n = 1;
  instance.m = 1;
  instance.T_end = 1.0;
  instance.stations = {
      {10, kdc::Point(0.0, 0.0)}};
  instance.trajectories = {
      kdc::Trajectory(
          {0.0, 0.5, 1.0},
          {kdc::Point(0.0, 0.0),
           kdc::Point(0.5, 0.0),
           kdc::Point(1.0, 0.0)})};
  return instance;
}

kdc::TemporalPiecewiseProgramV1 test_program(
    double start,
    double end) {
  kdc::TemporalPiecewiseProgramV1 program;
  program.point_assignment = {0};
  kdc::TemporalRadiusPieceV1 piece;
  piece.start = start;
  piece.end = end;
  piece.station_radii = {0.0};
  program.pieces.push_back(piece);
  return program;
}
}

TEST_CASE("Leaf priority uses gap, depth, length, and start tie key") {
  kdc::IntervalRecordV1 first;
  first.leaf_id = 1;
  first.depth = 1;
  first.delta = 0.5;
  first.start = 0.2;
  first.lb = test_bound(1.0);
  first.ub = test_bound(3.0);
  kdc::IntervalRecordV1 second = first;
  second.leaf_id = 2;
  second.depth = 0;
  second.delta = 0.5;
  second.start = 0.1;

  const auto first_key =
      kdc::TemporalSplittingPolicy::priority(first);
  const auto second_key =
      kdc::TemporalSplittingPolicy::priority(second);
  REQUIRE(first_key.gap == 2.0);
  REQUIRE(kdc::TemporalSplittingPolicy::higher_priority(
              second_key, first_key));
}

TEST_CASE("B-grid selects longest leaf and ties by start then leaf ID") {
  const kdc::Instance instance = moving_instance(1.0);
  kdc::TemporalSplitConfigV1 config;
  config.selection = kdc::TemporalLeafSelectionV1::LONGEST_INTERVAL;
  kdc::TemporalSplittingPolicy policy(instance, config);

  kdc::TemporalPartition uneven(0.0, 1.0);
  const auto uneven_children = uneven.split_leaf(1U, 0.25);
  auto& short_leaf =
      const_cast<kdc::IntervalRecordV1&>(
          uneven.leaf(uneven_children.first));
  auto& long_leaf =
      const_cast<kdc::IntervalRecordV1&>(
          uneven.leaf(uneven_children.second));
  short_leaf.lb = test_bound(0.0);
  short_leaf.ub = test_bound(100.0);
  long_leaf.lb = test_bound(0.0);
  long_leaf.ub = test_bound(1.0);
  REQUIRE(policy.next_leaf(uneven) == uneven_children.second);

  kdc::TemporalPartition partition(0.0, 1.0);
  const auto children = partition.split_leaf(1U, 0.5);

  REQUIRE(partition.queue().size() == 2U);
  REQUIRE(policy.next_leaf(partition) == children.first);
}

TEST_CASE("Minimum-length guard freezes instead of splitting") {
  kdc::Instance instance =
      moving_instance(0.001);
  kdc::TemporalSplitConfigV1 config;
  config.delta_min = 0.001;
  kdc::TemporalSplittingPolicy policy(instance, config);
  kdc::TemporalPartition partition(0.0, 0.001);
  kdc::IntervalRecordV1& leaf =
      const_cast<kdc::IntervalRecordV1&>(
          partition.leaf(1U));
  leaf.lb = test_bound(0.0);
  leaf.ub = test_bound(0.001);
  leaf.ub_piecewise_program =
      test_program(0.0, 0.001);

  const auto attempt =
      policy.attempt_split(partition, 1U);
  REQUIRE(attempt.outcome ==
          kdc::TemporalSplitOutcomeV1::MIN_INTERVAL);
  REQUIRE(partition.leaf(1U).status ==
          kdc::TemporalLeafStatusV1::FROZEN);
  REQUIRE(partition.leaf(1U).freeze_reason ==
          "MIN_INTERVAL");
  REQUIRE(partition.queue().empty());
}

TEST_CASE("Midpoint collapse freezes the leaf without losing partition coverage") {
  const double start = 1.0;
  const double end = std::nextafter(
      start, std::numeric_limits<double>::infinity());
  kdc::Instance instance = moving_instance(end);
  kdc::TemporalSplitConfigV1 config;
  config.delta_min = std::numeric_limits<double>::denorm_min();
  kdc::TemporalSplittingPolicy policy(instance, config);
  kdc::TemporalPartition partition(start, end);
  kdc::IntervalRecordV1& leaf =
      const_cast<kdc::IntervalRecordV1&>(
          partition.leaf(1U));
  leaf.lb = test_bound(0.0);
  leaf.ub = test_bound(1.0);
  leaf.ub_piecewise_program = test_program(start, end);

  const auto attempt =
      policy.attempt_split(partition, 1U);
  REQUIRE(attempt.outcome ==
          kdc::TemporalSplitOutcomeV1::MIDPOINT_COLLAPSE);
  REQUIRE(partition.leaf(1U).status ==
          kdc::TemporalLeafStatusV1::FROZEN);
  REQUIRE(partition.leaf(1U).freeze_reason ==
          "MIDPOINT_COLLAPSE");
  REQUIRE(partition.queue().empty());
  REQUIRE(partition.partition().size() == 1U);
  REQUIRE(partition.validate().valid);
}

TEST_CASE("Shared trajectory breakpoint is never moved") {
  kdc::Instance instance =
      shared_breakpoint_instance();
  kdc::TemporalSplitConfigV1 config;
  config.delta_min = 0.1;
  kdc::TemporalSplittingPolicy policy(instance, config);
  kdc::TemporalPartition partition(0.0, 1.0);
  kdc::IntervalRecordV1& leaf =
      const_cast<kdc::IntervalRecordV1&>(
          partition.leaf(1U));
  leaf.lb = test_bound(0.0);
  leaf.ub = test_bound(1.0);

  const auto attempt =
      policy.attempt_split(partition, 1U);
  REQUIRE(attempt.outcome ==
          kdc::TemporalSplitOutcomeV1::SHARED_BREAKPOINT);
  REQUIRE(partition.leaf(1U).status ==
          kdc::TemporalLeafStatusV1::FROZEN);
  REQUIRE(partition.leaf(1U).freeze_reason ==
          "SHARED_BREAKPOINT");
  REQUIRE(partition.queue().empty());
}

TEST_CASE("Successful split preserves parent until transaction and updates P/Q") {
  kdc::Instance instance =
      moving_instance(1.0);
  kdc::TemporalSplitConfigV1 config;
  config.delta_min = 0.1;
  kdc::TemporalSplittingPolicy policy(instance, config);
  kdc::TemporalPartition partition(0.0, 1.0);
  kdc::IntervalRecordV1& leaf =
      const_cast<kdc::IntervalRecordV1&>(
          partition.leaf(1U));
  leaf.lb = test_bound(0.0);
  leaf.ub = test_bound(
      std::numeric_limits<double>::infinity(),
      kdc::TemporalBoundValidityV1::INVALID);
  leaf.ub_piecewise_program =
      test_program(0.0, 1.0);

  const auto attempt =
      policy.attempt_split(partition, 1U);
  REQUIRE(attempt.outcome ==
          kdc::TemporalSplitOutcomeV1::SPLIT);
  REQUIRE(partition.partition().size() == 2U);
  REQUIRE(partition.queue().size() == 2U);
  REQUIRE(partition.history().at(1U).status ==
          kdc::TemporalLeafStatusV1::FROZEN);
  REQUIRE(partition.leaf(attempt.left_id).start == 0.0);
  REQUIRE(partition.leaf(attempt.left_id).end == 0.5);
  REQUIRE(partition.leaf(attempt.right_id).start == 0.5);
  REQUIRE(partition.leaf(attempt.right_id).end == 1.0);
  REQUIRE(partition.validate().valid);
}

TEST_CASE("Structural rollback can restore parent P/Q") {
  kdc::TemporalPartition partition(0.0, 1.0);
  kdc::IntervalRecordV1 parent =
      partition.leaf(1U);
  const auto split = partition.split_leaf(1U, 0.5);
  partition.rollback_split(
      1U,
      split.first,
      split.second,
      parent,
      "VERIFY_FAIL");

  REQUIRE(partition.partition().size() == 1U);
  REQUIRE(partition.queue().size() == 0U);
  REQUIRE(partition.leaf(1U).status ==
          kdc::TemporalLeafStatusV1::FROZEN);
  REQUIRE(partition.leaf(1U).freeze_reason ==
          "VERIFY_FAIL");
  REQUIRE(partition.validate().valid);
}

TEST_CASE("Split policy freezes on missing child UB with ORACLE_FAIL") {
  kdc::Instance instance =
      moving_instance(1.0);
  kdc::TemporalSplitConfigV1 config;
  config.delta_min = 0.1;
  kdc::TemporalSplittingPolicy policy(instance, config);
  kdc::TemporalPartition partition(0.0, 1.0);
  kdc::IntervalRecordV1& leaf =
      const_cast<kdc::IntervalRecordV1&>(
          partition.leaf(1U));
  leaf.lb = test_bound(0.0);
  leaf.ub = test_bound(
      1.0, kdc::TemporalBoundValidityV1::VALID);

  const auto attempt =
      policy.attempt_split(partition, 1U);
  REQUIRE(attempt.outcome ==
          kdc::TemporalSplitOutcomeV1::ORACLE_FAIL);
  REQUIRE(partition.leaf(1U).freeze_reason ==
          "ORACLE_FAIL");
  REQUIRE(partition.queue().empty());
  REQUIRE(partition.validate().valid);
}

TEST_CASE("Missing child LB does not prevent successful split") {
  kdc::Instance instance =
      moving_instance(1.0);
  kdc::TemporalSplitConfigV1 config;
  config.delta_min = 0.1;
  kdc::TemporalSplittingPolicy policy(instance, config);
  kdc::TemporalPartition partition(0.0, 1.0);
  kdc::IntervalRecordV1& leaf =
      const_cast<kdc::IntervalRecordV1&>(
          partition.leaf(1U));
  leaf.lb = test_bound(
      0.0,
      kdc::TemporalBoundValidityV1::ABSENT);
  leaf.ub = test_bound(
      std::numeric_limits<double>::infinity(),
      kdc::TemporalBoundValidityV1::INVALID);
  leaf.ub_piecewise_program =
      test_program(0.0, 1.0);

  const auto attempt =
      policy.attempt_split(partition, 1U);
  REQUIRE(attempt.outcome ==
          kdc::TemporalSplitOutcomeV1::SPLIT);
  REQUIRE(partition.partition().size() == 2U);
  REQUIRE(partition.queue().size() == 2U);
  REQUIRE(partition.leaf(attempt.left_id).lb.method ==
          "TRIVIAL_NONNEGATIVE");
  REQUIRE(partition.leaf(attempt.right_id).lb.method ==
          "TRIVIAL_NONNEGATIVE");
}

TEST_CASE("Split policy freezes VERIFY_FAIL on LB-UB contradiction") {
  kdc::Instance instance =
      moving_instance(1.0);
  kdc::TemporalSplitConfigV1 config;
  ContradictoryExactMock exact;
  config.evaluator.exact_dminus = &exact;
  config.delta_min = 0.1;
  kdc::TemporalSplittingPolicy policy(instance, config);
  kdc::TemporalPartition partition(0.0, 1.0);
  kdc::IntervalRecordV1& leaf =
      const_cast<kdc::IntervalRecordV1&>(
          partition.leaf(1U));
  leaf.lb = test_bound(0.0);
  leaf.ub = test_bound(
      std::numeric_limits<double>::infinity(),
      kdc::TemporalBoundValidityV1::INVALID);
  leaf.ub_piecewise_program =
      test_program(0.0, 1.0);

  const auto attempt =
      policy.attempt_split(partition, 1U);
  REQUIRE(attempt.outcome ==
          kdc::TemporalSplitOutcomeV1::VERIFY_FAIL);
  REQUIRE(partition.leaf(1U).freeze_reason ==
          "VERIFY_FAIL");
  REQUIRE(partition.queue().empty());
}
