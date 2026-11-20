#pragma once

#include "kdc/solver_budget.hpp"
#include "kdc/types.hpp"
#include "kdc/weighted_geometry.hpp"
#include "kdc/weighted_lipschitz.hpp"
#include "kdc/weighted_oracle.hpp"
#include "kdc/weighted_upper_envelope.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace kdc {

enum class IntervalEvaluatorArithmeticV1 {
  FAST_DOUBLE,
  VALIDATED_NOT_AVAILABLE
};

enum class IntervalEvaluatorStatusV1 {
  COMPLETE,
  INCOMPLETE,
  RECONCILIATION_FAILURE,
  INVALID_INTERVAL,
  VALIDATED_UNAVAILABLE
};

enum class IntervalLowerMethodV1 {
  NONE,
  TRIVIAL_NONNEGATIVE,
  DMINUS_EXACT_CERTIFICATE,
  DMINUS_RELAXATION_CERTIFICATE,
  LIPSCHITZ_DIAGNOSTIC
};

enum class IntervalUpperMethodV1 {
  NONE,
  DIRECT_UPPER_ENVELOPE,
  DPLUS_FALLBACK
};

struct IntervalLowerContributionV1 {
  Weight value{0.0};
  bool valid{false};
  bool trivial{false};
  IntervalLowerMethodV1 method{
      IntervalLowerMethodV1::NONE};
  std::string certificate_id;
  std::string provenance;
};

struct IntervalEvaluatorConfigV1 {
  IWeightedExactOracle* exact_dminus{nullptr};
  IWeightedRelaxationOracle* relaxation_dminus{nullptr};
  IWeightedHeuristicOracle* heuristic_dplus{nullptr};
  std::optional<ValidatedMidpointLowerBoundV1>
      midpoint_lower_bound;
  IntervalEvaluatorArithmeticV1 arithmetic{
      IntervalEvaluatorArithmeticV1::FAST_DOUBLE};
};

struct IntervalEvaluatorResultV1 {
  std::uint32_t schema_version{1};
  IntervalEvaluatorStatusV1 status{
      IntervalEvaluatorStatusV1::INCOMPLETE};
  double interval_start{0.0};
  double interval_end{0.0};
  double midpoint{0.0};
  IntervalLowerContributionV1 dminus_lb;
  IntervalLowerContributionV1 lipschitz_lb;
  IntervalLowerContributionV1 final_lb;
  IntervalUpperMethodV1 upper_method{
      IntervalUpperMethodV1::NONE};
  Weight upper_bound{
      std::numeric_limits<Weight>::infinity()};
  std::optional<std::vector<int>> assignment;
  UpperEnvelopeResultV1 envelope;
  LipschitzLocalLowerBoundV1 lipschitz_detail;
  std::string failure_reason;
};

class IntervalEvaluator {
 public:
  explicit IntervalEvaluator(const Instance& instance);

  IntervalEvaluatorResultV1 evaluate(
      double interval_start,
      double interval_end,
      const std::optional<std::vector<int>>& inherited_assignment,
      const IntervalEvaluatorConfigV1& config,
      SolverBudget* budget = nullptr) const;

 private:
  Instance instance_;
  WeightedGeometry geometry_;

  bool is_one_common_segment(
      double interval_start,
      double interval_end) const;
};

}  // namespace kdc
