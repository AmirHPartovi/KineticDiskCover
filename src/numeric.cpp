#include "kdc/numeric.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace kdc {
namespace {

void require_finite(double value, const char* name) {
  if (!std::isfinite(value)) {
    throw std::invalid_argument(std::string(name) +
                                " must be finite");
  }
}

mpfr_prec_t operation_precision(const NumericInterval& lhs,
                                const NumericInterval& rhs) {
  return std::max(lhs.precision(), rhs.precision());
}

struct DecimalMagnitude {
  int sign{0};
  std::string digits;
  std::int64_t leading_exponent{0};
};

DecimalMagnitude parse_decimal_magnitude(std::string_view text) {
  if (text.empty()) {
    throw std::invalid_argument("decimal endpoint is empty");
  }
  std::size_t position = 0U;
  int sign = 1;
  if (text[position] == '+' || text[position] == '-') {
    sign = text[position] == '-' ? -1 : 1;
    ++position;
  }
  std::string digits;
  std::size_t fractional_digits = 0U;
  bool decimal_point_seen = false;
  bool digit_seen = false;
  while (position < text.size() &&
         text[position] != 'e' && text[position] != 'E') {
    const char character = text[position++];
    if (character == '.') {
      if (decimal_point_seen) {
        throw std::invalid_argument("invalid decimal endpoint");
      }
      decimal_point_seen = true;
      continue;
    }
    if (character < '0' || character > '9') {
      throw std::invalid_argument("invalid decimal endpoint");
    }
    digits.push_back(character);
    digit_seen = true;
    if (decimal_point_seen) {
      ++fractional_digits;
    }
  }
  if (!digit_seen ||
      text.size() > static_cast<std::size_t>(
                        std::numeric_limits<std::int64_t>::max()) ||
      fractional_digits > static_cast<std::size_t>(
                              std::numeric_limits<std::int64_t>::max())) {
    throw std::invalid_argument("invalid decimal endpoint");
  }
  std::int64_t exponent = 0;
  if (position < text.size()) {
    ++position;
    if (position == text.size()) {
      throw std::invalid_argument("invalid decimal endpoint exponent");
    }
    bool exponent_negative = false;
    if (text[position] == '+' || text[position] == '-') {
      exponent_negative = text[position] == '-';
      ++position;
    }
    if (position == text.size()) {
      throw std::invalid_argument("invalid decimal endpoint exponent");
    }
    for (; position < text.size(); ++position) {
      const char character = text[position];
      if (character < '0' || character > '9') {
        throw std::invalid_argument("invalid decimal endpoint exponent");
      }
      if (exponent > 1000000) {
        throw std::invalid_argument(
            "decimal endpoint exponent exceeds supported comparison range");
      }
      exponent = exponent * 10 + (character - '0');
    }
    if (exponent > 1000000) {
      throw std::invalid_argument(
          "decimal endpoint exponent exceeds supported comparison range");
    }
    if (exponent_negative) {
      exponent = -exponent;
    }
  }

  const std::size_t first_significant = digits.find_first_not_of('0');
  if (first_significant == std::string::npos) {
    return {};
  }
  const std::size_t original_digit_count = digits.size();
  digits.erase(0U, first_significant);
  while (!digits.empty() && digits.back() == '0') {
    digits.pop_back();
  }
  const std::int64_t fractional =
      static_cast<std::int64_t>(fractional_digits);
  const std::int64_t digit_count =
      static_cast<std::int64_t>(original_digit_count);
  if (exponent < std::numeric_limits<std::int64_t>::min() +
                     fractional) {
    throw std::invalid_argument(
        "decimal endpoint exponent exceeds supported range");
  }
  const std::int64_t adjusted_exponent = exponent - fractional;
  const std::int64_t digit_offset = digit_count - 1;
  if (adjusted_exponent >
      std::numeric_limits<std::int64_t>::max() - digit_offset) {
    throw std::invalid_argument(
        "decimal endpoint exponent exceeds supported range");
  }
  const std::int64_t leading_exponent =
      adjusted_exponent + digit_offset;
  return {sign, std::move(digits), leading_exponent};
}

int compare_decimal_values(std::string_view lhs,
                           std::string_view rhs) {
  const DecimalMagnitude left = parse_decimal_magnitude(lhs);
  const DecimalMagnitude right = parse_decimal_magnitude(rhs);
  if (left.sign != right.sign) {
    return left.sign < right.sign ? -1 : 1;
  }
  if (left.sign == 0) {
    return 0;
  }
  int magnitude_order = 0;
  if (left.leading_exponent != right.leading_exponent) {
    magnitude_order =
        left.leading_exponent < right.leading_exponent ? -1 : 1;
  } else {
    const std::size_t digit_count =
        std::max(left.digits.size(), right.digits.size());
    for (std::size_t index = 0U; index < digit_count; ++index) {
      const char left_digit =
          index < left.digits.size() ? left.digits[index] : '0';
      const char right_digit =
          index < right.digits.size() ? right.digits[index] : '0';
      if (left_digit != right_digit) {
        magnitude_order = left_digit < right_digit ? -1 : 1;
        break;
      }
    }
  }
  return left.sign > 0 ? magnitude_order : -magnitude_order;
}

}  // namespace

NumericInterval::NumericInterval(mpfr_prec_t precision)
    : precision_(std::max(precision, static_cast<mpfr_prec_t>(MPFR_PREC_MIN))) {
  mpfr_init2(lower_, precision_);
  mpfr_init2(upper_, precision_);
  mpfr_set_zero(lower_, 0);
  mpfr_set_zero(upper_, 0);
}

NumericInterval::NumericInterval(double value, mpfr_prec_t precision)
    : NumericInterval(value, value, precision) {}

NumericInterval::NumericInterval(double lower, double upper,
                                 mpfr_prec_t precision)
    : NumericInterval(precision) {
  require_finite(lower, "lower endpoint");
  require_finite(upper, "upper endpoint");
  if (lower > upper) {
    throw std::invalid_argument("interval lower endpoint exceeds upper");
  }
  mpfr_set_d(lower_, lower, MPFR_RNDD);
  mpfr_set_d(upper_, upper, MPFR_RNDU);
}

NumericInterval::NumericInterval(const NumericInterval& other)
    : NumericInterval(other.precision_) {
  mpfr_set(lower_, other.lower_, MPFR_RNDD);
  mpfr_set(upper_, other.upper_, MPFR_RNDU);
}

NumericInterval& NumericInterval::operator=(NumericInterval other) {
  swap(other);
  return *this;
}

NumericInterval::~NumericInterval() {
  mpfr_clear(lower_);
  mpfr_clear(upper_);
}

void NumericInterval::swap(NumericInterval& other) noexcept {
  mpfr_swap(lower_, other.lower_);
  mpfr_swap(upper_, other.upper_);
  std::swap(precision_, other.precision_);
}

NumericInterval NumericInterval::from_mpfr_bounds(
    mpfr_srcptr lower, mpfr_srcptr upper, mpfr_prec_t precision) {
  if (mpfr_number_p(lower) == 0 || mpfr_number_p(upper) == 0) {
    throw std::invalid_argument(
        "MPFR interval endpoints must be finite numbers");
  }
  if (mpfr_cmp(lower, upper) > 0) {
    throw std::invalid_argument("MPFR interval lower endpoint exceeds upper");
  }
  NumericInterval result(precision);
  mpfr_set(result.lower_, lower, MPFR_RNDD);
  mpfr_set(result.upper_, upper, MPFR_RNDU);
  if (!result.is_finite() ||
      mpfr_cmp(result.lower_, result.upper_) > 0) {
    throw std::overflow_error(
        "validated arithmetic produced a non-finite or invalid interval");
  }
  return result;
}

NumericInterval NumericInterval::from_decimal_bounds(
    const std::string& lower, const std::string& upper,
    mpfr_prec_t precision) {
  if (compare_decimal_values(lower, upper) > 0) {
    throw std::invalid_argument(
        "decimal interval lower endpoint exceeds upper");
  }
  NumericInterval result(precision);
  if (mpfr_set_str(result.lower_, lower.c_str(), 10, MPFR_RNDD) == -1 ||
      mpfr_set_str(result.upper_, upper.c_str(), 10, MPFR_RNDU) == -1 ||
      !result.is_finite() ||
      mpfr_cmp(result.lower_, result.upper_) > 0) {
    throw std::invalid_argument(
        "decimal interval endpoints are invalid or reversed");
  }
  return result;
}

NumericInterval NumericInterval::pi(mpfr_prec_t precision) {
  NumericInterval result(precision);
  mpfr_const_pi(result.lower_, MPFR_RNDD);
  mpfr_const_pi(result.upper_, MPFR_RNDU);
  return result;
}

NumericInterval NumericInterval::sum(
    const std::vector<NumericInterval>& values,
    mpfr_prec_t precision) {
  for (const NumericInterval& value : values) {
    precision = std::max(precision, value.precision());
  }
  NumericInterval result(0.0, precision);
  for (const NumericInterval& value : values) {
    result = result + value;
  }
  return result;
}

bool NumericInterval::is_finite() const noexcept {
  return mpfr_number_p(lower_) != 0 && mpfr_number_p(upper_) != 0;
}

bool NumericInterval::is_point() const noexcept {
  return mpfr_cmp(lower_, upper_) == 0;
}

bool NumericInterval::contains_zero() const noexcept {
  return mpfr_sgn(lower_) <= 0 && mpfr_sgn(upper_) >= 0;
}

bool NumericInterval::contains(
    const NumericInterval& other) const noexcept {
  return mpfr_cmp(lower_, other.lower_) <= 0 &&
         mpfr_cmp(upper_, other.upper_) >= 0;
}

double NumericInterval::lower_as_double() const {
  const double result = mpfr_get_d(lower_, MPFR_RNDD);
  if (!std::isfinite(result)) {
    throw std::overflow_error(
        "lower endpoint is not representable as a finite binary64");
  }
  return result;
}

double NumericInterval::upper_as_double() const {
  const double result = mpfr_get_d(upper_, MPFR_RNDU);
  if (!std::isfinite(result)) {
    throw std::overflow_error(
        "upper endpoint is not representable as a finite binary64");
  }
  return result;
}

std::string NumericInterval::endpoint_decimal(
    mpfr_srcptr endpoint, mpfr_rnd_t rounding) const {
  const std::size_t digits_count =
      static_cast<std::size_t>(precision_ * 30103 / 100000 + 4);
  mpfr_exp_t exponent = 0;
  char* digits = mpfr_get_str(nullptr, &exponent, 10, digits_count,
                              endpoint, rounding);
  if (digits == nullptr) {
    throw std::runtime_error(
        "MPFR failed to format an interval endpoint");
  }
  const std::string significant(digits);
  mpfr_free_str(digits);
  const std::size_t significant_digits =
      !significant.empty() && significant.front() == '-'
          ? significant.size() - 1U
          : significant.size();
  const long long decimal_exponent =
      static_cast<long long>(exponent) -
      static_cast<long long>(significant_digits);
  return significant + "e" + std::to_string(decimal_exponent);
}

std::string NumericInterval::lower_decimal() const {
  return endpoint_decimal(lower_, MPFR_RNDD);
}

std::string NumericInterval::upper_decimal() const {
  return endpoint_decimal(upper_, MPFR_RNDU);
}

NumericInterval operator+(const NumericInterval& lhs,
                          const NumericInterval& rhs) {
  const mpfr_prec_t precision = operation_precision(lhs, rhs);
  NumericInterval result(precision);
  mpfr_add(result.lower_, lhs.lower_, rhs.lower_, MPFR_RNDD);
  mpfr_add(result.upper_, lhs.upper_, rhs.upper_, MPFR_RNDU);
  if (!result.is_finite()) {
    throw std::overflow_error("interval addition overflowed");
  }
  return result;
}

NumericInterval operator-(const NumericInterval& lhs,
                          const NumericInterval& rhs) {
  const mpfr_prec_t precision = operation_precision(lhs, rhs);
  NumericInterval result(precision);
  mpfr_sub(result.lower_, lhs.lower_, rhs.upper_, MPFR_RNDD);
  mpfr_sub(result.upper_, lhs.upper_, rhs.lower_, MPFR_RNDU);
  if (!result.is_finite()) {
    throw std::overflow_error("interval subtraction overflowed");
  }
  return result;
}

NumericInterval operator*(const NumericInterval& lhs,
                          const NumericInterval& rhs) {
  const mpfr_prec_t precision = operation_precision(lhs, rhs);
  NumericInterval result(precision);
  mpfr_t candidate;
  mpfr_init2(candidate, precision);
  const std::array<mpfr_srcptr, 2> left{lhs.lower_, lhs.upper_};
  const std::array<mpfr_srcptr, 2> right{rhs.lower_, rhs.upper_};
  bool initialized = false;
  for (mpfr_srcptr left_endpoint : left) {
    for (mpfr_srcptr right_endpoint : right) {
      mpfr_mul(candidate, left_endpoint, right_endpoint, MPFR_RNDD);
      if (!initialized || mpfr_cmp(candidate, result.lower_) < 0) {
        mpfr_set(result.lower_, candidate, MPFR_RNDD);
      }
      mpfr_mul(candidate, left_endpoint, right_endpoint, MPFR_RNDU);
      if (!initialized || mpfr_cmp(candidate, result.upper_) > 0) {
        mpfr_set(result.upper_, candidate, MPFR_RNDU);
      }
      initialized = true;
    }
  }
  mpfr_clear(candidate);
  if (!result.is_finite()) {
    throw std::overflow_error("interval multiplication overflowed");
  }
  return result;
}

NumericInterval operator/(const NumericInterval& lhs,
                          const NumericInterval& rhs) {
  if (rhs.contains_zero()) {
    throw std::domain_error(
        "interval division by a denominator containing zero");
  }
  const mpfr_prec_t precision = operation_precision(lhs, rhs);
  NumericInterval result(precision);
  mpfr_t candidate;
  mpfr_init2(candidate, precision);
  const std::array<mpfr_srcptr, 2> left{lhs.lower_, lhs.upper_};
  const std::array<mpfr_srcptr, 2> right{rhs.lower_, rhs.upper_};
  bool initialized = false;
  for (mpfr_srcptr left_endpoint : left) {
    for (mpfr_srcptr right_endpoint : right) {
      mpfr_div(candidate, left_endpoint, right_endpoint, MPFR_RNDD);
      if (!initialized || mpfr_cmp(candidate, result.lower_) < 0) {
        mpfr_set(result.lower_, candidate, MPFR_RNDD);
      }
      mpfr_div(candidate, left_endpoint, right_endpoint, MPFR_RNDU);
      if (!initialized || mpfr_cmp(candidate, result.upper_) > 0) {
        mpfr_set(result.upper_, candidate, MPFR_RNDU);
      }
      initialized = true;
    }
  }
  mpfr_clear(candidate);
  if (!result.is_finite()) {
    throw std::overflow_error("interval division overflowed");
  }
  return result;
}

NumericInterval square(const NumericInterval& value) {
  NumericInterval result(value.precision_);
  if (value.contains_zero()) {
    mpfr_set_zero(result.lower_, 0);
    mpfr_mul(result.upper_, value.lower_, value.lower_, MPFR_RNDU);
    mpfr_t candidate;
    mpfr_init2(candidate, value.precision_);
    mpfr_mul(candidate, value.upper_, value.upper_, MPFR_RNDU);
    if (mpfr_cmp(candidate, result.upper_) > 0) {
      mpfr_set(result.upper_, candidate, MPFR_RNDU);
    }
    mpfr_clear(candidate);
  } else if (mpfr_sgn(value.upper_) < 0) {
    mpfr_mul(result.lower_, value.upper_, value.upper_, MPFR_RNDD);
    mpfr_mul(result.upper_, value.lower_, value.lower_, MPFR_RNDU);
  } else {
    mpfr_mul(result.lower_, value.lower_, value.lower_, MPFR_RNDD);
    mpfr_mul(result.upper_, value.upper_, value.upper_, MPFR_RNDU);
  }
  if (!result.is_finite()) {
    throw std::overflow_error("interval square overflowed");
  }
  return result;
}

NumericInterval sqrt(const NumericInterval& value) {
  if (mpfr_sgn(value.lower_) < 0) {
    throw std::domain_error(
        "interval square root requires a nonnegative lower endpoint");
  }
  NumericInterval result(value.precision_);
  mpfr_sqrt(result.lower_, value.lower_, MPFR_RNDD);
  mpfr_sqrt(result.upper_, value.upper_, MPFR_RNDU);
  if (!result.is_finite()) {
    throw std::overflow_error("interval square root overflowed");
  }
  return result;
}

NumericInterval evaluate_quadratic(
    const std::array<NumericInterval, 3>& coefficients,
    const NumericInterval& x) {
  return coefficients[0] * square(x) +
         coefficients[1] * x + coefficients[2];
}

NumericInterval integrate_quadratic(
    const std::array<NumericInterval, 3>& coefficients,
    double start, double end) {
  require_finite(start, "integration start");
  require_finite(end, "integration end");
  if (start > end) {
    throw std::invalid_argument(
        "quadratic integration start exceeds end");
  }
  mpfr_prec_t precision = NumericInterval::DEFAULT_PRECISION;
  for (const NumericInterval& coefficient : coefficients) {
    precision = std::max(precision, coefficient.precision());
  }
  const NumericInterval lower(start, precision);
  const NumericInterval upper(end, precision);
  const NumericInterval lower_squared = square(lower);
  const NumericInterval upper_squared = square(upper);
  const NumericInterval lower_cubed = lower_squared * lower;
  const NumericInterval upper_cubed = upper_squared * upper;
  const NumericInterval two(2.0, precision);
  const NumericInterval three(3.0, precision);
  const NumericInterval quadratic_integral =
      coefficients[0] * ((upper_cubed - lower_cubed) / three);
  const NumericInterval linear_integral =
      coefficients[1] * ((upper_squared - lower_squared) / two);
  const NumericInterval constant_integral =
      coefficients[2] * (upper - lower);
  return quadratic_integral + linear_integral + constant_integral;
}

namespace {
NumericInterval centered_quadratic_range(
    const std::array<NumericInterval, 3>& coefficients,
    const NumericInterval& x) {
  const mpfr_prec_t precision = std::max(
      {coefficients[0].precision(), coefficients[1].precision(),
       coefficients[2].precision(), x.precision()});
  mpfr_t midpoint;
  mpfr_init2(midpoint, precision);
  mpfr_add(midpoint, x.lower_endpoint(), x.upper_endpoint(),
           MPFR_RNDN);
  mpfr_div_2ui(midpoint, midpoint, 1U, MPFR_RNDN);
  const NumericInterval center =
      NumericInterval::from_mpfr_bounds(midpoint, midpoint, precision);
  mpfr_clear(midpoint);

  const NumericInterval two(2.0, precision);
  const NumericInterval derivative =
      coefficients[0] * two * x + coefficients[1];
  // The mean-value form preserves a root cover while reducing dependency.
  return evaluate_quadratic(coefficients, center) +
         derivative * (x - center);
}
}  // namespace

IntervalOrder compare(const NumericInterval& lhs,
                      const NumericInterval& rhs) {
  if (mpfr_cmp(lhs.upper_, rhs.lower_) < 0) {
    return IntervalOrder::LESS;
  }
  if (mpfr_cmp(lhs.lower_, rhs.upper_) > 0) {
    return IntervalOrder::GREATER;
  }
  if (lhs.is_point() && rhs.is_point() &&
      mpfr_cmp(lhs.lower_, rhs.lower_) == 0) {
    return IntervalOrder::EQUAL;
  }
  return IntervalOrder::OVERLAP;
}

RootCoverResult root_cover_quadratic(
    const std::array<NumericInterval, 3>& coefficients,
    double start, double end, double maximum_width,
    std::size_t maximum_boxes, unsigned maximum_depth) {
  require_finite(start, "root-cover start");
  require_finite(end, "root-cover end");
  require_finite(maximum_width, "root-cover maximum width");
  if (start > end || maximum_width <= 0.0 ||
      maximum_boxes == 0U) {
    throw std::invalid_argument("invalid quadratic root-cover limits");
  }
  mpfr_prec_t precision = NumericInterval::DEFAULT_PRECISION;
  for (const NumericInterval& coefficient : coefficients) {
    precision = std::max(precision, coefficient.precision());
  }
  RootCoverResult result;
  const NumericInterval zero(0.0, precision);
  bool identically_zero = true;
  for (const NumericInterval& coefficient : coefficients) {
    identically_zero =
        identically_zero &&
        compare(coefficient, zero) == IntervalOrder::EQUAL;
  }
  if (identically_zero) {
    result.possible_roots.emplace_back(start, end, precision);
    result.status = RootCoverStatus::IDENTICALLY_ZERO_ON_DOMAIN;
    return result;
  }

  struct WorkItem {
    NumericInterval interval;
    unsigned depth;
  };
  std::vector<WorkItem> pending;
  pending.push_back({NumericInterval(start, end, precision), 0U});
  const NumericInterval tolerance(maximum_width, precision);
  std::size_t visited = 0U;

  while (!pending.empty()) {
    if (visited >= maximum_boxes) {
      result.status = RootCoverStatus::BUDGET_EXHAUSTED;
      for (WorkItem& item : pending) {
        result.possible_roots.push_back(std::move(item.interval));
      }
      pending.clear();
      break;
    }
    WorkItem current = std::move(pending.back());
    pending.pop_back();
    ++visited;
    const NumericInterval range =
        centered_quadratic_range(coefficients, current.interval);
    if (!range.contains_zero()) {
      continue;
    }

    mpfr_t width;
    mpfr_init2(width, precision);
    mpfr_sub(width, current.interval.upper_endpoint(),
             current.interval.lower_endpoint(),
             MPFR_RNDU);
    const bool narrow_enough =
        mpfr_cmp(width, tolerance.lower_endpoint()) <= 0;
    mpfr_clear(width);
    if (narrow_enough) {
      result.possible_roots.push_back(
          std::move(current.interval));
      continue;
    }
    if (current.depth >= maximum_depth) {
      result.status = RootCoverStatus::PRECISION_LIMIT;
      result.possible_roots.push_back(
          std::move(current.interval));
      continue;
    }

    mpfr_t midpoint;
    mpfr_init2(midpoint, precision);
    mpfr_add(midpoint, current.interval.lower_endpoint(),
             current.interval.upper_endpoint(), MPFR_RNDN);
    mpfr_div_2ui(midpoint, midpoint, 1U, MPFR_RNDN);
    if (mpfr_cmp(midpoint, current.interval.lower_endpoint()) <= 0 ||
        mpfr_cmp(midpoint, current.interval.upper_endpoint()) >= 0) {
      mpfr_clear(midpoint);
      result.status = RootCoverStatus::PRECISION_LIMIT;
      result.possible_roots.push_back(
          std::move(current.interval));
      continue;
    }
    NumericInterval left = NumericInterval::from_mpfr_bounds(
        current.interval.lower_endpoint(), midpoint, precision);
    NumericInterval right = NumericInterval::from_mpfr_bounds(
        midpoint, current.interval.upper_endpoint(), precision);
    mpfr_clear(midpoint);
    pending.push_back(
        {std::move(right), current.depth + 1U});
    pending.push_back(
        {std::move(left), current.depth + 1U});
  }

  std::sort(result.possible_roots.begin(),
            result.possible_roots.end(),
            [](const NumericInterval& lhs,
               const NumericInterval& rhs) {
              return mpfr_cmp(lhs.lower_endpoint(),
                              rhs.lower_endpoint()) < 0;
            });
  return result;
}

}  // namespace kdc
