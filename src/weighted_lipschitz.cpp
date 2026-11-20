#include "kdc/weighted_lipschitz.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace kdc {
namespace {
double add_up(double lhs, double rhs) {
  return std::nextafter(
      lhs + rhs,
      std::numeric_limits<double>::infinity());
}

double sub_up(double lhs, double rhs) {
  return std::nextafter(
      lhs - rhs,
      std::numeric_limits<double>::infinity());
}

double mul_up(double lhs, double rhs) {
  return std::nextafter(
      lhs * rhs,
      std::numeric_limits<double>::infinity());
}

double div_down(double lhs, double rhs) {
  if (rhs <= 0.0) {
    throw std::invalid_argument(
        "Lipschitz directed division requires positive denominator");
  }
  return std::nextafter(
      lhs / rhs,
      -std::numeric_limits<double>::infinity());
}

double div_up(double lhs, double rhs) {
  if (rhs <= 0.0) {
    throw std::invalid_argument(
        "Lipschitz directed division requires positive denominator");
  }
  return std::nextafter(
      lhs / rhs,
      std::numeric_limits<double>::infinity());
}

double mul_down(double lhs, double rhs) {
  return std::nextafter(
      lhs * rhs,
      -std::numeric_limits<double>::infinity());
}

double sub_down(double lhs, double rhs) {
  return std::nextafter(
      lhs - rhs,
      -std::numeric_limits<double>::infinity());
}

double sqrt_up(double value) {
  if (value < 0.0) {
    throw std::invalid_argument(
        "cannot take the square root of a negative interval");
  }
  return std::nextafter(
      std::sqrt(value),
      std::numeric_limits<double>::infinity());
}

double abs_up(double value) {
  return std::nextafter(
      std::abs(value),
      std::numeric_limits<double>::infinity());
}

double zero_if_near_zero(double value) {
  return value == 0.0 ? 0.0 : value;
}
}

WeightedLipschitzBound::WeightedLipschitzBound(
    const Instance& instance)
    : instance_(instance),
      geometry_(instance) {}

ValidatedMidpointLowerBoundV1
WeightedLipschitzBound::make_validated_midpoint_bound(
    Weight value,
    MidpointLowerBoundSourceV1 source) {
  if (!std::isfinite(value) || value < 0.0) {
    throw std::invalid_argument(
        "validated midpoint lower bound must be finite and nonnegative");
  }
  if (source != MidpointLowerBoundSourceV1::EXACT_STATIC &&
      source != MidpointLowerBoundSourceV1::DUAL_CERTIFICATE &&
      source != MidpointLowerBoundSourceV1::RELAXATION_CERTIFICATE) {
    throw std::invalid_argument(
        "Lipschitz midpoint lower bound cannot use heuristic or unknown source");
  }
  return {value, source, true};
}

Weight WeightedLipschitzBound::velocity_norm_upper(
    WeightIndex point,
    double interval_start,
    double interval_end) const {
  if (point >=
      static_cast<WeightIndex>(instance_.trajectories.size())) {
    throw std::out_of_range(
        "Lipschitz point index is out of range");
  }
  if (!std::isfinite(interval_start) ||
      !std::isfinite(interval_end) ||
      interval_start < 0.0 ||
      interval_end > instance_.T_end ||
      interval_start > interval_end) {
    throw std::invalid_argument(
        "Lipschitz velocity interval is invalid");
  }
  const auto& trajectory =
      instance_.trajectories[point];
  for (WeightIndex index = 1U;
       index < trajectory.t_breaks.size();
       ++index) {
    const double breakpoint =
        trajectory.t_breaks[index];
    if (breakpoint > interval_start &&
        breakpoint < interval_end) {
      throw std::invalid_argument(
          "Lipschitz interval crosses a trajectory breakpoint");
    }
  }

  const double midpoint =
      interval_start +
      (interval_end - interval_start) / 2.0;
  WeightIndex segment = 0U;
  bool found = false;
  for (WeightIndex index = 0U;
       index + 1U < trajectory.t_breaks.size();
       ++index) {
    if (midpoint >= trajectory.t_breaks[index] &&
        midpoint <= trajectory.t_breaks[index + 1U]) {
      segment = index;
      found = true;
      break;
    }
  }
  if (!found) {
    throw std::invalid_argument(
        "Lipschitz interval has no trajectory segment");
  }
  const double duration =
      trajectory.t_breaks[segment + 1U] -
      trajectory.t_breaks[segment];
  if (!std::isfinite(duration) ||
      duration <= 0.0) {
    throw std::invalid_argument(
        "Lipschitz trajectory segment duration is invalid");
  }
  const Point difference =
      trajectory.waypoints[segment + 1U] -
      trajectory.waypoints[segment];
  const double dx_up =
      abs_up(difference.x);
  const double dy_up =
      abs_up(difference.y);
  const double squared =
      add_up(
          mul_up(dx_up, dx_up),
          mul_up(dy_up, dy_up));
  if (squared == 0.0) {
    return 0.0;
  }
  const double norm_upper =
      sqrt_up(squared);
  const double speed_upper =
      std::nextafter(
          norm_upper / duration,
          std::numeric_limits<double>::infinity());
  if (!std::isfinite(speed_upper) ||
      speed_upper < 0.0) {
    throw std::overflow_error(
        "Lipschitz velocity enclosure is not finite");
  }
  return zero_if_near_zero(speed_upper);
}

LipschitzPairBoundV1 WeightedLipschitzBound::pair_upper(
    WeightIndex point,
    WeightIndex station,
    double interval_start,
    double interval_end) const {
  if (point >=
      static_cast<WeightIndex>(instance_.trajectories.size())) {
    throw std::out_of_range(
        "Lipschitz point index is out of range");
  }
  if (station >=
      static_cast<WeightIndex>(instance_.stations.size())) {
    throw std::out_of_range(
        "Lipschitz station index is out of range");
  }

  const auto& trajectory =
      instance_.trajectories[point];
  const Point center =
      instance_.stations[station].pos;

  double maximum_squared = 0.0;
  for (const double time :
       {interval_start, interval_end}) {
    const Point position =
        trajectory.position(time);
    const double dx_up =
        abs_up(position.x - center.x);
    const double dy_up =
        abs_up(position.y - center.y);
    const double squared =
        add_up(
            mul_up(dx_up, dx_up),
            mul_up(dy_up, dy_up));
    maximum_squared =
        std::max(maximum_squared, squared);
  }
  const double r_upper =
      sqrt_up(maximum_squared);
  const double velocity_upper =
      velocity_norm_upper(
          point,
          interval_start,
          interval_end);
  const double l_upper =
      mul_up(
          mul_up(2.0, r_upper),
          velocity_upper);

  LipschitzPairBoundV1 result;
  result.r_upper = r_upper;
  result.velocity_norm_upper = velocity_upper;
  result.l_upper = l_upper;
  return result;
}

LipschitzLambdaBoundV1 WeightedLipschitzBound::lambda_upper(
    double interval_start,
    double interval_end) const {
  LipschitzLambdaBoundV1 result;
  Weight station_sum = 0.0;
  for (WeightIndex station = 0U;
       station < instance_.stations.size();
       ++station) {
    Weight point_max = 0.0;
    for (WeightIndex point = 0U;
         point < instance_.trajectories.size();
         ++point) {
      const auto pair =
          pair_upper(point, station,
                     interval_start, interval_end);
      result.pairs.push_back(pair);
      point_max =
          std::max(point_max, pair.l_upper);
    }
    station_sum =
        add_up(station_sum, point_max);
  }
  const double pi_upper =
      std::nextafter(
          std::acos(-1.0),
          std::numeric_limits<double>::infinity());
  result.lambda_upper =
      mul_up(pi_upper, station_sum);
  if (!std::isfinite(result.lambda_upper) ||
      result.lambda_upper < 0.0) {
    throw std::overflow_error(
        "Lipschitz Lambda enclosure is invalid");
  }
  return result;
}

Weight WeightedLipschitzBound::phi_fast(
    Weight u,
    Weight K,
    Weight delta) {
  if (!std::isfinite(u) ||
      !std::isfinite(K) ||
      !std::isfinite(delta) ||
      u < 0.0 ||
      K < 0.0 ||
      delta < 0.0) {
    throw std::invalid_argument(
        "Phi requires finite nonnegative u,K,Delta");
  }
  if (K == 0.0 || delta == 0.0 || u == 0.0) {
    return 0.0;
  }

  const double K_upper =
      std::nextafter(
          K,
          std::numeric_limits<double>::infinity());
  const double delta_lower =
      std::nextafter(
          delta,
          -std::numeric_limits<double>::infinity());
  const double delta_upper =
      std::nextafter(
          delta,
          std::numeric_limits<double>::infinity());
  const double threshold =
      mul_up(
          mul_up(K_upper, delta_upper),
          0.5);

  const double delta_u_low =
      mul_down(delta_lower, u);
  const double k_delta_upper =
      mul_up(K_upper, delta_upper);
  const double k_delta_squared_upper =
      mul_up(k_delta_upper, delta_upper);
  const double correction_upper =
      div_up(k_delta_squared_upper, 4.0);
  const double high =
      sub_down(delta_u_low, correction_upper);
  const double u_squared_down =
      mul_down(u, u);
  const double low =
      div_down(u_squared_down, K_upper);

  double result = 0.0;
  if (u >= threshold) {
    result = high;
  } else {
    result = low;
  }

  // Conservatively evaluate both branches at/near the boundary.
  if (u == threshold) {
    result = std::min({
        high,
        low,
        zero_if_near_zero(result)});
  }
  return std::max(0.0, zero_if_near_zero(result));
}

LipschitzLocalLowerBoundV1
WeightedLipschitzBound::local_lower(
    double interval_start,
    double interval_end,
    const ValidatedMidpointLowerBoundV1& midpoint_lower) const {
  if (!midpoint_lower.independently_validated ||
      (midpoint_lower.source ==
           MidpointLowerBoundSourceV1::HEURISTIC ||
       midpoint_lower.source ==
           MidpointLowerBoundSourceV1::UNKNOWN)) {
    throw std::invalid_argument(
        "Lipschitz local lower bound requires an independently validated midpoint LB");
  }
  const double delta =
      interval_end - interval_start;
  if (!std::isfinite(delta) ||
      delta < 0.0) {
    throw std::invalid_argument(
        "Lipschitz interval delta is invalid");
  }
  const auto lambda =
      lambda_upper(interval_start, interval_end);
  const double lower =
      phi_fast(midpoint_lower.value,
               lambda.lambda_upper,
               delta);

  LipschitzLocalLowerBoundV1 result;
  result.u_lower = midpoint_lower.value;
  result.lambda_upper = lambda.lambda_upper;
  result.delta = delta;
  result.lower_bound = lower;
  result.validity =
      LipschitzValidityV1::FAST_FLOAT_ONLY;
  result.arithmetic =
      LipschitzArithmeticModeV1::FAST_DOUBLE;
  return result;
}

}  // namespace kdc
