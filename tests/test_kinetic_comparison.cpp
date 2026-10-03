#include "test_utils.hpp"

#include "kdc/algorithms/ip_static_solver.hpp"
#include "kdc/algorithm_comparison.hpp"
#include "kdc/io.hpp"
#include "kdc/kont_solver.hpp"
#include "kdc/minmax.hpp"
#include "kdc/minsum.hpp"
#include "kdc/static_solver_registry.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <algorithm>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
std::filesystem::path temporary_path(const std::string& suffix) {
  const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
  return std::filesystem::temp_directory_path() /
         ("kdc-kinetic-comparison-" + std::to_string(stamp) + suffix);
}

std::string write_temp_instance(const kdc::Instance& instance) {
  const auto path = temporary_path(".json");
  kdc::DatasetReader::write_json(instance, path.string());
  return path.string();
}
}

TEST_CASE("Kinetic comparison: MinMax records are verified") {
  kdc::AlgorithmComparisonConfig config;
  config.algorithm_names = {"nn"};
  const std::string instance_path =
      write_temp_instance(kdc::test::make_dummy_instance(5, 2, 17U));
  config.instance_paths = {instance_path};
  config.num_repeats = 2;
  auto kont = std::make_unique<kdc::KontSolver>();
  std::vector<kdc::KineticComparisonResult> results;

  kdc::AlgorithmComparator::compare_kinetic(config, kont.get(), "minmax",
                                             results);

  REQUIRE(results.size() == 3U);
  std::vector<kdc::KineticComparisonResult> heuristic_runs;
  std::size_t exact_runs = 0U;
  for (const auto& result : results) {
    if (result.algorithm_name == "nn") {
      heuristic_runs.push_back(result);
    }
    if (result.algorithm_category == "exact_reference") {
      ++exact_runs;
    }
  }
  REQUIRE(heuristic_runs.size() == 2U);
  REQUIRE(exact_runs == 1U);
  REQUIRE(heuristic_runs[0].objective == "minmax");
  REQUIRE(heuristic_runs[0].n == 5);
  REQUIRE(heuristic_runs[0].m == 2);
  REQUIRE(heuristic_runs[0].objective_value >= 0.0);
  REQUIRE(heuristic_runs[0].verified);
  REQUIRE(heuristic_runs[0].seed != heuristic_runs[1].seed);
  std::filesystem::remove(instance_path);
}

TEST_CASE("Kinetic comparison: MinSum records can be saved") {
  kdc::AlgorithmComparisonConfig config;
  config.algorithm_names = {"nn"};
  const std::string instance_path =
      write_temp_instance(kdc::test::make_dummy_instance(5, 2, 23U));
  config.instance_paths = {instance_path};
  auto kont = std::make_unique<kdc::KontSolver>();
  std::vector<kdc::KineticComparisonResult> results;

  kdc::AlgorithmComparator::compare_kinetic(config, kont.get(), "minsum",
                                             results);
  REQUIRE(results.size() == 2U);
  const auto heuristic = std::find_if(
      results.begin(), results.end(), [](const auto& result) {
        return result.algorithm_name == "nn";
      });
  REQUIRE(heuristic != results.end());
  REQUIRE(heuristic->objective == "minsum");
  REQUIRE(heuristic->verified);

  const auto json_path = temporary_path("-results.json");
  const auto csv_path = temporary_path("-results.csv");
  kdc::AlgorithmComparator::save_results(results, json_path.string(),
                                          csv_path.string());
  REQUIRE(std::filesystem::file_size(json_path) > 0U);
  REQUIRE(std::filesystem::file_size(csv_path) > 0U);
  std::filesystem::remove(instance_path);
  std::filesystem::remove(json_path);
  std::filesystem::remove(csv_path);
}

TEST_CASE("Kinetic comparison: invalid objective is rejected") {
  kdc::AlgorithmComparisonConfig config;
  config.algorithm_names = {"nn"};
  config.instance_paths = {"unused.json"};
  std::vector<kdc::KineticComparisonResult> results;
  REQUIRE_THROWS_AS(kdc::AlgorithmComparator::compare_kinetic(
                        config, nullptr, "static", results),
                    std::invalid_argument);
}

TEST_CASE("Kinetic solvers accept static solver adapters") {
  const auto instance = kdc::test::make_dummy_instance(5, 2, 31U);
  auto kont = std::make_unique<kdc::KontSolver>();
  auto solver = kdc::StaticSolverRegistry::create("nn", kont.get());
  REQUIRE(solver != nullptr);
  const kdc::MinMaxSolver::Config max_config;
  const kdc::MinSumSolver::Config sum_config;

  const auto max_result =
      kdc::MinMaxSolver::solve(instance, *solver, max_config);
  const auto sum_result =
      kdc::MinSumSolver::solve(instance, *solver, sum_config);

  REQUIRE(max_result.verified);
  REQUIRE(sum_result.verified);
  REQUIRE(max_result.peak_cost >= 0.0);
  REQUIRE(sum_result.total_integral >= 0.0);

  kdc::IPStaticSolver ip_static_solver(kont.get());
  const auto max_ip_result =
      kdc::MinMaxSolver::solve(instance, *kont, max_config);
  const auto max_adapter_result =
      kdc::MinMaxSolver::solve(instance, ip_static_solver, max_config);
  const auto sum_ip_result =
      kdc::MinSumSolver::solve(instance, *kont, sum_config);
  const auto sum_adapter_result =
      kdc::MinSumSolver::solve(instance, ip_static_solver, sum_config);
  REQUIRE(kdc::test::near(max_ip_result.peak_cost,
                          max_adapter_result.peak_cost, 1e-9));
  REQUIRE(kdc::test::near(sum_ip_result.total_integral,
                          sum_adapter_result.total_integral, 1e-9));
}
