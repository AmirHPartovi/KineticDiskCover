#include "kdc/weighted_optimizations.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace kdc {
namespace {

void validate_scope(const WeightedMatrixScopeV1& scope) {
  switch (scope.role) {
    case WeightedMatrixRoleV1::GENERIC_WEIGHT_MATRIX:
      if (scope.time || scope.interval_start || scope.interval_end) {
        throw std::invalid_argument(
            "generic weighted matrix scope cannot have time endpoints");
      }
      return;
    case WeightedMatrixRoleV1::D_AT_TIME:
      if (!scope.time || scope.interval_start || scope.interval_end ||
          !std::isfinite(*scope.time)) {
        throw std::invalid_argument(
            "D(t) scope requires only one finite time value");
      }
      return;
    case WeightedMatrixRoleV1::D_MINUS:
    case WeightedMatrixRoleV1::D_PLUS:
      if (scope.time || !scope.interval_start || !scope.interval_end ||
          !std::isfinite(*scope.interval_start) ||
          !std::isfinite(*scope.interval_end) ||
          *scope.interval_start > *scope.interval_end) {
        throw std::invalid_argument(
            "D-/D+ scope requires ordered finite interval endpoints");
      }
      return;
  }
  throw std::invalid_argument("unknown weighted matrix scope");
}

bool row_leq(
    const WeightMatrix& matrix,
    WeightIndex left,
    WeightIndex right,
    const std::vector<WeightIndex>& columns) {
  for (const WeightIndex column : columns) {
    if (matrix.weight(left, column) > matrix.weight(right, column)) {
      return false;
    }
  }
  return true;
}

bool column_leq(
    const WeightMatrix& matrix,
    WeightIndex left,
    WeightIndex right,
    const std::vector<WeightIndex>& rows) {
  for (const WeightIndex row : rows) {
    if (matrix.weight(row, left) > matrix.weight(row, right)) {
      return false;
    }
  }
  return true;
}

std::vector<WeightIndex> make_identity_map(WeightIndex count) {
  std::vector<WeightIndex> result(count);
  for (WeightIndex index = 0; index < count; ++index) {
    result[index] = index;
  }
  return result;
}

void append_token(std::string& output, const std::string& token) {
  output += std::to_string(token.size());
  output += ':';
  output += token;
}

std::string double_bits(Weight value) {
  static_assert(sizeof(Weight) == sizeof(std::uint64_t),
                "cache keys require binary64 weights");
  static_assert(std::numeric_limits<Weight>::is_iec559,
                "cache keys require IEEE-754 weights");
  std::uint64_t bits = 0;
  std::memcpy(&bits, &value, sizeof(bits));
  std::ostringstream stream;
  stream << std::hex;
  stream.width(16);
  stream.fill('0');
  stream << bits;
  return stream.str();
}

void append_optional_double(
    std::string& output,
    const std::optional<double>& value) {
  append_token(output, value ? double_bits(*value) : "unset");
}

std::string cache_key(
    const WeightMatrix& matrix,
    const WeightedSolveCacheContextV1& context) {
  if (context.instance_hash.empty() || context.objective_id.empty() ||
      context.model_version.empty() || context.backend_name.empty() ||
      context.backend_version.empty() || context.arithmetic_mode.empty() ||
      context.solver_configuration.empty() ||
      context.feature_configuration.empty()) {
    throw std::invalid_argument(
        "cache context must identify the instance, objective, model, backend, "
        "arithmetic, solver configuration, and enabled features");
  }
  validate_scope(context.matrix_scope);

  std::string result;
  append_token(result, std::to_string(matrix.station_count()));
  append_token(result, std::to_string(matrix.point_count()));
  for (const Weight value : matrix.row_major_weights()) {
    append_token(result, double_bits(value));
  }
  append_token(result, context.instance_hash);
  append_token(result, std::to_string(
                           static_cast<int>(context.matrix_scope.role)));
  append_optional_double(result, context.matrix_scope.time);
  append_optional_double(result, context.matrix_scope.interval_start);
  append_optional_double(result, context.matrix_scope.interval_end);
  append_token(result, context.objective_id);
  append_token(result, context.model_version);
  append_token(result, context.backend_name);
  append_token(result, context.backend_version);
  append_token(result, context.arithmetic_mode);
  append_token(result, context.solver_configuration);
  append_token(result, context.feature_configuration);

  auto parameters = context.parameters;
  std::sort(parameters.begin(), parameters.end());
  for (WeightIndex index = 1; index < parameters.size(); ++index) {
    if (parameters[index - 1].first == parameters[index].first) {
      throw std::invalid_argument(
          "cache context has duplicate solver parameter names");
    }
  }
  append_token(result, std::to_string(parameters.size()));
  for (const auto& parameter : parameters) {
    append_token(result, parameter.first);
    append_token(result, parameter.second);
  }
  return result;
}

bool is_finite_optional(const std::optional<Weight>& value) {
  return !value || std::isfinite(*value);
}

void validate_cache_record(const WeightedSolveCacheRecordV1& record) {
  if (record.schema_version != 1 || record.termination_status.empty() ||
      record.provenance.empty()) {
    throw std::invalid_argument(
        "cache record requires a supported schema, status, and provenance");
  }
  if (!is_finite_optional(record.feasible_assignment_cost) ||
      !is_finite_optional(record.upper_bound) ||
      !is_finite_optional(record.lower_bound)) {
    throw std::invalid_argument("cache record cannot contain NaN or infinity");
  }
  if (record.assignment.has_value() !=
      record.feasible_assignment_cost.has_value()) {
    throw std::invalid_argument(
        "cached assignment and its recomputed feasible cost must be stored "
        "together");
  }
  if (record.upper_bound_validity != CachedBoundValidityV1::ABSENT &&
      !record.upper_bound) {
    throw std::invalid_argument(
        "upper-bound validity requires an upper-bound value");
  }
  if (record.lower_bound_validity != CachedBoundValidityV1::ABSENT &&
      !record.lower_bound) {
    throw std::invalid_argument(
        "lower-bound validity requires a lower-bound value");
  }
  if (record.upper_bound_validity == CachedBoundValidityV1::VALIDATED &&
      record.upper_bound_certificate_id.empty()) {
    throw std::invalid_argument(
        "validated cached upper bound requires a certificate ID");
  }
  if (record.upper_bound_validity ==
          CachedBoundValidityV1::NUMERICALLY_CHECKED &&
      record.upper_bound_certificate_id.empty()) {
    throw std::invalid_argument(
        "numerically checked cached upper bound requires an evidence ID");
  }
  if (record.lower_bound_validity == CachedBoundValidityV1::VALIDATED &&
      record.lower_bound_certificate_id.empty()) {
    throw std::invalid_argument(
        "validated cached lower bound requires a certificate ID");
  }
  if (record.lower_bound_validity ==
          CachedBoundValidityV1::NUMERICALLY_CHECKED &&
      record.lower_bound_certificate_id.empty()) {
    throw std::invalid_argument(
        "numerically checked cached lower bound requires an evidence ID");
  }
}

bool has_certificate(const WeightedSolveCacheRecordV1& record) {
  return !record.upper_bound_certificate_id.empty() ||
         !record.lower_bound_certificate_id.empty();
}

}  // namespace

std::vector<int> WeightedPreprocessingResultV1::reconstruct_assignment(
    const std::vector<int>& reduced_assignment) const {
  WeightedStaticObjective::validate_assignment(reduced_matrix,
                                                reduced_assignment);
  std::vector<int> original_assignment(
      original_point_to_reduced.size(), -1);
  for (WeightIndex point = 0; point < original_assignment.size(); ++point) {
    const WeightIndex reduced_point = original_point_to_reduced[point];
    if (reduced_point >= reduced_assignment.size()) {
      throw std::logic_error(
          "preprocessing point map references a missing reduced point");
    }
    const WeightIndex reduced_station =
        static_cast<WeightIndex>(reduced_assignment[reduced_point]);
    if (reduced_station >= reduced_station_to_original.size()) {
      throw std::logic_error(
          "preprocessing assignment references a missing reduced station");
    }
    original_assignment[point] =
        static_cast<int>(reduced_station_to_original[reduced_station]);
  }
  return original_assignment;
}

WeightedPreprocessingResultV1 preprocess_weight_matrix(
    const WeightMatrix& matrix,
    const WeightedMatrixScopeV1& scope,
    const WeightedPreprocessingOptionsV1& options) {
  validate_scope(scope);
  if (matrix.station_count() == 0 && matrix.point_count() != 0) {
    throw WeightedProblemInfeasible(
        "positive point count requires at least one station");
  }
  if (matrix.station_count() >
          static_cast<WeightIndex>(std::numeric_limits<int>::max()) ||
      matrix.point_count() >
          static_cast<WeightIndex>(std::numeric_limits<int>::max())) {
    throw std::length_error(
        "weighted preprocessing exceeds assignment index range");
  }

  const auto original_stations =
      make_identity_map(matrix.station_count());
  const auto original_points = make_identity_map(matrix.point_count());
  std::vector<WeightIndex> stations = original_stations;
  std::vector<WeightIndex> points = original_points;
  std::vector<WeightIndex> original_station_to_reduced(
      matrix.station_count(), 0);
  std::vector<WeightIndex> original_point_to_reduced(
      matrix.point_count(), 0);

  if (options.station_dominance && matrix.point_count() > 0) {
    std::vector<bool> keep(matrix.station_count(), true);
    for (WeightIndex left = 0; left < matrix.station_count(); ++left) {
      for (WeightIndex right = left + 1; right < matrix.station_count();
           ++right) {
        if (!keep[left] || !keep[right]) {
          continue;
        }
        const bool left_le_right =
            row_leq(matrix, left, right, points);
        const bool right_le_left =
            row_leq(matrix, right, left, points);
        if (left_le_right && right_le_left) {
          keep[right] = false;
        } else if (left_le_right) {
          keep[right] = false;
        } else if (right_le_left) {
          keep[left] = false;
        }
      }
    }
    stations.clear();
    for (WeightIndex station = 0; station < keep.size(); ++station) {
      if (keep[station]) {
        stations.push_back(station);
      }
    }
  }

  if (options.point_dominance && matrix.point_count() > 0) {
    std::vector<bool> keep(matrix.point_count(), true);
    for (WeightIndex left = 0; left < matrix.point_count(); ++left) {
      for (WeightIndex right = left + 1; right < matrix.point_count();
           ++right) {
        if (!keep[left] || !keep[right]) {
          continue;
        }
        const bool left_le_right =
            column_leq(matrix, left, right, stations);
        const bool right_le_left =
            column_leq(matrix, right, left, stations);
        if (left_le_right && right_le_left) {
          keep[right] = false;
        } else if (left_le_right) {
          keep[left] = false;
        } else if (right_le_left) {
          keep[right] = false;
        }
      }
    }
    points.clear();
    for (WeightIndex point = 0; point < keep.size(); ++point) {
      if (keep[point]) {
        points.push_back(point);
      }
    }
  }

  for (WeightIndex station = 0; station < matrix.station_count(); ++station) {
    WeightIndex representative = matrix.station_count();
    for (const WeightIndex kept_station : stations) {
      if (row_leq(matrix, kept_station, station, original_points) &&
          kept_station < representative) {
        representative = kept_station;
      }
    }
    if (representative == matrix.station_count()) {
      throw std::logic_error(
          "no retained station dominates an original station");
    }
    const auto position =
        std::find(stations.begin(), stations.end(), representative);
    original_station_to_reduced[station] =
        static_cast<WeightIndex>(position - stations.begin());
  }

  for (WeightIndex point = 0; point < matrix.point_count(); ++point) {
    WeightIndex representative = matrix.point_count();
    for (const WeightIndex kept_point : points) {
      if (column_leq(matrix, point, kept_point, stations) &&
          kept_point < representative) {
        representative = kept_point;
      }
    }
    if (representative == matrix.point_count()) {
      throw std::logic_error(
          "no retained point dominates an original point");
    }
    const auto position =
        std::find(points.begin(), points.end(), representative);
    original_point_to_reduced[point] =
        static_cast<WeightIndex>(position - points.begin());
  }

  std::vector<Weight> reduced_values;
  reduced_values.reserve(stations.size() * points.size());
  for (const WeightIndex station : stations) {
    for (const WeightIndex point : points) {
      reduced_values.push_back(matrix.weight(station, point));
    }
  }
  WeightMatrix reduced_matrix(
      stations.size(), points.size(), std::move(reduced_values));

  WeightedPreprocessingResultV1 result;
  result.scope = scope;
  result.reduced_matrix = std::move(reduced_matrix);
  result.reduced_station_to_original = stations;
  result.original_station_to_reduced =
      std::move(original_station_to_reduced);
  result.reduced_point_to_original = points;
  result.original_point_to_reduced = std::move(original_point_to_reduced);

  if (options.radius_square_upper_bounds) {
    result.radius_square_upper_bounds.assign(stations.size(), 0.0);
    for (WeightIndex station = 0; station < stations.size(); ++station) {
      for (WeightIndex point = 0; point < points.size(); ++point) {
        result.radius_square_upper_bounds[station] = std::max(
            result.radius_square_upper_bounds[station],
            result.reduced_matrix.weight(station, point));
      }
    }
    result.applied_rules.emplace_back("radius_square_upper_bounds");
  }

  if (options.objective_lower_cut) {
    Weight lower_cut = 0.0;
    for (WeightIndex point = 0;
         point < result.reduced_matrix.point_count(); ++point) {
      Weight minimum = std::numeric_limits<Weight>::infinity();
      for (WeightIndex station = 0;
           station < result.reduced_matrix.station_count(); ++station) {
        minimum = std::min(
            minimum, result.reduced_matrix.weight(station, point));
      }
      lower_cut = std::max(lower_cut, minimum);
    }
    result.objective_cut_sum_lower_bound = lower_cut;
    result.applied_rules.emplace_back("objective_lower_cut");
  }

  if (options.equal_row_symmetry_breaking) {
    std::vector<bool> grouped(stations.size(), false);
    for (WeightIndex first = 0; first < stations.size(); ++first) {
      if (grouped[first]) {
        continue;
      }
      std::vector<WeightIndex> group{first};
      for (WeightIndex candidate = first + 1;
           candidate < stations.size(); ++candidate) {
        bool equal = true;
        for (WeightIndex point = 0; point < points.size(); ++point) {
          if (result.reduced_matrix.weight(first, point) !=
              result.reduced_matrix.weight(candidate, point)) {
            equal = false;
            break;
          }
        }
        if (equal) {
          grouped[candidate] = true;
          group.push_back(candidate);
        }
      }
      if (group.size() > 1) {
        result.equal_row_symmetry_groups.push_back(std::move(group));
      }
    }
    result.applied_rules.emplace_back("equal_row_symmetry_breaking");
  }

  if (options.station_dominance) {
    result.applied_rules.emplace_back("station_dominance");
  }
  if (options.point_dominance) {
    result.applied_rules.emplace_back("point_dominance");
  }
  return result;
}

StructurallyValidatedWeightedAssignmentV1
make_validated_weighted_assignment(
    const WeightMatrix& source_matrix,
    std::vector<int> assignment,
    std::string instance_hash,
    std::vector<int> station_ids,
    std::vector<int> point_ids) {
  if (instance_hash.empty() ||
      station_ids.size() != source_matrix.station_count() ||
      point_ids.size() != source_matrix.point_count()) {
    throw WeightedAssignmentError(
        "warm-start identity metadata does not match the source matrix");
  }
  WeightedStaticObjective::validate_assignment(source_matrix, assignment);
  const Weight objective =
      WeightedStaticObjective::evaluate_assignment(source_matrix, assignment);
  if (!std::isfinite(objective)) {
    throw WeightedAssignmentError(
        "warm-start assignment objective is not finite");
  }
  return {1, std::move(instance_hash), std::move(station_ids),
          std::move(point_ids), std::move(assignment), objective};
}

WeightedWarmStartV1 rebuild_weighted_warm_start(
    const WeightMatrix& target_matrix,
    const StructurallyValidatedWeightedAssignmentV1& source,
    const std::string& target_instance_hash,
    const std::vector<int>& target_station_ids,
    const std::vector<int>& target_point_ids) {
  WeightedWarmStartV1 result;
  if (source.schema_version != 1 || source.instance_hash.empty() ||
      source.instance_hash != target_instance_hash) {
    result.reason = "instance identity mismatch";
    return result;
  }
  if (source.station_ids != target_station_ids ||
      source.point_ids != target_point_ids ||
      target_station_ids.size() != target_matrix.station_count() ||
      target_point_ids.size() != target_matrix.point_count()) {
    result.reason = "station or point identity/order mismatch";
    return result;
  }

  try {
    WeightedStaticObjective::validate_assignment(target_matrix,
                                                  source.assignment);
    result.station_squared_radii.assign(
        target_matrix.station_count(), 0.0);
    for (WeightIndex point = 0; point < target_matrix.point_count(); ++point) {
      const auto station =
          static_cast<WeightIndex>(source.assignment[point]);
      result.station_squared_radii[station] = std::max(
          result.station_squared_radii[station],
          target_matrix.weight(station, point));
    }
    Weight sum = 0.0;
    for (const Weight radius_squared : result.station_squared_radii) {
      sum += radius_squared;
    }
    result.recomputed_objective = std::acos(-1.0) * sum;
    if (!std::isfinite(result.recomputed_objective)) {
      result.station_squared_radii.clear();
      result.reason = "recomputed target objective is not finite";
      return result;
    }
  } catch (const std::exception& error) {
    result.reason = error.what();
    return result;
  }

  result.accepted = true;
  result.assignment = source.assignment;
  result.status = "FEASIBLE_WARM_START";
  result.reason =
      "assignment revalidated and station radii rebuilt on target matrix";
  return result;
}

void WeightedSolveCacheV1::store(
    const WeightMatrix& matrix,
    const WeightedSolveCacheContextV1& context,
    const WeightedSolveCacheRecordV1& record) {
  const auto key = cache_key(matrix, context);
  validate_cache_record(record);
  if (record.assignment) {
    WeightedStaticObjective::validate_assignment(matrix, *record.assignment);
    const Weight recomputed =
        WeightedStaticObjective::evaluate_assignment(matrix,
                                                      *record.assignment);
    if (recomputed != *record.feasible_assignment_cost) {
      throw std::invalid_argument(
          "cached assignment cost does not match the exact matrix");
    }
  }
  entries_[key] = record;
}

WeightedSolveCacheLookupV1 WeightedSolveCacheV1::lookup(
    const WeightMatrix& matrix,
    const WeightedSolveCacheContextV1& context,
    const IWeightedCacheCertificateVerifier* verifier) const {
  const auto key = cache_key(matrix, context);
  const auto found = entries_.find(key);
  if (found == entries_.end()) {
    return {WeightedSolveCacheLookupStatusV1::MISS, std::nullopt,
            "no exact cache-key match"};
  }

  const auto& record = found->second;
  if (record.assignment) {
    try {
      WeightedStaticObjective::validate_assignment(matrix,
                                                    *record.assignment);
      if (WeightedStaticObjective::evaluate_assignment(
              matrix, *record.assignment) !=
          *record.feasible_assignment_cost) {
        return {WeightedSolveCacheLookupStatusV1::REJECTED, std::nullopt,
                "cached feasible assignment cost failed revalidation"};
      }
    } catch (const std::exception& error) {
      return {WeightedSolveCacheLookupStatusV1::REJECTED, std::nullopt,
              error.what()};
    }
  }

  if (has_certificate(record)) {
    if (verifier == nullptr) {
      return {WeightedSolveCacheLookupStatusV1::REJECTED, std::nullopt,
              "cached certificate requires an independent verifier"};
    }
    if (!verifier->revalidate(matrix, context, record)) {
      return {WeightedSolveCacheLookupStatusV1::REJECTED, std::nullopt,
              "cached certificate failed independent revalidation"};
    }
  }

  return {WeightedSolveCacheLookupStatusV1::HIT, record,
          "exact cache key matched; validity and provenance were preserved"};
}

}  // namespace kdc
