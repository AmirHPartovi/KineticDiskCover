#include "kdc/weighted_upper_envelope.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace kdc {
namespace {
constexpr double kRootRelativeTolerance = 64.0 *
                                         std::numeric_limits<double>::epsilon();

bool finite_polynomial(const QuadraticFunctionV1& polynomial) {
  return std::isfinite(polynomial.a) &&
         std::isfinite(polynomial.b) &&
         std::isfinite(polynomial.c);
}

Weight polynomial_at(
    const QuadraticFunctionV1& polynomial,
    double time) {
  return polynomial.a * time * time +
         polynomial.b * time +
         polynomial.c;
}

bool roots_too_close(double lhs, double rhs) {
  if (lhs == rhs) {
    return false;
  }
  const double scale =
      std::max({1.0, std::abs(lhs), std::abs(rhs)});
  return std::abs(lhs - rhs) <=
         kRootRelativeTolerance * scale;
}
}

WeightedUpperEnvelope::WeightedUpperEnvelope(
    const Instance& instance)
    : instance_(instance),
      geometry_(instance) {}

bool WeightedUpperEnvelope::validate_input(
    double interval_start,
    double interval_end,
    const std::vector<int>& assigned_station) const {
  if (!std::isfinite(interval_start) ||
      !std::isfinite(interval_end) ||
      interval_start < 0.0 ||
      interval_end > instance_.T_end ||
      interval_start > interval_end) {
    return false;
  }
  if (assigned_station.size() !=
      static_cast<WeightIndex>(instance_.n)) {
    return false;
  }
  for (const int station : assigned_station) {
    if (station < -1 ||
        station >= instance_.m) {
      return false;
    }
  }
  return true;
}

std::vector<double>
WeightedUpperEnvelope::collect_interval_breaks(
    double interval_start,
    double interval_end) const {
  std::vector<double> cuts{interval_start, interval_end};
  for (const double breakpoint :
       geometry_.common_partition()) {
    if (breakpoint > interval_start &&
        breakpoint < interval_end) {
      cuts.push_back(breakpoint);
    }
  }
  std::sort(cuts.begin(), cuts.end());
  cuts.erase(std::unique(cuts.begin(), cuts.end()),
             cuts.end());
  return cuts;
}

std::vector<double>
WeightedUpperEnvelope::real_quadratic_roots(
    const QuadraticFunctionV1& difference,
    double interval_start,
    double interval_end,
    bool& indeterminate) const {
  indeterminate = false;
  if (!finite_polynomial(difference)) {
    indeterminate = true;
    return {};
  }
  if (difference.a == 0.0) {
    if (difference.b == 0.0) {
      return {};
    }
    const long double root =
        -static_cast<long double>(difference.c) /
        static_cast<long double>(difference.b);
    if (!std::isfinite(static_cast<double>(root))) {
      indeterminate = true;
      return {};
    }
    const double result =
        static_cast<double>(root);
    if (result < interval_start ||
        result > interval_end) {
      return {};
    }
    return {result};
  }

  const long double a =
      static_cast<long double>(difference.a);
  const long double b =
      static_cast<long double>(difference.b);
  const long double c =
      static_cast<long double>(difference.c);
  const long double discriminant =
      b * b - 4.0L * a * c;
  if (!std::isfinite(
          static_cast<double>(discriminant))) {
    indeterminate = true;
    return {};
  }
  if (discriminant < 0.0L) {
    return {};
  }
  if (discriminant == 0.0L) {
    const long double root =
        -b / (2.0L * a);
    if (!std::isfinite(static_cast<double>(root))) {
      indeterminate = true;
      return {};
    }
    const double result =
        static_cast<double>(root);
    if (result < interval_start ||
        result > interval_end) {
      return {};
    }
    return {result};
  }

  const long double sqrt_discriminant =
      std::sqrt(discriminant);
  const long double first =
      (-b - sqrt_discriminant) / (2.0L * a);
  const long double second =
      (-b + sqrt_discriminant) / (2.0L * a);
  if (!std::isfinite(static_cast<double>(first)) ||
      !std::isfinite(static_cast<double>(second))) {
    indeterminate = true;
    return {};
  }

  std::vector<double> roots;
  for (long double root : {first, second}) {
    const double result =
        static_cast<double>(root);
    if (result >= interval_start &&
        result <= interval_end) {
      roots.push_back(result);
    }
  }
  if (roots.size() == 2U &&
      roots_too_close(roots[0], roots[1])) {
    indeterminate = true;
    return {};
  }
  std::sort(roots.begin(), roots.end());
  roots.erase(std::unique(roots.begin(), roots.end()),
              roots.end());
  return roots;
}

Weight WeightedUpperEnvelope::integrate_polynomial(
    const QuadraticFunctionV1& polynomial,
    double start,
    double end) const {
  if (!finite_polynomial(polynomial) ||
      !std::isfinite(start) ||
      !std::isfinite(end) ||
      end < start) {
    throw std::invalid_argument(
        "cannot integrate invalid quadratic interval");
  }
  const double start2 = start * start;
  const double end2 = end * end;
  const double start3 = start2 * start;
  const double end3 = end2 * end;
  const Weight integral =
      polynomial.a / 3.0 * (end3 - start3) +
      polynomial.b / 2.0 * (end2 - start2) +
      polynomial.c * (end - start);
  if (!std::isfinite(integral)) {
    throw std::overflow_error(
        "upper-envelope polynomial integral is not finite");
  }
  return std::max(Weight{0.0}, integral);
}

UpperEnvelopePieceResultV1
WeightedUpperEnvelope::integrate_station_segment(
    const std::vector<int>& assigned_points,
    int station,
    double segment_start,
    double segment_end) const {
  UpperEnvelopePieceResultV1 piece;
  piece.start = segment_start;
  piece.end = segment_end;
  piece.active_point = -1;

  if (assigned_points.empty()) {
    piece.integral = 0.0;
    return piece;
  }

  std::vector<QuadraticFunctionV1> functions;
  functions.reserve(assigned_points.size());
  for (const int point : assigned_points) {
    if (point < 0 ||
        point >= instance_.n) {
      throw std::invalid_argument(
          "upper envelope assignment contains invalid point");
    }
    const auto coefficient =
        geometry_.coefficients(
            static_cast<WeightIndex>(point),
            static_cast<WeightIndex>(station),
            segment_start,
            segment_end);
    functions.push_back(
        {coefficient.a,
         coefficient.b,
         coefficient.c});
  }

  if (functions.size() == 1U) {
    piece.integral =
        integrate_polynomial(
            functions.front(),
            segment_start,
            segment_end);
    piece.active_point = assigned_points.front();
    piece.active_station = station;
    piece.station_radii.assign(
        static_cast<WeightIndex>(instance_.m),
        0.0);
    const Weight radius =
        std::sqrt(std::max(
            Weight{0.0},
            polynomial_at(functions.front(),
                          segment_start)));
    piece.station_radii[
        static_cast<WeightIndex>(station)] =
        std::sqrt(
            std::max(
                Weight{0.0},
                polynomial_at(
                    functions.front(),
                    segment_end)));
    return piece;
  }

  std::vector<double> roots;
  for (WeightIndex first = 0U;
       first < functions.size();
       ++first) {
    for (WeightIndex second = first + 1U;
         second < functions.size();
         ++second) {
      QuadraticFunctionV1 difference;
      difference.a =
          functions[first].a -
          functions[second].a;
      difference.b =
          functions[first].b -
          functions[second].b;
      difference.c =
          functions[first].c -
          functions[second].c;
      if (difference.a == 0.0 &&
          difference.b == 0.0 &&
          difference.c == 0.0) {
        continue;
      }
      bool indeterminate = false;
      const auto pair_roots =
          real_quadratic_roots(
              difference,
              segment_start,
              segment_end,
              indeterminate);
      if (indeterminate) {
        throw std::runtime_error(
            "upper-envelope root isolation is indeterminate");
      }
      for (const double root : pair_roots) {
        if (root >= segment_start &&
            root <= segment_end) {
          roots.push_back(root);
        }
      }
    }
  }

  std::sort(roots.begin(), roots.end());
  roots.erase(std::unique(roots.begin(), roots.end()),
              roots.end());
  for (WeightIndex index = 1U;
       index < roots.size();
       ++index) {
    if (roots_too_close(roots[index - 1U],
                        roots[index])) {
      throw std::runtime_error(
          "upper-envelope root ordering is indeterminate");
    }
  }

  std::vector<double> cuts;
  cuts.push_back(segment_start);
  for (const double root : roots) {
    if (root > segment_start &&
        root < segment_end) {
      cuts.push_back(root);
    }
  }
  cuts.push_back(segment_end);
  std::sort(cuts.begin(), cuts.end());
  cuts.erase(std::unique(cuts.begin(), cuts.end()),
             cuts.end());

  Weight sum = 0.0;
  int active_point = -1;
  for (WeightIndex cut = 0U;
       cut + 1U < cuts.size();
       ++cut) {
    const double sub_start = cuts[cut];
    const double sub_end = cuts[cut + 1U];
    if (sub_end <= sub_start) {
      continue;
    }
    const double midpoint =
        sub_start +
        (sub_end - sub_start) / 2.0;
    Weight maximum =
        -std::numeric_limits<Weight>::infinity();
    int selected = -1;
    for (WeightIndex index = 0U;
         index < functions.size();
         ++index) {
      const Weight value =
          polynomial_at(functions[index], midpoint);
      if (value > maximum ||
          (value == maximum &&
           assigned_points[index] < selected)) {
        maximum = value;
        selected = assigned_points[index];
      }
    }
    if (selected < 0) {
      throw std::runtime_error(
          "upper envelope has no active function");
    }
    for (WeightIndex index = 0U;
         index < functions.size();
         ++index) {
      if (assigned_points[index] == selected) {
        const Weight integral =
            integrate_polynomial(
                functions[index],
                sub_start,
                sub_end);
        sum += integral;
        active_point = selected;
        break;
      }
    }
  }
  piece.integral = sum;
  piece.active_point = active_point;
  piece.active_station = station;
  piece.station_radii.assign(
      static_cast<WeightIndex>(instance_.m),
      0.0);
  Weight radius_squared = 0.0;
  for (const double cut : cuts) {
    for (WeightIndex index = 0U;
         index < functions.size();
         ++index) {
      radius_squared =
          std::max(radius_squared,
                   polynomial_at(functions[index], cut));
    }
  }
  piece.station_radii[
      static_cast<WeightIndex>(station)] =
      std::sqrt(std::max(Weight{0.0}, radius_squared));
  return piece;
}

Weight WeightedUpperEnvelope::d_plus_fallback(
    const std::vector<int>& assigned_station,
    double interval_start,
    double interval_end) const {
  const auto bounds =
      geometry_.bounds_on(
          interval_start,
          interval_end);
  const double delta =
      interval_end - interval_start;
  Weight station_sum = 0.0;
  for (int station = 0;
       station < instance_.m;
       ++station) {
    Weight station_max = 0.0;
    bool assigned = false;
    for (int point = 0;
         point < instance_.n;
         ++point) {
      if (assigned_station[point] == station) {
        assigned = true;
        station_max =
            std::max(
                station_max,
                bounds.upper.matrix.weight(
                    static_cast<WeightIndex>(station),
                    static_cast<WeightIndex>(point)));
      }
    }
    if (assigned) {
      station_sum += station_max;
    }
  }
  const double pi =
      std::acos(-1.0);
  const Weight result =
      pi * delta * station_sum;
  if (!std::isfinite(result) ||
      result < 0.0) {
    throw std::overflow_error(
        "D+ fallback upper bound is invalid");
  }
  return result;
}

UpperEnvelopeResultV1 WeightedUpperEnvelope::solve(
    double interval_start,
    double interval_end,
    const std::vector<int>& assigned_station,
    UpperEnvelopeModeV1 mode) const {
  UpperEnvelopeResultV1 result;
  result.mode = mode;
  if (!validate_input(interval_start,
                      interval_end,
                      assigned_station)) {
    result.status =
        UpperEnvelopeStatusV1::INFEASIBLE;
    result.failure_reason =
        "invalid interval or assignment dimensions";
    return result;
  }
  if (mode == UpperEnvelopeModeV1::VALIDATED) {
    result.status =
        UpperEnvelopeStatusV1::VALIDATED_UNAVAILABLE;
    result.failure_reason =
        "validated envelope arithmetic is not available";
  }

  bool fallback_valid = false;
  Weight fallback = std::numeric_limits<Weight>::infinity();
  if (mode == UpperEnvelopeModeV1::FAST_DOUBLE) {
    try {
      fallback = d_plus_fallback(
          assigned_station,
          interval_start,
          interval_end);
      fallback_valid = true;
    } catch (const std::exception& error) {
      fallback_valid = false;
    }
  }
  result.d_plus_fallback_valid = fallback_valid;
  if (fallback_valid) {
    result.d_plus_fallback_integral = fallback;
  }

  if (mode == UpperEnvelopeModeV1::VALIDATED) {
    result.status =
        UpperEnvelopeStatusV1::VALIDATED_UNAVAILABLE;
    result.direct_valid = false;
    result.upper_bound =
        std::numeric_limits<Weight>::infinity();
    result.failure_reason =
        "validated envelope arithmetic unavailable";
    return result;
  }

  try {
    const auto cuts =
        collect_interval_breaks(
            interval_start,
            interval_end);
    Weight direct_total = 0.0;
    for (WeightIndex cut = 0U;
         cut + 1U < cuts.size();
         ++cut) {
      const double sub_start = cuts[cut];
      const double sub_end = cuts[cut + 1U];
      if (sub_end <= sub_start) {
        continue;
      }
      for (int station = 0;
           station < instance_.m;
           ++station) {
        std::vector<int> assigned_points;
        for (int point = 0;
             point < instance_.n;
             ++point) {
          if (assigned_station[point] == station) {
            assigned_points.push_back(point);
          }
        }
        const auto piece =
            integrate_station_segment(
                assigned_points,
                station,
                sub_start,
                sub_end);
        direct_total += piece.integral;
        result.pieces.push_back(piece);
      }
    }
    const double pi =
        std::acos(-1.0);
    direct_total = pi * direct_total;
    result.direct_integral = direct_total;
    result.direct_valid =
        std::isfinite(direct_total);
  } catch (const std::exception& error) {
    result.direct_valid = false;
    result.status =
        UpperEnvelopeStatusV1::ROOT_INDETERMINATE;
    result.failure_reason = error.what();
  }

  if (result.direct_valid) {
    result.status =
        UpperEnvelopeStatusV1::FAST_COMPLETE;
    result.upper_bound =
        std::min(result.direct_integral,
                 fallback_valid
                     ? result.d_plus_fallback_integral
                     : result.direct_integral);
  } else if (fallback_valid) {
    result.status =
        UpperEnvelopeStatusV1::FAST_COMPLETE;
    result.failure_reason =
        "direct envelope indeterminate; D+ fallback used";
    result.upper_bound =
        result.d_plus_fallback_integral;
  } else {
    result.status =
        UpperEnvelopeStatusV1::INVALID;
    result.upper_bound =
        std::numeric_limits<Weight>::infinity();
  }
  return result;
}

}  // namespace kdc
