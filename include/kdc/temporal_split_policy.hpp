#pragma once

#include "kdc/interval_evaluator.hpp"
#include "kdc/temporal_partition.hpp"

#include <optional>
#include <string>

namespace kdc {

enum class TemporalLeafSelectionV1 {
  GREATEST_GAP,
  LONGEST_INTERVAL
};

struct TemporalSplitConfigV1 {
  double delta_min{1e-3};
  IntervalEvaluatorConfigV1 evaluator;
  TemporalLeafSelectionV1 selection{
      TemporalLeafSelectionV1::GREATEST_GAP};
};

enum class TemporalSplitOutcomeV1 {
  SPLIT,
  NO_ACTION,
  MIN_INTERVAL,
  MIDPOINT_COLLAPSE,
  SHARED_BREAKPOINT,
  ORACLE_FAIL,
  VERIFY_FAIL,
  PARTITION_INVALID
};

struct TemporalSplitAttemptV1 {
  TemporalSplitOutcomeV1 outcome{
      TemporalSplitOutcomeV1::NO_ACTION};
  TemporalLeafId parent_id{0};
  TemporalLeafId left_id{0};
  TemporalLeafId right_id{0};
  std::string reason;
};

struct TemporalLeafPriorityV1 {
  TemporalLeafId leaf_id{0};
  double gap{0.0};
  unsigned depth{0};
  double delta{0.0};
  double start{0.0};
};

class TemporalSplittingPolicy {
 public:
  explicit TemporalSplittingPolicy(
      const Instance& instance,
      TemporalSplitConfigV1 config);

  static TemporalLeafPriorityV1 priority(
      const IntervalRecordV1& leaf);
  static bool higher_priority(
      const TemporalLeafPriorityV1& lhs,
      const TemporalLeafPriorityV1& rhs);

  TemporalLeafId next_leaf(
      const TemporalPartition& partition) const;

  TemporalSplitAttemptV1 attempt_split(
      TemporalPartition& partition,
      TemporalLeafId parent_id,
      SolverBudget* budget = nullptr);

 private:
  Instance instance_;
  WeightedGeometry geometry_;
  TemporalSplitConfigV1 config_;

  bool is_shared_breakpoint(double midpoint) const;
};

}  // namespace kdc
