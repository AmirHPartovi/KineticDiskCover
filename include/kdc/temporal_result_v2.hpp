#pragma once

#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <vector>

namespace kdc {

enum class TemporalFinalStatusV2 {
  CERTIFIED,
  NUMERICALLY_BOUNDED,
  HEURISTIC,
  SAMPLED,
  INCOMPLETE
};

enum class TemporalStopReasonV2 {
  NONE,
  GAP_ABS,
  GAP_REL,
  TIMEOUT,
  INTERVAL_CAP,
  ORACLE_CAP,
  ORACLE_TIME_LIMIT,
  MIN_INTERVAL,
  MIDPOINT_COLLAPSE,
  ORACLE_FAIL,
  VERIFY_FAIL,
  INCONSISTENT,
  CERTIFICATE_UNAVAILABLE,
  INVALID_INPUT
};

enum class TemporalArithmeticAssuranceV2 {
  UNAVAILABLE,
  FAST_FLOAT,
  DIRECTED_INTERVAL,
  EXACT
};

struct TemporalOracleStatisticsV2 {
  std::uint64_t exact_calls{0};
  std::uint64_t heuristic_calls{0};
  std::uint64_t relaxation_calls{0};
  std::uint64_t failures{0};
  std::uint64_t total_calls{0};
};

struct TemporalFinalEvidenceV2 {
  double epsilon_abs{0.0};
  double epsilon_rel{0.0};

  double lower_bound{0.0};
  double upper_bound{std::numeric_limits<double>::infinity()};
  bool lower_bound_valid{false};
  bool upper_bound_valid{false};
  bool current_lb_contributions_valid{false};
  bool historical_certificates_valid{false};
  std::string lower_bound_method{"none"};
  std::string upper_bound_method{"none"};
  TemporalArithmeticAssuranceV2 lower_bound_arithmetic{
      TemporalArithmeticAssuranceV2::UNAVAILABLE};
  TemporalArithmeticAssuranceV2 upper_bound_arithmetic{
      TemporalArithmeticAssuranceV2::UNAVAILABLE};
  std::vector<std::string> lower_bound_provenance;
  std::vector<std::string> upper_bound_provenance;
  std::vector<std::string> lower_bound_certificate_ids;
  std::vector<std::string> upper_bound_certificate_ids;

  bool complete_model_a_program{false};
  bool continuous_verification_passed{false};
  bool continuous_verification_failed{false};
  bool upper_cost_enclosure_consistent{false};
  bool sampled_only{false};

  bool has_inconsistent_bounds{false};
  bool has_oracle_failure{false};
  bool has_verification_failure{false};
  bool unresolved_leaves_resolution_limited{false};
  bool all_unresolved_leaves_midpoint_collapsed{false};

  TemporalStopReasonV2 requested_stop_reason{
      TemporalStopReasonV2::NONE};
  std::optional<std::string> incumbent_ref;
  std::vector<std::string> provenance;
  double solve_time_seconds{0.0};
  double verify_time_seconds{0.0};
  TemporalOracleStatisticsV2 oracle_statistics;
};

struct TemporalFinalResultV2 {
  std::uint32_t schema_version{2};

  double LB{0.0};
  double UB{std::numeric_limits<double>::infinity()};
  bool LB_valid{false};
  bool UB_valid{false};
  std::string lower_bound_method{"none"};
  std::string upper_bound_method{"none"};
  TemporalArithmeticAssuranceV2 lower_bound_arithmetic{
      TemporalArithmeticAssuranceV2::UNAVAILABLE};
  TemporalArithmeticAssuranceV2 upper_bound_arithmetic{
      TemporalArithmeticAssuranceV2::UNAVAILABLE};
  std::vector<std::string> lower_bound_provenance;
  std::vector<std::string> upper_bound_provenance;
  std::vector<std::string> lower_bound_certificate_ids;
  std::vector<std::string> upper_bound_certificate_ids;
  double absolute_gap{std::numeric_limits<double>::infinity()};
  std::optional<double> gamma_rep;
  std::optional<double> gamma_cert;

  TemporalFinalStatusV2 status{TemporalFinalStatusV2::INCOMPLETE};
  TemporalStopReasonV2 stop_reason{TemporalStopReasonV2::NONE};
  std::optional<std::string> incumbent_ref;
  std::vector<std::string> provenance;
  double solve_time_seconds{0.0};
  double verify_time_seconds{0.0};
  TemporalOracleStatisticsV2 oracle_statistics;
  std::string metric_note;
};

TemporalFinalResultV2 finalize_temporal_result_v2(
    const TemporalFinalEvidenceV2& evidence);

bool valid_temporal_status_stop_pair_v2(
    TemporalFinalStatusV2 status,
    TemporalStopReasonV2 stop_reason);

const char* temporal_final_status_name_v2(
    TemporalFinalStatusV2 status) noexcept;
const char* temporal_stop_reason_name_v2(
    TemporalStopReasonV2 reason) noexcept;

}  // namespace kdc
