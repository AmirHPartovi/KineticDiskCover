#include "kdc/ablation.hpp"

#include <algorithm>
#include <cctype>
#include <limits>
#include <set>
#include <stdexcept>
#include <string>

namespace kdc {
namespace {

const std::vector<E6FactorConfigurationV1> kE6Configurations{
    {false, false, false},
    {true, false, false},
    {false, true, false},
    {false, false, true},
    {true, true, false},
    {true, false, true},
    {false, true, true},
    {true, true, true}};

void append_token(std::string& output, const std::string& value) {
  output += std::to_string(value.size());
  output += ':';
  output += value;
}

bool valid_sha256(const std::string& value) {
  return value.size() == 64U &&
         std::all_of(value.begin(), value.end(), [](unsigned char ch) {
           return std::isxdigit(ch) != 0;
         });
}

void validate_pair_context(const AblationPairContextV1& context) {
  if (context.schema_version != 1U || context.experiment_id != "E6" ||
      context.sample_id.empty() || !valid_sha256(context.instance_sha256) ||
      context.source_revision.empty() || !valid_sha256(context.binary_sha256) ||
      context.machine_identity.empty() || context.compiler_identity.empty() ||
      context.solver_identity.empty() || context.model_version.empty() ||
      context.arithmetic_mode.empty() || context.budget_manifest.empty() ||
      context.solver_configuration.empty() ||
      context.fixed_feature_configuration.empty()) {
    throw std::invalid_argument(
        "E6 pairing context is incomplete or has an invalid digest");
  }
  for (std::size_t index = 0; index < context.parameters.size(); ++index) {
    const auto& parameter = context.parameters[index];
    if (parameter.first.empty() || parameter.second.empty() ||
        (index > 0U &&
         context.parameters[index - 1U].first >= parameter.first)) {
      throw std::invalid_argument(
          "E6 parameters must have unique, nonempty names in sorted order");
    }
  }
}

std::uint64_t checked_sum(
    std::uint64_t left,
    std::uint64_t right,
    const char* message) {
  if (right > std::numeric_limits<std::uint64_t>::max() - left) {
    throw std::overflow_error(message);
  }
  return left + right;
}

bool valid_configuration(const E6FactorConfigurationV1& configuration) {
  return std::any_of(
      kE6Configurations.begin(), kE6Configurations.end(),
      [&configuration](const E6FactorConfigurationV1& known) {
        return configuration == known;
      });
}

}  // namespace

std::string E6FactorConfigurationV1::code() const {
  std::string result{"R"};
  result += warm_start ? '1' : '0';
  result += cache ? '1' : '0';
  result += value_preserving_reductions ? '1' : '0';
  return result;
}

bool E6FactorConfigurationV1::operator==(
    const E6FactorConfigurationV1& other) const noexcept {
  return warm_start == other.warm_start && cache == other.cache &&
         value_preserving_reductions ==
             other.value_preserving_reductions;
}

const std::vector<E6FactorConfigurationV1>& e6_configurations_v1() {
  return kE6Configurations;
}

E6FactorConfigurationV1 e6_configuration_v1(const std::string& code) {
  const auto found = std::find_if(
      kE6Configurations.begin(), kE6Configurations.end(),
      [&code](const E6FactorConfigurationV1& configuration) {
        return configuration.code() == code;
      });
  if (found == kE6Configurations.end()) {
    throw std::invalid_argument("unknown Chapter 7 E6 configuration: " + code);
  }
  return *found;
}

WeightedPreprocessingOptionsV1 apply_e6_configuration_v1(
    const E6FactorConfigurationV1& configuration,
    const WeightedPreprocessingOptionsV1& fixed_options) {
  if (!valid_configuration(configuration)) {
    throw std::invalid_argument("invalid E6 factor configuration");
  }
  auto result = fixed_options;
  result.station_dominance =
      configuration.value_preserving_reductions;
  result.point_dominance =
      configuration.value_preserving_reductions;
  return result;
}

void validate_ablation_manifest_v1(
    const AblationRunManifestV1& manifest) {
  if (manifest.schema_version != 1U ||
      !valid_configuration(manifest.configuration)) {
    throw std::invalid_argument(
        "unsupported E6 manifest schema or feature configuration");
  }
  validate_pair_context(manifest.pair_context);
}

std::string canonical_ablation_pair_context_v1(
    const AblationPairContextV1& context) {
  validate_pair_context(context);
  std::string result;
  append_token(result, std::to_string(context.schema_version));
  append_token(result, context.experiment_id);
  append_token(result, context.sample_id);
  append_token(result, context.instance_sha256);
  append_token(result, std::to_string(context.replicate_seed));
  append_token(result, context.source_revision);
  append_token(result, context.binary_sha256);
  append_token(result, context.machine_identity);
  append_token(result, context.compiler_identity);
  append_token(result, context.solver_identity);
  append_token(result, context.model_version);
  append_token(result, context.arithmetic_mode);
  append_token(result, context.budget_manifest);
  append_token(result, context.solver_configuration);
  append_token(result, context.fixed_feature_configuration);
  append_token(result, std::to_string(context.parameters.size()));
  for (const auto& parameter : context.parameters) {
    append_token(result, parameter.first);
    append_token(result, parameter.second);
  }
  return result;
}

std::string canonical_ablation_manifest_v1(
    const AblationRunManifestV1& manifest) {
  validate_ablation_manifest_v1(manifest);
  std::string result{"E6-RUN-V1"};
  append_token(result,
               canonical_ablation_pair_context_v1(manifest.pair_context));
  append_token(result, manifest.configuration.code());
  return result;
}

bool same_ablation_pair_context_v1(
    const AblationRunManifestV1& left,
    const AblationRunManifestV1& right) {
  validate_ablation_manifest_v1(left);
  validate_ablation_manifest_v1(right);
  return canonical_ablation_pair_context_v1(left.pair_context) ==
         canonical_ablation_pair_context_v1(right.pair_context);
}

bool AblationTimingV1::identities_hold() const noexcept {
  if (t_pre_ns > std::numeric_limits<std::uint64_t>::max() - t_bound_ns ||
      t_pre_ns + t_bound_ns >
          std::numeric_limits<std::uint64_t>::max() - t_oracle_ns) {
    return false;
  }
  const auto classified_loop_ns = t_pre_ns + t_bound_ns + t_oracle_ns;
  if (classified_loop_ns >
          std::numeric_limits<std::uint64_t>::max() - t_other_loop_ns ||
      classified_loop_ns + t_other_loop_ns != t_loop_ns ||
      t_reduce_ns > t_pre_ns) {
    return false;
  }
  if (t_loop_ns > std::numeric_limits<std::uint64_t>::max() -
                      t_verify_ns ||
      t_loop_ns + t_verify_ns >
          std::numeric_limits<std::uint64_t>::max() - t_post_ns) {
    return false;
  }
  return t_loop_ns + t_verify_ns + t_post_ns == t_wall_ns;
}

double AblationTimingV1::t_pre() const noexcept {
  return static_cast<double>(t_pre_ns) / 1e9;
}

double AblationTimingV1::t_bound() const noexcept {
  return static_cast<double>(t_bound_ns) / 1e9;
}

double AblationTimingV1::t_oracle() const noexcept {
  return static_cast<double>(t_oracle_ns) / 1e9;
}

double AblationTimingV1::t_reduce() const noexcept {
  return static_cast<double>(t_reduce_ns) / 1e9;
}

double AblationTimingV1::t_other_loop() const noexcept {
  return static_cast<double>(t_other_loop_ns) / 1e9;
}

double AblationTimingV1::t_loop() const noexcept {
  return static_cast<double>(t_loop_ns) / 1e9;
}

double AblationTimingV1::t_verify() const noexcept {
  return static_cast<double>(t_verify_ns) / 1e9;
}

double AblationTimingV1::t_post() const noexcept {
  return static_cast<double>(t_post_ns) / 1e9;
}

double AblationTimingV1::t_wall() const noexcept {
  return static_cast<double>(t_wall_ns) / 1e9;
}

std::uint64_t AblationTimingRecorderV1::elapsed_nanoseconds(
    Clock::time_point start,
    Clock::time_point end) {
  const auto elapsed =
      std::chrono::duration_cast<std::chrono::nanoseconds>(end - start)
          .count();
  if (elapsed < 0) {
    throw std::logic_error("steady clock returned a negative duration");
  }
  return static_cast<std::uint64_t>(elapsed);
}

void AblationTimingRecorderV1::start() {
  if (state_ != State::CREATED) {
    throw std::logic_error("ablation timer can only be started once");
  }
  run_start_ = Clock::now();
  state_ = State::RUNNING;
}

void AblationTimingRecorderV1::begin_loop() {
  if (state_ != State::RUNNING) {
    throw std::logic_error("main loop must start exactly once");
  }
  loop_start_ = run_start_;
  state_ = State::IN_LOOP;
}

void AblationTimingRecorderV1::begin_phase(
    AblationTimingPhaseV1 phase) {
  if (active_phase_) {
    throw std::logic_error("ablation timing phases cannot overlap");
  }
  const bool loop_phase =
      phase == AblationTimingPhaseV1::PRE ||
      phase == AblationTimingPhaseV1::BOUND ||
      phase == AblationTimingPhaseV1::ORACLE;
  if ((loop_phase && state_ != State::IN_LOOP) ||
      (phase == AblationTimingPhaseV1::VERIFY &&
       state_ != State::LOOP_ENDED)) {
    throw std::logic_error(
        "timing phase is outside its Chapter 7 accounting interval");
  }
  active_phase_ = phase;
  phase_start_ = Clock::now();
}

void AblationTimingRecorderV1::end_phase() {
  if (!active_phase_) {
    throw std::logic_error("no ablation timing phase is active");
  }
  if (reduction_start_) {
    throw std::logic_error(
        "reduction timing must end before its preprocessing phase");
  }
  const std::uint64_t elapsed =
      elapsed_nanoseconds(phase_start_, Clock::now());
  switch (*active_phase_) {
    case AblationTimingPhaseV1::PRE:
      timing_.t_pre_ns =
          checked_sum(timing_.t_pre_ns, elapsed, "pre time overflow");
      break;
    case AblationTimingPhaseV1::BOUND:
      timing_.t_bound_ns =
          checked_sum(timing_.t_bound_ns, elapsed, "bound time overflow");
      break;
    case AblationTimingPhaseV1::ORACLE:
      timing_.t_oracle_ns =
          checked_sum(timing_.t_oracle_ns, elapsed, "oracle time overflow");
      break;
    case AblationTimingPhaseV1::VERIFY:
      timing_.t_verify_ns =
          checked_sum(timing_.t_verify_ns, elapsed, "verify time overflow");
      break;
  }
  active_phase_.reset();
}

void AblationTimingRecorderV1::begin_reduction() {
  if (state_ != State::IN_LOOP || !active_phase_ ||
      *active_phase_ != AblationTimingPhaseV1::PRE || reduction_start_) {
    throw std::logic_error(
        "reduction time must be measured inside preprocessing");
  }
  reduction_start_ = Clock::now();
}

void AblationTimingRecorderV1::end_reduction() {
  if (!reduction_start_) {
    throw std::logic_error("no reduction timing phase is active");
  }
  const auto elapsed =
      elapsed_nanoseconds(*reduction_start_, Clock::now());
  timing_.t_reduce_ns =
      checked_sum(timing_.t_reduce_ns, elapsed, "reduction time overflow");
  reduction_start_.reset();
}

void AblationTimingRecorderV1::end_loop() {
  if (state_ != State::IN_LOOP || active_phase_ || reduction_start_) {
    throw std::logic_error(
        "main loop cannot end with an active timing phase");
  }
  timing_.t_loop_ns = elapsed_nanoseconds(loop_start_, Clock::now());
  const auto classified_ns = checked_sum(
      checked_sum(timing_.t_pre_ns, timing_.t_bound_ns,
                  "classified loop time overflow"),
      timing_.t_oracle_ns, "classified loop time overflow");
  if (classified_ns > timing_.t_loop_ns) {
    throw std::logic_error(
        "classified loop phases exceed measured Chapter 7 t_loop");
  }
  timing_.t_other_loop_ns = timing_.t_loop_ns - classified_ns;
  state_ = State::LOOP_ENDED;
}

AblationTimingV1 AblationTimingRecorderV1::finish() {
  if (state_ != State::LOOP_ENDED || active_phase_ || reduction_start_) {
    throw std::logic_error(
        "ablation timing requires a completed loop and no active phase");
  }
  timing_.t_wall_ns = elapsed_nanoseconds(run_start_, Clock::now());
  const auto loop_and_verify = checked_sum(
      timing_.t_loop_ns, timing_.t_verify_ns, "wall time overflow");
  if (loop_and_verify > timing_.t_wall_ns) {
    throw std::logic_error(
        "loop and verifier times exceed measured wall time");
  }
  timing_.t_post_ns = timing_.t_wall_ns - loop_and_verify;
  if (!timing_.identities_hold()) {
    throw std::logic_error("Chapter 7 timing identities do not hold");
  }
  state_ = State::FINISHED;
  return timing_;
}

AblationRunValidationV1 validate_ablation_run_v1(
    const AblationRunV1& run) {
  try {
    validate_ablation_manifest_v1(run.manifest);
  } catch (const std::exception& error) {
    return {false, std::nullopt, error.what()};
  }
  if (!run.timing.identities_hold()) {
    return {false, std::nullopt,
            "timing record violates a Chapter 7 time identity"};
  }

  const auto& configuration = run.manifest.configuration;
  if (!configuration.warm_start && run.output.warm_start_used) {
    return {false, std::nullopt,
            "warm start was used while its E6 factor was off"};
  }
  if (run.output.warm_start_used &&
      !run.output.warm_start_revalidated_on_target) {
    return {false, std::nullopt,
            "used warm start was not validated on the target input"};
  }
  if (run.output.warm_start_used_as_lower_bound) {
    return {false, std::nullopt,
            "a warm-start assignment/cost cannot be a lower-bound certificate"};
  }
  if (!configuration.cache && run.output.cache_hit_used) {
    return {false, std::nullopt, "cache hit used while its E6 factor was off"};
  }
  if (run.output.cache_hit_used &&
      (!run.output.cache_payload_revalidated ||
       !run.output.cached_bound_certificates_revalidated)) {
    return {false, std::nullopt,
            "cache hit lacks payload or certificate revalidation"};
  }
  if (!configuration.value_preserving_reductions &&
      run.output.reductions_applied) {
    return {false, std::nullopt,
            "value-preserving reductions were applied while their E6 factor "
            "was off"};
  }
  if (run.output.reductions_applied &&
      (!run.output.reduction_value_preservation_validated ||
       !run.output.reduced_assignment_reconstructed_and_validated)) {
    return {false, std::nullopt,
            "applied reductions lack value-preservation or assignment "
            "revalidation"};
  }
  if (run.output.reductions_applied !=
      (run.rows_removed > 0U || run.columns_removed > 0U)) {
    return {false, std::nullopt,
            "reduction counters disagree with whether reductions were applied"};
  }

  auto final_result =
      finalize_temporal_result_v2(run.output.temporal_evidence);
  if (!valid_temporal_status_stop_pair_v2(final_result.status,
                                          final_result.stop_reason)) {
    return {false, std::nullopt,
            "shared V2 finalizer returned an invalid status/stop pair"};
  }
  if (final_result.UB_valid &&
      !run.output.assignment_valid_on_original_input) {
    return {false, std::nullopt,
            "valid UB lacks an assignment validated on the original input"};
  }
  return {true, std::move(final_result),
          "standard V2 finalization and E6 evidence checks passed"};
}

E6BlockValidationV1 validate_e6_block_v1(
    const std::vector<AblationRunV1>& runs) {
  if (runs.size() != kE6Configurations.size()) {
    return {false, "an E6 block must contain exactly eight runs"};
  }
  std::set<std::string> seen_configurations;
  for (std::size_t index = 0; index < runs.size(); ++index) {
    const auto validation = validate_ablation_run_v1(runs[index]);
    if (!validation.accepted) {
      return {false, "run " + std::to_string(index) +
                         " is not semantically valid: " +
                         validation.reason};
    }
    if (index > 0U &&
        !same_ablation_pair_context_v1(runs.front().manifest,
                                       runs[index].manifest)) {
      return {false,
              "E6 block runs do not have identical non-factor manifests"};
    }
    if (!seen_configurations.insert(
             runs[index].manifest.configuration.code())
             .second) {
      return {false, "E6 block contains a duplicate configuration"};
    }
  }
  for (const auto& configuration : kE6Configurations) {
    if (seen_configurations.count(configuration.code()) == 0U) {
      return {false, "E6 block is missing " + configuration.code()};
    }
  }
  return {true, "complete, paired, semantically valid E6 block"};
}

PairedAblationTimingComparisonV1 compare_paired_ablation_runs_v1(
    const AblationRunV1& left,
    const AblationRunV1& right) {
  const auto reject = [](const std::string& reason) {
    PairedAblationTimingComparisonV1 result;
    result.reason = reason;
    return result;
  };
  try {
    if (!same_ablation_pair_context_v1(left.manifest, right.manifest)) {
      return reject("paired runs do not have identical non-factor manifests");
    }
  } catch (const std::exception& error) {
    return reject(std::string("invalid pairing manifest: ") + error.what());
  }
  if (left.manifest.configuration == right.manifest.configuration) {
    return reject("paired runs must use two distinct E6 configurations");
  }

  const auto left_validation = validate_ablation_run_v1(left);
  if (!left_validation.accepted) {
    return reject("left run is not semantically comparable: " +
                  left_validation.reason);
  }
  const auto right_validation = validate_ablation_run_v1(right);
  if (!right_validation.accepted) {
    return reject("right run is not semantically comparable: " +
                  right_validation.reason);
  }

  PairedAblationTimingComparisonV1 result;
  result.accepted = true;
  result.reason =
      "identical paired context; both outputs passed the shared V2 semantics";
  result.left_status = left_validation.final_result->status;
  result.right_status = right_validation.final_result->status;
  result.delta_t_pre = left.timing.t_pre() - right.timing.t_pre();
  result.delta_t_bound = left.timing.t_bound() - right.timing.t_bound();
  result.delta_t_oracle = left.timing.t_oracle() - right.timing.t_oracle();
  result.delta_t_reduce = left.timing.t_reduce() - right.timing.t_reduce();
  result.delta_t_loop = left.timing.t_loop() - right.timing.t_loop();
  result.delta_t_verify = left.timing.t_verify() - right.timing.t_verify();
  result.delta_t_wall = left.timing.t_wall() - right.timing.t_wall();
  return result;
}

}  // namespace kdc
