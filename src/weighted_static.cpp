#include "kdc/weighted_static.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <sstream>

namespace kdc {
namespace {
bool dimensions_fit(WeightIndex station_count, WeightIndex point_count) {
  return point_count == 0U ||
         station_count <= std::numeric_limits<WeightIndex>::max() /
                              point_count;
}

std::string invalid_weight_message(WeightIndex station, WeightIndex point,
                                   Weight value) {
  std::ostringstream message;
  message << "invalid weight at station " << station << ", point " << point
          << ": " << value;
  return message.str();
}
}

WeightMatrix::WeightMatrix(WeightIndex station_count, WeightIndex point_count)
    : WeightMatrix(station_count, point_count,
                   std::vector<Weight>(dimensions_fit(station_count,
                                                     point_count)
                                           ? station_count * point_count
                                           : 0U,
                                       0.0)) {
  if (!dimensions_fit(station_count, point_count)) {
    throw WeightedMatrixError("station*point dimension overflow");
  }
  validate();
}

WeightMatrix::WeightMatrix(WeightIndex station_count, WeightIndex point_count,
                           std::vector<Weight> row_major_weights)
    : station_count_(station_count),
      point_count_(point_count),
      weights_(std::move(row_major_weights)) {
  if (!dimensions_fit(station_count_, point_count_)) {
    throw WeightedMatrixError("station*point dimension overflow");
  }
  validate();
}

WeightMatrix WeightMatrix::from_rows(
    const std::vector<std::vector<Weight>>& station_rows) {
  const WeightIndex station_count = station_rows.size();
  if (station_count == 0U) {
    return WeightMatrix(0U, 0U);
  }
  const WeightIndex point_count = station_rows.front().size();
  std::vector<Weight> weights;
  if (!dimensions_fit(station_count, point_count)) {
    throw WeightedMatrixError("station*point dimension overflow");
  }
  weights.reserve(station_count * point_count);
  for (WeightIndex station = 0U; station < station_count; ++station) {
    if (station_rows[station].size() != point_count) {
      throw WeightedMatrixError(
          "from_rows requires the same point count for every station");
    }
    weights.insert(weights.end(), station_rows[station].begin(),
                   station_rows[station].end());
  }
  return WeightMatrix(station_count, point_count, std::move(weights));
}

WeightIndex WeightMatrix::index(WeightIndex station, WeightIndex point) const {
  if (station >= station_count_) {
    throw WeightedMatrixError("station index is out of range");
  }
  if (point >= point_count_) {
    throw WeightedMatrixError("point index is out of range");
  }
  return station * point_count_ + point;
}

void WeightMatrix::validate() const {
  if (!dimensions_fit(station_count_, point_count_) ||
      weights_.size() != station_count_ * point_count_) {
    throw WeightedMatrixError(
        "weight matrix dimensions do not match row-major data");
  }
  for (WeightIndex station = 0U; station < station_count_; ++station) {
    for (WeightIndex point = 0U; point < point_count_; ++point) {
      const Weight value =
          weights_[station * point_count_ + point];
      if (!std::isfinite(value) || value < 0.0) {
        throw WeightedMatrixError(
            invalid_weight_message(station, point, value));
      }
    }
  }
}

Weight WeightMatrix::weight(WeightIndex station, WeightIndex point) const {
  return weights_[index(station, point)];
}

bool WeightMatrix::operator==(const WeightMatrix& other) const noexcept {
  return station_count_ == other.station_count_ &&
         point_count_ == other.point_count_ &&
         weights_ == other.weights_;
}

void WeightedStaticObjective::validate_assignment(
    const WeightMatrix& matrix,
    const std::vector<int>& assignment) {
  if (matrix.station_count() == 0U) {
    if (!assignment.empty()) {
      throw WeightedProblemInfeasible(
          "points cannot be assigned because the station count is zero");
    }
    return;
  }
  if (assignment.size() != matrix.point_count()) {
    throw WeightedAssignmentError(
        "assignment size must equal the point count");
  }
  for (WeightIndex point = 0U; point < assignment.size(); ++point) {
    const int station = assignment[point];
    if (station < 0 ||
        static_cast<WeightIndex>(station) >= matrix.station_count()) {
      throw WeightedAssignmentError(
          "assignment station index is out of range");
    }
  }
}

Weight WeightedStaticObjective::evaluate_assignment(
    const WeightMatrix& matrix,
    const std::vector<int>& assignment) {
  validate_assignment(matrix, assignment);
  if (matrix.point_count() == 0U) {
    return 0.0;
  }
  if (matrix.station_count() == 0U) {
    throw WeightedProblemInfeasible(
        "no assignment exists with zero stations and positive point count");
  }

  std::vector<Weight> station_max(matrix.station_count(), 0.0);
  for (WeightIndex point = 0U; point < matrix.point_count(); ++point) {
    const WeightIndex station =
        static_cast<WeightIndex>(assignment[point]);
    station_max[station] =
        std::max(station_max[station], matrix.weight(station, point));
  }

  Weight station_sum = 0.0;
  for (const Weight station_maximum : station_max) {
    station_sum += station_maximum;
  }
  const Weight pi = std::acos(-1.0);
  return pi * station_sum;
}

}  // namespace kdc
