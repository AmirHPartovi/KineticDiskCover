#pragma once

#include "kdc/types.hpp"
#include "kdc/weighted_static.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace kdc {

enum class GeometryArithmeticModeV1 {
  FAST_DOUBLE,
  VALIDATED_NOT_AVAILABLE
};

enum class GeometryBoundValidityV1 {
  FAST_FLOAT_ONLY,
  INVALID
};

struct GeometryQuadraticCoefficientsV1 {
  std::uint32_t schema_version{1};
  Weight a{0.0};
  Weight b{0.0};
  Weight c{0.0};
  double segment_start{0.0};
  double segment_end{0.0};
};

struct DAtTimeMatrixV1 {
  std::uint32_t schema_version{1};
  WeightMatrix matrix;
  double time{0.0};
  GeometryArithmeticModeV1 arithmetic{
      GeometryArithmeticModeV1::FAST_DOUBLE};
};

struct DMinusMatrixV1 {
  std::uint32_t schema_version{1};
  WeightMatrix matrix;
};

struct DPlusMatrixV1 {
  std::uint32_t schema_version{1};
  WeightMatrix matrix;
};

struct IntervalDistanceBoundsV1 {
  std::uint32_t schema_version{1};
  DMinusMatrixV1 lower;
  DPlusMatrixV1 upper;
  double interval_start{0.0};
  double interval_end{0.0};
  GeometryArithmeticModeV1 arithmetic{
      GeometryArithmeticModeV1::FAST_DOUBLE};
  GeometryBoundValidityV1 validity{
      GeometryBoundValidityV1::FAST_FLOAT_ONLY};
};

class WeightedGeometry {
 public:
  explicit WeightedGeometry(const Instance& instance);

  DAtTimeMatrixV1 d_at(double time) const;
  IntervalDistanceBoundsV1 bounds_on(double interval_start,
                                     double interval_end) const;

  GeometryQuadraticCoefficientsV1 coefficients(
      WeightIndex point,
      WeightIndex station,
      double segment_start,
      double segment_end) const;

  const std::vector<double>& common_partition() const noexcept {
    return common_partition_;
  }
  const std::vector<int>& station_ids() const noexcept {
    return station_ids_;
  }

 private:
  int point_count_{0};
  int station_count_{0};
  double domain_start_{0.0};
  double domain_end_{1.0};
  std::vector<int> station_ids_;
  std::vector<Point> station_positions_;
  std::vector<Trajectory> trajectories_;
  std::vector<double> common_partition_;

  void validate_instance() const;
  WeightIndex find_segment(double time) const;
  GeometryQuadraticCoefficientsV1 coefficients_on(
      WeightIndex point,
      WeightIndex station,
      double segment_start,
      double segment_end) const;
};

}  // namespace kdc
