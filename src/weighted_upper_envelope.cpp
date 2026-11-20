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

Weight evaluate_local(
    const QuadraticFunctionV1& polynomial,
    double local_time) {
  return (polynomial.a * local_time + polynomial.b) *
             local_time +
         polynomial.c;
}

Weight integrate_local(
    const QuadraticFunctionV1& polynomial,
    double duration) {
  const long double width = duration;
  const long double value =
      static_cast<long double>(polynomial.a) *
          width * width * width / 3.0L +
      static_cast<long double>(polynomial.b) *
          width * width / 2.0L +
      static_cast<long double>(polynomial.c) * width;
  return static_cast<Weight>(value);
}

Weight round_up_nonnegative(long double value) {
  if (!std::isfinite(value) || value < 0.0L) {
    return std::numeric_limits<Weight>::infinity();
  }
  if (value == 0.0L) {
    return 0.0;
  }
  Weight result = static_cast<Weight>(value);
  if (static_cast<long double>(result) < value) {
    result = std::nextafter(
        result, std::numeric_limits<Weight>::infinity());
  }
  return std::nextafter(
      result, std::numeric_limits<Weight>::infinity());
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

std::vector<UpperEnvelopePieceResultV1>
WeightedUpperEnvelope::integrate_common_segment(
    const std::vector<int>& assigned_station,
    double segment_start,
    double segment_end) const {
  struct AssignedPolynomial {
    int point;
    QuadraticFunctionV1 function;
  };
  std::vector<std::vector<AssignedPolynomial>> functions(
      static_cast<WeightIndex>(instance_.m));
  for (int point = 0; point < instance_.n; ++point) {
    const int station =
        assigned_station[static_cast<WeightIndex>(point)];
    if (station < 0) {
      continue;
    }
    const auto coefficient = geometry_.coefficients(
        static_cast<WeightIndex>(point),
        static_cast<WeightIndex>(station),
        segment_start,
        segment_end);
    functions[static_cast<WeightIndex>(station)].push_back(
        {point,
         {coefficient.a, coefficient.b, coefficient.c}});
  }
  std::vector<double> roots;
  for (const auto& station_functions : functions) {
    for (WeightIndex first = 0U;
         first < station_functions.size();
         ++first) {
      for (WeightIndex second = first + 1U;
           second < station_functions.size();
           ++second) {
      QuadraticFunctionV1 difference;
      difference.a =
          station_functions[first].function.a -
          station_functions[second].function.a;
      difference.b =
          station_functions[first].function.b -
          station_functions[second].function.b;
      difference.c =
          station_functions[first].function.c -
          station_functions[second].function.c;
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

  std::vector<UpperEnvelopePieceResultV1> pieces;
  for (WeightIndex cut = 0U;
       cut + 1U < cuts.size();
       ++cut) {
    const double sub_start = cuts[cut];
    const double sub_end = cuts[cut + 1U];
    if (sub_end <= sub_start) {
      continue;
    }
    UpperEnvelopePieceResultV1 piece;
    piece.start = sub_start;
    piece.end = sub_end;
    piece.time_origin = sub_start;
    piece.station_q.resize(
        static_cast<WeightIndex>(instance_.m));
    piece.station_radii.assign(
        static_cast<WeightIndex>(instance_.m), 0.0);
    const double midpoint =
        sub_start +
        (sub_end - sub_start) / 2.0;
    for (WeightIndex station = 0U;
         station < functions.size();
         ++station) {
      const auto& station_functions = functions[station];
      if (station_functions.empty()) {
        continue;
      }
      const AssignedPolynomial* active = nullptr;
      Weight maximum =
          -std::numeric_limits<Weight>::infinity();
      for (const auto& candidate : station_functions) {
        const Weight value =
            polynomial_at(candidate.function, midpoint);
        if (value > maximum ||
            (value == maximum &&
             (active == nullptr ||
              candidate.point < active->point))) {
          maximum = value;
          active = &candidate;
        }
      }
      if (active == nullptr) {
        throw std::runtime_error(
            "upper envelope has no active function");
      }
      const auto& absolute = active->function;
      auto& local = piece.station_q[station];
      local.a = absolute.a;
      local.b = 2.0 * absolute.a * sub_start + absolute.b;
      local.c = polynomial_at(absolute, sub_start);
      if (!finite_polynomial(local)) {
        throw std::runtime_error(
            "serialized upper-envelope polynomial is non-finite");
      }
      Weight maximum_q = std::max(
          evaluate_local(local, 0.0),
          evaluate_local(local, sub_end - sub_start));
      if (local.a != 0.0) {
        const Weight vertex = -local.b / (2.0 * local.a);
        if (vertex > 0.0 &&
            vertex < sub_end - sub_start) {
          maximum_q = std::max(maximum_q,
                               evaluate_local(local, vertex));
        }
      }
      piece.station_radii[station] =
          std::sqrt(std::max(Weight{0.0}, maximum_q));
      piece.integral += integrate_local(
          local, sub_end - sub_start);
    }
    pieces.push_back(std::move(piece));
  }
  return pieces;
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
  const long double pi =
      std::acos(-1.0L);
  const Weight result =
      round_up_nonnegative(
          pi * delta * station_sum);
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
    long double direct_program_cost = 0.0L;
    for (WeightIndex cut = 0U;
         cut + 1U < cuts.size();
         ++cut) {
      const double sub_start = cuts[cut];
      const double sub_end = cuts[cut + 1U];
      if (sub_end <= sub_start) {
        continue;
      }
      const auto pieces = integrate_common_segment(
          assigned_station, sub_start, sub_end);
      for (const auto& piece : pieces) {
        const long double width =
            static_cast<long double>(piece.end) - piece.start;
        for (const auto& q : piece.station_q) {
          direct_program_cost +=
              static_cast<long double>(q.a) *
                  width * width * width / 3.0L +
              static_cast<long double>(q.b) *
                  width * width / 2.0L +
              static_cast<long double>(q.c) * width;
        }
        result.pieces.push_back(piece);
      }
    }
    result.direct_integral = round_up_nonnegative(
        std::acos(-1.0L) * direct_program_cost);
    result.direct_valid =
        std::isfinite(result.direct_integral);
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
