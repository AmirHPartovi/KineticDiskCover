#include "kdc/weighted_static.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <cmath>
#include <algorithm>
#include <limits>
#include <vector>

namespace {

struct BruteForceWeightedResult {
  kdc::Weight optimum{0.0};
  std::vector<int> lexicographic_assignment;
};

// Testing-only exhaustive reference: O(m^n * m*n) time and O(n) extra memory.
BruteForceWeightedResult brute_force_weighted_reference(
    const kdc::WeightMatrix& matrix) {
  const kdc::WeightIndex station_count = matrix.station_count();
  const kdc::WeightIndex point_count = matrix.point_count();
  if (station_count == 0U) {
    if (point_count == 0U) {
      return {0.0, {}};
    }
    throw kdc::WeightedProblemInfeasible(
        "no assignment exists with zero stations");
  }

  std::uint64_t assignment_count = 1U;
  for (kdc::WeightIndex point = 0U; point < point_count; ++point) {
    if (assignment_count >
        std::numeric_limits<std::uint64_t>::max() / station_count) {
      throw kdc::WeightedAssignmentError("brute-force assignment count overflow");
    }
    assignment_count *= station_count;
  }

  kdc::Weight optimum =
      std::numeric_limits<kdc::Weight>::infinity();
  std::vector<int> best_assignment;
  std::vector<int> assignment(point_count);
  std::uint64_t enumeration_count = 0U;
  const auto enumerate = [&](const auto& self,
                             kdc::WeightIndex point) -> void {
    if (point == point_count) {
      ++enumeration_count;
      const kdc::Weight cost =
          kdc::WeightedStaticObjective::evaluate_assignment(matrix,
                                                            assignment);
      if (cost < optimum) {
        optimum = cost;
        best_assignment = assignment;
      }
      return;
    }
    for (kdc::WeightIndex station = 0U; station < station_count; ++station) {
      assignment[point] = static_cast<int>(station);
      self(self, point + 1U);
    }
  };
  enumerate(enumerate, 0U);
  if (enumeration_count != assignment_count) {
    throw kdc::WeightedAssignmentError(
        "brute-force reference enumeration count mismatch");
  }
  if (!std::isfinite(optimum)) {
    throw kdc::WeightedProblemInfeasible(
        "brute-force reference found no assignment");
  }
  return {optimum, best_assignment};
}

bool assignments_equal(const std::vector<int>& lhs,
                       const std::vector<int>& rhs) {
  return lhs == rhs;
}

bool weights_close(kdc::Weight lhs, kdc::Weight rhs,
                   kdc::Weight tolerance = 1e-12) {
  return std::abs(lhs - rhs) <= tolerance *
                                    std::max(kdc::Weight{1.0},
                                             std::abs(rhs));
}

}

TEST_CASE("Weighted objective matches m=1,n=1 formula") {
  kdc::WeightMatrix matrix(1U, 1U, {4.0});
  REQUIRE(matrix.station_count() == 1U);
  REQUIRE(matrix.point_count() == 1U);
  REQUIRE(weights_close(
      kdc::WeightedStaticObjective::evaluate_assignment(matrix, {0}),
      std::acos(-1.0) * 4.0));
}

TEST_CASE("Weighted objective handles empty dimensions explicitly") {
  SECTION("empty m and n") {
    kdc::WeightMatrix matrix(0U, 0U);
    REQUIRE(kdc::WeightedStaticObjective::evaluate_assignment(matrix, {}) ==
            0.0);
  }

  SECTION("empty points with empty stations") {
    kdc::WeightMatrix matrix(0U, 0U);
    REQUIRE(kdc::WeightedStaticObjective::evaluate_assignment(matrix, {}) ==
            0.0);
  }

  SECTION("empty points with positive stations") {
    kdc::WeightMatrix matrix(3U, 0U);
    REQUIRE(kdc::WeightedStaticObjective::evaluate_assignment(matrix, {}) ==
            0.0);
  }

  SECTION("points but no stations") {
    kdc::WeightMatrix matrix(0U, 2U);
    REQUIRE_THROWS_AS(
        kdc::WeightedStaticObjective::evaluate_assignment(matrix, {}),
        kdc::WeightedProblemInfeasible);
  }
}

TEST_CASE("Weighted objective handles zero weights and repeated rows") {
  kdc::WeightMatrix zero_matrix =
      kdc::WeightMatrix::from_rows({{0.0, 0.0}, {0.0, 0.0}});
  REQUIRE(kdc::WeightedStaticObjective::evaluate_assignment(
              zero_matrix, {0, 1}) == 0.0);

  kdc::WeightMatrix repeated =
      kdc::WeightMatrix::from_rows({{2.0, 3.0}, {2.0, 3.0}});
  const kdc::Weight optimum =
      kdc::WeightedStaticObjective::evaluate_assignment(repeated, {1, 1});
  REQUIRE(weights_close(optimum, std::acos(-1.0) * 3.0));
}

TEST_CASE("Tie-breaking is separate from the objective value") {
  kdc::WeightMatrix matrix =
      kdc::WeightMatrix::from_rows({{1.0, 1.0}, {1.0, 1.0}});
  const auto reference = brute_force_weighted_reference(matrix);

  REQUIRE(weights_close(reference.optimum, std::acos(-1.0) * 1.0));
  REQUIRE(assignments_equal(reference.lexicographic_assignment,
                            std::vector<int>({0, 0})));
  REQUIRE(kdc::WeightedStaticObjective::evaluate_assignment(
              matrix, std::vector<int>({1, 1})) ==
          reference.optimum);
  REQUIRE(kdc::WeightedStaticObjective::evaluate_assignment(
              matrix, reference.lexicographic_assignment) ==
          reference.optimum);
}

TEST_CASE("Multiple equal-cost optima are deterministic") {
  kdc::WeightMatrix matrix =
      kdc::WeightMatrix::from_rows({{2.0, 1.0}, {1.0, 2.0}});
  REQUIRE(matrix.weight(0U, 0U) == 2.0);
  REQUIRE(matrix.weight(0U, 1U) == 1.0);
  REQUIRE(matrix.weight(1U, 0U) == 1.0);
  REQUIRE(matrix.weight(1U, 1U) == 2.0);
  REQUIRE(kdc::WeightedStaticObjective::evaluate_assignment(
              matrix, std::vector<int>({0, 0})) ==
          std::acos(-1.0) * 2.0);
  const auto reference = brute_force_weighted_reference(matrix);

  // Assignment {0,0} and {1,1} are both tied; lexicographic tie is {0,0}.
  REQUIRE(reference.optimum == std::acos(-1.0) * 2.0);
  REQUIRE(assignments_equal(reference.lexicographic_assignment,
                            std::vector<int>({0, 0})));
}

TEST_CASE("WeightMatrix validates boundary dimensions and data") {
  REQUIRE_NOTHROW(kdc::WeightMatrix(0U, 0U));
  REQUIRE_NOTHROW(kdc::WeightMatrix(5U, 0U));

  REQUIRE_THROWS_AS(kdc::WeightMatrix(1U, 1U, {1.0, 2.0}),
                    kdc::WeightedMatrixError);
  REQUIRE_THROWS_AS(kdc::WeightMatrix(1U, 1U, {-1.0}),
                    kdc::WeightedMatrixError);
  REQUIRE_THROWS_AS(
      kdc::WeightMatrix(1U, 1U,
                        {std::numeric_limits<kdc::Weight>::infinity()}),
      kdc::WeightedMatrixError);
  REQUIRE_THROWS_AS(kdc::WeightMatrix(1U, 1U,
                                      {std::numeric_limits<kdc::Weight>::quiet_NaN()}),
                    kdc::WeightedMatrixError);

  REQUIRE_THROWS_AS(kdc::WeightMatrix::from_rows({{1.0}, {1.0, 2.0}}),
                    kdc::WeightedMatrixError);
}

TEST_CASE("Assignment validation and objective formula use ordered indexes") {
  kdc::WeightMatrix matrix =
      kdc::WeightMatrix::from_rows({{1.0, 5.0}, {3.0, 2.0}});

  REQUIRE_THROWS_AS(
      kdc::WeightedStaticObjective::evaluate_assignment(matrix, {}),
      kdc::WeightedAssignmentError);
  REQUIRE_THROWS_AS(
      kdc::WeightedStaticObjective::evaluate_assignment(matrix, {0}),
      kdc::WeightedAssignmentError);
  REQUIRE_THROWS_AS(
      kdc::WeightedStaticObjective::evaluate_assignment(matrix, {0, 2}),
      kdc::WeightedAssignmentError);
  REQUIRE_THROWS_AS(
      kdc::WeightedStaticObjective::evaluate_assignment(matrix, {-1, 0}),
      kdc::WeightedAssignmentError);

  const kdc::Weight assigned =
      kdc::WeightedStaticObjective::evaluate_assignment(matrix, {0, 1});
  REQUIRE(weights_close(assigned, std::acos(-1.0) * 3.0));
}

TEST_CASE("Brute-force reference matches direct objective minima") {
  kdc::WeightMatrix matrix =
      kdc::WeightMatrix::from_rows({{2.0, 7.0}, {5.0, 1.0}, {3.0, 4.0}});
  const auto reference = brute_force_weighted_reference(matrix);

  kdc::Weight direct_min =
      std::numeric_limits<kdc::Weight>::infinity();
  for (int first = 0; first < 3; ++first) {
    for (int second = 0; second < 3; ++second) {
      direct_min =
          (std::min)(direct_min,
                     kdc::WeightedStaticObjective::evaluate_assignment(
                         matrix, {first, second}));
    }
  }
  REQUIRE(weights_close(reference.optimum, direct_min));
}
