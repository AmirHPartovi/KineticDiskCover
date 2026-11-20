#include "kdc/interval_evaluator.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <vector>

namespace {
kdc::Instance small_moving_instance() {
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

kdc::Instance small_stationary_instance() {
  kdc::Instance instance;
  instance.n = 1;
  instance.m = 1;
  instance.T_end = 1.0;
  instance.stations = {{10, kdc::Point(0.0, 0.0)}};
  instance.trajectories = {
      kdc::Trajectory(
          {0.0, 1.0},
          {kdc::Point(0.0, 0.0),
           kdc::Point(0.0, 0.0)})};
  return instance;
}

class IndependentLBExactMock final
    : public kdc::IWeightedExactOracle {
 public:
  kdc::Weight lower_bound{0.0};

  kdc::WeightedExactOracleResultV1 solve(
      const kdc::WeightMatrix&,
      kdc::SolverBudget*) override {
    kdc::WeightedExactOracleResultV1 result;
    result.termination =
        kdc::WeightedOracleTerminationStatusV1::OPTIMAL;
    result.solver_reports_optimal = true;
    result.independently_validated_lower_bound =
        lower_bound;
    result.independently_validated_lower_bound_validity =
        kdc::WeightedBoundValidityV1::VALID;
    result.optimality_claim_possible = true;
    result.optimality_certified = true;
    return result;
  }
  std::string name() const override {
    return "independent-lb-exact-mock";
  }
};

bool close_weight(kdc::Weight lhs, kdc::Weight rhs,
                  kdc::Weight tolerance = 1e-12) {
  return std::abs(lhs - rhs) <=
         tolerance * std::max(kdc::Weight{1.0},
                              std::abs(rhs));
}
}

TEST_CASE("Interval evaluator stationary example gives trivial LB and zero UB") {
  const kdc::Instance instance =
      small_stationary_instance();
  kdc::IntervalEvaluator evaluator(instance);
  kdc::IntervalEvaluatorConfigV1 config;
  config.arithmetic =
      kdc::IntervalEvaluatorArithmeticV1::FAST_DOUBLE;

  const auto result =
      evaluator.evaluate(
          0.0, 1.0,
          std::vector<int>({0}),
          config);
  INFO(result.failure_reason);
  INFO("direct_valid=" << result.envelope.direct_valid);
  INFO("fallback_valid=" << result.envelope.d_plus_fallback_valid);
  INFO("direct_integral=" << result.envelope.direct_integral);
  INFO("fallback_integral=" << result.envelope.d_plus_fallback_integral);
  INFO("upper=" << result.upper_bound);

  REQUIRE(result.status ==
          kdc::IntervalEvaluatorStatusV1::COMPLETE);
  REQUIRE(result.final_lb.valid);
  REQUIRE(result.final_lb.trivial);
  REQUIRE(result.final_lb.value == 0.0);
  REQUIRE(close_weight(result.upper_bound, 0.0));
  REQUIRE(result.assignment.has_value());
}

TEST_CASE("Interval evaluator moving example encloses reference integral") {
  const kdc::Instance instance =
      small_moving_instance();
  kdc::IntervalEvaluator evaluator(instance);
  kdc::IntervalEvaluatorConfigV1 config;
  config.arithmetic =
      kdc::IntervalEvaluatorArithmeticV1::FAST_DOUBLE;

  const auto result =
      evaluator.evaluate(
          0.0, 1.0,
          std::vector<int>({0}),
          config);
  const kdc::Weight reference =
      std::acos(-1.0) / 3.0;
  INFO(result.failure_reason);

  REQUIRE(result.status ==
          kdc::IntervalEvaluatorStatusV1::COMPLETE);
  REQUIRE(result.final_lb.valid);
  REQUIRE(result.final_lb.value <=
          reference + 1e-12);
  REQUIRE(result.upper_bound >=
          reference - 1e-12);
  REQUIRE(result.upper_method ==
          kdc::IntervalUpperMethodV1::DIRECT_UPPER_ENVELOPE);
  REQUIRE(result.assignment.has_value());
}

TEST_CASE("Interval evaluator rejects reconciliation LB greater than UB") {
  const kdc::Instance instance =
      small_moving_instance();
  kdc::IntervalEvaluator evaluator(instance);
  IndependentLBExactMock exact;
  exact.lower_bound = 10.0;
  kdc::IntervalEvaluatorConfigV1 config;
  config.exact_dminus = &exact;
  config.arithmetic =
      kdc::IntervalEvaluatorArithmeticV1::FAST_DOUBLE;

  const auto result =
      evaluator.evaluate(
          0.0, 1.0,
          std::vector<int>({0}),
          config);

  REQUIRE(result.status ==
          kdc::IntervalEvaluatorStatusV1::RECONCILIATION_FAILURE);
  REQUIRE(result.final_lb.value > result.upper_bound);
  REQUIRE_FALSE(result.failure_reason.empty());
}

TEST_CASE("Interval evaluator reports validated backend unavailable") {
  const kdc::Instance instance =
      small_moving_instance();
  kdc::IntervalEvaluator evaluator(instance);
  kdc::IntervalEvaluatorConfigV1 config;
  config.arithmetic =
      kdc::IntervalEvaluatorArithmeticV1::VALIDATED_NOT_AVAILABLE;

  const auto result =
      evaluator.evaluate(
          0.0, 1.0,
          std::vector<int>({0}),
          config);

  REQUIRE(result.status ==
          kdc::IntervalEvaluatorStatusV1::VALIDATED_UNAVAILABLE);
  REQUIRE(result.upper_bound ==
          std::numeric_limits<kdc::Weight>::infinity());
}
