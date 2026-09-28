#pragma once

#include "kdc/solution.hpp"
#include "kdc/types.hpp"

#include <string>
#include <vector>

namespace kdc {
struct VerificationReport {
  bool coverage_ok{false};
  bool supporting_points_ok{false};
  bool cost_consistent_ok{false};
  bool integral_consistent_ok{false};
  bool assignment_consistent_ok{false};
  double max_coverage_violation{0.0};
  std::vector<std::string> errors;
  bool all_ok() const noexcept {
    return coverage_ok && supporting_points_ok && cost_consistent_ok &&
           integral_consistent_ok && assignment_consistent_ok;
  }
};

class Verifier {
 public:
  static VerificationReport verify(const Instance& instance,
                                   const KineticSolution& solution,
                                   int samples_per_interval = 100,
                                   double tolerance = 1e-6);
  static bool check_coverage(const Instance& instance,
                             const KineticSolution& solution, double time,
                             double tolerance,
                             double* out_max_violation = nullptr);
  static bool check_supporting_points(const Instance& instance,
                                      const KineticSolution& solution,
                                      double time, double tolerance);
  static bool check_cost(const Instance& instance,
                         const KineticSolution& solution, double time,
                         double tolerance);
  static bool check_integral(const Instance& instance,
                             const KineticSolution& solution,
                             int num_samples, double tolerance);
};

bool verify_solution(const KineticSolution& solution);
}
