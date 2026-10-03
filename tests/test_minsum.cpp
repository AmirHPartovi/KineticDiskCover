#include "test_utils.hpp"

#include "kdc/kont_solver.hpp"
#include "kdc/minmax.hpp"
#include "kdc/minsum.hpp"
#include "kdc/algorithms/nn_static_solver.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

namespace {
kdc::Instance make_minsum_instance() {
  return kdc::test::make_instance_linear(
      {{kdc::Point(0.0, 0.0), kdc::Point(0.0, 0.0)},
       {kdc::Point(1.0, 0.0), kdc::Point(1.0, 0.0)},
       {kdc::Point(2.0, 0.0), kdc::Point(2.0, 0.0)},
       {kdc::Point(4.0, 0.0), kdc::Point(4.0, 0.0)},
       {kdc::Point(5.0, 0.0), kdc::Point(5.0, 0.0)}},
      {{0.0, 0.0}, {3.0, 0.0}, {6.0, 0.0}});
}

kdc::MinSumSolver::Result solve_minsum(const kdc::Instance& instance) {
  kdc::KontSolver solver;
  kdc::MinSumSolver::Config config;
  config.lb_num_samples = 4;
  config.time_limit_per_ip = 10.0;
  config.gap_target = 0.01;
  return kdc::MinSumSolver::solve(instance, solver, config);
}

class TrackingNNSolver final : public kdc::IStaticSolver {
 public:
  kdc::StaticSolution solve(const kdc::Instance& instance,
                            double time) override {
    solve_times.push_back(time);
    return nn_.solve(instance, time);
  }
  std::string name() const override { return "tracking-nn"; }
  bool is_exact() const override { return false; }
  bool provides_lower_bound() const override { return false; }
  void set_time_limit(double time_limit_sec) override {
    nn_.set_time_limit(time_limit_sec);
  }

  std::vector<double> solve_times;

 private:
  kdc::NNStaticSolver nn_;
};
}

TEST_CASE("MinSum heuristic-adaptive policy skips bound sampling") {
  const auto instance = make_minsum_instance();
  TrackingNNSolver solver;
  kdc::MinSumSolver::Config config;
  config.refinement_policy =
      kdc::MinSumRefinementPolicy::HEURISTIC_ADAPTIVE;
  config.lb_num_samples = 20;
  config.max_iterations = 1;
  config.verify_after = false;

  const auto result = kdc::MinSumSolver::solve(instance, solver, config);
  REQUIRE(result.refinement_policy ==
          kdc::MinSumRefinementPolicy::HEURISTIC_ADAPTIVE);
  REQUIRE(result.num_ip_solves == 2);
  REQUIRE(solver.solve_times.size() == 2U);
  REQUIRE(solver.solve_times[0] == 0.0);
  REQUIRE(solver.solve_times[1] == instance.T_end / 2.0);
  REQUIRE(result.bound_status == kdc::BoundStatus::CERTIFIED);
  REQUIRE(result.lower_bound == 0.0);
  REQUIRE(result.certified_lower_bound_integral == 0.0);
  REQUIRE_FALSE(result.certified_gap.has_value());
}

TEST_CASE("MinSum heuristic-adaptive refinement stops after stagnation patience") {
  const auto instance = make_minsum_instance();
  TrackingNNSolver solver;
  kdc::MinSumSolver::Config config;
  config.refinement_policy =
      kdc::MinSumRefinementPolicy::HEURISTIC_ADAPTIVE;
  config.max_iterations = 12;
  config.stagnation_patience = 2;
  config.improvement_tolerance = 1e-9;
  config.verify_after = false;

  const auto result = kdc::MinSumSolver::solve(instance, solver, config);
  REQUIRE(result.num_iterations == 2);
  REQUIRE(result.num_ip_solves == 3);
  REQUIRE(solver.solve_times.size() == 3U);
  REQUIRE(result.optimality_status == kdc::OptimalityStatus::FEASIBLE);
}

TEST_CASE("MinSum stops when improvement is below configured tolerance") {
  const auto instance = make_minsum_instance();
  TrackingNNSolver solver;
  kdc::MinSumSolver::Config config;
  config.refinement_policy =
      kdc::MinSumRefinementPolicy::HEURISTIC_ADAPTIVE;
  config.max_iterations = 10;
  config.stagnation_patience = 1;
  config.improvement_tolerance = 1e6;
  config.verify_after = false;

  const auto result = kdc::MinSumSolver::solve(instance, solver, config);
  REQUIRE(result.num_iterations == 1);
  REQUIRE(result.num_ip_solves == 2);
}

TEST_CASE("MinSum adaptive refinement is deterministic on a fixed instance") {
  const auto instance = make_minsum_instance();
  TrackingNNSolver first_solver;
  TrackingNNSolver second_solver;
  kdc::MinSumSolver::Config config;
  config.refinement_policy =
      kdc::MinSumRefinementPolicy::HEURISTIC_ADAPTIVE;
  config.max_iterations = 4;
  config.stagnation_patience = 2;
  config.verify_after = false;

  const auto first =
      kdc::MinSumSolver::solve(instance, first_solver, config);
  const auto second =
      kdc::MinSumSolver::solve(instance, second_solver, config);
  REQUIRE(first_solver.solve_times == second_solver.solve_times);
  REQUIRE(first.num_ip_solves == second.num_ip_solves);
  REQUIRE(first.num_iterations == second.num_iterations);
  REQUIRE(kdc::test::near(first.total_integral, second.total_integral, 1e-12));
}

TEST_CASE("MinSum returns a time-limited result when budget is already expired") {
  const auto instance = make_minsum_instance();
  TrackingNNSolver solver;
  kdc::MinSumSolver::Config config;
  kdc::SolverBudget budget(kdc::SolverBudget::Clock::now());

  const auto result =
      kdc::MinSumSolver::solve(instance, solver, config, budget);
  REQUIRE(result.time_limited);
  REQUIRE(result.optimality_status == kdc::OptimalityStatus::TIME_LIMIT);
  REQUIRE_FALSE(result.feasible);
  REQUIRE(result.num_ip_solves == 0);
  REQUIRE(solver.solve_times.empty());
}

TEST_CASE("MinSum sampled refinement estimates are not certified gaps") {
  const auto instance = make_minsum_instance();
  TrackingNNSolver solver;
  kdc::MinSumSolver::Config config;
  config.refinement_policy = kdc::MinSumRefinementPolicy::CERTIFIED_BOUND;
  config.lb_num_samples = 2;
  config.max_iterations = 1;
  config.verify_after = false;

  const auto result = kdc::MinSumSolver::solve(instance, solver, config);
  REQUIRE(result.refinement_policy ==
          kdc::MinSumRefinementPolicy::CERTIFIED_BOUND);
  REQUIRE(result.num_ip_solves >= 1 + config.lb_num_samples);
  REQUIRE(result.bound_status == kdc::BoundStatus::CERTIFIED);
  REQUIRE(result.certified_lower_bound_integral == 0.0);
  REQUIRE_FALSE(result.certified_gap.has_value());
  REQUIRE(result.heuristic_lower_bound_integral >= 0.0);
}

TEST_CASE("MinSum exact static solver does not certify kinetic optimality") {
  const auto instance = make_minsum_instance();
  kdc::KontSolver solver;
  kdc::MinSumSolver::Config config;
  config.refinement_policy =
      kdc::MinSumRefinementPolicy::HEURISTIC_ADAPTIVE;
  config.gap_target = 1e12;
  config.verify_after = false;

  const auto result = kdc::MinSumSolver::solve(instance, solver, config);
  REQUIRE(result.feasible);
  REQUIRE(result.exact_solver);
  REQUIRE(result.bound_status == kdc::BoundStatus::CERTIFIED);
  REQUIRE(result.lower_bound == 0.0);
  REQUIRE_FALSE(result.certified_gap.has_value());
  REQUIRE(result.optimality_status == kdc::OptimalityStatus::FEASIBLE);
}

TEST_CASE("MinSum solver produces a verified kinetic solution") {
  const auto instance = make_minsum_instance();
  const auto result = solve_minsum(instance);

  SECTION("small") {
    REQUIRE(result.verified);
    REQUIRE(result.verification_kind ==
            kdc::VerificationKind::CERTIFIED_CONTINUOUS);
    REQUIRE(result.verification_time_sec >= 0.0);
    REQUIRE(result.solution.is_well_formed());
    REQUIRE(result.num_ip_solves >= 1);
    REQUIRE(result.num_iterations >= 1);
  }

  SECTION("int <= minmax int") {
    kdc::KontSolver solver;
    kdc::MinMaxSolver::Config config;
    config.time_limit_per_ip = 10.0;
    const auto minmax = kdc::MinMaxSolver::solve(instance, solver, config);
    REQUIRE(result.total_integral <= minmax.solution.total_integral() + 1e-6);
  }

  SECTION("lb valid") {
    REQUIRE(result.lower_bound_integral <= result.total_integral + 1e-6);
  }

  SECTION("gap nonincreasing") {
    REQUIRE_FALSE(result.gap_trace.empty());
    for (kdc::Index index = 1; index < result.gap_trace.size(); ++index) {
      REQUIRE(result.gap_trace[index] <= result.gap_trace[index - 1U] + 1e-9);
    }
    REQUIRE(result.gap <= result.gap_trace.front() + 1e-9);
  }
}

TEST_CASE("MinSum solver verifies a moving instance") {
  const auto instance = kdc::test::make_dummy_instance(5, 3, 271U);
  const auto result = solve_minsum(instance);
  REQUIRE(result.verified);
  REQUIRE(result.solution.is_well_formed());
  REQUIRE(result.lower_bound_integral <= result.total_integral + 1e-6);
  for (kdc::Index index = 1; index < result.gap_trace.size(); ++index) {
    REQUIRE(result.gap_trace[index] <= result.gap_trace[index - 1U] + 1e-9);
  }
}
