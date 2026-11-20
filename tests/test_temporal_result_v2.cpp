#include "kdc/temporal_thesis_solver.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <limits>

namespace {
kdc::TemporalFinalEvidenceV2 certified_evidence() {
  kdc::TemporalFinalEvidenceV2 evidence;
  evidence.epsilon_abs = 0.01;
  evidence.epsilon_rel = 0.1;
  evidence.lower_bound = 1.0;
  evidence.upper_bound = 1.005;
  evidence.lower_bound_valid = true;
  evidence.upper_bound_valid = true;
  evidence.current_lb_contributions_valid = true;
  evidence.historical_certificates_valid = true;
  evidence.lower_bound_method = "validated lower certificate";
  evidence.upper_bound_method = "validated upper snapshot";
  evidence.lower_bound_arithmetic =
      kdc::TemporalArithmeticAssuranceV2::DIRECTED_INTERVAL;
  evidence.upper_bound_arithmetic =
      kdc::TemporalArithmeticAssuranceV2::DIRECTED_INTERVAL;
  evidence.complete_model_a_program = true;
  evidence.continuous_verification_passed = true;
  evidence.upper_cost_enclosure_consistent = true;
  evidence.lower_bound_certificate_ids = {"lb-cert-1"};
  evidence.upper_bound_certificate_ids = {"ub-cert-1"};
  evidence.lower_bound_provenance = {"dual-feasible lower certificate"};
  evidence.upper_bound_provenance = {"complete program snapshot"};
  evidence.incumbent_ref = "snapshot-7";
  evidence.provenance.emplace_back("independently validated evidence");
  return evidence;
}
}  // namespace

TEST_CASE("Temporal result V2 applies absolute and relative gap rules") {
  auto absolute = certified_evidence();
  absolute.lower_bound = 0.0;
  absolute.upper_bound = 0.01;
  absolute.epsilon_abs = 0.01;
  absolute.epsilon_rel = 0.0;
  auto absolute_result =
      kdc::finalize_temporal_result_v2(absolute);
  REQUIRE(absolute_result.status ==
          kdc::TemporalFinalStatusV2::CERTIFIED);
  REQUIRE(absolute_result.stop_reason ==
          kdc::TemporalStopReasonV2::GAP_ABS);
  REQUIRE_FALSE(absolute_result.gamma_cert.has_value());
  REQUIRE(absolute_result.lower_bound_method ==
          "validated lower certificate");
  REQUIRE(absolute_result.upper_bound_method ==
          "validated upper snapshot");
  REQUIRE(absolute_result.lower_bound_certificate_ids ==
          std::vector<std::string>{"lb-cert-1"});

  auto numerical = certified_evidence();
  numerical.continuous_verification_passed = false;
  numerical.epsilon_abs = 0.001;
  numerical.epsilon_rel = 0.0;
  numerical.requested_stop_reason =
      kdc::TemporalStopReasonV2::TIMEOUT;
  auto numerical_result =
      kdc::finalize_temporal_result_v2(numerical);
  REQUIRE(numerical_result.status ==
          kdc::TemporalFinalStatusV2::NUMERICALLY_BOUNDED);
  REQUIRE(numerical_result.stop_reason ==
          kdc::TemporalStopReasonV2::TIMEOUT);
  REQUIRE(numerical_result.LB_valid);
  REQUIRE(numerical_result.UB_valid);

  auto relative = certified_evidence();
  relative.lower_bound = 2.0;
  relative.upper_bound = 2.2;
  relative.epsilon_abs = 0.05;
  relative.epsilon_rel = 0.11;
  auto relative_result =
      kdc::finalize_temporal_result_v2(relative);
  REQUIRE(relative_result.status ==
          kdc::TemporalFinalStatusV2::CERTIFIED);
  REQUIRE(relative_result.stop_reason ==
          kdc::TemporalStopReasonV2::GAP_REL);
  REQUIRE(relative_result.gamma_cert.has_value());
  REQUIRE(*relative_result.gamma_cert <= relative.epsilon_rel);

  auto zero_lower = certified_evidence();
  zero_lower.lower_bound = 0.0;
  zero_lower.upper_bound = 1.0;
  zero_lower.epsilon_abs = 0.1;
  zero_lower.epsilon_rel = 1.0;
  auto zero_lower_result =
      kdc::finalize_temporal_result_v2(zero_lower);
  REQUIRE(zero_lower_result.status ==
          kdc::TemporalFinalStatusV2::HEURISTIC);
  REQUIRE_FALSE(zero_lower_result.gamma_cert.has_value());
  REQUIRE(zero_lower_result.metric_note.find("LB=0") !=
          std::string::npos);
}

TEST_CASE("Temporal result V2 defines zero-UB metrics explicitly") {
  auto evidence = certified_evidence();
  evidence.lower_bound = 0.0;
  evidence.upper_bound = 0.0;
  evidence.epsilon_abs = 0.0;
  evidence.epsilon_rel = 0.1;
  auto result = kdc::finalize_temporal_result_v2(evidence);
  REQUIRE(result.status == kdc::TemporalFinalStatusV2::CERTIFIED);
  REQUIRE(result.absolute_gap == 0.0);
  REQUIRE_FALSE(result.gamma_rep.has_value());
  REQUIRE_FALSE(result.gamma_cert.has_value());
  REQUIRE(result.metric_note.find("UB=0") != std::string::npos);
}

TEST_CASE("Temporal result V2 refuses unsupported assurance and sampled bounds") {
  auto fast = certified_evidence();
  fast.lower_bound_arithmetic =
      kdc::TemporalArithmeticAssuranceV2::FAST_FLOAT;
  fast.upper_bound_arithmetic =
      kdc::TemporalArithmeticAssuranceV2::FAST_FLOAT;
  fast.requested_stop_reason =
      kdc::TemporalStopReasonV2::GAP_ABS;
  auto fast_result = kdc::finalize_temporal_result_v2(fast);
  REQUIRE(fast_result.status == kdc::TemporalFinalStatusV2::INCOMPLETE);
  REQUIRE(fast_result.stop_reason ==
          kdc::TemporalStopReasonV2::CERTIFICATE_UNAVAILABLE);
  REQUIRE_FALSE(fast_result.UB_valid);

  auto sampled = certified_evidence();
  sampled.sampled_only = true;
  auto sampled_result =
      kdc::finalize_temporal_result_v2(sampled);
  REQUIRE(sampled_result.status ==
          kdc::TemporalFinalStatusV2::SAMPLED);
  REQUIRE_FALSE(sampled_result.LB_valid);
  REQUIRE_FALSE(sampled_result.UB_valid);
  REQUIRE(std::isinf(sampled_result.UB));
  REQUIRE_FALSE(sampled_result.incumbent_ref.has_value());

  sampled.has_inconsistent_bounds = true;
  const auto failed_sample =
      kdc::finalize_temporal_result_v2(sampled);
  REQUIRE(failed_sample.status ==
          kdc::TemporalFinalStatusV2::INCOMPLETE);
  REQUIRE(failed_sample.stop_reason ==
          kdc::TemporalStopReasonV2::INCONSISTENT);
  REQUIRE_FALSE(failed_sample.LB_valid);
  REQUIRE_FALSE(failed_sample.UB_valid);
}

TEST_CASE("Temporal result V2 preserves failures and rejects contradictions") {
  auto failed = certified_evidence();
  failed.continuous_verification_passed = false;
  failed.has_verification_failure = true;
  failed.requested_stop_reason =
      kdc::TemporalStopReasonV2::MIN_INTERVAL;
  auto failed_result = kdc::finalize_temporal_result_v2(failed);
  REQUIRE(failed_result.status ==
          kdc::TemporalFinalStatusV2::INCOMPLETE);
  REQUIRE(failed_result.stop_reason ==
          kdc::TemporalStopReasonV2::VERIFY_FAIL);

  auto contradiction = certified_evidence();
  contradiction.lower_bound = 2.0;
  contradiction.upper_bound = 1.0;
  auto contradiction_result =
      kdc::finalize_temporal_result_v2(contradiction);
  REQUIRE(contradiction_result.status ==
          kdc::TemporalFinalStatusV2::INCOMPLETE);
  REQUIRE(contradiction_result.stop_reason ==
          kdc::TemporalStopReasonV2::INCONSISTENT);
  REQUIRE_FALSE(contradiction_result.LB_valid);
  REQUIRE_FALSE(contradiction_result.UB_valid);
  REQUIRE(contradiction_result.LB == 0.0);
  REQUIRE(std::isinf(contradiction_result.UB));
}

TEST_CASE("Temporal result V2 validates every status and stop-reason pairing") {
  using Status = kdc::TemporalFinalStatusV2;
  using Reason = kdc::TemporalStopReasonV2;
  constexpr std::array statuses{
      Status::CERTIFIED,
      Status::NUMERICALLY_BOUNDED,
      Status::HEURISTIC,
      Status::SAMPLED,
      Status::INCOMPLETE};
  constexpr std::array reasons{
      Reason::NONE,
      Reason::GAP_ABS,
      Reason::GAP_REL,
      Reason::TIMEOUT,
      Reason::INTERVAL_CAP,
      Reason::ORACLE_CAP,
      Reason::ORACLE_TIME_LIMIT,
      Reason::MIN_INTERVAL,
      Reason::MIDPOINT_COLLAPSE,
      Reason::ORACLE_FAIL,
      Reason::VERIFY_FAIL,
      Reason::INCONSISTENT,
      Reason::CERTIFICATE_UNAVAILABLE,
      Reason::INVALID_INPUT};

  for (const auto status : statuses) {
    for (const auto reason : reasons) {
      const bool is_gap =
          reason == Reason::GAP_ABS ||
          reason == Reason::GAP_REL;
      const bool failure_reason =
          reason == Reason::VERIFY_FAIL ||
          reason == Reason::INCONSISTENT ||
          reason == Reason::INVALID_INPUT;
      const bool expected =
          status == Status::CERTIFIED
              ? is_gap
              : (!is_gap &&
                 (status == Status::INCOMPLETE ||
                  !failure_reason));
      CAPTURE(kdc::temporal_final_status_name_v2(status));
      CAPTURE(kdc::temporal_stop_reason_name_v2(reason));
      REQUIRE(kdc::valid_temporal_status_stop_pair_v2(
          status, reason) == expected);
    }
  }
}

TEST_CASE("Temporal result V2 accepts exact-zero and rejects negative tolerances") {
  auto evidence = certified_evidence();
  evidence.epsilon_abs = 0.0;
  evidence.epsilon_rel = 0.0;
  evidence.lower_bound = 1.0;
  evidence.upper_bound = 1.0;
  auto result = kdc::finalize_temporal_result_v2(evidence);
  REQUIRE(result.status == kdc::TemporalFinalStatusV2::CERTIFIED);
  REQUIRE(result.stop_reason == kdc::TemporalStopReasonV2::GAP_ABS);

  evidence.epsilon_abs = -1.0;
  result = kdc::finalize_temporal_result_v2(evidence);
  REQUIRE(result.status == kdc::TemporalFinalStatusV2::INCOMPLETE);
  REQUIRE(result.stop_reason ==
          kdc::TemporalStopReasonV2::INVALID_INPUT);
}

TEST_CASE("Temporal thesis solver returns a separate V2 final schema") {
  kdc::Instance instance;
  instance.n = 1;
  instance.m = 1;
  instance.T_end = 1.0;
  instance.stations = {
      {10, kdc::Point(0.0, 0.0)}};
  instance.trajectories = {
      kdc::Trajectory(
          {0.0, 1.0},
          {kdc::Point(0.0, 0.0),
           kdc::Point(1.0, 0.0)})};

  kdc::TemporalThesisSolver solver(instance);
  kdc::TemporalThesisConfigV2 config;
  config.execution.max_leaves = 1U;
  config.execution.global_time_limit_seconds = 1.0;
  config.execution.oracle_time_limit_seconds = 0.5;
  config.epsilon_abs = 0.01;
  config.epsilon_rel = 0.0;

  const auto result = solver.solve_v2({}, config);
  REQUIRE(result.schema_version == 2U);
  REQUIRE(result.final.schema_version == 2U);
  REQUIRE(result.execution.schema_version == 1U);
  REQUIRE(result.execution.status ==
          kdc::TemporalThesisStatusV1::MAX_LEAVES);
  REQUIRE(result.final.status ==
          kdc::TemporalFinalStatusV2::INCOMPLETE);
  REQUIRE(result.final.stop_reason ==
          kdc::TemporalStopReasonV2::INTERVAL_CAP);
  REQUIRE(result.final.LB == 0.0);
  REQUIRE(result.final.LB_valid);
  REQUIRE(std::isinf(result.final.UB));
  REQUIRE_FALSE(result.final.UB_valid);
  REQUIRE(result.final.oracle_statistics.heuristic_calls > 0U);
}
