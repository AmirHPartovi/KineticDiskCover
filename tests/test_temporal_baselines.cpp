#include "kdc/temporal_baselines.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace {
kdc::Instance moving_nearest_instance() {
  kdc::Instance instance;
  instance.n = 1;
  instance.m = 2;
  instance.T_end = 1.0;
  instance.stations = {
      {9, kdc::Point(0.0, 0.0)},
      {3, kdc::Point(1.0, 0.0)}};
  instance.trajectories = {
      kdc::Trajectory({0.0, 1.0},
                      {kdc::Point(0.0, 0.0),
                       kdc::Point(1.0, 0.0)})};
  return instance;
}

kdc::Instance static_hold_instance() {
  kdc::Instance instance;
  instance.n = 1;
  instance.m = 1;
  instance.T_end = 1.0;
  instance.stations = {{10, kdc::Point(0.0, 0.0)}};
  instance.trajectories = {
      kdc::Trajectory({0.0, 1.0},
                      {kdc::Point(0.0, 0.0),
                       kdc::Point(1.0, 0.0)})};
  return instance;
}

kdc::Instance fixed_minmax_instance() {
  kdc::Instance instance;
  instance.n = 2;
  instance.m = 2;
  instance.T_end = 1.0;
  instance.stations = {
      {20, kdc::Point(0.0, 0.0)},
      {10, kdc::Point(2.0, 0.0)}};
  instance.trajectories = {
      kdc::Trajectory({0.0, 1.0},
                      {kdc::Point(1.0, 0.0),
                       kdc::Point(1.0, 0.0)}),
      kdc::Trajectory({0.0, 1.0},
                      {kdc::Point(1.0, 0.0),
                       kdc::Point(1.0, 0.0)})};
  return instance;
}

class FixedExactOracle final : public kdc::IWeightedExactOracle {
 public:
  explicit FixedExactOracle(bool optimal) : optimal_(optimal) {}

  kdc::WeightedExactOracleResultV1 solve(
      const kdc::WeightMatrix&,
      kdc::SolverBudget*) override {
    kdc::WeightedExactOracleResultV1 result;
    result.assignment = std::vector<int>{0};
    result.incumbent_feasible = true;
    result.termination =
        optimal_ ? kdc::WeightedOracleTerminationStatusV1::OPTIMAL
                 : kdc::WeightedOracleTerminationStatusV1::FEASIBLE;
    result.optimality_certified = optimal_;
    result.solver_reports_optimal = optimal_;
    result.optimality_claim_possible = optimal_;
    result.arithmetic = kdc::WeightedArithmeticModeV1::DOUBLE;
    result.provenance.backend_name = "fixed-exact-test";
    return result;
  }

  std::string name() const override { return "fixed-exact-test"; }

 private:
  bool optimal_;
};
}  // namespace

TEST_CASE("B-nn splits at the exact nearest-station switch and ties by ID") {
  const auto instance = moving_nearest_instance();
  const kdc::TemporalBaselineSolver solver(instance);
  const auto result = solver.solve_nn();

  INFO(result.failure_reason);
  const kdc::WeightedUpperEnvelope envelope(instance);
  const auto left = envelope.solve(
      0.0, 0.5, {0}, kdc::UpperEnvelopeModeV1::FAST_DOUBLE);
  const auto right = envelope.solve(
      0.5, 1.0, {1}, kdc::UpperEnvelopeModeV1::FAST_DOUBLE);
  INFO(left.upper_bound);
  INFO(right.upper_bound);
  REQUIRE(result.status == kdc::TemporalBaselineStatusV1::COMPLETE);
  REQUIRE(result.program_complete);
  REQUIRE(result.program.pieces.size() == 2U);
  REQUIRE(result.program.pieces[0].end == 0.5);
  REQUIRE(result.program.pieces[1].start == 0.5);
  REQUIRE(result.program.pieces[0].assignment[0] == 0);
  REQUIRE(result.program.pieces[1].assignment[0] == 1);
  REQUIRE(result.program.boundaries.size() == 3U);
  REQUIRE(result.program.boundaries[1].time == 0.5);
  REQUIRE(result.program.boundaries[1].assignment[0] == 1);
  REQUIRE(result.assignment_switch_count == 1U);
  REQUIRE(result.cost.has_value());
  REQUIRE(std::abs(*result.cost - (std::acos(-1.0) / 12.0)) < 1e-12);
  REQUIRE_FALSE(result.minsum_ub_valid);
}

TEST_CASE("B-static-hold distinguishes optimal-at-zero from a candidate") {
  const kdc::TemporalBaselineSolver solver(static_hold_instance());
  FixedExactOracle candidate_oracle(false);
  FixedExactOracle optimal_oracle(true);
  kdc::GreedyWeightedHeuristicOracle heuristic;

  const auto candidate =
      solver.solve_static_hold(&candidate_oracle, &heuristic);
  INFO(candidate.failure_reason);
  REQUIRE(candidate.status == kdc::TemporalBaselineStatusV1::CANDIDATE);
  REQUIRE(candidate.program_complete);
  REQUIRE(candidate.static_at_zero.termination == "FEASIBLE");
  REQUIRE_FALSE(candidate.static_at_zero.optimality_proved);
  REQUIRE(candidate.cost.has_value());
  REQUIRE(std::abs(*candidate.cost - (std::acos(-1.0) / 3.0)) < 1e-12);

  const auto optimal =
      solver.solve_static_hold(&optimal_oracle, &heuristic);
  REQUIRE(optimal.status ==
          kdc::TemporalBaselineStatusV1::OPTIMAL_AT_ZERO);
  REQUIRE(optimal.static_at_zero.optimality_proved);
  REQUIRE(optimal.program_complete);

  const auto heuristic_only =
      solver.solve_static_hold(nullptr, &heuristic);
  REQUIRE(heuristic_only.status ==
          kdc::TemporalBaselineStatusV1::CANDIDATE);
  REQUIRE(heuristic_only.program_complete);
  REQUIRE_FALSE(heuristic_only.static_at_zero.optimality_proved);
}

TEST_CASE("B-minmax uses per-interval D-plus radii without a MinSum claim") {
  const kdc::TemporalBaselineSolver solver(fixed_minmax_instance());
  kdc::ColumnMinimumWeightedMinMaxOracle oracle;
  const auto result = solver.solve_minmax(oracle);

  INFO(result.failure_reason);
  REQUIRE(result.status == kdc::TemporalBaselineStatusV1::COMPLETE);
  REQUIRE(result.program_complete);
  REQUIRE(result.program.pieces.size() == 1U);
  REQUIRE(result.minmax_subproblems.size() == 1U);
  REQUIRE(result.minmax_subproblems[0].termination == "OPTIMAL");
  REQUIRE(result.minmax_subproblems[0].optimality_proved);
  REQUIRE(result.program.pieces[0].stations[0]
              .fixed_radius_squared.has_value());
  REQUIRE(result.program.pieces[0].stations[1]
              .fixed_radius_squared.has_value());
  REQUIRE(*result.program.pieces[0].stations[0]
               .fixed_radius_squared == 0.0);
  REQUIRE(*result.program.pieces[0].stations[1]
               .fixed_radius_squared == 1.0);
  REQUIRE(result.cost.has_value());
  REQUIRE(std::abs(*result.cost - std::acos(-1.0)) < 1e-12);
  REQUIRE(result.cost_role ==
          "MIN_SUM_COST_OF_MINMAX_CONSTRUCTED_PROGRAM");
  REQUIRE_FALSE(result.minsum_ub_valid);
}

TEST_CASE("Column MinMax oracle matches exhaustive assignments") {
  const kdc::WeightMatrix matrix(
      3U, 2U, {4.0, 1.0, 2.0, 3.0, 5.0, 0.0});
  const std::vector<int> station_ids{30, 10, 20};
  double reference = std::numeric_limits<double>::infinity();
  for (int first = 0; first < 3; ++first) {
    for (int second = 0; second < 3; ++second) {
      reference = std::min(
          reference,
          std::max(matrix.weight(static_cast<std::size_t>(first), 0U),
                   matrix.weight(static_cast<std::size_t>(second), 1U)));
    }
  }

  kdc::ColumnMinimumWeightedMinMaxOracle oracle;
  const auto result = oracle.solve(matrix, station_ids);
  REQUIRE(result.termination == kdc::WeightedMinMaxTerminationV1::OPTIMAL);
  REQUIRE(result.optimality_proved);
  REQUIRE(result.incumbent_feasible);
  REQUIRE(result.objective.has_value());
  REQUIRE(*result.objective == reference);
  REQUIRE(result.assignment == std::vector<int>{1, 2});
}
