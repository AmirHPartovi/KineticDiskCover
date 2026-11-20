#pragma once

#include "kdc/temporal_result_v2.hpp"
#include "kdc/weighted_optimizations.hpp"

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace kdc {

struct E6FactorConfigurationV1 {
  bool warm_start{false};
  bool cache{false};
  bool value_preserving_reductions{false};

  std::string code() const;
  bool operator==(const E6FactorConfigurationV1& other) const noexcept;
};

const std::vector<E6FactorConfigurationV1>& e6_configurations_v1();

E6FactorConfigurationV1 e6_configuration_v1(const std::string& code);

WeightedPreprocessingOptionsV1 apply_e6_configuration_v1(
    const E6FactorConfigurationV1& configuration,
    const WeightedPreprocessingOptionsV1& fixed_options);

struct AblationPairContextV1 {
  std::uint32_t schema_version{1};
  std::string experiment_id{"E6"};
  std::string sample_id;
  std::string instance_sha256;
  std::uint64_t replicate_seed{0};
  std::string source_revision;
  std::string binary_sha256;
  std::string machine_identity;
  std::string compiler_identity;
  std::string solver_identity;
  std::string model_version;
  std::string arithmetic_mode;
  std::string budget_manifest;
  std::string solver_configuration;
  std::string fixed_feature_configuration;
  std::vector<std::pair<std::string, std::string>> parameters;
};

struct AblationRunManifestV1 {
  std::uint32_t schema_version{1};
  AblationPairContextV1 pair_context;
  E6FactorConfigurationV1 configuration;
};

void validate_ablation_manifest_v1(const AblationRunManifestV1& manifest);

std::string canonical_ablation_pair_context_v1(
    const AblationPairContextV1& context);
std::string canonical_ablation_manifest_v1(
    const AblationRunManifestV1& manifest);

bool same_ablation_pair_context_v1(
    const AblationRunManifestV1& left,
    const AblationRunManifestV1& right);

struct AblationTimingV1 {
  std::uint64_t t_pre_ns{0};
  std::uint64_t t_bound_ns{0};
  std::uint64_t t_oracle_ns{0};
  std::uint64_t t_reduce_ns{0};
  std::uint64_t t_other_loop_ns{0};
  std::uint64_t t_loop_ns{0};
  std::uint64_t t_verify_ns{0};
  std::uint64_t t_post_ns{0};
  std::uint64_t t_wall_ns{0};

  bool identities_hold() const noexcept;
  double t_pre() const noexcept;
  double t_bound() const noexcept;
  double t_oracle() const noexcept;
  double t_reduce() const noexcept;
  double t_other_loop() const noexcept;
  double t_loop() const noexcept;
  double t_verify() const noexcept;
  double t_post() const noexcept;
  double t_wall() const noexcept;
};

enum class AblationTimingPhaseV1 {
  PRE,
  BOUND,
  ORACLE,
  VERIFY
};

class AblationTimingRecorderV1 {
 public:
  void start();
  void begin_loop();
  void begin_phase(AblationTimingPhaseV1 phase);
  void end_phase();
  void begin_reduction();
  void end_reduction();
  void end_loop();
  AblationTimingV1 finish();

 private:
  using Clock = std::chrono::steady_clock;
  enum class State {
    CREATED,
    RUNNING,
    IN_LOOP,
    LOOP_ENDED,
    FINISHED
  };

  State state_{State::CREATED};
  std::optional<AblationTimingPhaseV1> active_phase_;
  std::optional<Clock::time_point> reduction_start_;
  Clock::time_point run_start_;
  Clock::time_point loop_start_;
  Clock::time_point phase_start_;
  AblationTimingV1 timing_;

  static std::uint64_t elapsed_nanoseconds(
      Clock::time_point start,
      Clock::time_point end);
};

struct AblationOutputEvidenceV1 {
  TemporalFinalEvidenceV2 temporal_evidence;
  bool assignment_valid_on_original_input{false};
  bool warm_start_used{false};
  bool warm_start_revalidated_on_target{false};
  bool warm_start_used_as_lower_bound{false};
  bool cache_hit_used{false};
  bool cache_payload_revalidated{false};
  bool cached_bound_certificates_revalidated{false};
  bool reductions_applied{false};
  bool reduction_value_preservation_validated{false};
  bool reduced_assignment_reconstructed_and_validated{false};
};

struct AblationRunV1 {
  AblationRunManifestV1 manifest;
  AblationTimingV1 timing;
  AblationOutputEvidenceV1 output;
  std::uint64_t rows_removed{0};
  std::uint64_t columns_removed{0};
  std::uint64_t events_processed{0};
};

struct AblationRunValidationV1 {
  bool accepted{false};
  std::optional<TemporalFinalResultV2> final_result;
  std::string reason;
};

AblationRunValidationV1 validate_ablation_run_v1(
    const AblationRunV1& run);

struct E6BlockValidationV1 {
  bool accepted{false};
  std::string reason;
};

E6BlockValidationV1 validate_e6_block_v1(
    const std::vector<AblationRunV1>& runs);

struct PairedAblationTimingComparisonV1 {
  bool accepted{false};
  std::string reason;
  TemporalFinalStatusV2 left_status{TemporalFinalStatusV2::INCOMPLETE};
  TemporalFinalStatusV2 right_status{TemporalFinalStatusV2::INCOMPLETE};
  double delta_t_pre{0.0};
  double delta_t_bound{0.0};
  double delta_t_oracle{0.0};
  double delta_t_reduce{0.0};
  double delta_t_loop{0.0};
  double delta_t_verify{0.0};
  double delta_t_wall{0.0};
};

PairedAblationTimingComparisonV1 compare_paired_ablation_runs_v1(
    const AblationRunV1& left,
    const AblationRunV1& right);

}  // namespace kdc
