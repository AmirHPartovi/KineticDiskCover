#include "kdc/interval_evaluator.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace kdc {
namespace {
bool complete_assignment(
    const std::vector<int>& assignment,
    int point_count,
    int station_count) {
  if (static_cast<int>(assignment.size()) != point_count) {
    return false;
  }
  for (const int station : assignment) {
    if (station < 0 || station >= station_count) {
      return false;
    }
  }
  return true;
}

UpperEnvelopeModeV1 to_envelope_mode(
    IntervalEvaluatorArithmeticV1 arithmetic) {
  return arithmetic ==
                 IntervalEvaluatorArithmeticV1::VALIDATED_NOT_AVAILABLE
             ? UpperEnvelopeModeV1::VALIDATED
             : UpperEnvelopeModeV1::FAST_DOUBLE;
}
}

IntervalEvaluator::IntervalEvaluator(const Instance& instance)
    : instance_(instance),
      geometry_(instance) {}

bool IntervalEvaluator::is_one_common_segment(
    double interval_start,
    double interval_end) const {
  if (!std::isfinite(interval_start) ||
      !std::isfinite(interval_end) ||
      interval_start > interval_end) {
    return false;
  }
  const auto& partition =
      geometry_.common_partition();
  for (WeightIndex index = 0U;
       index + 1U < partition.size();
       ++index) {
    if (interval_start >= partition[index] &&
        interval_end <= partition[index + 1U]) {
      return true;
    }
  }
  return false;
}

IntervalEvaluatorResultV1 IntervalEvaluator::evaluate(
    double interval_start,
    double interval_end,
    const std::optional<std::vector<int>>& inherited_assignment,
    const IntervalEvaluatorConfigV1& config,
    SolverBudget* budget) const {
  IntervalEvaluatorResultV1 result;
  result.interval_start = interval_start;
  result.interval_end = interval_end;
  result.midpoint =
      interval_start +
      (interval_end - interval_start) / 2.0;

  if (!is_one_common_segment(
          interval_start,
          interval_end)) {
    result.status =
        IntervalEvaluatorStatusV1::INVALID_INTERVAL;
    result.failure_reason =
        "interval is not contained in one common-partition segment";
    return result;
  }
  if (config.arithmetic ==
          IntervalEvaluatorArithmeticV1::VALIDATED_NOT_AVAILABLE &&
      geometry_.common_partition().size() < 2U) {
    result.status =
        IntervalEvaluatorStatusV1::VALIDATED_UNAVAILABLE;
    result.failure_reason =
        "validated interval backend unavailable";
    return result;
  }

  const double delta =
      interval_end - interval_start;
  const auto dminus =
      geometry_.bounds_on(
          interval_start,
          interval_end).lower.matrix;
  const auto dplus =
      geometry_.bounds_on(
          interval_start,
          interval_end).upper.matrix;

  // LB from Dminus exact or relaxation only.
  if (config.exact_dminus != nullptr) {
    if (budget != nullptr) {
      budget->checkpoint();
    }
    const auto exact =
        config.exact_dminus->solve(
            dminus, budget);
    if (exact.independently_validated_lower_bound.has_value() &&
        exact.independently_validated_lower_bound_validity ==
            WeightedBoundValidityV1::VALID &&
        std::isfinite(
            *exact.independently_validated_lower_bound)) {
      result.dminus_lb.valid = true;
      result.dminus_lb.value =
          *exact.independently_validated_lower_bound;
      result.dminus_lb.method =
          IntervalLowerMethodV1::DMINUS_EXACT_CERTIFICATE;
      result.dminus_lb.provenance =
          config.exact_dminus->name();
    }
  }
  if (config.relaxation_dminus != nullptr &&
      !result.dminus_lb.valid) {
    if (budget != nullptr) {
      budget->checkpoint();
    }
    const auto relaxation =
        config.relaxation_dminus->solve(
            dminus, budget);
    if (relaxation.independently_validated_lower_bound.has_value() &&
        relaxation.lower_bound_validity ==
            WeightedBoundValidityV1::VALID &&
        std::isfinite(
            *relaxation.independently_validated_lower_bound)) {
      result.dminus_lb.valid = true;
      result.dminus_lb.value =
          *relaxation.independently_validated_lower_bound;
      result.dminus_lb.method =
          IntervalLowerMethodV1::DMINUS_RELAXATION_CERTIFICATE;
      result.dminus_lb.provenance =
          config.relaxation_dminus->name();
    }
  }

  // Lipschitz path: only independently validated midpoint LB may be used.
  result.lipschitz_lb.valid = false;
  if (config.midpoint_lower_bound.has_value()) {
    WeightedLipschitzBound lipschitz(instance_);
    try {
      const auto local =
          lipschitz.local_lower(
              interval_start,
              interval_end,
              *config.midpoint_lower_bound);
      if (config.arithmetic ==
              IntervalEvaluatorArithmeticV1::FAST_DOUBLE &&
          std::isfinite(local.lower_bound) &&
          local.lower_bound >= 0.0) {
        result.lipschitz_lb.valid = false;
        result.lipschitz_lb.method =
            IntervalLowerMethodV1::LIPSCHITZ_DIAGNOSTIC;
        result.lipschitz_lb.value =
            local.lower_bound;
        result.lipschitz_lb.provenance =
            "FAST_DOUBLE;not-certificate";
        result.lipschitz_detail = local;
      }
    } catch (const std::exception& error) {
      result.failure_reason = error.what();
    }
  }

  // Choose largest independently valid official LB.
  // Lipschitz is diagnostic in FAST_DOUBLE and not official LB.
  if (result.dminus_lb.valid) {
    result.final_lb = result.dminus_lb;
  } else {
    result.final_lb.valid = true;
    result.final_lb.trivial = true;
    result.final_lb.value = 0.0;
    result.final_lb.method =
        IntervalLowerMethodV1::TRIVIAL_NONNEGATIVE;
    result.final_lb.provenance =
        "nonnegative integral;no Oracle certificate";
  }

  // UB assignment: inherited or greedy on Dplus.
  std::optional<std::vector<int>> assignment =
      inherited_assignment;
  if (!assignment.has_value() &&
      config.heuristic_dplus != nullptr &&
      config.arithmetic ==
          IntervalEvaluatorArithmeticV1::FAST_DOUBLE) {
    if (budget != nullptr) {
      budget->checkpoint();
    }
    const auto heuristic =
        config.heuristic_dplus->solve(dplus, budget);
    if (heuristic.incumbent_feasible &&
        heuristic.assignment.has_value()) {
      assignment = heuristic.assignment;
    }
  }

  if (assignment.has_value() &&
      complete_assignment(
          *assignment,
          instance_.n,
          instance_.m)) {
    result.assignment = assignment;
  } else {
    result.assignment.reset();
  }

  WeightedUpperEnvelope envelope(instance_);
  if (config.arithmetic ==
          IntervalEvaluatorArithmeticV1::FAST_DOUBLE &&
      result.assignment.has_value()) {
    const auto upper =
        envelope.solve(
            interval_start,
            interval_end,
            *result.assignment,
            UpperEnvelopeModeV1::FAST_DOUBLE);
    result.envelope = upper;
    if (upper.direct_valid &&
        (!upper.d_plus_fallback_valid ||
         upper.direct_integral <=
             upper.d_plus_fallback_integral)) {
      result.upper_method =
          IntervalUpperMethodV1::DIRECT_UPPER_ENVELOPE;
      result.upper_bound = upper.direct_integral;
    } else if (upper.d_plus_fallback_valid) {
      result.upper_method =
          IntervalUpperMethodV1::DPLUS_FALLBACK;
      result.upper_bound = upper.d_plus_fallback_integral;
    } else {
      result.upper_method =
          IntervalUpperMethodV1::NONE;
      result.upper_bound =
          std::numeric_limits<Weight>::infinity();
      result.status =
          IntervalEvaluatorStatusV1::INCOMPLETE;
      result.failure_reason =
          upper.failure_reason.empty()
              ? "no valid direct or D+ fallback UB"
              : upper.failure_reason;
    }
  } else {
    result.status =
        IntervalEvaluatorStatusV1::VALIDATED_UNAVAILABLE;
    result.upper_bound =
        std::numeric_limits<Weight>::infinity();
    result.failure_reason =
        "validated upper-envelope arithmetic unavailable";
  }

  if (config.arithmetic ==
          IntervalEvaluatorArithmeticV1::VALIDATED_NOT_AVAILABLE) {
    result.status =
        IntervalEvaluatorStatusV1::VALIDATED_UNAVAILABLE;
    result.upper_bound =
        std::numeric_limits<Weight>::infinity();
    return result;
  }

  if (std::isfinite(result.upper_bound) &&
      result.final_lb.valid &&
      result.final_lb.value >
          result.upper_bound + 1e-12 *
              std::max(1.0,
                       std::abs(result.upper_bound))) {
    result.status =
        IntervalEvaluatorStatusV1::RECONCILIATION_FAILURE;
    result.failure_reason =
        "LB exceeds UB; reconciliation required";
  } else if (std::isfinite(result.upper_bound)) {
    result.status =
        IntervalEvaluatorStatusV1::COMPLETE;
  } else {
    result.status =
        IntervalEvaluatorStatusV1::INCOMPLETE;
  }
  return result;
}

}  // namespace kdc
