#include "kdc/ablation.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <string>

namespace {

kdc::AblationPairContextV1 pair_context() {
  kdc::AblationPairContextV1 context;
  context.sample_id = "F1-20-5-2-r00";
  context.instance_sha256 = std::string(64U, 'a');
  context.replicate_seed = 19U;
  context.source_revision = "source-revision";
  context.binary_sha256 = std::string(64U, 'b');
  context.machine_identity = "host-os-cpu-resource-class";
  context.compiler_identity = "clang-17;flags=release";
  context.solver_identity = "built-in-bnb-v1";
  context.model_version = "Model-A-v2";
  context.arithmetic_mode = "FAST_FLOAT";
  context.budget_manifest = "wall=1800;leaves=4096;oracle=24576";
  context.solver_configuration = "threads=1;seed=31";
  context.fixed_feature_configuration =
      "C0-C4=fixed;duplicate-center-symmetry=on";
  context.parameters = {{"delta_min", "2^-20"},
                        {"epsilon_abs", "0.01"},
                        {"epsilon_rel", "0.05"}};
  return context;
}

kdc::TemporalFinalEvidenceV2 certified_evidence() {
  kdc::TemporalFinalEvidenceV2 evidence;
  evidence.epsilon_abs = 0.01;
  evidence.epsilon_rel = 0.05;
  evidence.lower_bound = 1.0;
  evidence.upper_bound = 1.005;
  evidence.lower_bound_valid = true;
  evidence.upper_bound_valid = true;
  evidence.current_lb_contributions_valid = true;
  evidence.historical_certificates_valid = true;
  evidence.lower_bound_method = "validated lower";
  evidence.upper_bound_method = "verified complete program";
  evidence.lower_bound_arithmetic =
      kdc::TemporalArithmeticAssuranceV2::DIRECTED_INTERVAL;
  evidence.upper_bound_arithmetic =
      kdc::TemporalArithmeticAssuranceV2::DIRECTED_INTERVAL;
  evidence.lower_bound_provenance = {"dual certificate"};
  evidence.upper_bound_provenance = {"snapshot verifier"};
  evidence.lower_bound_certificate_ids = {"lb-1"};
  evidence.upper_bound_certificate_ids = {"ub-1"};
  evidence.complete_model_a_program = true;
  evidence.continuous_verification_passed = true;
  evidence.upper_cost_enclosure_consistent = true;
  evidence.requested_stop_reason = kdc::TemporalStopReasonV2::GAP_ABS;
  evidence.incumbent_ref = "snapshot-1";
  evidence.provenance = {"paired fixture"};
  return evidence;
}

kdc::AblationTimingV1 timing(
    std::uint64_t t_pre,
    std::uint64_t t_bound,
    std::uint64_t t_oracle,
    std::uint64_t t_reduce,
    std::uint64_t t_other,
    std::uint64_t t_verify,
    std::uint64_t t_post) {
  kdc::AblationTimingV1 result;
  result.t_pre_ns = t_pre;
  result.t_bound_ns = t_bound;
  result.t_oracle_ns = t_oracle;
  result.t_reduce_ns = t_reduce;
  result.t_other_loop_ns = t_other;
  result.t_loop_ns = t_pre + t_bound + t_oracle + t_other;
  result.t_verify_ns = t_verify;
  result.t_post_ns = t_post;
  result.t_wall_ns = result.t_loop_ns + t_verify + t_post;
  return result;
}

kdc::AblationRunV1 run_for(const std::string& code) {
  kdc::AblationRunV1 run;
  run.manifest.pair_context = pair_context();
  run.manifest.configuration = kdc::e6_configuration_v1(code);
  run.timing = timing(10U, 20U, 30U, 2U, 40U, 25U, 5U);
  run.output.temporal_evidence = certified_evidence();
  run.output.assignment_valid_on_original_input = true;

  if (run.manifest.configuration.warm_start) {
    run.output.warm_start_used = true;
    run.output.warm_start_revalidated_on_target = true;
  }
  if (run.manifest.configuration.cache) {
    run.output.cache_hit_used = true;
    run.output.cache_payload_revalidated = true;
    run.output.cached_bound_certificates_revalidated = true;
  }
  if (run.manifest.configuration.value_preserving_reductions) {
    run.output.reductions_applied = true;
    run.output.reduction_value_preservation_validated = true;
    run.output.reduced_assignment_reconstructed_and_validated = true;
    run.rows_removed = 1U;
    run.columns_removed = 1U;
  }
  return run;
}

}  // namespace

TEST_CASE("E6 factor IDs follow the Chapter 7 w-c-r bit order") {
  const std::array<std::string, 8U> expected{
      "R000", "R100", "R010", "R001",
      "R110", "R101", "R011", "R111"};
  const auto& configurations = kdc::e6_configurations_v1();
  REQUIRE(configurations.size() == expected.size());
  for (std::size_t index = 0; index < expected.size(); ++index) {
    REQUIRE(configurations[index].code() == expected[index]);
    REQUIRE(kdc::e6_configuration_v1(expected[index]) ==
            configurations[index]);
  }

  REQUIRE(kdc::e6_configuration_v1("R100").warm_start);
  REQUIRE_FALSE(kdc::e6_configuration_v1("R100").cache);
  REQUIRE_FALSE(
      kdc::e6_configuration_v1("R100").value_preserving_reductions);
  REQUIRE(kdc::e6_configuration_v1("R010").cache);
  REQUIRE(kdc::e6_configuration_v1("R001").value_preserving_reductions);
  REQUIRE_THROWS_AS(kdc::e6_configuration_v1("R10"), std::invalid_argument);
  REQUIRE_THROWS_AS(kdc::e6_configuration_v1("R200"), std::invalid_argument);
}

TEST_CASE("E6 reduction factor toggles only the two Chapter 7 reductions") {
  kdc::WeightedPreprocessingOptionsV1 fixed;
  fixed.radius_square_upper_bounds = true;
  fixed.equal_row_symmetry_breaking = true;
  fixed.objective_lower_cut = true;
  fixed.station_dominance = true;
  fixed.point_dominance = true;

  const auto off =
      kdc::apply_e6_configuration_v1(kdc::e6_configuration_v1("R000"), fixed);
  REQUIRE(off.radius_square_upper_bounds);
  REQUIRE(off.equal_row_symmetry_breaking);
  REQUIRE(off.objective_lower_cut);
  REQUIRE_FALSE(off.station_dominance);
  REQUIRE_FALSE(off.point_dominance);

  const auto on =
      kdc::apply_e6_configuration_v1(kdc::e6_configuration_v1("R001"), fixed);
  REQUIRE(on.radius_square_upper_bounds);
  REQUIRE(on.equal_row_symmetry_breaking);
  REQUIRE(on.objective_lower_cut);
  REQUIRE(on.station_dominance);
  REQUIRE(on.point_dominance);
}

TEST_CASE("E6 manifests allow pairing only with identical non-factor context") {
  auto baseline = run_for("R000");
  auto warm = run_for("R100");
  REQUIRE(kdc::same_ablation_pair_context_v1(baseline.manifest, warm.manifest));
  REQUIRE(kdc::canonical_ablation_pair_context_v1(
              baseline.manifest.pair_context) ==
          kdc::canonical_ablation_pair_context_v1(warm.manifest.pair_context));
  REQUIRE(kdc::canonical_ablation_manifest_v1(baseline.manifest) !=
          kdc::canonical_ablation_manifest_v1(warm.manifest));

  auto changed = warm;
  changed.manifest.pair_context.budget_manifest += ";max-leaves=4095";
  REQUIRE_FALSE(
      kdc::same_ablation_pair_context_v1(baseline.manifest,
                                         changed.manifest));
  changed = warm;
  changed.manifest.pair_context.instance_sha256[0] = 'c';
  REQUIRE_FALSE(
      kdc::same_ablation_pair_context_v1(baseline.manifest,
                                         changed.manifest));
  changed = warm;
  changed.manifest.pair_context.fixed_feature_configuration =
      "C0-C4=changed";
  REQUIRE_FALSE(
      kdc::same_ablation_pair_context_v1(baseline.manifest,
                                         changed.manifest));
  changed = warm;
  changed.manifest.pair_context.parameters[1].second = "0.02";
  REQUIRE_FALSE(
      kdc::same_ablation_pair_context_v1(baseline.manifest,
                                         changed.manifest));
  changed = warm;
  changed.manifest.pair_context.parameters[0].first = "z-last";
  REQUIRE_THROWS_AS(
      kdc::validate_ablation_manifest_v1(changed.manifest),
      std::invalid_argument);
}

TEST_CASE("E6 timer enforces Chapter 7 exclusive timing identities") {
  kdc::AblationTimingRecorderV1 recorder;
  recorder.start();
  recorder.begin_loop();
  recorder.begin_phase(kdc::AblationTimingPhaseV1::PRE);
  recorder.begin_reduction();
  recorder.end_reduction();
  recorder.end_phase();
  recorder.begin_phase(kdc::AblationTimingPhaseV1::BOUND);
  recorder.end_phase();
  recorder.begin_phase(kdc::AblationTimingPhaseV1::ORACLE);
  recorder.end_phase();
  recorder.end_loop();
  recorder.begin_phase(kdc::AblationTimingPhaseV1::VERIFY);
  recorder.end_phase();
  const auto measured = recorder.finish();

  REQUIRE(measured.identities_hold());
  REQUIRE(measured.t_loop_ns ==
          measured.t_pre_ns + measured.t_bound_ns +
              measured.t_oracle_ns + measured.t_other_loop_ns);
  REQUIRE(measured.t_wall_ns ==
          measured.t_loop_ns + measured.t_verify_ns + measured.t_post_ns);
  REQUIRE(measured.t_reduce_ns <= measured.t_pre_ns);
}

TEST_CASE("E6 timer rejects phases outside their declared accounting periods") {
  kdc::AblationTimingRecorderV1 recorder;
  REQUIRE_THROWS_AS(
      recorder.begin_phase(kdc::AblationTimingPhaseV1::PRE),
      std::logic_error);
  recorder.start();
  recorder.begin_loop();
  REQUIRE_THROWS_AS(
      recorder.begin_phase(kdc::AblationTimingPhaseV1::VERIFY),
      std::logic_error);
  recorder.begin_phase(kdc::AblationTimingPhaseV1::PRE);
  REQUIRE_THROWS_AS(
      recorder.begin_phase(kdc::AblationTimingPhaseV1::BOUND),
      std::logic_error);
  REQUIRE_THROWS_AS(recorder.end_loop(), std::logic_error);
  recorder.end_phase();
  recorder.end_loop();
  REQUIRE_THROWS_AS(
      recorder.begin_phase(kdc::AblationTimingPhaseV1::ORACLE),
      std::logic_error);
}

TEST_CASE("All E6 configs retain the shared final-result semantics") {
  for (const auto& configuration : kdc::e6_configurations_v1()) {
    auto run = run_for(configuration.code());
    const auto validation = kdc::validate_ablation_run_v1(run);
    REQUIRE(validation.accepted);
    REQUIRE(validation.final_result.has_value());
    REQUIRE(validation.final_result->status ==
            kdc::TemporalFinalStatusV2::CERTIFIED);
    REQUIRE(validation.final_result->stop_reason ==
            kdc::TemporalStopReasonV2::GAP_ABS);
    REQUIRE(validation.final_result->LB_valid);
    REQUIRE(validation.final_result->UB_valid);
  }
}

TEST_CASE("E6 block requires all eight valid runs with identical context") {
  std::vector<kdc::AblationRunV1> runs;
  for (const auto& configuration : kdc::e6_configurations_v1()) {
    runs.push_back(run_for(configuration.code()));
  }
  REQUIRE(kdc::validate_e6_block_v1(runs).accepted);

  auto incomplete = runs;
  incomplete.pop_back();
  REQUIRE_FALSE(kdc::validate_e6_block_v1(incomplete).accepted);

  auto duplicate = runs;
  duplicate.back().manifest.configuration =
      duplicate.front().manifest.configuration;
  REQUIRE_FALSE(kdc::validate_e6_block_v1(duplicate).accepted);

  auto unpaired = runs;
  unpaired[4].manifest.pair_context.budget_manifest += ";budget-change";
  REQUIRE_FALSE(kdc::validate_e6_block_v1(unpaired).accepted);

  auto invalid_output = runs;
  invalid_output[0].output.cache_hit_used = true;
  REQUIRE_FALSE(kdc::validate_e6_block_v1(invalid_output).accepted);
}

TEST_CASE("E6 refuses semantically unvalidated feature outputs") {
  SECTION("warm-start assignment must be revalidated") {
    auto run = run_for("R100");
    run.output.warm_start_revalidated_on_target = false;
    REQUIRE_FALSE(kdc::validate_ablation_run_v1(run).accepted);
    run.output.warm_start_revalidated_on_target = true;
    run.output.warm_start_used_as_lower_bound = true;
    REQUIRE_FALSE(kdc::validate_ablation_run_v1(run).accepted);
  }

  SECTION("cache payload and claimed certificates must be revalidated") {
    auto run = run_for("R010");
    run.output.cached_bound_certificates_revalidated = false;
    REQUIRE_FALSE(kdc::validate_ablation_run_v1(run).accepted);
    run.output.cached_bound_certificates_revalidated = true;
    run.output.cache_payload_revalidated = false;
    REQUIRE_FALSE(kdc::validate_ablation_run_v1(run).accepted);
  }

  SECTION("reductions require proof and original-index reconstruction") {
    auto run = run_for("R001");
    run.output.reduction_value_preservation_validated = false;
    REQUIRE_FALSE(kdc::validate_ablation_run_v1(run).accepted);
    run.output.reduction_value_preservation_validated = true;
    run.output.reduced_assignment_reconstructed_and_validated = false;
    REQUIRE_FALSE(kdc::validate_ablation_run_v1(run).accepted);
  }

  SECTION("valid upper bound requires original-input assignment validation") {
    auto run = run_for("R000");
    run.output.assignment_valid_on_original_input = false;
    REQUIRE_FALSE(kdc::validate_ablation_run_v1(run).accepted);
  }

  SECTION("disabled factors cannot affect the run") {
    auto run = run_for("R000");
    run.output.cache_hit_used = true;
    REQUIRE_FALSE(kdc::validate_ablation_run_v1(run).accepted);
    run.output.cache_hit_used = false;
    run.output.reductions_applied = true;
    REQUIRE_FALSE(kdc::validate_ablation_run_v1(run).accepted);
  }
}

TEST_CASE("E6 paired timing comparison requires matching manifests and semantics") {
  auto baseline = run_for("R000");
  auto treatment = run_for("R111");
  treatment.timing = timing(20U, 10U, 15U, 7U, 5U, 25U, 5U);

  const auto comparison =
      kdc::compare_paired_ablation_runs_v1(baseline, treatment);
  REQUIRE(comparison.accepted);
  REQUIRE(comparison.left_status ==
          kdc::TemporalFinalStatusV2::CERTIFIED);
  REQUIRE(comparison.right_status ==
          kdc::TemporalFinalStatusV2::CERTIFIED);
  REQUIRE(comparison.delta_t_loop > 0.0);
  REQUIRE(comparison.delta_t_verify == 0.0);
  REQUIRE(comparison.delta_t_wall > 0.0);

  SECTION("counts alone do not contribute a timing effect") {
    baseline.rows_removed = 0U;
    baseline.columns_removed = 0U;
    baseline.events_processed = 100000U;
    treatment.rows_removed = 10U;
    treatment.columns_removed = 20U;
    treatment.events_processed = 1U;
    baseline.timing = timing(10U, 10U, 10U, 0U, 10U, 5U, 5U);
    treatment.timing = baseline.timing;
    const auto counts_only =
        kdc::compare_paired_ablation_runs_v1(baseline, treatment);
    REQUIRE(counts_only.accepted);
    REQUIRE(counts_only.delta_t_loop == 0.0);
    REQUIRE(counts_only.delta_t_wall == 0.0);
  }

  SECTION("unpaired contexts cannot produce timing deltas") {
    treatment.manifest.pair_context.solver_configuration += ";presolve=off";
    REQUIRE_FALSE(
        kdc::compare_paired_ablation_runs_v1(baseline, treatment).accepted);
  }

  SECTION("identical configurations are not an ablation pair") {
    treatment.manifest.configuration = baseline.manifest.configuration;
    REQUIRE_FALSE(
        kdc::compare_paired_ablation_runs_v1(baseline, treatment).accepted);
  }

  SECTION("invalid manifests cannot be compared") {
    treatment.manifest.pair_context.binary_sha256 = "not-a-digest";
    REQUIRE_FALSE(
        kdc::compare_paired_ablation_runs_v1(baseline, treatment).accepted);
  }
}
