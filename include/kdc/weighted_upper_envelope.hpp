#pragma once

#include "kdc/types.hpp"
#include "kdc/weighted_geometry.hpp"

#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <vector>

namespace kdc {

enum class UpperEnvelopeModeV1 {
  FAST_DOUBLE,
  VALIDATED
};

enum class UpperEnvelopeStatusV1 {
  FAST_COMPLETE,
  ROOT_INDETERMINATE,
  VALIDATED_UNAVAILABLE,
  INFEASIBLE,
  INVALID
};

struct QuadraticFunctionV1 {
  Weight a{0.0};
  Weight b{0.0};
  Weight c{0.0};
};

struct UpperEnvelopePieceResultV1 {
  double start{0.0};
  double end{0.0};
  double time_origin{0.0};
  Weight integral{0.0};
  int active_point{-1};
  int active_station{-1};
  std::vector<Weight> station_radii;
  std::vector<QuadraticFunctionV1> station_q;
};

struct UpperEnvelopeResultV1 {
  std::uint32_t schema_version{1};
  UpperEnvelopeModeV1 mode{
      UpperEnvelopeModeV1::FAST_DOUBLE};
  UpperEnvelopeStatusV1 status{
      UpperEnvelopeStatusV1::INVALID};
  bool direct_valid{false};
  bool d_plus_fallback_valid{false};
  Weight direct_integral{
      std::numeric_limits<Weight>::infinity()};
  Weight d_plus_fallback_integral{
      std::numeric_limits<Weight>::infinity()};
  Weight upper_bound{
      std::numeric_limits<Weight>::infinity()};
  std::string failure_reason;
  std::vector<UpperEnvelopePieceResultV1> pieces;
};

class WeightedUpperEnvelope {
 public:
  explicit WeightedUpperEnvelope(const Instance& instance);

  UpperEnvelopeResultV1 solve(
      double interval_start,
      double interval_end,
      const std::vector<int>& assigned_station,
      UpperEnvelopeModeV1 mode =
          UpperEnvelopeModeV1::FAST_DOUBLE) const;

 private:
  Instance instance_;
  WeightedGeometry geometry_;

  bool validate_input(
      double interval_start,
      double interval_end,
      const std::vector<int>& assigned_station) const;

  std::vector<double> collect_interval_breaks(
      double interval_start,
      double interval_end) const;

  std::vector<UpperEnvelopePieceResultV1> integrate_common_segment(
      const std::vector<int>& assigned_station,
      double segment_start,
      double segment_end) const;

  Weight integrate_polynomial(
      const QuadraticFunctionV1& polynomial,
      double start,
      double end) const;

  std::vector<double> real_quadratic_roots(
      const QuadraticFunctionV1& difference,
      double interval_start,
      double interval_end,
      bool& indeterminate) const;

  Weight d_plus_fallback(
      const std::vector<int>& assigned_station,
      double interval_start,
      double interval_end) const;
};

}  // namespace kdc
