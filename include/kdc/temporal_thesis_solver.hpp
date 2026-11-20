#pragma once

#include "kdc/global_bound_history.hpp"
#include "kdc/interval_evaluator.hpp"
#include "kdc/solver_budget.hpp"
#include "kdc/temporal_split_policy.hpp"
#include "kdc/temporal_result_v2.hpp"
#include "kdc/types.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace kdc {

enum class TemporalThesisStatusV1 {
  COMPLETE,
  INCOMPLETE,
  TIME_LIMIT,
  ORACLE_TIME_LIMIT,
  MAX_LEAVES,
  ORACLE_CAP,
  RECONCILIATION_FAILURE,
  INCONSISTENT,
  INVALID,
  FAILED
};

struct TemporalThesisConfigV1 {
  double global_time_limit_seconds{30.0};
  double oracle_time_limit_seconds{5.0};
  double delta_min{1e-3};
  std::uint64_t max_leaves{4096};
  std::uint64_t max_oracle_calls{10000};
  bool verify_snapshot{false};
  TemporalLeafSelectionV1 split_selection{
      TemporalLeafSelectionV1::GREATEST_GAP};
};

struct TemporalThesisResultV1 {
  std::uint32_t schema_version{1};
  TemporalThesisStatusV1 status{
      TemporalThesisStatusV1::INCOMPLETE};
  std::string stop_reason;
  std::uint64_t leaf_count{0};
  std::uint64_t oracle_call_count{0};
  std::uint64_t partition_version{0};
  GlobalBoundHistoryResultV1 global_bounds;
  std::vector<IntervalRecordV1> leaves;
  bool verified{false};
  std::string verifier_status;
  double elapsed_seconds{0.0};
};

struct TemporalThesisConfigV2 {
  TemporalThesisConfigV1 execution;
  double epsilon_abs{1e-2};
  double epsilon_rel{0.0};
};

struct TemporalThesisResultV2 {
  std::uint32_t schema_version{2};
  TemporalFinalResultV2 final;
  TemporalThesisResultV1 execution;
};

class TemporalThesisSolver {
 public:
  explicit TemporalThesisSolver(const Instance& instance);

  TemporalThesisResultV1 solve(
      const IntervalEvaluatorConfigV1& evaluator_config,
      const TemporalThesisConfigV1& config);

  TemporalThesisResultV2 solve_v2(
      const IntervalEvaluatorConfigV1& evaluator_config,
      const TemporalThesisConfigV2& config);

 private:
  TemporalThesisResultV1 solve_impl(
      const IntervalEvaluatorConfigV1& evaluator_config,
      const TemporalThesisConfigV1& config,
      TemporalOracleStatisticsV2* oracle_statistics);

  Instance instance_;
};

}  // namespace kdc
