#include "kdc/stats.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

namespace kdc {
namespace {
double median(std::vector<double> values) {
  if (values.empty()) {
    return 0.0;
  }
  const std::size_t middle = values.size() / 2U;
  std::nth_element(values.begin(), values.begin() +
                                       static_cast<std::ptrdiff_t>(middle),
                   values.end());
  const double upper = values[middle];
  if (values.size() % 2U != 0U) {
    return upper;
  }
  const double lower =
      *std::max_element(values.begin(),
                        values.begin() + static_cast<std::ptrdiff_t>(middle));
  return (lower + upper) / 2.0;
}

double normal_two_sided_p_value(double z) {
  return std::erfc(std::abs(z) / std::sqrt(2.0));
}

std::string delta_interpretation(double delta) {
  const double magnitude = std::abs(delta);
  if (magnitude < 0.147) {
    return "negligible";
  }
  if (magnitude < 0.33) {
    return "small";
  }
  if (magnitude < 0.474) {
    return "medium";
  }
  return "large";
}

std::vector<double> average_ranks(const std::vector<double>& values) {
  std::vector<std::size_t> order(values.size());
  for (std::size_t index = 0; index < values.size(); ++index) {
    order[index] = index;
  }
  std::sort(order.begin(), order.end(),
            [&values](std::size_t lhs, std::size_t rhs) {
              return values[lhs] < values[rhs];
            });
  std::vector<double> ranks(values.size(), 0.0);
  std::size_t begin = 0U;
  while (begin < order.size()) {
    std::size_t end = begin + 1U;
    while (end < order.size() &&
           values[order[end]] == values[order[begin]]) {
      ++end;
    }
    const double rank =
        (static_cast<double>(begin + 1U) + static_cast<double>(end)) / 2.0;
    for (std::size_t position = begin; position < end; ++position) {
      ranks[order[position]] = rank;
    }
    begin = end;
  }
  return ranks;
}

void validate_finite(const std::vector<double>& values) {
  for (const double value : values) {
    if (!std::isfinite(value)) {
      throw std::invalid_argument("statistical samples must be finite");
    }
  }
}
}

PairedTestResult Stats::paired_wilcoxon(const std::vector<double>& a,
                                        const std::vector<double>& b) {
  if (a.size() != b.size()) {
    throw std::invalid_argument("paired Wilcoxon samples must have equal size");
  }
  validate_finite(a);
  validate_finite(b);
  PairedTestResult result;
  result.median_a = median(a);
  result.median_b = median(b);
  result.cliffs_delta = cliffs_delta(a, b);

  std::vector<double> absolute_differences;
  std::vector<int> signs;
  absolute_differences.reserve(a.size());
  signs.reserve(a.size());
  for (std::size_t index = 0; index < a.size(); ++index) {
    const double difference = a[index] - b[index];
    if (difference != 0.0) {
      absolute_differences.push_back(std::abs(difference));
      signs.push_back(difference > 0.0 ? 1 : -1);
    }
  }
  const int count = static_cast<int>(absolute_differences.size());
  result.sample_size = count;
  if (count == 0) {
    result.p_value = 1.0;
    result.interpretation = delta_interpretation(result.cliffs_delta);
    return result;
  }

  const std::vector<double> ranks = average_ranks(absolute_differences);
  double positive_sum = 0.0;
  double negative_sum = 0.0;
  for (std::size_t index = 0; index < ranks.size(); ++index) {
    if (signs[index] > 0) {
      positive_sum += ranks[index];
    } else {
      negative_sum += ranks[index];
    }
  }
  result.wilcoxon_statistic = std::min(positive_sum, negative_sum);
  if (count <= 20) {
    const std::uint64_t assignments = std::uint64_t{1} << count;
    const double total_rank = positive_sum + negative_sum;
    std::uint64_t at_least_as_extreme = 0U;
    for (std::uint64_t mask = 0U; mask < assignments; ++mask) {
      double positive = 0.0;
      for (int index = 0; index < count; ++index) {
        if ((mask & (std::uint64_t{1} << index)) != 0U) {
          positive += ranks[static_cast<std::size_t>(index)];
        }
      }
      const double statistic = std::min(positive, total_rank - positive);
      if (statistic <= result.wilcoxon_statistic + 1e-12) {
        ++at_least_as_extreme;
      }
    }
    result.p_value = static_cast<double>(at_least_as_extreme) /
                     static_cast<double>(assignments);
  } else {
    const double count_value = static_cast<double>(count);
    const double mean = count_value * (count_value + 1.0) / 4.0;
    const double variance =
        count_value * (count_value + 1.0) * (2.0 * count_value + 1.0) /
        24.0;
    const double distance = std::abs(positive_sum - mean);
    const double corrected_distance = std::max(0.0, distance - 0.5);
    result.p_value =
        normal_two_sided_p_value(corrected_distance / std::sqrt(variance));
  }
  result.p_value = std::clamp(result.p_value, 0.0, 1.0);
  result.interpretation = delta_interpretation(result.cliffs_delta);
  return result;
}

double Stats::cliffs_delta(const std::vector<double>& a,
                           const std::vector<double>& b) {
  validate_finite(a);
  validate_finite(b);
  if (a.empty() || b.empty()) {
    return 0.0;
  }
  long double greater = 0.0L;
  long double less = 0.0L;
  for (const double lhs : a) {
    for (const double rhs : b) {
      if (lhs > rhs) {
        greater += 1.0L;
      } else if (lhs < rhs) {
        less += 1.0L;
      }
    }
  }
  const long double pairs =
      static_cast<long double>(a.size()) * static_cast<long double>(b.size());
  return static_cast<double>((greater - less) / pairs);
}

PairedTestResult Stats::mann_whitney(const std::vector<double>& a,
                                     const std::vector<double>& b) {
  validate_finite(a);
  validate_finite(b);
  PairedTestResult result;
  result.sample_size = static_cast<int>(a.size() + b.size());
  result.median_a = median(a);
  result.median_b = median(b);
  result.cliffs_delta = cliffs_delta(a, b);
  if (a.empty() || b.empty()) {
    result.interpretation = delta_interpretation(result.cliffs_delta);
    return result;
  }

  std::vector<double> combined;
  combined.reserve(a.size() + b.size());
  combined.insert(combined.end(), a.begin(), a.end());
  combined.insert(combined.end(), b.begin(), b.end());
  const std::vector<double> ranks = average_ranks(combined);
  double rank_sum_a = 0.0;
  for (std::size_t index = 0; index < a.size(); ++index) {
    rank_sum_a += ranks[index];
  }
  const double n_a = static_cast<double>(a.size());
  const double n_b = static_cast<double>(b.size());
  const double u_a = rank_sum_a - n_a * (n_a + 1.0) / 2.0;
  const double u_b = n_a * n_b - u_a;
  result.wilcoxon_statistic = std::min(u_a, u_b);
  const double mean = n_a * n_b / 2.0;
  const double variance = n_a * n_b * (n_a + n_b + 1.0) / 12.0;
  if (variance > 0.0) {
    const double distance =
        std::max(0.0, std::abs(u_a - mean) - 0.5);
    result.p_value =
        normal_two_sided_p_value(distance / std::sqrt(variance));
  }
  result.interpretation = delta_interpretation(result.cliffs_delta);
  return result;
}
}
