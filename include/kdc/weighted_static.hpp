#pragma once

#include <cstddef>
#include <stdexcept>
#include <vector>

namespace kdc {

using Weight = double;
using WeightIndex = std::size_t;

class WeightedMatrixError final : public std::invalid_argument {
 public:
  explicit WeightedMatrixError(const std::string& message)
      : std::invalid_argument(message) {}
};

class WeightedAssignmentError final : public std::invalid_argument {
 public:
  explicit WeightedAssignmentError(const std::string& message)
      : std::invalid_argument(message) {}
};

class WeightedProblemInfeasible final : public std::logic_error {
 public:
  explicit WeightedProblemInfeasible(const std::string& message)
      : std::logic_error(message) {}
};

// Independent row-major station/point weight matrix.
// Rows are stations 0..m-1 and columns are points 0..n-1.
// This type intentionally has no Instance, geometry, time, or solver dependency.
class WeightMatrix {
 public:
  WeightMatrix() = default;
  WeightMatrix(WeightIndex station_count, WeightIndex point_count);
  WeightMatrix(WeightIndex station_count, WeightIndex point_count,
               std::vector<Weight> row_major_weights);

  static WeightMatrix from_rows(
      const std::vector<std::vector<Weight>>& station_rows);

  WeightIndex station_count() const noexcept { return station_count_; }
  WeightIndex point_count() const noexcept { return point_count_; }
  WeightIndex size() const noexcept { return weights_.size(); }

  Weight weight(WeightIndex station, WeightIndex point) const;

  const std::vector<Weight>& row_major_weights() const noexcept {
    return weights_;
  }

  bool operator==(const WeightMatrix& other) const noexcept;

 private:
  WeightIndex station_count_{0};
  WeightIndex point_count_{0};
  std::vector<Weight> weights_;

  WeightIndex index(WeightIndex station, WeightIndex point) const;
  void validate() const;
};

class WeightedStaticObjective {
 public:
  // Thesis formula:
  // g(W) = pi * sum_i max_{j in S_i(sigma)} w_ij,
  // with max(empty)=0.
  static void validate_assignment(const WeightMatrix& matrix,
                                  const std::vector<int>& assignment);
  static Weight evaluate_assignment(
      const WeightMatrix& matrix,
      const std::vector<int>& assignment);
};

}  // namespace kdc
