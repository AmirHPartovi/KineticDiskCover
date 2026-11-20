#include "kdc/temporal_split_policy.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace kdc {

TemporalSplittingPolicy::TemporalSplittingPolicy(
    const Instance& instance,
    TemporalSplitConfigV1 config)
    : instance_(instance),
      geometry_(instance),
      config_(std::move(config)) {}

TemporalLeafPriorityV1
TemporalSplittingPolicy::priority(
    const IntervalRecordV1& leaf) {
  TemporalLeafPriorityV1 result;
  result.leaf_id = leaf.leaf_id;
  result.depth = leaf.depth;
  result.delta = leaf.delta;
  result.start = leaf.start;
  if (leaf.lb.validity ==
          TemporalBoundValidityV1::VALID &&
      leaf.ub.validity ==
          TemporalBoundValidityV1::VALID &&
      std::isfinite(leaf.lb.value) &&
      std::isfinite(leaf.ub.value)) {
    result.gap =
        std::max(
            0.0,
            leaf.ub.value - leaf.lb.value);
  } else {
    result.gap =
        std::numeric_limits<double>::infinity();
  }
  return result;
}

bool TemporalSplittingPolicy::higher_priority(
    const TemporalLeafPriorityV1& lhs,
    const TemporalLeafPriorityV1& rhs) {
  if (lhs.gap != rhs.gap) {
    return lhs.gap > rhs.gap;
  }
  if (lhs.depth != rhs.depth) {
    return lhs.depth < rhs.depth;
  }
  if (lhs.delta != rhs.delta) {
    return lhs.delta > rhs.delta;
  }
  if (lhs.start != rhs.start) {
    return lhs.start < rhs.start;
  }
  return lhs.leaf_id < rhs.leaf_id;
}

TemporalLeafId TemporalSplittingPolicy::next_leaf(
    const TemporalPartition& partition) const {
  const IntervalRecordV1* best = nullptr;
  for (const TemporalLeafId id :
       partition.queue()) {
    const IntervalRecordV1& leaf =
        partition.leaf(id);
    if (best == nullptr) {
      best = &leaf;
      continue;
    }
    if (config_.selection == TemporalLeafSelectionV1::LONGEST_INTERVAL) {
      // Chapter 7 B-grid order: longest interval, smallest start, leaf ID.
      const double candidate_length = leaf.end - leaf.start;
      const double best_length = best->end - best->start;
      if (candidate_length > best_length ||
          (candidate_length == best_length &&
           (leaf.start < best->start ||
            (leaf.start == best->start &&
             leaf.leaf_id < best->leaf_id)))) {
        best = &leaf;
      }
    } else if (higher_priority(priority(leaf), priority(*best))) {
      best = &leaf;
    }
  }
  return best == nullptr ? 0 : best->leaf_id;
}

bool TemporalSplittingPolicy::is_shared_breakpoint(
    double midpoint) const {
  for (const double breakpoint :
       geometry_.common_partition()) {
    if (breakpoint == midpoint) {
      return true;
    }
  }
  return false;
}

TemporalSplitAttemptV1
TemporalSplittingPolicy::attempt_split(
    TemporalPartition& partition,
    TemporalLeafId parent_id,
    SolverBudget* budget) {
  TemporalSplitAttemptV1 attempt;
  attempt.parent_id = parent_id;

  const IntervalRecordV1 parent =
      partition.leaf(parent_id);
  if (parent.status != TemporalLeafStatusV1::OPEN ||
      partition.queue().find(parent_id) ==
          partition.queue().end()) {
    attempt.outcome =
        TemporalSplitOutcomeV1::NO_ACTION;
    attempt.reason =
        "leaf is not OPEN/eligible";
    return attempt;
  }

  const double delta =
      parent.end - parent.start;
  if (delta <
      2.0 * config_.delta_min) {
    partition.freeze_leaf(
        parent_id,
        "MIN_INTERVAL");
    attempt.outcome =
        TemporalSplitOutcomeV1::MIN_INTERVAL;
    attempt.reason =
        "MIN_INTERVAL";
    return attempt;
  }

  const double midpoint =
      parent.start +
      (parent.end - parent.start) / 2.0;
  if (!std::isfinite(midpoint) ||
      midpoint <= parent.start ||
      midpoint >= parent.end) {
    partition.freeze_leaf(
        parent_id,
        "MIDPOINT_COLLAPSE");
    attempt.outcome =
        TemporalSplitOutcomeV1::MIDPOINT_COLLAPSE;
    attempt.reason =
        "MIDPOINT_COLLAPSE";
    return attempt;
  }
  if (is_shared_breakpoint(midpoint)) {
    partition.freeze_leaf(
        parent_id,
        "SHARED_BREAKPOINT");
    attempt.outcome =
        TemporalSplitOutcomeV1::SHARED_BREAKPOINT;
    attempt.reason =
        "midpoint is a shared trajectory breakpoint";
    return attempt;
  }

  std::optional<std::vector<int>> inherited_assignment;
  if (parent.ub_piecewise_program_v2.has_value()) {
    inherited_assignment =
        parent.ub_piecewise_program_v2->point_assignment;
  } else if (parent.ub_piecewise_program.has_value()) {
    inherited_assignment =
        parent.ub_piecewise_program->point_assignment;
  }

  IntervalEvaluator evaluator(instance_);
  const auto left_result =
      evaluator.evaluate(
          parent.start,
          midpoint,
          inherited_assignment,
          config_.evaluator,
          budget);
  const auto right_result =
      evaluator.evaluate(
          midpoint,
          parent.end,
          inherited_assignment,
          config_.evaluator,
          budget);

  if (left_result.status ==
          IntervalEvaluatorStatusV1::INVALID_INTERVAL ||
      right_result.status ==
          IntervalEvaluatorStatusV1::INVALID_INTERVAL) {
    partition.freeze_leaf(parent_id, "PARTITION_INVALID");
    attempt.outcome =
        TemporalSplitOutcomeV1::PARTITION_INVALID;
    attempt.reason =
        "one child interval failed structural validation";
    return attempt;
  }

  if (!left_result.assignment.has_value() ||
      !right_result.assignment.has_value() ||
      !left_result.program_v2.has_value() ||
      !right_result.program_v2.has_value() ||
      !std::isfinite(left_result.upper_bound) ||
      !std::isfinite(right_result.upper_bound)) {
    partition.freeze_leaf(
        parent_id,
        "ORACLE_FAIL");
    attempt.outcome =
        TemporalSplitOutcomeV1::ORACLE_FAIL;
    attempt.reason =
        "one child has no valid complete UB assignment";
    return attempt;
  }

  if (left_result.status ==
          IntervalEvaluatorStatusV1::RECONCILIATION_FAILURE ||
      right_result.status ==
          IntervalEvaluatorStatusV1::RECONCILIATION_FAILURE) {
    partition.freeze_leaf(
        parent_id,
        "VERIFY_FAIL");
    attempt.outcome =
        TemporalSplitOutcomeV1::VERIFY_FAIL;
    attempt.reason =
        "child LB/UB reconciliation failed";
    return attempt;
  }

  TemporalLeafId left_id = 0;
  TemporalLeafId right_id = 0;
  try {
    const auto split =
        partition.split_leaf(parent_id, midpoint);
    left_id = split.first;
    right_id = split.second;

    TemporalBoundRecordV1 left_lb;
    left_lb.value =
        left_result.final_lb.valid
            ? left_result.final_lb.value
            : 0.0;
    left_lb.validity =
        left_result.final_lb.valid
            ? TemporalBoundValidityV1::VALID
            : TemporalBoundValidityV1::ABSENT;
    left_lb.method =
        left_result.final_lb.trivial
            ? "TRIVIAL_NONNEGATIVE"
            : "INTERVAL_EVALUATOR";
    left_lb.arithmetic =
        TemporalBoundArithmeticV1::FAST_DOUBLE;

    TemporalBoundRecordV1 left_ub;
    left_ub.value = left_result.upper_bound;
    left_ub.validity =
        TemporalBoundValidityV1::VALID;
    left_ub.method = "DIRECT_OR_DPLUS";
    left_ub.arithmetic =
        TemporalBoundArithmeticV1::FAST_DOUBLE;

    TemporalBoundRecordV1 right_lb;
    right_lb.value =
        right_result.final_lb.valid
            ? right_result.final_lb.value
            : 0.0;
    right_lb.validity =
        right_result.final_lb.valid
            ? TemporalBoundValidityV1::VALID
            : TemporalBoundValidityV1::ABSENT;
    right_lb.method =
        right_result.final_lb.trivial
            ? "TRIVIAL_NONNEGATIVE"
            : "INTERVAL_EVALUATOR";
    right_lb.arithmetic =
        TemporalBoundArithmeticV1::FAST_DOUBLE;

    TemporalBoundRecordV1 right_ub;
    right_ub.value = right_result.upper_bound;
    right_ub.validity =
        TemporalBoundValidityV1::VALID;
    right_ub.method = "DIRECT_OR_DPLUS";
    right_ub.arithmetic =
        TemporalBoundArithmeticV1::FAST_DOUBLE;

    partition.set_leaf_bounds_v2(
        left_id,
        left_lb,
        left_ub,
        left_result.program_v2);
    partition.set_leaf_bounds_v2(
        right_id,
        right_lb,
        right_ub,
        right_result.program_v2);
  } catch (const std::exception& error) {
    if (left_id != 0 &&
        right_id != 0) {
      partition.rollback_split(
          parent_id,
          left_id,
          right_id,
          parent,
          "VERIFY_FAIL");
    } else {
      partition.freeze_leaf(parent_id, "VERIFY_FAIL");
    }
    attempt.outcome =
        TemporalSplitOutcomeV1::VERIFY_FAIL;
    attempt.reason = error.what();
    return attempt;
  }

  attempt.outcome =
      TemporalSplitOutcomeV1::SPLIT;
  attempt.left_id = left_id;
  attempt.right_id = right_id;
  return attempt;
}

}  // namespace kdc
