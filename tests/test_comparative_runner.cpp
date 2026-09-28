#include "test_utils.hpp"

#include "kdc/algorithms/ip_static_solver.hpp"
#include "kdc/algorithms/nn_static_solver.hpp"
#include "kdc/comparative_runner.hpp"
#include "kdc/mock_ilp_solver.hpp"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

namespace {
std::filesystem::path temp_dir(const std::string& prefix) {
  const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
  return std::filesystem::temp_directory_path() /
         (prefix + std::to_string(stamp));
}
}

TEST_CASE("ComparativeRunner: runs both objectives and verifies") {
  const auto instance = kdc::test::make_dummy_instance(20, 5, 7U);
  kdc::NNStaticSolver nn;
  kdc::ComparativeRunner::Config config;

  const auto result = kdc::ComparativeRunner::run(instance, nn, config);

  REQUIRE(result.peak_cost_minmax > 0.0);
  REQUIRE(result.integral_minsum > 0.0);
  REQUIRE(result.peak_ratio > 0.0);
  REQUIRE(result.integral_ratio > 0.0);
  REQUIRE(result.verified_minmax);
  REQUIRE(result.verified_minsum);
}

TEST_CASE("ComparativeRunner: objective ratios are bounded for Mock solver") {
  const auto instance = kdc::test::make_dummy_instance(20, 5, 7U);
  auto mock = std::make_unique<kdc::MockILPSolver>();
  kdc::IPStaticSolver ip(mock.get());
  kdc::ComparativeRunner::Config config;

  const auto result = kdc::ComparativeRunner::run(instance, ip, config);

  REQUIRE(result.peak_ratio <= 1.0 + 1e-6);
  REQUIRE(result.integral_ratio <= 1.0 + 1e-6);
}

TEST_CASE("ComparativeRunner: writes JSON and CSV result files") {
  const auto dir = temp_dir("kdc-comparative-save-");
  const std::vector<kdc::ComparativeResult> results{
      {"sample, one", 3, 2, "nn", 10.0, 5.0, 1.0, 0.1, 2,
       8.0, 4.0, 1.0, 0.2, 3, 0.8, 0.9, 1.0, -2.0, true, true}};
  const auto json_path = dir / "comparative.json";
  const auto csv_path = dir / "comparative.csv";

  kdc::ComparativeRunner::save_json(results, json_path.string());
  kdc::ComparativeRunner::save_csv(results, csv_path.string());

  REQUIRE(std::filesystem::exists(json_path));
  REQUIRE(std::filesystem::exists(csv_path));
  std::ifstream json_input(json_path);
  const auto document = nlohmann::json::parse(json_input);
  REQUIRE(document.size() == 1U);
  REQUIRE(document[0]["instance_name"] == "sample, one");
  std::ifstream csv_input(csv_path);
  std::string header;
  std::string row;
  std::getline(csv_input, header);
  std::getline(csv_input, row);
  REQUIRE(header.find("verified_minsum") != std::string::npos);
  REQUIRE(row.find("\"sample, one\"") != std::string::npos);
  std::filesystem::remove_all(dir);
}

TEST_CASE("ComparativeRunner: run_all writes comparison outputs") {
  const auto dir = temp_dir("kdc-comparative-all-");
  std::vector<kdc::Instance> instances;
  for (const unsigned seed : {1U, 2U, 3U}) {
    instances.push_back(kdc::test::make_dummy_instance(15, 4, seed));
  }
  auto mock = std::make_unique<kdc::MockILPSolver>();
  kdc::IPStaticSolver ip(mock.get());
  kdc::ComparativeRunner::Config config;
  config.output_dir = dir.string();
  config.minsum_cfg.gap_target = 1e9;

  kdc::ComparativeRunner::run_all(instances, ip, config);

  REQUIRE(std::filesystem::exists(dir / "comparative.json"));
  REQUIRE(std::filesystem::exists(dir / "comparative.csv"));
  std::filesystem::remove_all(dir);
}

TEST_CASE("ComparativeRunner: optionally serializes both kinetic solutions") {
  const auto dir = temp_dir("kdc-comparative-solutions-");
  const auto instance = kdc::test::make_dummy_instance(8, 3, 15U);
  kdc::NNStaticSolver nn;
  kdc::ComparativeRunner::Config config;
  config.output_dir = dir.string();
  config.save_solutions = true;

  const auto result = kdc::ComparativeRunner::run(instance, nn, config);

  REQUIRE(result.verified_minmax);
  REQUIRE(result.verified_minsum);
  REQUIRE(std::filesystem::exists(dir / (instance.name + "_minmax.json")));
  REQUIRE(std::filesystem::exists(dir / (instance.name + "_minsum.json")));
  std::filesystem::remove_all(dir);
}
