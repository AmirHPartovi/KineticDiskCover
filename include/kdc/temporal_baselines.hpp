#pragma once

#include "kdc/solver_budget.hpp"
#include "kdc/temporal_thesis_solver.hpp"
#include "kdc/weighted_oracle.hpp"
#include "kdc/weighted_upper_envelope.hpp"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace kdc {

enum class TemporalBaselineKindV1 {
  B_NN,
  B_STATIC_HOLD,
  B_MINMAX
};

enum class TemporalBaselineStatusV1 {
  COMPLETE,
  CANDIDATE,
  OPTIMAL_AT_ZERO,
  TIME_LIMIT,
  ORACLE_CAP,
  INDETERMINATE,
  FAILED
};

enum class WeightedMinMaxTerminationV1 {
  OPTIMAL,
  FEASIBLE,
  TIME_LIMIT,
  ERROR
};

struct WeightedMinMaxOracleResultV1 {
  std::optional<std::vector<int>> assignment;
  bool incumbent_feasible{false};
  std::optional<Weight> objective;
  bool optimality_proved{false};
  WeightedMinMaxTerminationV1 termination{
      WeightedMinMaxTerminationV1::ERROR};
  std::string method{"none"};
};

class IWeightedMinMaxOracle {
 public:
  virtual ~IWeightedMinMaxOracle() = default;
  virtual WeightedMinMaxOracleResultV1 solve(
      const WeightMatrix& matrix,
      const std::vector<int>& station_ids,
      SolverBudget* budget = nullptr) = 0;
  virtual std::string name() const = 0;
};

class ColumnMinimumWeightedMinMaxOracle final
    : public IWeightedMinMaxOracle {
 public:
  WeightedMinMaxOracleResultV1 solve(
      const WeightMatrix& matrix,
      const std::vector<int>& station_ids,
      SolverBudget* budget = nullptr) override;
  std::string name() const override {
    return "column-minimum-minmax";
  }
};

struct TemporalBaselineDistanceTermV1 {
  int point_index{-1};
  Weight a{0.0};
  Weight b{0.0};
  Weight c{0.0};
};

struct TemporalBaselineStationProgramV1 {
  int station_index{-1};
  std::optional<Weight> fixed_radius_squared;
  std::vector<TemporalBaselineDistanceTermV1> envelope_terms;
};

struct TemporalBaselineProgramPieceV1 {
  double start{0.0};
  double end{0.0};
  std::vector<int> assignment;
  std::vector<TemporalBaselineStationProgramV1> stations;
};

struct TemporalBaselineBoundaryV1 {
  double time{0.0};
  std::vector<int> assignment;
  std::vector<Weight> station_radius_squared;
};

struct TemporalBaselineProgramV1 {
  std::uint32_t schema_version{1};
  bool complete{false};
  std::string boundary_ownership;
  std::vector<TemporalBaselineProgramPieceV1> pieces;
  std::vector<TemporalBaselineBoundaryV1> boundaries;
};

struct TemporalBaselineMinMaxSubproblemV1 {
  double start{0.0};
  double end{0.0};
  std::string termination{"ERROR"};
  bool optimality_proved{false};
  std::optional<Weight> minmax_objective;
  std::optional<Weight> minsum_program_cost;
  std::vector<int> assignment;
  std::string oracle;
  std::string matrix_validity{"FAST_FLOAT_ONLY"};
};

struct TemporalBaselineStaticSolveV1 {
  std::string termination{"NOT_RUN"};
  bool optimality_proved{false};
  std::string oracle{"none"};
  std::optional<Weight> objective_at_zero;
  std::vector<int> assignment;
};

struct TemporalBaselineResultV1 {
  std::uint32_t schema_version{1};
  TemporalBaselineKindV1 baseline{TemporalBaselineKindV1::B_NN};
  TemporalBaselineStatusV1 status{TemporalBaselineStatusV1::FAILED};
  std::string engineering_maturity{"experimental_numerical_candidate"};
  std::string cost_role{"MIN_SUM_FEASIBLE_PROGRAM_COST"};
  std::string cost_validity{"FAST_FLOAT_ONLY"};
  std::optional<Weight> cost;
  bool program_complete{false};
  std::string program_feasibility{"NOT_CONSTRUCTED"};
  bool minsum_ub_valid{false};
  std::uint64_t oracle_call_count{0};
  std::uint64_t assignment_switch_count{0};
  std::string failure_reason;
  TemporalBaselineProgramV1 program;
  TemporalBaselineStaticSolveV1 static_at_zero;
  std::vector<TemporalBaselineMinMaxSubproblemV1> minmax_subproblems;
};

struct TemporalBaselineConfigV1 {
  double global_time_limit_seconds{30.0};
  double oracle_time_limit_seconds{5.0};
  std::uint64_t max_oracle_calls{10000};
};

class TemporalBaselineSolver {
 public:
  explicit TemporalBaselineSolver(const Instance& instance);

  TemporalBaselineResultV1 solve_nn(
      const TemporalBaselineConfigV1& config = {}) const;

  TemporalBaselineResultV1 solve_static_hold(
      IWeightedExactOracle* exact_oracle,
      IWeightedHeuristicOracle* heuristic_oracle,
      const TemporalBaselineConfigV1& config = {}) const;

  TemporalBaselineResultV1 solve_minmax(
      IWeightedMinMaxOracle& minmax_oracle,
      const TemporalBaselineConfigV1& config = {}) const;

 private:
  Instance instance_;
};

std::string temporal_baseline_name(TemporalBaselineKindV1 baseline);
std::string temporal_baseline_status_name(
    TemporalBaselineStatusV1 status);
std::string weighted_minmax_termination_name(
    WeightedMinMaxTerminationV1 termination);
nlohmann::json temporal_baseline_result_to_json(
    const TemporalBaselineResultV1& result);
nlohmann::json temporal_baseline_failure_to_json(
    TemporalBaselineKindV1 baseline,
    const std::string& reason);

}  // namespace kdc
