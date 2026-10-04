#include "test_utils.hpp"

#include "kdc/algorithm_comparison.hpp"
#include "kdc/io.hpp"
#include "kdc/kont_solver.hpp"
#include "kdc/stats.hpp"
#include "kdc/static_solver_registry.hpp"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

namespace {
std::filesystem::path temporary_path(const std::string& suffix) {
  const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
  return std::filesystem::temp_directory_path() /
         ("kdc-comparison-" + std::to_string(stamp) + suffix);
}

std::string write_temp_instance(const kdc::Instance& instance) {
  const auto path = temporary_path(".json");
  kdc::DatasetReader::write_json(instance, path.string());
  return path.string();
}
}

TEST_CASE("Comparison: brute force matches IP on n=6 m=3") {
  kdc::AlgorithmComparisonConfig config;
  config.algorithm_names = {"brute-force", "branch-and-bound"};
  config.reference_algorithm = "branch-and-bound";
  const std::string instance_path =
      write_temp_instance(kdc::test::make_dummy_instance(6, 3, 3U));
  config.instance_paths = {instance_path};
  config.time_points = {0.5};
  auto kont = std::make_unique<kdc::KontSolver>();
  std::vector<kdc::StaticComparisonResult> results;
  kdc::AlgorithmComparator::compare_static(config, kont.get(), results);
  REQUIRE(results.size() == 2U);
  const auto brute = std::find_if(
      results.begin(), results.end(), [](const auto& result) {
        return result.algorithm_name == "brute-force";
      });
  REQUIRE(brute != results.end());
  REQUIRE(brute->matches_reference);
  REQUIRE(brute->verified);
  std::filesystem::remove(instance_path);
}

TEST_CASE("Comparison: branch and bound matches IP on n=15 m=5") {
  if (!kdc::KontSolver::probe_native_backend()) {
    SUCCEED("native KONT/COPT runtime is unavailable");
    return;
  }
  kdc::AlgorithmComparisonConfig config;
  config.algorithm_names = {"branch-and-bound", "ip-kont"};
  config.reference_algorithm = "ip-kont";
  const std::string instance_path =
      write_temp_instance(kdc::test::make_dummy_instance(15, 5, 11U));
  config.instance_paths = {instance_path};
  config.time_points = {0.5};
  auto kont = std::make_unique<kdc::KontSolver>();
  std::vector<kdc::StaticComparisonResult> results;
  kdc::AlgorithmComparator::compare_static(config, kont.get(), results);
  const auto branch_and_bound = std::find_if(
      results.begin(), results.end(), [](const auto& result) {
        return result.algorithm_name == "branch-and-bound";
      });
  REQUIRE(branch_and_bound != results.end());
  REQUIRE(branch_and_bound->matches_reference);
  REQUIRE(branch_and_bound->verified);
  std::filesystem::remove(instance_path);
}

TEST_CASE("Comparison: greedy record has a valid bound") {
  kdc::AlgorithmComparisonConfig config;
  config.algorithm_names = {"greedy", "branch-and-bound"};
  config.reference_algorithm = "branch-and-bound";
  const std::string instance_path =
      write_temp_instance(kdc::test::make_dummy_instance(30, 5, 3U));
  config.instance_paths = {instance_path};
  config.time_points = {0.5};
  auto kont = std::make_unique<kdc::KontSolver>();
  std::vector<kdc::StaticComparisonResult> results;
  kdc::AlgorithmComparator::compare_static(config, kont.get(), results);
  const auto greedy = std::find_if(
      results.begin(), results.end(), [](const auto& result) {
        return result.algorithm_name == "greedy";
      });
  REQUIRE(greedy != results.end());
  REQUIRE(greedy->cost >= greedy->lower_bound - 1e-6);
  std::filesystem::remove(instance_path);
}

TEST_CASE("Comparison: every built-in solver verifies on n=10 m=3") {
  kdc::AlgorithmComparisonConfig config;
  config.algorithm_names = kdc::StaticSolverRegistry::list();
  config.reference_algorithm = "branch-and-bound";
  if (!kdc::KontSolver::probe_native_backend()) {
    config.algorithm_names.erase(
        std::remove(config.algorithm_names.begin(),
                    config.algorithm_names.end(), "ip-kont"),
        config.algorithm_names.end());
  }
  const std::string instance_path =
      write_temp_instance(kdc::test::make_dummy_instance(10, 3, 5U));
  config.instance_paths = {instance_path};
  config.time_points = {0.5};
  auto kont = std::make_unique<kdc::KontSolver>();
  std::vector<kdc::StaticComparisonResult> results;
  kdc::AlgorithmComparator::compare_static(config, kont.get(), results);
  REQUIRE(results.size() == config.algorithm_names.size());
  for (const auto& result : results) {
    if (result.feasible) {
      INFO("algorithm: " << result.algorithm_name);
      REQUIRE(result.verified);
    }
  }
  std::filesystem::remove(instance_path);
}

TEST_CASE("Comparison: saves JSON and CSV results") {
  const auto json_path = temporary_path("-results.json");
  const auto csv_path = temporary_path("-results.csv");
  const std::vector<kdc::StaticComparisonResult> results{
      {"sample, one", "nn", 3, 2, 0.5, 8.0, 4.0, 8.0, 1.0, 0.02, true, true,
       false, 1.5}};
  kdc::AlgorithmComparator::save_results(results, json_path.string(),
                                         csv_path.string());
  std::ifstream json_file(json_path);
  const auto json = nlohmann::json::parse(json_file);
  REQUIRE(json.size() == 1U);
  REQUIRE(json[0]["instance_name"] == "sample, one");
  std::ifstream csv_file(csv_path);
  std::string header;
  std::string row;
  std::getline(csv_file, header);
  std::getline(csv_file, row);
  REQUIRE(header.find("ratio_to_reference") != std::string::npos);
  REQUIRE(row.find("\"sample, one\"") != std::string::npos);
  std::filesystem::remove(json_path);
  std::filesystem::remove(csv_path);
}

TEST_CASE("Comparison: paired Wilcoxon sanity") {
  const std::vector<double> first{1.0, 2.0, 3.0, 4.0, 5.0};
  const std::vector<double> second{1.0, 2.0, 3.0, 4.0, 5.0};
  const auto result = kdc::Stats::paired_wilcoxon(first, second);
  REQUIRE(result.p_value > 0.99);
  REQUIRE(std::abs(result.cliffs_delta) < 1e-9);
}
