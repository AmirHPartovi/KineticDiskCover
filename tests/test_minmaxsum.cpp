#include "test_utils.hpp"

#include "kdc/minmaxsum.hpp"
#include "kdc/objective.hpp"
#include "kdc/stationary.hpp"
#include "kdc/verify.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <vector>

namespace {
kdc::KineticSolution polynomial(double a, double b, double c,
                                double start = 0.0, double end = 1.0) {
  kdc::KineticSolution solution;
  kdc::SolutionInterval interval;
  interval.t_start = start;
  interval.t_end = end;
  interval.supporting_point = {0};
  interval.assigned_points = {0};
  interval.a = a;
  interval.b = b;
  interval.c = c;
  solution.intervals.push_back(interval);
  return solution;
}

class SeededStaticSolver final : public kdc::IStaticSolver {
 public:
  explicit SeededStaticSolver(std::vector<unsigned> feasible_seeds)
      : feasible_seeds_(std::move(feasible_seeds)) {}

  kdc::StaticSolution solve(const kdc::Instance& instance,
                            double time) override {
    if (std::find(feasible_seeds_.begin(), feasible_seeds_.end(), seed_) ==
        feasible_seeds_.end()) {
      kdc::StaticSolution failed;
      failed.optimality_status = kdc::OptimalityStatus::INFEASIBLE;
      failed.exact_solver = true;
      return failed;
    }
    const auto assignment = kdc::StationarySolver::solve_nn(instance, time);
    kdc::StaticSolution result;
    result.supporting_point = assignment.supporting_point;
    result.radius = assignment.radius;
    result.assigned_points = assignment.assigned_points;
    result.cost = assignment.cost;
    result.feasible = assignment.feasible;
    result.lower_bound = assignment.cost;
    result.bound_status = kdc::BoundStatus::CERTIFIED;
    result.upper_bound = assignment.cost;
    result.certified_gap = 0.0;
    result.optimality_status = kdc::OptimalityStatus::OPTIMAL;
    result.exact_solver = true;
    result.solver_name = name();
    return result;
  }

  std::string name() const override { return "seeded-static"; }
  bool is_exact() const override { return true; }
  bool provides_lower_bound() const override { return true; }
  void set_seed(unsigned seed) override { seed_ = seed; }

 private:
  std::vector<unsigned> feasible_seeds_;
  unsigned seed_{0U};
};

kdc::Instance zero_instance() {
  return kdc::test::make_instance_linear(
      {{kdc::Point(0.0, 0.0), kdc::Point(0.0, 0.0)}},
      {{0.0, 0.0}});
}

kdc::MinMaxSumSolver::Config config() {
  kdc::MinMaxSumSolver::Config value;
  value.global_time_limit_sec = 10.0;
  value.minmax_config.time_limit_per_ip = 2.0;
  value.minsum_config.time_limit_per_ip = 2.0;
  return value;
}
}  // namespace

TEST_CASE("MinMaxSum objective name parses and round trips") {
  REQUIRE(kdc::objective_from_string("minmaxsum") ==
          kdc::ObjectiveType::MIN_MAX_SUM);
  REQUIRE(std::string(kdc::to_string(kdc::ObjectiveType::MIN_MAX_SUM)) ==
          "minmaxsum");
  REQUIRE(kdc::objective_from_string("minmax") ==
          kdc::ObjectiveType::MIN_MAX);
  REQUIRE(kdc::objective_from_string("minsum") ==
          kdc::ObjectiveType::MIN_SUM);
}

TEST_CASE("MinMaxSum envelope jointly reports and dominates both components") {
  const auto minmax_candidate = polynomial(0.0, 0.0, 2.0);
  const auto minsum_candidate = polynomial(8.0, -8.0, 2.5);
  REQUIRE(minmax_candidate.peak_cost() < minsum_candidate.peak_cost());
  REQUIRE(minsum_candidate.total_integral() <
          minmax_candidate.total_integral());

  const auto joint = kdc::KineticSolution::combine(
      minmax_candidate, minsum_candidate,
      kdc::ObjectiveType::MIN_MAX_SUM);
  REQUIRE(joint.objective == kdc::ObjectiveType::MIN_MAX_SUM);
  REQUIRE(joint.is_well_formed());
  REQUIRE(joint.cost_at(0.0) ==
          std::min(minmax_candidate.cost_at(0.0),
                   minsum_candidate.cost_at(0.0)));
  REQUIRE(joint.cost_at(1.0) ==
          std::min(minmax_candidate.cost_at(1.0),
                   minsum_candidate.cost_at(1.0)));
  for (const double time : {0.0, 0.067, 0.5, 0.933, 1.0}) {
    REQUIRE(kdc::test::near(
        joint.cost_at(time),
        std::min(minmax_candidate.cost_at(time),
                 minsum_candidate.cost_at(time))));
  }
  REQUIRE(joint.peak_cost() <= minmax_candidate.peak_cost() + 1e-9);
  REQUIRE(joint.peak_cost() <= minsum_candidate.peak_cost() + 1e-9);
  REQUIRE(joint.total_integral() <=
          minmax_candidate.total_integral() + 1e-9);
  REQUIRE(joint.total_integral() <=
          minsum_candidate.total_integral() + 1e-9);

  const auto identical = kdc::KineticSolution::combine(
      minmax_candidate, minmax_candidate,
      kdc::ObjectiveType::MIN_MAX_SUM);
  REQUIRE(kdc::test::solutions_equal(identical, minmax_candidate));
  REQUIRE(identical.objective == kdc::ObjectiveType::MIN_MAX_SUM);
}

TEST_CASE("MinMaxSum exact zero components certify the simultaneous optimum") {
  auto solver = SeededStaticSolver({42U, 43U});
  const auto result =
      kdc::MinMaxSumSolver::solve(zero_instance(), solver, config());

  REQUIRE(result.feasible);
  REQUIRE(result.verified);
  REQUIRE(result.coverage_ok);
  REQUIRE(result.supporting_points_ok);
  REQUIRE(result.assignment_ok);
  REQUIRE(result.cost_consistent);
  REQUIRE(result.peak_consistent);
  REQUIRE(result.integral_consistent);
  REQUIRE(result.dominance_invariants_ok);
  REQUIRE(result.dominates_minmax);
  REQUIRE(result.dominates_minsum);
  REQUIRE(result.peak_cost == 0.0);
  REQUIRE(result.integral_cost == 0.0);
  REQUIRE(result.minmax_component_optimality ==
          kdc::OptimalityStatus::OPTIMAL);
  REQUIRE(result.minsum_component_optimality ==
          kdc::OptimalityStatus::OPTIMAL);
  REQUIRE(result.joint_optimality_status == kdc::OptimalityStatus::OPTIMAL);
  REQUIRE(result.solution.objective == kdc::ObjectiveType::MIN_MAX_SUM);
  REQUIRE_FALSE(result.minmax_source_run.empty());
  REQUIRE_FALSE(result.minsum_source_run.empty());
}

TEST_CASE("MinMaxSum retains and verifies a sole feasible component") {
  SECTION("MinMax available, MinSum unavailable") {
    auto solver = SeededStaticSolver({42U});
    const auto result =
        kdc::MinMaxSumSolver::solve(zero_instance(), solver, config());
    REQUIRE(result.feasible);
    REQUIRE(result.verified);
    REQUIRE(result.minmax_component_status ==
            kdc::MinMaxSumComponentStatus::COMPLETED);
    REQUIRE(result.minsum_component_status ==
            kdc::MinMaxSumComponentStatus::FAILED);
    REQUIRE(result.dominates_minmax);
    REQUIRE_FALSE(result.dominates_minsum);
    REQUIRE(result.joint_optimality_status == kdc::OptimalityStatus::FEASIBLE);
  }

  SECTION("MinSum available, MinMax unavailable") {
    auto solver = SeededStaticSolver({43U});
    const auto result =
        kdc::MinMaxSumSolver::solve(zero_instance(), solver, config());
    REQUIRE(result.feasible);
    REQUIRE(result.verified);
    REQUIRE(result.minmax_component_status ==
            kdc::MinMaxSumComponentStatus::FAILED);
    REQUIRE(result.minsum_component_status ==
            kdc::MinMaxSumComponentStatus::COMPLETED);
    REQUIRE_FALSE(result.dominates_minmax);
    REQUIRE(result.dominates_minsum);
    REQUIRE(result.joint_optimality_status == kdc::OptimalityStatus::FEASIBLE);
  }
}

TEST_CASE("MinMaxSum reports failure or time limit without a component") {
  SECTION("neither component has a feasible result") {
    auto solver = SeededStaticSolver({});
    const auto result =
        kdc::MinMaxSumSolver::solve(zero_instance(), solver, config());
    REQUIRE_FALSE(result.feasible);
    REQUIRE_FALSE(result.verified);
    REQUIRE(result.joint_optimality_status == kdc::OptimalityStatus::FAILED);
  }

  SECTION("an already expired shared budget is not bypassed") {
    auto solver = SeededStaticSolver({42U, 43U});
    kdc::SolverBudget budget(0.0);
    const auto result =
        kdc::MinMaxSumSolver::solve(zero_instance(), solver, config(), budget);
    REQUIRE_FALSE(result.feasible);
    REQUIRE(result.time_limited);
    REQUIRE(result.minmax_component_status ==
            kdc::MinMaxSumComponentStatus::TIME_LIMIT);
    REQUIRE(result.minsum_component_status ==
            kdc::MinMaxSumComponentStatus::TIME_LIMIT);
    REQUIRE(result.joint_optimality_status ==
            kdc::OptimalityStatus::TIME_LIMIT);
  }
}

TEST_CASE("MinMaxSum budget fractions are explicit and exhaustive") {
  auto solver = SeededStaticSolver({42U, 43U});
  auto invalid = config();
  invalid.minmax_budget_fraction = 0.4;
  invalid.minsum_budget_fraction = 0.4;
  REQUIRE_THROWS_AS(
      kdc::MinMaxSumSolver::solve(zero_instance(), solver, invalid),
      std::invalid_argument);
}
