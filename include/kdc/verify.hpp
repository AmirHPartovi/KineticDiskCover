#pragma once

#include "kdc/solution.hpp"
#include "kdc/result_status.hpp"
#include "kdc/solver_budget.hpp"
#include "kdc/types.hpp"

#include <string>
#include <vector>

namespace kdc {
struct VerificationReport {
  VerificationKind kind{VerificationKind::NONE};
  bool coverage_ok{false};
  bool supporting_points_ok{false};
  bool cost_consistent_ok{false};
  bool integral_consistent_ok{false};
  bool assignment_consistent_ok{false};
  double max_coverage_violation{0.0};
  std::vector<std::string> errors;
  bool empirical_verification{false};
  bool certified_continuous_verification{false};
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
                                   double tolerance = 1e-6,
                                   SolverBudget* budget = nullptr);
  static VerificationReport verify_empirical(
      const Instance& instance, const KineticSolution& solution,
      int samples_per_interval = 100, double tolerance = 1e-6,
      SolverBudget* budget = nullptr);
  static VerificationReport verify_continuous(
      const Instance& instance, const KineticSolution& solution,
      double tolerance = 1e-6, SolverBudget* budget = nullptr);
  static bool check_coverage(const Instance& instance,
                             const KineticSolution& solution, double time,
                             double tolerance,
                             double* out_max_violation = nullptr,
                             SolverBudget* budget = nullptr);
  static bool check_supporting_points(const Instance& instance,
                                      const KineticSolution& solution,
                                      double time, double tolerance,
                                      SolverBudget* budget = nullptr);
  static bool check_cost(const Instance& instance,
                         const KineticSolution& solution, double time,
                         double tolerance,
                         SolverBudget* budget = nullptr);
  static bool check_integral(const Instance& instance,
                             const KineticSolution& solution,
                             int num_samples, double tolerance,
                             SolverBudget* budget = nullptr);
};

bool verify_solution(const KineticSolution& solution);
}
