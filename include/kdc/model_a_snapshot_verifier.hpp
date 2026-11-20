#pragma once

#include "kdc/global_bound_history.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace kdc {

enum class ModelASnapshotVerificationModeV1 {
  FAST_FLOAT,
  VALIDATED
};

enum class ModelASnapshotVerificationStatusV1 {
  NUMERIC_PASS,
  VALIDATED_PASS,
  FAIL,
  INDETERMINATE,
  NOT_CERTIFIED
};

struct ModelASnapshotViolationV1 {
  std::string code;
  std::string message;
  std::optional<double> interval_start;
  std::optional<double> interval_end;
  std::optional<int> station_index;
  std::optional<int> station_id;
  std::optional<int> point_index;
};

struct ModelASnapshotVerificationResultV1 {
  std::uint32_t schema_version{1};
  ModelASnapshotVerificationModeV1 mode{
      ModelASnapshotVerificationModeV1::FAST_FLOAT};
  ModelASnapshotVerificationStatusV1 status{
      ModelASnapshotVerificationStatusV1::INDETERMINATE};
  bool input_reloaded{false};
  bool instance_hash_matches{false};
  bool provenance_matches{false};
  bool partition_valid{false};
  bool boundary_ownership_valid{false};
  bool assignments_valid{false};
  bool radii_valid{false};
  bool continuous_coverage_valid{false};
  bool empty_stations_zero{false};
  bool cost_upper_check_valid{false};
  bool cost_enclosure_validated{false};
  std::size_t envelope_equality_points_checked{0};
  double cost_lower_endpoint{0.0};
  double cost_upper_endpoint{0.0};
  double stored_ub{0.0};
  std::string boundary_ownership{"LOWER_INTERVAL"};
  std::string arithmetic_note;
  std::optional<ModelASnapshotViolationV1> first_violation;
};

class ModelASnapshotVerifier {
 public:
  // Reloads both artifacts from disk. V1 radius vectors define constant radii
  // on each piece; overlapping station-sparse pieces compose componentwise max.
  static ModelASnapshotVerificationResultV1 verify_files(
      const std::string& instance_json_path,
      const std::string& bound_history_json_path,
      ModelASnapshotVerificationModeV1 mode);
};

const char* model_a_snapshot_verification_status_name_v1(
    ModelASnapshotVerificationStatusV1 status) noexcept;

}  // namespace kdc
