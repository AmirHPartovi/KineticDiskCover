#include "kdc/temporal_result_v2.hpp"

#include <cmath>
#include <utility>

namespace kdc {
namespace {
bool gap_reason(TemporalStopReasonV2 reason) {
  return reason == TemporalStopReasonV2::GAP_ABS ||
         reason == TemporalStopReasonV2::GAP_REL;
}

bool validated_arithmetic(TemporalArithmeticAssuranceV2 arithmetic) {
  return arithmetic ==
             TemporalArithmeticAssuranceV2::DIRECTED_INTERVAL ||
         arithmetic == TemporalArithmeticAssuranceV2::EXACT;
}

bool valid_tolerances(const TemporalFinalEvidenceV2& evidence) {
  return std::isfinite(evidence.epsilon_abs) &&
         std::isfinite(evidence.epsilon_rel) &&
         evidence.epsilon_abs >= 0.0 &&
         evidence.epsilon_rel >= 0.0;
}

double subtract_up_nonnegative(double lhs, double rhs) {
  if (rhs == 0.0 || lhs == rhs) {
    return lhs - rhs;
  }
  // Keep the gap upper-enclosed when subtracting stored bound endpoints.
  return std::nextafter(
      lhs - rhs,
      std::numeric_limits<double>::infinity());
}

double divide_up_nonnegative(double numerator, double denominator) {
  if (numerator == 0.0) {
    return 0.0;
  }
  // Ratio metrics participate in the stop test and must not round downward.
  return std::nextafter(
      numerator / denominator,
      std::numeric_limits<double>::infinity());
}

TemporalStopReasonV2 effective_stop_reason(
    const TemporalFinalEvidenceV2& evidence,
    bool certified,
    bool absolute_success,
    bool relative_success) {
  if (evidence.has_inconsistent_bounds) {
    return TemporalStopReasonV2::INCONSISTENT;
  }
  if (evidence.continuous_verification_failed ||
      evidence.has_verification_failure) {
    return TemporalStopReasonV2::VERIFY_FAIL;
  }
  if (certified) {
    return absolute_success
               ? TemporalStopReasonV2::GAP_ABS
               : (relative_success
                      ? TemporalStopReasonV2::GAP_REL
                      : TemporalStopReasonV2::NONE);
  }
  if (gap_reason(evidence.requested_stop_reason)) {
    return TemporalStopReasonV2::CERTIFICATE_UNAVAILABLE;
  }
  if ((evidence.requested_stop_reason ==
           TemporalStopReasonV2::MIN_INTERVAL ||
       evidence.requested_stop_reason ==
           TemporalStopReasonV2::MIDPOINT_COLLAPSE) &&
      !evidence.unresolved_leaves_resolution_limited) {
    if (evidence.has_oracle_failure) {
      return TemporalStopReasonV2::ORACLE_FAIL;
    }
    return TemporalStopReasonV2::CERTIFICATE_UNAVAILABLE;
  }
  if ((evidence.requested_stop_reason ==
           TemporalStopReasonV2::MIN_INTERVAL ||
       evidence.requested_stop_reason ==
           TemporalStopReasonV2::MIDPOINT_COLLAPSE) &&
      evidence.unresolved_leaves_resolution_limited) {
    return evidence.all_unresolved_leaves_midpoint_collapsed
               ? TemporalStopReasonV2::MIDPOINT_COLLAPSE
               : TemporalStopReasonV2::MIN_INTERVAL;
  }
  if (evidence.has_oracle_failure &&
      evidence.requested_stop_reason ==
          TemporalStopReasonV2::NONE) {
    return TemporalStopReasonV2::ORACLE_FAIL;
  }
  return evidence.requested_stop_reason;
}
}  // namespace

TemporalFinalResultV2 finalize_temporal_result_v2(
    const TemporalFinalEvidenceV2& evidence) {
  TemporalFinalResultV2 result;
  result.solve_time_seconds = evidence.solve_time_seconds;
  result.verify_time_seconds = evidence.verify_time_seconds;
  result.oracle_statistics = evidence.oracle_statistics;
  result.incumbent_ref = evidence.incumbent_ref;
  result.provenance = evidence.provenance;
  result.lower_bound_method = evidence.lower_bound_method;
  result.upper_bound_method = evidence.upper_bound_method;
  result.lower_bound_arithmetic =
      evidence.lower_bound_arithmetic;
  result.upper_bound_arithmetic =
      evidence.upper_bound_arithmetic;
  result.lower_bound_provenance =
      evidence.lower_bound_provenance;
  result.upper_bound_provenance =
      evidence.upper_bound_provenance;
  result.lower_bound_certificate_ids =
      evidence.lower_bound_certificate_ids;
  result.upper_bound_certificate_ids =
      evidence.upper_bound_certificate_ids;

  if (!valid_tolerances(evidence)) {
    result.status = TemporalFinalStatusV2::INCOMPLETE;
    result.stop_reason = TemporalStopReasonV2::INVALID_INPUT;
    result.provenance.emplace_back(
        "invalid gap tolerances: both must be finite and nonnegative");
    return result;
  }

  const bool lb_certificate_ok =
      evidence.lower_bound_valid &&
      evidence.current_lb_contributions_valid &&
      evidence.historical_certificates_valid &&
      validated_arithmetic(evidence.lower_bound_arithmetic) &&
      std::isfinite(evidence.lower_bound) &&
      evidence.lower_bound >= 0.0;
  const bool ub_certificate_ok =
      evidence.upper_bound_valid &&
      evidence.complete_model_a_program &&
      evidence.upper_cost_enclosure_consistent &&
      validated_arithmetic(evidence.upper_bound_arithmetic) &&
      std::isfinite(evidence.upper_bound) &&
      evidence.upper_bound >= 0.0;

  if (evidence.sampled_only) {
    result.LB = 0.0;
    result.UB = std::numeric_limits<double>::infinity();
    result.LB_valid = false;
    result.UB_valid = false;
    result.lower_bound_method = "none";
    result.upper_bound_method = "none";
    result.lower_bound_arithmetic =
        TemporalArithmeticAssuranceV2::UNAVAILABLE;
    result.upper_bound_arithmetic =
        TemporalArithmeticAssuranceV2::UNAVAILABLE;
    result.lower_bound_provenance.clear();
    result.upper_bound_provenance.clear();
    result.lower_bound_certificate_ids.clear();
    result.upper_bound_certificate_ids.clear();
    result.absolute_gap = std::numeric_limits<double>::infinity();
    result.gamma_rep.reset();
    result.gamma_cert.reset();
    result.stop_reason = effective_stop_reason(
        evidence, false, false, false);
    result.status =
        result.stop_reason == TemporalStopReasonV2::VERIFY_FAIL ||
                result.stop_reason ==
                    TemporalStopReasonV2::INCONSISTENT ||
                result.stop_reason ==
                    TemporalStopReasonV2::INVALID_INPUT
            ? TemporalFinalStatusV2::INCOMPLETE
            : TemporalFinalStatusV2::SAMPLED;
    result.incumbent_ref.reset();
    result.provenance.emplace_back(
        "sample-only evidence is excluded from official continuous bounds");
    result.metric_note =
        "sampled values do not define LB, UB, or a continuous gap";
    return result;
  }

  if (evidence.has_inconsistent_bounds ||
      (evidence.lower_bound_valid &&
       evidence.upper_bound_valid &&
       std::isfinite(evidence.lower_bound) &&
       std::isfinite(evidence.upper_bound) &&
       evidence.lower_bound > evidence.upper_bound)) {
    result.LB = 0.0;
    result.UB = std::numeric_limits<double>::infinity();
    result.LB_valid = false;
    result.UB_valid = false;
    result.lower_bound_method = "none";
    result.upper_bound_method = "none";
    result.lower_bound_arithmetic =
        TemporalArithmeticAssuranceV2::UNAVAILABLE;
    result.upper_bound_arithmetic =
        TemporalArithmeticAssuranceV2::UNAVAILABLE;
    result.lower_bound_provenance.clear();
    result.upper_bound_provenance.clear();
    result.lower_bound_certificate_ids.clear();
    result.upper_bound_certificate_ids.clear();
    result.absolute_gap = std::numeric_limits<double>::infinity();
    result.status = TemporalFinalStatusV2::INCOMPLETE;
    result.stop_reason = TemporalStopReasonV2::INCONSISTENT;
    result.incumbent_ref.reset();
    result.provenance.emplace_back(
        "contradictory bounds were excluded pending independent reconciliation");
    return result;
  }

  if (lb_certificate_ok) {
    result.LB = evidence.lower_bound;
    result.LB_valid = true;
  } else {
    result.LB = 0.0;
    result.LB_valid = true;
    result.lower_bound_method = "TRIVIAL_NONNEGATIVE";
    result.lower_bound_arithmetic =
        TemporalArithmeticAssuranceV2::EXACT;
    result.lower_bound_provenance = {
        "nonnegativity of the Model A integral"};
    result.lower_bound_certificate_ids.clear();
    result.provenance.emplace_back(
        "LB=0 from nonnegativity of the Model A integral");
  }

  if (ub_certificate_ok) {
    result.UB = evidence.upper_bound;
    result.UB_valid = true;
  } else {
    result.upper_bound_method = "none";
    result.upper_bound_arithmetic =
        TemporalArithmeticAssuranceV2::UNAVAILABLE;
    result.upper_bound_provenance.clear();
    result.upper_bound_certificate_ids.clear();
  }

  const bool bracket_consistent =
      !result.UB_valid || result.LB <= result.UB;
  if (!bracket_consistent) {
    result.LB = 0.0;
    result.UB = std::numeric_limits<double>::infinity();
    result.LB_valid = false;
    result.UB_valid = false;
    result.lower_bound_method = "none";
    result.upper_bound_method = "none";
    result.lower_bound_arithmetic =
        TemporalArithmeticAssuranceV2::UNAVAILABLE;
    result.upper_bound_arithmetic =
        TemporalArithmeticAssuranceV2::UNAVAILABLE;
    result.lower_bound_provenance.clear();
    result.upper_bound_provenance.clear();
    result.lower_bound_certificate_ids.clear();
    result.upper_bound_certificate_ids.clear();
    result.absolute_gap = std::numeric_limits<double>::infinity();
    result.status = TemporalFinalStatusV2::INCOMPLETE;
    result.stop_reason = TemporalStopReasonV2::INCONSISTENT;
    result.incumbent_ref.reset();
    result.provenance.emplace_back(
        "inconsistent bracket excluded; no numeric clamping performed");
    return result;
  }

  result.absolute_gap =
      result.UB_valid
          ? subtract_up_nonnegative(result.UB, result.LB)
          : std::numeric_limits<double>::infinity();
  if (result.UB_valid && result.UB > 0.0) {
    result.gamma_rep =
        divide_up_nonnegative(result.absolute_gap, result.UB);
  }
  if (result.LB_valid && result.LB > 0.0) {
    result.gamma_cert =
        divide_up_nonnegative(result.absolute_gap, result.LB);
  }
  if (result.UB_valid && result.UB == 0.0) {
    result.metric_note =
        "UB=0 implies a valid bracket has LB=0; both relative ratios "
        "are undefined and the absolute gap decides certification";
  } else if (result.LB_valid && result.LB == 0.0) {
    result.metric_note =
        "LB=0: gamma_cert is undefined; only the absolute-gap test applies";
  }

  const bool absolute_success =
      result.LB_valid && result.UB_valid &&
      result.absolute_gap <= evidence.epsilon_abs;
  const bool relative_success =
      result.LB_valid && result.UB_valid && result.LB > 0.0 &&
      result.gamma_cert.has_value() &&
      *result.gamma_cert <= evidence.epsilon_rel;
  const bool cert_prerequisites =
      lb_certificate_ok &&
      ub_certificate_ok &&
      evidence.current_lb_contributions_valid &&
      evidence.historical_certificates_valid &&
      validated_arithmetic(evidence.lower_bound_arithmetic) &&
      validated_arithmetic(evidence.upper_bound_arithmetic) &&
      evidence.complete_model_a_program &&
      evidence.continuous_verification_passed &&
      evidence.upper_cost_enclosure_consistent &&
      bracket_consistent;
  const bool certified =
      cert_prerequisites &&
      (absolute_success || relative_success);

  if (certified) {
    result.status = TemporalFinalStatusV2::CERTIFIED;
    result.stop_reason = effective_stop_reason(
        evidence, true, absolute_success, relative_success);
  } else if (evidence.continuous_verification_failed ||
             evidence.has_verification_failure ||
             evidence.has_inconsistent_bounds) {
    result.status = TemporalFinalStatusV2::INCOMPLETE;
    result.stop_reason = effective_stop_reason(
        evidence, false, false, false);
  } else if (lb_certificate_ok && ub_certificate_ok &&
             result.LB > 0.0 && result.UB > result.LB) {
    result.status = TemporalFinalStatusV2::NUMERICALLY_BOUNDED;
    result.stop_reason = effective_stop_reason(
        evidence, false, false, false);
  } else if (result.UB_valid &&
             evidence.complete_model_a_program &&
             evidence.continuous_verification_passed &&
             evidence.upper_cost_enclosure_consistent &&
             result.LB == 0.0) {
    result.status = TemporalFinalStatusV2::HEURISTIC;
    result.stop_reason = effective_stop_reason(
        evidence, false, false, false);
  } else if (evidence.sampled_only) {
    result.status = TemporalFinalStatusV2::SAMPLED;
    result.stop_reason = effective_stop_reason(
        evidence, false, false, false);
  } else {
    result.status = TemporalFinalStatusV2::INCOMPLETE;
    result.stop_reason = effective_stop_reason(
        evidence, false, false, false);
  }
  if (gap_reason(result.stop_reason) &&
      result.status != TemporalFinalStatusV2::CERTIFIED) {
    result.stop_reason =
        TemporalStopReasonV2::CERTIFICATE_UNAVAILABLE;
  }
  return result;
}

bool valid_temporal_status_stop_pair_v2(
    TemporalFinalStatusV2 status,
    TemporalStopReasonV2 stop_reason) {
  const bool is_gap = gap_reason(stop_reason);
  if (status == TemporalFinalStatusV2::CERTIFIED) {
    return is_gap;
  }
  if (is_gap) {
    return false;
  }
  if (status == TemporalFinalStatusV2::INCOMPLETE) {
    return true;
  }
  if (stop_reason == TemporalStopReasonV2::VERIFY_FAIL ||
      stop_reason == TemporalStopReasonV2::INCONSISTENT ||
      stop_reason == TemporalStopReasonV2::INVALID_INPUT) {
    return false;
  }
  return true;
}

const char* temporal_final_status_name_v2(
    TemporalFinalStatusV2 status) noexcept {
  switch (status) {
    case TemporalFinalStatusV2::CERTIFIED:
      return "CERTIFIED";
    case TemporalFinalStatusV2::NUMERICALLY_BOUNDED:
      return "NUMERICALLY_BOUNDED";
    case TemporalFinalStatusV2::HEURISTIC:
      return "HEURISTIC";
    case TemporalFinalStatusV2::SAMPLED:
      return "SAMPLED";
    case TemporalFinalStatusV2::INCOMPLETE:
      return "INCOMPLETE";
  }
  return "INCOMPLETE";
}

const char* temporal_stop_reason_name_v2(
    TemporalStopReasonV2 reason) noexcept {
  switch (reason) {
    case TemporalStopReasonV2::NONE:
      return "NONE";
    case TemporalStopReasonV2::GAP_ABS:
      return "GAP_ABS";
    case TemporalStopReasonV2::GAP_REL:
      return "GAP_REL";
    case TemporalStopReasonV2::TIMEOUT:
      return "TIMEOUT";
    case TemporalStopReasonV2::INTERVAL_CAP:
      return "INTERVAL_CAP";
    case TemporalStopReasonV2::ORACLE_CAP:
      return "ORACLE_CAP";
    case TemporalStopReasonV2::ORACLE_TIME_LIMIT:
      return "ORACLE_TIME_LIMIT";
    case TemporalStopReasonV2::MIN_INTERVAL:
      return "MIN_INTERVAL";
    case TemporalStopReasonV2::MIDPOINT_COLLAPSE:
      return "MIDPOINT_COLLAPSE";
    case TemporalStopReasonV2::ORACLE_FAIL:
      return "ORACLE_FAIL";
    case TemporalStopReasonV2::VERIFY_FAIL:
      return "VERIFY_FAIL";
    case TemporalStopReasonV2::INCONSISTENT:
      return "INCONSISTENT";
    case TemporalStopReasonV2::CERTIFICATE_UNAVAILABLE:
      return "CERTIFICATE_UNAVAILABLE";
    case TemporalStopReasonV2::INVALID_INPUT:
      return "INVALID_INPUT";
  }
  return "NONE";
}

}  // namespace kdc
