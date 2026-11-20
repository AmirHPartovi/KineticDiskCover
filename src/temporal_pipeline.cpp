#include "kdc/temporal_pipeline.hpp"

#include <cmath>
#include <limits>

namespace kdc {
namespace {
nlohmann::json finite_json(double value) {
  return std::isfinite(value) ? nlohmann::json(value)
                              : nlohmann::json(nullptr);
}

nlohmann::json optional_finite_json(const std::optional<double>& value) {
  return value.has_value() ? finite_json(*value) : nlohmann::json(nullptr);
}

const char* arithmetic_name(TemporalArithmeticAssuranceV2 value) noexcept {
  switch (value) {
    case TemporalArithmeticAssuranceV2::UNAVAILABLE:
      return "UNAVAILABLE";
    case TemporalArithmeticAssuranceV2::FAST_FLOAT:
      return "FAST_FLOAT";
    case TemporalArithmeticAssuranceV2::DIRECTED_INTERVAL:
      return "DIRECTED_INTERVAL";
    case TemporalArithmeticAssuranceV2::EXACT:
      return "EXACT";
  }
  return "UNAVAILABLE";
}

const char* execution_status_name(TemporalThesisStatusV1 value) noexcept {
  switch (value) {
    case TemporalThesisStatusV1::COMPLETE:
      return "COMPLETE";
    case TemporalThesisStatusV1::INCOMPLETE:
      return "INCOMPLETE";
    case TemporalThesisStatusV1::TIME_LIMIT:
      return "TIME_LIMIT";
    case TemporalThesisStatusV1::ORACLE_TIME_LIMIT:
      return "ORACLE_TIME_LIMIT";
    case TemporalThesisStatusV1::MAX_LEAVES:
      return "MAX_LEAVES";
    case TemporalThesisStatusV1::ORACLE_CAP:
      return "ORACLE_CAP";
    case TemporalThesisStatusV1::RECONCILIATION_FAILURE:
      return "RECONCILIATION_FAILURE";
    case TemporalThesisStatusV1::INCONSISTENT:
      return "INCONSISTENT";
    case TemporalThesisStatusV1::INVALID:
      return "INVALID";
    case TemporalThesisStatusV1::FAILED:
      return "FAILED";
  }
  return "FAILED";
}

nlohmann::json final_result_json(const TemporalFinalResultV2& result) {
  return {
      {"schema_version", result.schema_version},
      {"LB", finite_json(result.LB)},
      {"UB", finite_json(result.UB)},
      {"LB_valid", result.LB_valid},
      {"UB_valid", result.UB_valid},
      {"lower_bound_method", result.lower_bound_method},
      {"upper_bound_method", result.upper_bound_method},
      {"lower_bound_arithmetic",
       arithmetic_name(result.lower_bound_arithmetic)},
      {"upper_bound_arithmetic",
       arithmetic_name(result.upper_bound_arithmetic)},
      {"lower_bound_provenance", result.lower_bound_provenance},
      {"upper_bound_provenance", result.upper_bound_provenance},
      {"lower_bound_certificate_ids", result.lower_bound_certificate_ids},
      {"upper_bound_certificate_ids", result.upper_bound_certificate_ids},
      {"absolute_gap", finite_json(result.absolute_gap)},
      {"gamma_rep", optional_finite_json(result.gamma_rep)},
      {"gamma_cert", optional_finite_json(result.gamma_cert)},
      {"status", temporal_final_status_name_v2(result.status)},
      {"stop_reason", temporal_stop_reason_name_v2(result.stop_reason)},
      {"incumbent_ref",
       result.incumbent_ref ? nlohmann::json(*result.incumbent_ref)
                            : nlohmann::json(nullptr)},
      {"provenance", result.provenance},
      {"solve_time_sec", finite_json(result.solve_time_seconds)},
      {"verify_time_sec", finite_json(result.verify_time_seconds)},
      {"oracle_statistics",
       {{"exact_calls", result.oracle_statistics.exact_calls},
        {"heuristic_calls", result.oracle_statistics.heuristic_calls},
        {"relaxation_calls", result.oracle_statistics.relaxation_calls},
        {"failures", result.oracle_statistics.failures},
        {"total_calls", result.oracle_statistics.total_calls}}},
      {"metric_note", result.metric_note}};
}

nlohmann::json snapshot_json(const GlobalBoundUbSnapshotV1& snapshot) {
  nlohmann::json leaves = nlohmann::json::array();
  for (const auto& leaf : snapshot.leaves) {
    nlohmann::json pieces = nlohmann::json::array();
    for (const auto& piece : leaf.pieces) {
      nlohmann::json radii = nlohmann::json::array();
      for (const double radius : piece.station_radii) {
        radii.push_back(finite_json(radius));
      }
      pieces.push_back({{"start", finite_json(piece.start)},
                        {"end", finite_json(piece.end)},
                        {"station_radii", std::move(radii)}});
    }
    leaves.push_back(
        {{"leaf_id", leaf.leaf_id},
         {"start", finite_json(leaf.start)},
         {"end", finite_json(leaf.end)},
         {"ub_value", finite_json(leaf.ub_value)},
         {"assignment", leaf.assignment},
         {"pieces", std::move(pieces)},
         {"certificate_ids", leaf.certificate_ids}});
  }
  const auto& identity = snapshot.identity;
  return {{"schema_version", snapshot.schema_version},
          {"partition_version", snapshot.partition_version},
          {"global_ub", finite_json(snapshot.global_ub)},
          {"complete", snapshot.complete},
          {"identity",
           {{"instance_hash", identity.instance_hash},
            {"solver_identity", identity.solver_identity},
            {"config_identity", identity.config_identity},
            {"numeric_backend", identity.numeric_backend},
            {"verifier_state", identity.verifier_state},
            {"certificate_ids", identity.certificate_ids}}},
          {"leaves", std::move(leaves)}};
}
}  // namespace

nlohmann::json temporal_thesis_result_to_json(
    const TemporalThesisResultV2& result) {
  nlohmann::json incumbent = nullptr;
  if (result.execution.global_bounds.ub_best.has_value()) {
    incumbent = snapshot_json(
        result.execution.global_bounds.ub_best->snapshot);
  }
  return {
      {"schema_version", 2},
      {"final", final_result_json(result.final)},
      {"incumbent_snapshot", std::move(incumbent)},
      {"execution",
       {{"schema_version", result.execution.schema_version},
        {"status", execution_status_name(result.execution.status)},
        {"stop_reason", result.execution.stop_reason},
        {"leaf_count", result.execution.leaf_count},
        {"oracle_call_count", result.execution.oracle_call_count},
        {"partition_version", result.execution.partition_version},
        {"complete_model_a_program",
         result.execution.global_bounds.ub_best.has_value() &&
             result.execution.global_bounds.ub_best->snapshot.complete},
        {"verified", result.execution.verified},
        {"verifier_status", result.execution.verifier_status},
        {"elapsed_seconds", finite_json(result.execution.elapsed_seconds)}}}};
}

nlohmann::json temporal_thesis_failure_to_json(const std::string& reason) {
  TemporalThesisResultV2 result;
  result.final.status = TemporalFinalStatusV2::INCOMPLETE;
  result.final.stop_reason = TemporalStopReasonV2::INVALID_INPUT;
  result.final.lower_bound_method = "none";
  result.final.upper_bound_method = "none";
  result.final.metric_note = reason;
  result.execution.status = TemporalThesisStatusV1::FAILED;
  result.execution.stop_reason = reason;
  result.execution.verifier_status = "NOT_RUN";
  return temporal_thesis_result_to_json(result);
}

bool temporal_thesis_result_failed(
    const TemporalThesisResultV2& result) noexcept {
  switch (result.execution.status) {
    case TemporalThesisStatusV1::INVALID:
    case TemporalThesisStatusV1::FAILED:
    case TemporalThesisStatusV1::RECONCILIATION_FAILURE:
    case TemporalThesisStatusV1::INCONSISTENT:
      return true;
    case TemporalThesisStatusV1::COMPLETE:
    case TemporalThesisStatusV1::INCOMPLETE:
    case TemporalThesisStatusV1::TIME_LIMIT:
    case TemporalThesisStatusV1::ORACLE_TIME_LIMIT:
    case TemporalThesisStatusV1::MAX_LEAVES:
    case TemporalThesisStatusV1::ORACLE_CAP:
      break;
  }
  switch (result.final.stop_reason) {
    case TemporalStopReasonV2::ORACLE_FAIL:
    case TemporalStopReasonV2::VERIFY_FAIL:
    case TemporalStopReasonV2::INCONSISTENT:
    case TemporalStopReasonV2::INVALID_INPUT:
      return true;
    case TemporalStopReasonV2::NONE:
    case TemporalStopReasonV2::GAP_ABS:
    case TemporalStopReasonV2::GAP_REL:
    case TemporalStopReasonV2::TIMEOUT:
    case TemporalStopReasonV2::INTERVAL_CAP:
    case TemporalStopReasonV2::ORACLE_CAP:
    case TemporalStopReasonV2::ORACLE_TIME_LIMIT:
    case TemporalStopReasonV2::MIN_INTERVAL:
    case TemporalStopReasonV2::MIDPOINT_COLLAPSE:
    case TemporalStopReasonV2::CERTIFICATE_UNAVAILABLE:
      return false;
  }
  return true;
}

bool temporal_thesis_result_timed_out(
    const TemporalThesisResultV2& result) noexcept {
  return result.execution.status == TemporalThesisStatusV1::TIME_LIMIT ||
         result.execution.status ==
             TemporalThesisStatusV1::ORACLE_TIME_LIMIT ||
         result.final.stop_reason == TemporalStopReasonV2::TIMEOUT ||
         result.final.stop_reason ==
             TemporalStopReasonV2::ORACLE_TIME_LIMIT;
}

}  // namespace kdc
