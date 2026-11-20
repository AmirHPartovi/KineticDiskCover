#pragma once

#include "kdc/weighted_static.hpp"

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace kdc {

enum class WeightedMatrixRoleV1 {
  GENERIC_WEIGHT_MATRIX,
  D_AT_TIME,
  D_MINUS,
  D_PLUS
};

struct WeightedMatrixScopeV1 {
  WeightedMatrixRoleV1 role{
      WeightedMatrixRoleV1::GENERIC_WEIGHT_MATRIX};
  std::optional<double> time;
  std::optional<double> interval_start;
  std::optional<double> interval_end;
};

struct WeightedPreprocessingOptionsV1 {
  bool radius_square_upper_bounds{false};
  bool equal_row_symmetry_breaking{false};
  bool objective_lower_cut{false};
  bool station_dominance{false};
  bool point_dominance{false};
};

struct WeightedPreprocessingResultV1 {
  std::uint32_t schema_version{1};
  WeightedMatrixScopeV1 scope;
  WeightMatrix reduced_matrix;
  std::vector<WeightIndex> reduced_station_to_original;
  std::vector<WeightIndex> original_station_to_reduced;
  std::vector<WeightIndex> reduced_point_to_original;
  std::vector<WeightIndex> original_point_to_reduced;
  std::vector<Weight> radius_square_upper_bounds;
  std::optional<Weight> objective_cut_sum_lower_bound;
  std::vector<std::vector<WeightIndex>> equal_row_symmetry_groups;
  std::vector<std::string> applied_rules;

  std::vector<int> reconstruct_assignment(
      const std::vector<int>& reduced_assignment) const;
};

WeightedPreprocessingResultV1 preprocess_weight_matrix(
    const WeightMatrix& matrix,
    const WeightedMatrixScopeV1& scope,
    const WeightedPreprocessingOptionsV1& options = {});

struct StructurallyValidatedWeightedAssignmentV1 {
  std::uint32_t schema_version{1};
  std::string instance_hash;
  std::vector<int> station_ids;
  std::vector<int> point_ids;
  std::vector<int> assignment;
  Weight source_matrix_objective{0.0};
};

StructurallyValidatedWeightedAssignmentV1
make_validated_weighted_assignment(
    const WeightMatrix& source_matrix,
    std::vector<int> assignment,
    std::string instance_hash,
    std::vector<int> station_ids,
    std::vector<int> point_ids);

struct WeightedWarmStartV1 {
  std::uint32_t schema_version{1};
  bool accepted{false};
  std::vector<int> assignment;
  std::vector<Weight> station_squared_radii;
  Weight recomputed_objective{0.0};
  bool is_lower_bound{false};
  std::string status{"REJECTED"};
  std::string reason;
};

WeightedWarmStartV1 rebuild_weighted_warm_start(
    const WeightMatrix& target_matrix,
    const StructurallyValidatedWeightedAssignmentV1& source,
    const std::string& target_instance_hash,
    const std::vector<int>& target_station_ids,
    const std::vector<int>& target_point_ids);

enum class CachedBoundValidityV1 {
  ABSENT,
  REPORTED_UNVALIDATED,
  NUMERICALLY_CHECKED,
  VALIDATED,
  INVALID
};

struct WeightedSolveCacheContextV1 {
  std::string instance_hash;
  WeightedMatrixScopeV1 matrix_scope;
  std::string objective_id;
  std::string model_version;
  std::string backend_name;
  std::string backend_version;
  std::string arithmetic_mode;
  std::string solver_configuration;
  std::string feature_configuration;
  std::vector<std::pair<std::string, std::string>> parameters;
};

struct WeightedSolveCacheRecordV1 {
  std::uint32_t schema_version{1};
  std::string termination_status;
  std::optional<std::vector<int>> assignment;
  std::optional<Weight> feasible_assignment_cost;
  std::optional<Weight> upper_bound;
  CachedBoundValidityV1 upper_bound_validity{
      CachedBoundValidityV1::ABSENT};
  std::string upper_bound_certificate_id;
  std::optional<Weight> lower_bound;
  CachedBoundValidityV1 lower_bound_validity{
      CachedBoundValidityV1::ABSENT};
  std::string lower_bound_certificate_id;
  std::string provenance;
};

class IWeightedCacheCertificateVerifier {
 public:
  virtual ~IWeightedCacheCertificateVerifier() = default;
  virtual bool revalidate(
      const WeightMatrix& matrix,
      const WeightedSolveCacheContextV1& context,
      const WeightedSolveCacheRecordV1& record) const = 0;
};

enum class WeightedSolveCacheLookupStatusV1 {
  MISS,
  HIT,
  REJECTED
};

struct WeightedSolveCacheLookupV1 {
  WeightedSolveCacheLookupStatusV1 status{
      WeightedSolveCacheLookupStatusV1::MISS};
  std::optional<WeightedSolveCacheRecordV1> record;
  std::string reason;
};

class WeightedSolveCacheV1 {
 public:
  void store(
      const WeightMatrix& matrix,
      const WeightedSolveCacheContextV1& context,
      const WeightedSolveCacheRecordV1& record);

  WeightedSolveCacheLookupV1 lookup(
      const WeightMatrix& matrix,
      const WeightedSolveCacheContextV1& context,
      const IWeightedCacheCertificateVerifier* verifier = nullptr) const;

  std::size_t size() const noexcept { return entries_.size(); }

 private:
  std::map<std::string, WeightedSolveCacheRecordV1> entries_;
};

}  // namespace kdc
