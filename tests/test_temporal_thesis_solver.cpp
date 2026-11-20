#include "kdc/temporal_thesis_solver.hpp"

#include <catch2/catch_test_macros.hpp>

#include <limits>
#include <vector>

namespace {
kdc::Instance moving_instance() {
  kdc::Instance instance;
  instance.n = 1;
  instance.m = 1;
  instance.T_end = 1.0;
  instance.stations = {
      {10, kdc::Point(0.0, 0.0)}};
  instance.trajectories = {
      kdc::Trajectory(
          {0.0, 1.0},
          {kdc::Point(0.0, 0.0),
           kdc::Point(1.0, 0.0)})};
  return instance;
}

kdc::Instance breakpoint_instance() {
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

class UnavailableExactForBudget final
    : public kdc::IWeightedExactOracle {
 public:
  kdc::WeightedExactOracleResultV1 solve(
      const kdc::WeightMatrix&,
      kdc::SolverBudget*) override {
    return {};
  }
  std::string name() const override {
    return "unavailable-exact-for-budget-test";
  }
};
}

TEST_CASE("Temporal thesis solver executes a small synthetic split") {
  kdc::Instance instance =
      moving_instance();
  kdc::TemporalThesisSolver solver(instance);
  kdc::IntervalEvaluatorConfigV1 evaluator_config;
  evaluator_config.arithmetic =
      kdc::IntervalEvaluatorArithmeticV1::FAST_DOUBLE;
  kdc::TemporalThesisConfigV1 config;
  config.global_time_limit_seconds = 1.0;
  config.oracle_time_limit_seconds = 0.5;
  config.delta_min = 0.1;
  config.max_leaves = 2;
  config.max_oracle_calls = 100;

  const auto result =
      solver.solve(evaluator_config, config);
  REQUIRE(result.leaf_count >= 1U);
  REQUIRE(result.status ==
          kdc::TemporalThesisStatusV1::MAX_LEAVES);
  REQUIRE(result.oracle_call_count >= 1U);
  REQUIRE(result.partition_version >= 1U);
  REQUIRE_FALSE(result.verified);
  REQUIRE(result.verifier_status.find(
              "validated arithmetic is unavailable") !=
          std::string::npos);
  REQUIRE(result.leaves.size() == result.leaf_count);
  REQUIRE(result.global_bounds.ub_current.value ==
          std::numeric_limits<double>::infinity());
  REQUIRE_FALSE(result.global_bounds.ub_current.valid);
  REQUIRE(result.leaves.size() == 2U);
  for (const auto& leaf : result.leaves) {
    REQUIRE(leaf.ub.validity ==
            kdc::TemporalBoundValidityV1::NUMERICALLY_UNBOUNDED);
    REQUIRE(leaf.ub_piecewise_program.has_value());
  }
}

TEST_CASE("Temporal thesis solver starts from the common breakpoint partition") {
  kdc::TemporalThesisSolver solver(breakpoint_instance());
  kdc::IntervalEvaluatorConfigV1 evaluator_config;
  kdc::TemporalThesisConfigV1 config;
  config.global_time_limit_seconds = 1.0;
  config.oracle_time_limit_seconds = 0.5;
  config.delta_min = 0.1;
  config.max_leaves = 2U;

  const auto result =
      solver.solve(evaluator_config, config);
  REQUIRE(result.status ==
          kdc::TemporalThesisStatusV1::MAX_LEAVES);
  REQUIRE(result.leaves.size() == 2U);
  REQUIRE(result.leaves[0].start == 0.0);
  REQUIRE(result.leaves[0].end == 0.5);
  REQUIRE(result.leaves[1].start == 0.5);
  REQUIRE(result.leaves[1].end == 1.0);
}

TEST_CASE("Temporal thesis solver returns time-limit before leaf evaluation") {
  kdc::Instance instance =
      moving_instance();
  kdc::TemporalThesisSolver solver(instance);
  kdc::IntervalEvaluatorConfigV1 evaluator_config;
  kdc::TemporalThesisConfigV1 config;
  config.global_time_limit_seconds =
      1e-9;
  config.oracle_time_limit_seconds =
      1e-9;
  config.delta_min = 0.1;
  config.max_leaves = 10;

  const auto result =
      solver.solve(evaluator_config, config);
  REQUIRE(result.status ==
          kdc::TemporalThesisStatusV1::TIME_LIMIT);
  REQUIRE(result.leaf_count >= 1U);
  REQUIRE_FALSE(result.stop_reason.empty());
  REQUIRE(result.leaves.size() == 1U);
  REQUIRE(result.leaves.front().status ==
          kdc::TemporalLeafStatusV1::FROZEN);
  REQUIRE(result.leaves.front().freeze_reason == "TIME_LIMIT");
  REQUIRE(result.leaves.front().lb.validity ==
          kdc::TemporalBoundValidityV1::VALID);
  REQUIRE(result.leaves.front().lb.value == 0.0);
  REQUIRE(result.leaves.front().ub.value ==
          std::numeric_limits<double>::infinity());
  REQUIRE_FALSE(result.leaves.front().ub_piecewise_program.has_value());
}

TEST_CASE("Temporal thesis solver enforces the per-Oracle time limit") {
  kdc::TemporalThesisSolver solver(moving_instance());
  kdc::IntervalEvaluatorConfigV1 evaluator_config;
  kdc::TemporalThesisConfigV1 config;
  config.global_time_limit_seconds = 1.0;
  config.oracle_time_limit_seconds = 1e-12;
  config.delta_min = 0.1;

  const auto result =
      solver.solve(evaluator_config, config);
  REQUIRE(result.status ==
          kdc::TemporalThesisStatusV1::ORACLE_TIME_LIMIT);
  REQUIRE(result.leaves.size() == 1U);
  REQUIRE(result.leaves.front().freeze_reason ==
          "ORACLE_TIME_LIMIT");
  REQUIRE_FALSE(result.leaves.front().ub_piecewise_program.has_value());
}

TEST_CASE("Temporal thesis solver enforces Oracle-call cap before evaluation") {
  kdc::TemporalThesisSolver solver(moving_instance());
  UnavailableExactForBudget exact;
  kdc::IntervalEvaluatorConfigV1 evaluator_config;
  evaluator_config.exact_dminus = &exact;
  kdc::TemporalThesisConfigV1 config;
  config.max_oracle_calls = 1U;
  config.global_time_limit_seconds = 1.0;
  config.oracle_time_limit_seconds = 0.5;

  const auto result =
      solver.solve(evaluator_config, config);
  REQUIRE(result.status ==
          kdc::TemporalThesisStatusV1::ORACLE_CAP);
  REQUIRE(result.oracle_call_count == 1U);
  REQUIRE(result.leaves.size() == 1U);
  REQUIRE(result.leaves.front().freeze_reason == "ORACLE_CAP");
  REQUIRE(result.leaves.front().ub.value ==
          std::numeric_limits<double>::infinity());
}
