#pragma once

#include <mpfr.h>

#include <array>
#include <cstddef>
#include <string>
#include <vector>

namespace kdc {

enum class IntervalOrder {
  LESS,
  GREATER,
  EQUAL,
  OVERLAP,
};

struct RootCoverResult;

class NumericInterval {
 public:
  static constexpr mpfr_prec_t DEFAULT_PRECISION = 128;

  explicit NumericInterval(double value,
                           mpfr_prec_t precision = DEFAULT_PRECISION);
  NumericInterval(double lower, double upper,
                  mpfr_prec_t precision = DEFAULT_PRECISION);
  NumericInterval(const NumericInterval& other);
  NumericInterval& operator=(NumericInterval other);
  ~NumericInterval();

  static NumericInterval from_decimal_bounds(
      const std::string& lower, const std::string& upper,
      mpfr_prec_t precision = DEFAULT_PRECISION);
  static NumericInterval from_mpfr_bounds(
      mpfr_srcptr lower, mpfr_srcptr upper,
      mpfr_prec_t precision = DEFAULT_PRECISION);
  static NumericInterval pi(
      mpfr_prec_t precision = DEFAULT_PRECISION);
  static NumericInterval sum(
      const std::vector<NumericInterval>& values,
      mpfr_prec_t precision = DEFAULT_PRECISION);

  mpfr_prec_t precision() const noexcept { return precision_; }
  bool is_finite() const noexcept;
  bool is_point() const noexcept;
  bool contains_zero() const noexcept;
  bool contains(const NumericInterval& other) const noexcept;
  double lower_as_double() const;
  double upper_as_double() const;
  std::string lower_decimal() const;
  std::string upper_decimal() const;
  mpfr_srcptr lower_endpoint() const noexcept { return lower_; }
  mpfr_srcptr upper_endpoint() const noexcept { return upper_; }

  friend NumericInterval operator+(const NumericInterval& lhs,
                                   const NumericInterval& rhs);
  friend NumericInterval operator-(const NumericInterval& lhs,
                                   const NumericInterval& rhs);
  friend NumericInterval operator*(const NumericInterval& lhs,
                                   const NumericInterval& rhs);
  friend NumericInterval operator/(const NumericInterval& lhs,
                                   const NumericInterval& rhs);

  friend NumericInterval square(const NumericInterval& value);
  friend NumericInterval sqrt(const NumericInterval& value);
  friend IntervalOrder compare(const NumericInterval& lhs,
                               const NumericInterval& rhs);

 private:
  explicit NumericInterval(mpfr_prec_t precision);
  void swap(NumericInterval& other) noexcept;
  std::string endpoint_decimal(mpfr_srcptr endpoint,
                               mpfr_rnd_t rounding) const;

  mpfr_t lower_;
  mpfr_t upper_;
  mpfr_prec_t precision_;
};

enum class RootCoverStatus {
  // Candidate boxes meet the requested width; this does not isolate roots.
  CANDIDATE_COVER_ONLY,
  // Legacy spelling retained as an alias; this status is never certification.
  COMPLETE_COVER = CANDIDATE_COVER_ONLY,
  BUDGET_EXHAUSTED,
  PRECISION_LIMIT,
};

struct RootCoverResult {
  RootCoverStatus status{RootCoverStatus::CANDIDATE_COVER_ONLY};
  // Boxes conservatively cover possible roots, but may include non-roots or
  // multiple roots; this API proves neither uniqueness nor event ordering.
  std::vector<NumericInterval> possible_roots;
};

NumericInterval square(const NumericInterval& value);
NumericInterval sqrt(const NumericInterval& value);
// Coefficients are ordered as a, b, c for a*x^2 + b*x + c.
NumericInterval evaluate_quadratic(
    const std::array<NumericInterval, 3>& coefficients,
    const NumericInterval& x);
NumericInterval integrate_quadratic(
    const std::array<NumericInterval, 3>& coefficients,
    double start, double end);
IntervalOrder compare(const NumericInterval& lhs,
                      const NumericInterval& rhs);

RootCoverResult root_cover_quadratic(
    const std::array<NumericInterval, 3>& coefficients,
    double start, double end, double maximum_width,
    std::size_t maximum_boxes = 100000U,
    unsigned maximum_depth = 128U);

}  // namespace kdc
