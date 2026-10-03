#pragma once

#include "kdc/objective.hpp"
#include "kdc/solver_budget.hpp"
#include "kdc/types.hpp"

#include <vector>

namespace kdc {
struct StaticAssignment;

struct SolutionInterval {
  double t_start{0.0};
  double t_end{0.0};
  std::vector<int> supporting_point;
  std::vector<int> assigned_points;
  double a{0.0};
  double b{0.0};
  double c{0.0};
};

class KineticSolution {
 public:
  std::vector<SolutionInterval> intervals;
  ObjectiveType objective{ObjectiveType::MIN_MAX};

  static void compute_quadratic_coeffs(const Instance& instance,
                                       const std::vector<int>& supporting_points,
                                       SolutionInterval& output,
                                       SolverBudget* budget = nullptr);

  static KineticSolution extend(const Instance& instance,
                                const StaticAssignment& init_assignment,
                                double t_start, double t_end, bool forward,
                                bool use_handovers, ObjectiveType objective,
                                SolverBudget* budget = nullptr);
  static KineticSolution combine(const KineticSolution& s1,
                                 const KineticSolution& s2,
                                 ObjectiveType objective,
                                 SolverBudget* budget = nullptr);
  void remove_duplicates();
  static KineticSolution partial_extend(const KineticSolution& new_solution,
                                        const KineticSolution& current,
                                        ObjectiveType objective,
                                        SolverBudget* budget = nullptr);

  double cost_at(double time) const;
  double integral_on(double t_start, double t_end) const;
  double total_integral() const;
  double peak_cost() const;
  double peak_time() const;
  bool is_well_formed() const;
  void dump() const;
};
}
