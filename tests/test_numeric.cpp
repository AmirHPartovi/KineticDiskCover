#include "kdc/numeric.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {
kdc::NumericInterval point(double value) {
  return kdc::NumericInterval(value);
}
}

TEST_CASE("Validated intervals enclose arithmetic with directed rounding") {
  const kdc::NumericInterval a(-2.0, 3.0);
  const kdc::NumericInterval b(-4.0, 5.0);

  const auto product = a * b;
  REQUIRE(product.lower_as_double() <= -12.0);
  REQUIRE(product.upper_as_double() >= 15.0);

  const auto quotient =
      kdc::NumericInterval(1.0, 2.0) /
      kdc::NumericInterval(2.0, 4.0);
  REQUIRE(quotient.lower_as_double() <= 0.25);
  REQUIRE(quotient.upper_as_double() >= 1.0);
  REQUIRE((point(1.0) + point(2.0)).contains(point(3.0)));
  REQUIRE((point(3.0) - point(2.0)).contains(point(1.0)));

  REQUIRE_THROWS_AS(
      point(1.0) / kdc::NumericInterval(-1.0, 1.0),
      std::domain_error);
}

TEST_CASE("Validated square root and polynomial evaluation enclose results") {
  const auto squared = kdc::square(kdc::NumericInterval(-2.0, 3.0));
  REQUIRE(squared.lower_as_double() <= 0.0);
  REQUIRE(squared.upper_as_double() >= 9.0);

  const auto root_two = kdc::sqrt(point(2.0));
  REQUIRE(root_two.lower_as_double() * root_two.lower_as_double() <= 2.0);
  REQUIRE(root_two.upper_as_double() * root_two.upper_as_double() >= 2.0);
  REQUIRE_THROWS_AS(kdc::sqrt(point(-1.0)), std::domain_error);

  const std::array<kdc::NumericInterval, 3> coefficients{
      point(1.0), point(-2.0), point(1.0)};
  const auto tangent_value =
      kdc::evaluate_quadratic(coefficients, point(1.0));
  REQUIRE(tangent_value.contains_zero());
}

TEST_CASE("Interval comparisons distinguish order from unresolved overlap") {
  REQUIRE(kdc::compare(point(1.0), point(2.0)) ==
          kdc::IntervalOrder::LESS);
  REQUIRE(kdc::compare(point(2.0), point(1.0)) ==
          kdc::IntervalOrder::GREATER);
  REQUIRE(kdc::compare(point(1.0), point(1.0)) ==
          kdc::IntervalOrder::EQUAL);
  REQUIRE(kdc::compare(kdc::NumericInterval(0.0, 2.0),
                       kdc::NumericInterval(1.0, 3.0)) ==
          kdc::IntervalOrder::OVERLAP);
  REQUIRE(kdc::compare(kdc::NumericInterval(0.0, 2.0),
                       kdc::NumericInterval(0.0, 2.0)) ==
          kdc::IntervalOrder::OVERLAP);
}

TEST_CASE("Quadratic root cover contains simple and tangent roots") {
  const std::array<kdc::NumericInterval, 3> two_roots{
      point(1.0), point(0.0), point(-2.0)};
  const auto roots = kdc::root_cover_quadratic(
      two_roots, -2.0, 2.0, 1e-8);
  REQUIRE(roots.status == kdc::RootCoverStatus::CANDIDATE_COVER_ONLY);
  REQUIRE(roots.status == kdc::RootCoverStatus::COMPLETE_COVER);
  const auto root_two = kdc::sqrt(point(2.0));
  const auto negative_root_two =
      kdc::NumericInterval(-root_two.upper_as_double(),
                           -root_two.lower_as_double());
  REQUIRE(std::any_of(
      roots.possible_roots.begin(), roots.possible_roots.end(),
      [&root_two](const kdc::NumericInterval& candidate) {
        return candidate.contains(root_two);
      }));
  REQUIRE(std::any_of(
      roots.possible_roots.begin(), roots.possible_roots.end(),
      [&negative_root_two](const kdc::NumericInterval& candidate) {
        return candidate.contains(negative_root_two);
      }));

  const std::array<kdc::NumericInterval, 3> tangent{
      point(1.0), point(-2.0), point(1.0)};
  const auto tangent_roots = kdc::root_cover_quadratic(
      tangent, 0.0, 2.0, 1e-8);
  REQUIRE(tangent_roots.status ==
          kdc::RootCoverStatus::CANDIDATE_COVER_ONLY);
  REQUIRE(std::any_of(
      tangent_roots.possible_roots.begin(),
      tangent_roots.possible_roots.end(),
      [](const kdc::NumericInterval& candidate) {
        return candidate.contains(point(1.0));
      }));

  const std::array<kdc::NumericInterval, 3> endpoint_root{
      point(0.0), point(1.0), point(0.0)};
  const auto endpoint_roots = kdc::root_cover_quadratic(
      endpoint_root, 0.0, 1.0, 1e-8);
  REQUIRE(endpoint_roots.status ==
          kdc::RootCoverStatus::CANDIDATE_COVER_ONLY);
  REQUIRE(std::any_of(
      endpoint_roots.possible_roots.begin(),
      endpoint_roots.possible_roots.end(),
      [](const kdc::NumericInterval& candidate) {
        return candidate.contains(point(0.0));
      }));
}

TEST_CASE("Quadratic integration and global sum preserve enclosure") {
  const std::array<kdc::NumericInterval, 3> t_squared{
      point(1.0), point(0.0), point(0.0)};
  const auto integral =
      kdc::integrate_quadratic(t_squared, 0.0, 1.0);
  const auto one_third = point(1.0) / point(3.0);
  REQUIRE(integral.contains(one_third));

  const std::array<kdc::NumericInterval, 3> signed_polynomial{
      point(-2.0), point(3.0), point(4.0)};
  const auto signed_integral =
      kdc::integrate_quadratic(signed_polynomial, -1.0, 2.0);
  REQUIRE(signed_integral.contains(point(10.5)));

  const std::vector<kdc::NumericInterval> contributions{
      one_third, point(2.0)};
  const auto global_sum = kdc::NumericInterval::sum(contributions);
  REQUIRE(global_sum.contains(one_third + point(2.0)));

  const auto pi = kdc::NumericInterval::pi();
  REQUIRE(kdc::compare(pi, point(3.0)) ==
          kdc::IntervalOrder::GREATER);
}

TEST_CASE("Binary64 input semantics and endpoint storage preserve enclosure") {
  const auto parsed_binary64 = point(0.1);
  const auto exact_decimal =
      kdc::NumericInterval::from_decimal_bounds("0.1", "0.1");
  REQUIRE(kdc::compare(exact_decimal, parsed_binary64) ==
          kdc::IntervalOrder::LESS);
  REQUIRE_THROWS_AS(
      kdc::NumericInterval::from_decimal_bounds("2", "1"),
      std::invalid_argument);

  const auto restored = kdc::NumericInterval::from_decimal_bounds(
      parsed_binary64.lower_decimal(),
      parsed_binary64.upper_decimal());
  REQUIRE(restored.contains(parsed_binary64));
  REQUIRE(parsed_binary64.lower_as_double() == 0.1);
  REQUIRE(parsed_binary64.upper_as_double() == 0.1);
}

TEST_CASE("NumericInterval handles signed zero and preserves MPFR precision") {
  const kdc::NumericInterval signed_zero(
      -0.0, 0.0, static_cast<mpfr_prec_t>(192));
  REQUIRE(signed_zero.is_point());
  REQUIRE(signed_zero.contains_zero());
  REQUIRE(signed_zero.precision() == 192);

  const kdc::NumericInterval copied(signed_zero);
  REQUIRE(copied.precision() == 192);
  REQUIRE(copied.contains(signed_zero));

  kdc::NumericInterval assigned(1.0, static_cast<mpfr_prec_t>(64));
  assigned = copied;
  REQUIRE(assigned.precision() == 192);
  REQUIRE(assigned.contains(signed_zero));

  const kdc::NumericInterval moved(std::move(assigned));
  REQUIRE(moved.precision() == 192);
  REQUIRE(moved.contains(signed_zero));
  REQUIRE(assigned.is_finite());
}

TEST_CASE("NumericInterval encloses underflow and rejects binary64 overflow") {
  const double smallest_positive =
      std::numeric_limits<double>::denorm_min();
  const auto tiny_squared = kdc::square(point(smallest_positive));
  REQUIRE(tiny_squared.is_finite());
  REQUIRE(tiny_squared.lower_as_double() == 0.0);
  REQUIRE(tiny_squared.upper_as_double() == smallest_positive);

  const auto huge_squared = kdc::square(point(1e308));
  REQUIRE(huge_squared.is_finite());
  REQUIRE_THROWS_AS(huge_squared.lower_as_double(),
                    std::overflow_error);
  REQUIRE_THROWS_AS(huge_squared.upper_as_double(),
                    std::overflow_error);

  const auto restored = kdc::NumericInterval::from_decimal_bounds(
      huge_squared.lower_decimal(), huge_squared.upper_decimal(),
      huge_squared.precision());
  REQUIRE(restored.contains(huge_squared));
}

TEST_CASE("NumericInterval rejects invalid and non-finite endpoints") {
  const double infinity = std::numeric_limits<double>::infinity();
  const double nan = std::numeric_limits<double>::quiet_NaN();
  REQUIRE_THROWS_AS(point(infinity), std::invalid_argument);
  REQUIRE_THROWS_AS(point(nan), std::invalid_argument);
  REQUIRE_THROWS_AS(kdc::NumericInterval(2.0, 1.0),
                    std::invalid_argument);
  REQUIRE_THROWS_AS(
      kdc::NumericInterval::from_decimal_bounds("inf", "inf"),
      std::invalid_argument);

  mpfr_t invalid_endpoint;
  mpfr_init2(invalid_endpoint, 128);
  mpfr_set_inf(invalid_endpoint, 1);
  REQUIRE_THROWS_AS(
      kdc::NumericInterval::from_mpfr_bounds(
          invalid_endpoint, invalid_endpoint),
      std::invalid_argument);
  mpfr_set_nan(invalid_endpoint);
  REQUIRE_THROWS_AS(
      kdc::NumericInterval::from_mpfr_bounds(
          invalid_endpoint, invalid_endpoint),
      std::invalid_argument);
  mpfr_clear(invalid_endpoint);

  REQUIRE_THROWS_AS(
      point(0.0) / point(-0.0), std::domain_error);
  REQUIRE_THROWS_AS(
      kdc::sqrt(kdc::NumericInterval(-1.0, 0.0)),
      std::domain_error);
}

TEST_CASE("Root-cover budget exhaustion remains explicit and conservative") {
  const std::array<kdc::NumericInterval, 3> polynomial{
      point(1.0), point(0.0), point(-2.0)};
  const auto roots = kdc::root_cover_quadratic(
      polynomial, -2.0, 2.0, 1e-12, 1U, 128U);
  REQUIRE(roots.status == kdc::RootCoverStatus::BUDGET_EXHAUSTED);
  REQUIRE_FALSE(roots.possible_roots.empty());
  REQUIRE(roots.possible_roots.front().lower_as_double() <= -2.0);
  REQUIRE(roots.possible_roots.back().upper_as_double() >= 2.0);
}
