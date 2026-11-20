#pragma once

#include "kdc/types.hpp"
#include "kdc/weighted_geometry.hpp"

#include <cstdint>
#include <stdexcept>
#include <vector>

namespace kdc {

enum class LipschitzArithmeticModeV1 {
  FAST_DOUBLE,
  VALIDATED_NOT_AVAILABLE
};

enum class LipschitzValidityV1 {
  FAST_FLOAT_ONLY,
  INVALID
};

enum class MidpointLowerBoundSourceV1 {
  EXACT_STATIC,
  DUAL_CERTIFICATE,
  RELAXATION_CERTIFICATE,
  HEURISTIC,
  UNKNOWN
};

struct ValidatedMidpointLowerBoundV1 {
  Weight value{0.0};
  MidpointLowerBoundSourceV1 source{
      MidpointLowerBoundSourceV1::UNKNOWN};
  bool independently_validated{false};
};

struct LipschitzPairBoundV1 {
  std::uint32_t schema_version{1};
  Weight r_upper{0.0};
  Weight velocity_norm_upper{0.0};
  Weight l_upper{0.0};
};

struct LipschitzLambdaBoundV1 {
  std::uint32_t schema_version{1};
  Weight lambda_upper{0.0};
  std::vector<LipschitzPairBoundV1> pairs;
};

struct LipschitzLocalLowerBoundV1 {
  std::uint32_t schema_version{1};
  Weight u_lower{0.0};
  Weight lambda_upper{0.0};
  Weight delta{0.0};
  Weight lower_bound{0.0};
  LipschitzValidityV1 validity{
      LipschitzValidityV1::FAST_FLOAT_ONLY};
  LipschitzArithmeticModeV1 arithmetic{
      LipschitzArithmeticModeV1::FAST_DOUBLE};
};

class WeightedLipschitzBound {
 public:
  explicit WeightedLipschitzBound(const Instance& instance);

  static ValidatedMidpointLowerBoundV1 make_validated_midpoint_bound(
      Weight value,
      MidpointLowerBoundSourceV1 source);

  LipschitzPairBoundV1 pair_upper(
      WeightIndex point,
      WeightIndex station,
      double interval_start,
      double interval_end) const;

  Weight velocity_norm_upper(
      WeightIndex point,
      double interval_start,
      double interval_end) const;

  LipschitzLambdaBoundV1 lambda_upper(
      double interval_start,
      double interval_end) const;

  // Thesis Chapter 5, Equations (12.5)-(13.5):
  // Phi(u,K,Delta)=integral_{-Delta/2}^{Delta/2}
  //                max{0,u-K|s|} ds.
  //
  // Branch:
  // K=0: Delta==0 or u=0 => 0 in the zero branch.
  // u>=K*Delta/2: Delta*u-K*Delta^2/4.
  // 0<=u<K*Delta/2: u^2/K.
  //
  // This implementation is FAST_FLOAT diagnostic unless replaced by the
  // validated MPFR interval backend; it must not be presented as CERTIFIED.
  static Weight phi_fast(
      Weight u,
      Weight K,
      Weight delta);

  LipschitzLocalLowerBoundV1 local_lower(
      double interval_start,
      double interval_end,
      const ValidatedMidpointLowerBoundV1& midpoint_lower) const;

 private:
  Instance instance_;
  WeightedGeometry geometry_;
};

}  // namespace kdc
