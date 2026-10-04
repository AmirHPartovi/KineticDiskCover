#include "test_utils.hpp"

#include "kdc/algorithms/branch_and_bound_solver.hpp"
#include "kdc/algorithms/brute_force_solver.hpp"
#include "kdc/algorithms/nn_static_solver.hpp"
#include "kdc/benchmark.hpp"
#include "kdc/kont_solver.hpp"
#include "kdc/minmax.hpp"
#include "kdc/minsum.hpp"
#include "kdc/static_solver_registry.hpp"
#include "kdc/stationary.hpp"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <chrono>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {
kdc::Instance status_instance() {
  return kdc::test::make_instance_linear(
      {{kdc::Point(0.0, 0.0), kdc::Point(0.0, 0.0)},
       {kdc::Point(1.0, 0.0), kdc::Point(1.0, 0.0)},
       {kdc::Point(2.0, 0.0), kdc::Point(2.0, 0.0)}},
      {{0.0, 0.0}, {3.0, 0.0}});
}

std::string temp_path(const std::string& extension) {
  const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
  return (std::filesystem::temp_directory_path() /
          ("kdc-result-status-" + std::to_string(stamp) + extension))
      .string();
}

class ExactTimeoutSolver final : public kdc::IStaticSolver {
 public:
  kdc::StaticSolution solve(const kdc::Instance& instance,
                            double time) override {
    ++calls;
    if (calls > 1) {
      kdc::StaticSolution timed_out;
      timed_out.time_limited = true;
      kdc::set_static_result_status(
          timed_out, kdc::BoundStatus::CERTIFIED,
          kdc::OptimalityStatus::TIME_LIMIT, true);
      return timed_out;
    }
    const auto assignment = kdc::StationarySolver::solve_nn(instance, time);
    kdc::StaticSolution result;
    result.supporting_point = assignment.supporting_point;
    result.radius = assignment.radius;
    result.cost = assignment.cost;
    result.feasible = assignment.feasible;
    result.lower_bound = 0.0;
    result.solver_name = name();
    kdc::set_static_result_status(
        result, kdc::BoundStatus::CERTIFIED,
        kdc::OptimalityStatus::FEASIBLE, true);
    return result;
  }

  std::string name() const override { return "test-exact"; }
  bool is_exact() const override { return true; }
  bool provides_lower_bound() const override { return false; }

  int calls{0};
};
}  // namespace

TEST_CASE("Result status enum values round-trip") {
  for (const auto status : {kdc::BoundStatus::NONE,
                            kdc::BoundStatus::HEURISTIC,
                            kdc::BoundStatus::CERTIFIED}) {
    REQUIRE(kdc::bound_status_from_string(
                kdc::bound_status_to_string(status)) == status);
  }
  for (const auto status : {kdc::OptimalityStatus::OPTIMAL,
                            kdc::OptimalityStatus::FEASIBLE,
                            kdc::OptimalityStatus::TIME_LIMIT,
                            kdc::OptimalityStatus::INFEASIBLE,
                            kdc::OptimalityStatus::FAILED}) {
    REQUIRE(kdc::optimality_status_from_string(
                kdc::optimality_status_to_string(status)) == status);
  }
  for (const auto policy : {
           kdc::MinSumRefinementPolicy::CERTIFIED_BOUND,
           kdc::MinSumRefinementPolicy::HEURISTIC_ADAPTIVE}) {
    REQUIRE(kdc::minsum_refinement_policy_from_string(
                kdc::minsum_refinement_policy_to_string(policy)) == policy);
  }
}

TEST_CASE("Static lower-bound provenance is explicit for every solver") {
  const auto instance = status_instance();
  kdc::KontSolver kont;
  std::vector<std::pair<std::string, kdc::BoundStatus>> solvers{
      {"nn", kdc::BoundStatus::CERTIFIED},
      {"greedy", kdc::BoundStatus::CERTIFIED},
      {"primal-dual", kdc::BoundStatus::HEURISTIC},
      {"local-search", kdc::BoundStatus::CERTIFIED},
      {"sa", kdc::BoundStatus::CERTIFIED},
      {"genetic", kdc::BoundStatus::CERTIFIED},
      {"lp-rounding", kdc::BoundStatus::CERTIFIED},
      {"shifting", kdc::BoundStatus::CERTIFIED},
      {"ip-kont", kdc::BoundStatus::CERTIFIED},
      {"branch-and-bound", kdc::BoundStatus::CERTIFIED},
      {"brute-force", kdc::BoundStatus::CERTIFIED}};
  if (!kdc::KontSolver::probe_native_backend()) {
    solvers.erase(
        std::remove_if(solvers.begin(), solvers.end(), [](const auto& entry) {
          return entry.first == "ip-kont";
        }),
        solvers.end());
  }

  for (const auto& entry : solvers) {
    auto solver = kdc::StaticSolverRegistry::create(entry.first, &kont);
    REQUIRE(solver != nullptr);
    const auto result = solver->solve(instance, 0.5);
    INFO("algorithm: " << entry.first);
    REQUIRE(result.feasible);
    REQUIRE(result.bound_status == entry.second);
    REQUIRE(result.upper_bound == result.cost);
    if (!solver->is_exact()) {
      REQUIRE_FALSE(result.exact_solver);
      REQUIRE_FALSE(result.certified_gap.has_value());
      REQUIRE(result.optimality_status == kdc::OptimalityStatus::FEASIBLE);
    } else {
      REQUIRE(result.exact_solver);
      REQUIRE(result.certified_gap.has_value());
      REQUIRE((result.optimality_status == kdc::OptimalityStatus::OPTIMAL ||
               result.optimality_status == kdc::OptimalityStatus::FEASIBLE));
    }
  }
}

TEST_CASE("Static statuses distinguish infeasible, timed out, and failed") {
  const auto impossible = kdc::test::make_instance_linear(
      {{kdc::Point(1.0, 0.0), kdc::Point(1.0, 0.0)}}, {});
  kdc::BranchAndBoundSolver infeasible_solver(nullptr);
  const auto infeasible = infeasible_solver.solve(impossible, 0.0);
  REQUIRE_FALSE(infeasible.feasible);
  REQUIRE(infeasible.optimality_status == kdc::OptimalityStatus::INFEASIBLE);

  auto instance = kdc::test::make_dummy_instance(14, 5, 23U);
  kdc::BranchAndBoundSolver::Config config;
  config.time_limit_sec = 1e-12;
  config.use_lp_lower_bound = false;
  const auto timed = kdc::BranchAndBoundSolver(nullptr, config).solve(instance,
                                                                      0.5);
  REQUIRE(timed.time_limited);
  REQUIRE(timed.optimality_status == kdc::OptimalityStatus::TIME_LIMIT);
  REQUIRE(timed.optimality_status != kdc::OptimalityStatus::OPTIMAL);

  kdc::BruteForceSolver::Config brute_config;
  brute_config.time_limit_sec = 1e-12;
  const auto brute_instance = kdc::test::make_dummy_instance(8, 5, 23U);
  const auto brute_timed =
      kdc::BruteForceSolver(brute_config).solve(brute_instance, 0.0);
  REQUIRE(brute_timed.time_limited);
  REQUIRE(brute_timed.optimality_status == kdc::OptimalityStatus::TIME_LIMIT);
  REQUIRE(brute_timed.bound_status == kdc::BoundStatus::CERTIFIED);
  REQUIRE(brute_timed.optimality_status != kdc::OptimalityStatus::OPTIMAL);
  REQUIRE(brute_timed.feasible == brute_timed.certified_gap.has_value());

  kdc::StaticSolution failed;
  kdc::set_static_result_status(failed, kdc::BoundStatus::NONE,
                                kdc::OptimalityStatus::FAILED, false);
  REQUIRE(failed.optimality_status == kdc::OptimalityStatus::FAILED);
}

TEST_CASE("Heuristic kinetic runs do not report certified gaps or optimality") {
  const auto instance = status_instance();
  kdc::NNStaticSolver solver;

  kdc::MinMaxSolver::Config max_config;
  const auto max_result =
      kdc::MinMaxSolver::solve(instance, solver, max_config);
  REQUIRE(max_result.feasible);
  REQUIRE_FALSE(max_result.exact_solver);
  REQUIRE_FALSE(max_result.certified_gap.has_value());
  REQUIRE(max_result.optimality_status == kdc::OptimalityStatus::FEASIBLE);

  kdc::MinSumSolver::Config sum_config;
  sum_config.lb_num_samples = 2;
  const auto sum_result =
      kdc::MinSumSolver::solve(instance, solver, sum_config);
  REQUIRE(sum_result.feasible);
  REQUIRE_FALSE(sum_result.exact_solver);
  REQUIRE_FALSE(sum_result.certified_gap.has_value());
  REQUIRE(sum_result.optimality_status == kdc::OptimalityStatus::FEASIBLE);
  REQUIRE(sum_result.bound_status == kdc::BoundStatus::CERTIFIED);
  REQUIRE(sum_result.heuristic_lower_bound_integral >= 0.0);
}

TEST_CASE("A timed-out exact child solve cannot make MinMax optimal") {
  ExactTimeoutSolver solver;
  kdc::MinMaxSolver::Config config;
  config.gap_target = 0.0;
  config.verify_after = false;
  const auto result =
      kdc::MinMaxSolver::solve(status_instance(), solver, config);

  REQUIRE(result.feasible);
  REQUIRE(result.time_limited);
  REQUIRE(result.optimality_status == kdc::OptimalityStatus::TIME_LIMIT);
  REQUIRE(result.optimality_status != kdc::OptimalityStatus::OPTIMAL);
}

TEST_CASE("Benchmark result statuses serialize and load compatibly") {
  const std::string path = temp_path(".json");
  kdc::BenchmarkResult record;
  record.instance_name = "status-instance";
  record.n = 1;
  record.m = 1;
  record.objective = kdc::ObjectiveType::MIN_MAX;
  record.objective_value = 4.0;
  record.lower_bound = 0.0;
  record.bound_status = kdc::BoundStatus::CERTIFIED;
  record.upper_bound = 4.0;
  record.certified_gap = 4.0;
  record.optimality_status = kdc::OptimalityStatus::OPTIMAL;
  record.exact_solver = true;
  record.feasible = true;
  record.verified = true;
  record.time_limited = true;
  record.timestamp = "test";
  record.certified_lower_bound = 0.0;
  record.heuristic_lower_bound = 0.0;
  kdc::BenchmarkRunner::save_json({record}, path);

  const auto loaded = kdc::BenchmarkRunner::load_json(path);
  REQUIRE(loaded.size() == 1U);
  REQUIRE(loaded.front().bound_status == kdc::BoundStatus::CERTIFIED);
  REQUIRE(loaded.front().optimality_status ==
          kdc::OptimalityStatus::TIME_LIMIT);
  REQUIRE(loaded.front().time_limited);
  REQUIRE(loaded.front().exact_solver);
  REQUIRE(loaded.front().certified_gap.has_value());
  REQUIRE(*loaded.front().certified_gap == 4.0);
  std::filesystem::remove(path);

  const std::string legacy_path = temp_path(".json");
  nlohmann::json legacy = nlohmann::json::array(
      {{{"instance_name", "old"},
        {"n", 1},
        {"m", 1},
        {"objective", "minmax"},
        {"wall_time_sec", 1.0},
        {"cpu_time_sec", 1.0},
        {"ip_time_sec", 1.0},
        {"peak_memory_mb", 1.0},
        {"objective_value", 5.0},
        {"lower_bound", 3.0},
        {"gap", 0.4},
        {"num_ip_solves", 1},
        {"num_iterations", 1},
        {"verified", false},
        {"timestamp", "legacy"}}});
  {
    std::ofstream output(legacy_path);
    output << legacy;
  }
  const auto old_loaded = kdc::BenchmarkRunner::load_json(legacy_path);
  REQUIRE(old_loaded.front().bound_status == kdc::BoundStatus::NONE);
  REQUIRE(old_loaded.front().optimality_status ==
          kdc::OptimalityStatus::FEASIBLE);
  REQUIRE(old_loaded.front().feasible);
  REQUIRE_FALSE(old_loaded.front().certified_gap.has_value());
  std::filesystem::remove(legacy_path);
}
