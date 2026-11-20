#include "kdc/weighted_optimizations.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace {

kdc::Weight brute_force_optimum(const kdc::WeightMatrix& matrix) {
  if (matrix.point_count() == 0U) {
    return 0.0;
  }
  kdc::Weight best = std::numeric_limits<kdc::Weight>::infinity();
  std::vector<int> assignment(matrix.point_count(), 0);
  const auto enumerate = [&](const auto& self, kdc::WeightIndex point) -> void {
    if (point == matrix.point_count()) {
      best = (std::min)(
          best, kdc::WeightedStaticObjective::evaluate_assignment(
                    matrix, assignment));
      return;
    }
    for (kdc::WeightIndex station = 0;
         station < matrix.station_count(); ++station) {
      assignment[point] = static_cast<int>(station);
      self(self, point + 1U);
    }
  };
  enumerate(enumerate, 0U);
  return best;
}

kdc::WeightedSolveCacheContextV1 cache_context() {
  kdc::WeightedSolveCacheContextV1 context;
  context.instance_hash = "instance-hash";
  context.matrix_scope = {
      kdc::WeightedMatrixRoleV1::D_MINUS, std::nullopt, 0.0, 1.0};
  context.objective_id = "pi-sum-radius-squared-v1";
  context.model_version = "assignment-v1";
  context.backend_name = "test-backend";
  context.backend_version = "1.2";
  context.arithmetic_mode = "FAST_FLOAT";
  context.solver_configuration = "threads=1";
  context.feature_configuration = "reductions=off";
  context.parameters = {{"seed", "4"}, {"time_limit", "10"}};
  return context;
}

class AcceptCertificates final
    : public kdc::IWeightedCacheCertificateVerifier {
 public:
  explicit AcceptCertificates(bool accept) : accept_(accept) {}

  bool revalidate(
      const kdc::WeightMatrix&,
      const kdc::WeightedSolveCacheContextV1&,
      const kdc::WeightedSolveCacheRecordV1&) const override {
    return accept_;
  }

 private:
  bool accept_;
};

}  // namespace

TEST_CASE("Weighted preprocessing flags are opt-in and retain matrix scope") {
  const auto matrix = kdc::WeightMatrix::from_rows(
      {{1.0, 2.0}, {1.0, 2.0}, {1.0, 2.0 + 1e-12}});
  const kdc::WeightedMatrixScopeV1 scope{
      kdc::WeightedMatrixRoleV1::D_MINUS, std::nullopt, 0.25, 0.75};

  const auto unchanged = kdc::preprocess_weight_matrix(matrix, scope);
  REQUIRE(unchanged.reduced_matrix == matrix);
  REQUIRE(unchanged.applied_rules.empty());
  REQUIRE(unchanged.scope.role == kdc::WeightedMatrixRoleV1::D_MINUS);
  REQUIRE(unchanged.scope.interval_start == 0.25);
  REQUIRE(unchanged.scope.interval_end == 0.75);
  REQUIRE(unchanged.reduced_station_to_original ==
          std::vector<kdc::WeightIndex>{0U, 1U, 2U});

  kdc::WeightedPreprocessingOptionsV1 options;
  options.equal_row_symmetry_breaking = true;
  const auto symmetry =
      kdc::preprocess_weight_matrix(matrix, scope, options);
  REQUIRE(symmetry.reduced_matrix == matrix);
  REQUIRE(symmetry.equal_row_symmetry_groups.size() == 1U);
  REQUIRE(symmetry.equal_row_symmetry_groups.front() ==
          std::vector<kdc::WeightIndex>{0U, 1U});
  REQUIRE(symmetry.applied_rules ==
          std::vector<std::string>{"equal_row_symmetry_breaking"});
  kdc::Weight constrained_optimum =
      std::numeric_limits<kdc::Weight>::infinity();
  for (int first_station = 0; first_station < 3; ++first_station) {
    for (int second_station = 0; second_station < 3; ++second_station) {
      const std::vector<int> assignment{first_station, second_station};
      std::vector<kdc::Weight> radii(3U, 0.0);
      radii[static_cast<kdc::WeightIndex>(first_station)] = matrix.weight(
          static_cast<kdc::WeightIndex>(first_station), 0U);
      radii[static_cast<kdc::WeightIndex>(second_station)] = std::max(
          radii[static_cast<kdc::WeightIndex>(second_station)],
          matrix.weight(static_cast<kdc::WeightIndex>(second_station), 1U));
      if (radii[0] >= radii[1]) {
        constrained_optimum = (std::min)(
            constrained_optimum,
            kdc::WeightedStaticObjective::evaluate_assignment(matrix,
                                                              assignment));
      }
    }
  }
  REQUIRE(constrained_optimum == brute_force_optimum(matrix));

  options = {};
  options.station_dominance = true;
  const auto dominance =
      kdc::preprocess_weight_matrix(matrix, scope, options);
  REQUIRE(dominance.reduced_matrix.station_count() == 1U);
  REQUIRE(dominance.reduced_station_to_original ==
          std::vector<kdc::WeightIndex>{0U});
  REQUIRE(dominance.original_station_to_reduced ==
          std::vector<kdc::WeightIndex>{0U, 0U, 0U});
}

TEST_CASE("Weighted preprocessing rejects invalid matrix-scope metadata") {
  const kdc::WeightMatrix matrix =
      kdc::WeightMatrix::from_rows({{1.0}});
  REQUIRE_THROWS_AS(
      kdc::preprocess_weight_matrix(
          matrix, {kdc::WeightedMatrixRoleV1::D_MINUS, 1.0, 0.0, 1.0}),
      std::invalid_argument);
  REQUIRE_THROWS_AS(
      kdc::preprocess_weight_matrix(
          matrix, {kdc::WeightedMatrixRoleV1::D_PLUS, std::nullopt, 1.0, 0.0}),
      std::invalid_argument);
  REQUIRE_THROWS_AS(
      kdc::preprocess_weight_matrix(
          kdc::WeightMatrix(0U, 2U),
          {kdc::WeightedMatrixRoleV1::D_AT_TIME, 0.5, std::nullopt,
           std::nullopt}),
      kdc::WeightedProblemInfeasible);
}

TEST_CASE("Weighted reductions preserve optimum on exhaustive small matrices") {
  const auto verify_all_reductions = [](const kdc::WeightMatrix& matrix) {
    const kdc::Weight optimum = brute_force_optimum(matrix);
    for (int mode = 1; mode < 4; ++mode) {
      kdc::WeightedPreprocessingOptionsV1 options;
      options.station_dominance = (mode & 1) != 0;
      options.point_dominance = (mode & 2) != 0;
      const auto reduced = kdc::preprocess_weight_matrix(
          matrix, {kdc::WeightedMatrixRoleV1::GENERIC_WEIGHT_MATRIX},
          options);
      REQUIRE(brute_force_optimum(reduced.reduced_matrix) == optimum);

      std::vector<int> reduced_assignment(
          reduced.reduced_matrix.point_count(), 0);
      const auto reconstructed =
          reduced.reconstruct_assignment(reduced_assignment);
      REQUIRE(reconstructed.size() == matrix.point_count());
      REQUIRE(kdc::WeightedStaticObjective::evaluate_assignment(
                  matrix, reconstructed) >= optimum - 1e-12);
    }
  };

  for (int encoded = 0; encoded < 81; ++encoded) {
    int digits = encoded;
    std::vector<kdc::Weight> values(4U);
    for (auto& value : values) {
      value = static_cast<kdc::Weight>(digits % 3);
      digits /= 3;
    }
    verify_all_reductions(kdc::WeightMatrix(2U, 2U, std::move(values)));
  }

  for (const auto dimensions : {std::pair<kdc::WeightIndex, kdc::WeightIndex>{
                                    2U, 3U},
                                {3U, 2U}}) {
    const kdc::WeightIndex count = dimensions.first * dimensions.second;
    for (int encoded = 0; encoded < 64; ++encoded) {
      int digits = encoded;
      std::vector<kdc::Weight> values(count);
      for (auto& value : values) {
        value = static_cast<kdc::Weight>(digits % 2);
        digits /= 2;
      }
      verify_all_reductions(kdc::WeightMatrix(
          dimensions.first, dimensions.second, std::move(values)));
    }
  }
}

TEST_CASE("Dominance mappings preserve station and point identity") {
  SECTION("station dominance maps removed choices to retained rows") {
    const auto matrix =
        kdc::WeightMatrix::from_rows({{1.0, 4.0}, {2.0, 5.0}, {0.0, 3.0}});
    kdc::WeightedPreprocessingOptionsV1 options;
    options.station_dominance = true;
    const auto result = kdc::preprocess_weight_matrix(
        matrix, {kdc::WeightedMatrixRoleV1::GENERIC_WEIGHT_MATRIX}, options);
    REQUIRE(result.reduced_station_to_original ==
            std::vector<kdc::WeightIndex>{2U});
    REQUIRE(result.original_station_to_reduced ==
            std::vector<kdc::WeightIndex>{0U, 0U, 0U});
    REQUIRE(result.reconstruct_assignment({0, 0}) ==
            std::vector<int>{2, 2});
    REQUIRE(kdc::WeightedStaticObjective::evaluate_assignment(
                matrix, result.reconstruct_assignment({0, 0})) ==
            brute_force_optimum(matrix));
  }

  SECTION("point dominance maps dominated points to a kept representative") {
    const auto matrix =
        kdc::WeightMatrix::from_rows({{1.0, 3.0, 5.0}, {2.0, 2.0, 4.0}});
    kdc::WeightedPreprocessingOptionsV1 options;
    options.point_dominance = true;
    const auto result = kdc::preprocess_weight_matrix(
        matrix, {kdc::WeightedMatrixRoleV1::GENERIC_WEIGHT_MATRIX}, options);
    REQUIRE(result.reduced_point_to_original ==
            std::vector<kdc::WeightIndex>{2U});
    REQUIRE(result.original_point_to_reduced ==
            std::vector<kdc::WeightIndex>{0U, 0U, 0U});
    const auto assignment = result.reconstruct_assignment({1});
    REQUIRE(assignment.size() == 3U);
    REQUIRE(kdc::WeightedStaticObjective::evaluate_assignment(
                matrix, assignment) == brute_force_optimum(matrix));
  }
}

TEST_CASE("Radius caps and objective cut are exact matrix-level facts") {
  SECTION("radius caps include empty stations and empty point sets") {
    const auto matrix =
        kdc::WeightMatrix::from_rows({{1.0, 3.0}, {2.0, 2.0}, {0.0, 0.0}});
    kdc::WeightedPreprocessingOptionsV1 options;
    options.radius_square_upper_bounds = true;
    const auto result = kdc::preprocess_weight_matrix(
        matrix, {kdc::WeightedMatrixRoleV1::D_AT_TIME, 0.5, std::nullopt,
                std::nullopt},
        options);
    REQUIRE(result.radius_square_upper_bounds ==
            std::vector<kdc::Weight>{3.0, 2.0, 0.0});

    const auto empty = kdc::preprocess_weight_matrix(
        kdc::WeightMatrix(2U, 0U),
        {kdc::WeightedMatrixRoleV1::GENERIC_WEIGHT_MATRIX}, options);
    REQUIRE(empty.radius_square_upper_bounds ==
            std::vector<kdc::Weight>{0.0, 0.0});
  }

  SECTION("objective cut is the maximum of column minima") {
    const auto matrix =
        kdc::WeightMatrix::from_rows({{2.0, 8.0}, {5.0, 3.0}});
    kdc::WeightedPreprocessingOptionsV1 options;
    options.objective_lower_cut = true;
    const auto result = kdc::preprocess_weight_matrix(
        matrix, {kdc::WeightedMatrixRoleV1::D_PLUS, std::nullopt, 2.0, 4.0},
        options);
    REQUIRE(result.objective_cut_sum_lower_bound == 3.0);
    REQUIRE(*result.objective_cut_sum_lower_bound <=
            brute_force_optimum(matrix) / std::acos(-1.0));

    const auto empty = kdc::preprocess_weight_matrix(
        kdc::WeightMatrix(2U, 0U),
        {kdc::WeightedMatrixRoleV1::GENERIC_WEIGHT_MATRIX}, options);
    REQUIRE(empty.objective_cut_sum_lower_bound == 0.0);
  }
}

TEST_CASE("Validated assignment warm start rebuilds target radii and is not an LB") {
  const auto source_matrix =
      kdc::WeightMatrix::from_rows({{1.0, 4.0}, {3.0, 2.0}});
  const auto source = kdc::make_validated_weighted_assignment(
      source_matrix, {0, 1}, "instance-A", {10, 20}, {100, 200});
  const auto target_matrix =
      kdc::WeightMatrix::from_rows({{9.0, 1.0}, {2.0, 7.0}});

  const auto warm = kdc::rebuild_weighted_warm_start(
      target_matrix, source, "instance-A", {10, 20}, {100, 200});
  REQUIRE(warm.accepted);
  REQUIRE(warm.assignment == std::vector<int>{0, 1});
  REQUIRE(warm.station_squared_radii == std::vector<kdc::Weight>{9.0, 7.0});
  REQUIRE(warm.recomputed_objective ==
          kdc::WeightedStaticObjective::evaluate_assignment(target_matrix,
                                                            {0, 1}));
  REQUIRE_FALSE(warm.is_lower_bound);
  REQUIRE(warm.status == "FEASIBLE_WARM_START");

  REQUIRE_FALSE(kdc::rebuild_weighted_warm_start(
                    target_matrix, source, "different-instance", {10, 20},
                    {100, 200})
                    .accepted);
  REQUIRE_FALSE(kdc::rebuild_weighted_warm_start(
                    target_matrix, source, "instance-A", {20, 10},
                    {100, 200})
                    .accepted);
  REQUIRE_THROWS_AS(kdc::make_validated_weighted_assignment(
                        source_matrix, {0}, "instance-A", {10, 20},
                        {100, 200}),
                    kdc::WeightedAssignmentError);
}

TEST_CASE("Weighted solve cache keys every solve-affecting input") {
  const kdc::WeightMatrix matrix(1U, 1U, {1.0});
  const auto context = cache_context();
  kdc::WeightedSolveCacheRecordV1 record;
  record.termination_status = "FEASIBLE";
  record.assignment = std::vector<int>{0};
  record.feasible_assignment_cost =
      kdc::WeightedStaticObjective::evaluate_assignment(matrix, {0});
  record.provenance = "test-provenance";

  kdc::WeightedSolveCacheV1 cache;
  cache.store(matrix, context, record);
  REQUIRE(cache.size() == 1U);
  REQUIRE(cache.lookup(matrix, context).status ==
          kdc::WeightedSolveCacheLookupStatusV1::HIT);

  std::vector<kdc::WeightedSolveCacheContextV1> changed;
  auto variant = context;
  variant.instance_hash += "-changed";
  changed.push_back(variant);
  variant = context;
  variant.matrix_scope.interval_start = std::nextafter(0.0, 1.0);
  changed.push_back(variant);
  variant = context;
  variant.objective_id += "-changed";
  changed.push_back(variant);
  variant = context;
  variant.model_version += "-changed";
  changed.push_back(variant);
  variant = context;
  variant.backend_name += "-changed";
  changed.push_back(variant);
  variant = context;
  variant.backend_version += "-changed";
  changed.push_back(variant);
  variant = context;
  variant.arithmetic_mode = "VALIDATED";
  changed.push_back(variant);
  variant = context;
  variant.solver_configuration += ";presolve=off";
  changed.push_back(variant);
  variant = context;
  variant.feature_configuration = "point_dominance=on";
  changed.push_back(variant);
  variant = context;
  variant.parameters[0].second = "5";
  changed.push_back(variant);
  for (const auto& different_context : changed) {
    REQUIRE(cache.lookup(matrix, different_context).status ==
            kdc::WeightedSolveCacheLookupStatusV1::MISS);
  }

  const kdc::WeightMatrix different_matrix(
      1U, 1U, {std::nextafter(1.0, 2.0)});
  REQUIRE(cache.lookup(different_matrix, context).status ==
          kdc::WeightedSolveCacheLookupStatusV1::MISS);

  variant = context;
  variant.matrix_scope = {kdc::WeightedMatrixRoleV1::D_PLUS, std::nullopt,
                          0.0, 1.0};
  REQUIRE(cache.lookup(matrix, variant).status ==
          kdc::WeightedSolveCacheLookupStatusV1::MISS);
}

TEST_CASE("Cached bounds retain validity and require certificate revalidation") {
  const kdc::WeightMatrix matrix(1U, 1U, {1.0});
  const auto context = cache_context();
  kdc::WeightedSolveCacheRecordV1 record;
  record.termination_status = "OPTIMAL";
  record.lower_bound = 2.0;
  record.lower_bound_validity = kdc::CachedBoundValidityV1::VALIDATED;
  record.lower_bound_certificate_id = "dual-cert-7";
  record.provenance = "dual certificate from test";

  kdc::WeightedSolveCacheV1 cache;
  cache.store(matrix, context, record);
  const auto missing_verifier = cache.lookup(matrix, context);
  REQUIRE(missing_verifier.status ==
          kdc::WeightedSolveCacheLookupStatusV1::REJECTED);
  REQUIRE_FALSE(missing_verifier.record);

  const AcceptCertificates reject(false);
  REQUIRE(cache.lookup(matrix, context, &reject).status ==
          kdc::WeightedSolveCacheLookupStatusV1::REJECTED);
  const AcceptCertificates accept(true);
  const auto hit = cache.lookup(matrix, context, &accept);
  REQUIRE(hit.status == kdc::WeightedSolveCacheLookupStatusV1::HIT);
  REQUIRE(hit.record->lower_bound_validity ==
          kdc::CachedBoundValidityV1::VALIDATED);
  REQUIRE(hit.record->lower_bound_certificate_id == "dual-cert-7");
  REQUIRE(hit.record->provenance == record.provenance);

  record.lower_bound_validity =
      kdc::CachedBoundValidityV1::REPORTED_UNVALIDATED;
  record.lower_bound_certificate_id.clear();
  record.provenance = "unvalidated report";
  cache.store(matrix, context, record);
  const auto unvalidated = cache.lookup(matrix, context);
  REQUIRE(unvalidated.status == kdc::WeightedSolveCacheLookupStatusV1::HIT);
  REQUIRE(unvalidated.record->lower_bound_validity ==
          kdc::CachedBoundValidityV1::REPORTED_UNVALIDATED);

  record.lower_bound_validity =
      kdc::CachedBoundValidityV1::NUMERICALLY_CHECKED;
  REQUIRE_THROWS_AS(cache.store(matrix, context, record),
                    std::invalid_argument);
}

TEST_CASE("Cache rejects inconsistent assignment evidence and ambiguous keys") {
  const kdc::WeightMatrix matrix(1U, 1U, {1.0});
  const auto context = cache_context();
  kdc::WeightedSolveCacheRecordV1 record;
  record.termination_status = "FEASIBLE";
  record.assignment = std::vector<int>{0};
  record.feasible_assignment_cost = 0.0;
  record.provenance = "bad test record";
  kdc::WeightedSolveCacheV1 cache;
  REQUIRE_THROWS_AS(cache.store(matrix, context, record),
                    std::invalid_argument);

  record.assignment.reset();
  record.feasible_assignment_cost.reset();
  record.lower_bound = 1.0;
  record.lower_bound_validity =
      kdc::CachedBoundValidityV1::NUMERICALLY_CHECKED;
  REQUIRE_THROWS_AS(cache.store(matrix, context, record),
                    std::invalid_argument);

  auto ambiguous = context;
  ambiguous.parameters.push_back({"seed", "5"});
  REQUIRE_THROWS_AS(cache.lookup(matrix, ambiguous), std::invalid_argument);
}
