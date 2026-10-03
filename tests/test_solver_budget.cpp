#include "test_utils.hpp"

#include "kdc/minmax.hpp"
#include "kdc/minsum.hpp"
#include "kdc/stationary.hpp"
#include "kdc/verify.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cmath>

namespace {
class BudgetAwareStaticSolver final : public kdc::IStaticSolver {
 public:
  explicit BudgetAwareStaticSolver(bool cancel_on_second_solve = false)
      : cancel_on_second_solve_(cancel_on_second_solve) {}

  kdc::StaticSolution solve(const kdc::Instance& instance,
                            double time) override {
    check_budget();
    ++calls;
    if (cancel_on_second_solve_ && calls == 2 && active_budget() != nullptr) {
      active_budget()->cancel();
    }
    const auto assignment =
        kdc::StationarySolver::solve_nn(instance, time, active_budget());
    kdc::StaticSolution solution;
    solution.supporting_point = assignment.supporting_point;
    solution.radius = assignment.radius;
    solution.cost = assignment.cost;
    solution.upper_bound = assignment.cost;
    solution.feasible = assignment.feasible;
    solution.solver_name = name();
    return solution;
  }

  std::string name() const override { return "budget-test"; }
  bool is_exact() const override { return false; }
  bool provides_lower_bound() const override { return false; }

  void set_time_limit(double seconds) override {
    received_time_limit = seconds;
  }

  int calls{0};
  double received_time_limit{0.0};

 private:
  bool cancel_on_second_solve_{false};
};

kdc::Instance moving_instance() {
  return kdc::test::make_instance_linear(
      {{kdc::Point(0.0, 0.0), kdc::Point(0.0, 0.0)},
       {kdc::Point(1.0, 0.0), kdc::Point(10.0, 0.0)}},
      {{0.0, 0.0}});
}
}  // namespace

TEST_CASE("SolverBudget expiration is deterministic and monotone") {
  kdc::SolverBudget expired(kdc::SolverBudget::TimePoint::min());
  REQUIRE(expired.expired());
  REQUIRE(expired.remaining_seconds() == 0.0);
  REQUIRE(expired.limit_seconds(10.0) == 0.0);
  REQUIRE_THROWS_AS(expired.checkpoint(), kdc::SolverBudgetExpired);

  kdc::SolverBudget cancelled(10.0);
  REQUIRE_FALSE(cancelled.expired());
  cancelled.cancel();
  REQUIRE(cancelled.expired());
  REQUIRE(cancelled.remaining_seconds() == 0.0);
  REQUIRE_THROWS_AS(cancelled.checkpoint(), kdc::SolverBudgetExpired);
}

TEST_CASE("A child static solve receives no more than remaining run time") {
  const auto instance = moving_instance();
  BudgetAwareStaticSolver solver;
  kdc::SolverBudget budget(10.0);
  const double remaining_before_child = budget.remaining_seconds();

  const auto solution =
      solver.solve_with_budget(instance, 0.0, budget, 0.025);

  REQUIRE(solution.feasible);
  REQUIRE(solver.received_time_limit > 0.0);
  REQUIRE(solver.received_time_limit <= 0.025);
  REQUIRE(solver.received_time_limit <= remaining_before_child);

  BudgetAwareStaticSolver global_limited_solver;
  kdc::SolverBudget short_budget(0.025);
  const double short_remaining = short_budget.remaining_seconds();
  const auto short_solution = global_limited_solver.solve_with_budget(
      instance, 0.0, short_budget, 10.0);
  REQUIRE(short_solution.feasible);
  REQUIRE(global_limited_solver.received_time_limit > 0.0);
  REQUIRE(global_limited_solver.received_time_limit <= short_remaining);
}

TEST_CASE("MinMax timeout returns its last feasible incumbent") {
  const auto instance = moving_instance();
  BudgetAwareStaticSolver solver(true);
  kdc::MinMaxSolver::Config config;
  config.global_time_limit_sec = 10.0;
  config.verify_after = true;

  const auto result = kdc::MinMaxSolver::solve(instance, solver, config);

  REQUIRE(result.time_limited);
  REQUIRE(result.solution.is_well_formed());
  REQUIRE(solver.calls == 2);
  REQUIRE(kdc::Verifier::verify(instance, result.solution).all_ok());
}

TEST_CASE("MinSum timeout returns its initial feasible incumbent") {
  const auto instance = moving_instance();
  BudgetAwareStaticSolver solver(true);
  kdc::MinSumSolver::Config config;
  config.global_time_limit_sec = 10.0;
  config.verify_after = true;

  const auto result = kdc::MinSumSolver::solve(instance, solver, config);

  REQUIRE(result.time_limited);
  REQUIRE(result.solution.is_well_formed());
  REQUIRE(solver.calls == 2);
  REQUIRE(kdc::Verifier::verify(instance, result.solution).all_ok());
}
