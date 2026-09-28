#pragma once

#include <string>
#include <vector>

namespace kdc {
struct PairedTestResult {
  int sample_size{0};
  double median_a{0.0};
  double median_b{0.0};
  double wilcoxon_statistic{0.0};
  double p_value{1.0};
  double cliffs_delta{0.0};
  std::string interpretation;
};

class Stats {
 public:
  static PairedTestResult paired_wilcoxon(const std::vector<double>& a,
                                          const std::vector<double>& b);
  static double cliffs_delta(const std::vector<double>& a,
                             const std::vector<double>& b);
  static PairedTestResult mann_whitney(const std::vector<double>& a,
                                       const std::vector<double>& b);
};
}
