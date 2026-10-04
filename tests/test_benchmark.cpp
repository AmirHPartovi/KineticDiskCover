#include "test_utils.hpp"

#include "kdc/benchmark.hpp"
#include "kdc/kont_solver.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {
kdc::Instance benchmark_instance() {
  return kdc::test::make_instance_linear(
      {{kdc::Point(0.0, 0.0), kdc::Point(1.0, 0.0)},
       {kdc::Point(2.0, 0.0), kdc::Point(2.0, 1.0)}},
      {{0.0, 0.0}, {2.0, 0.0}});
}

std::string unique_temp_stem() {
  const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
  return (std::filesystem::temp_directory_path() /
          ("kdc-benchmark-" + std::to_string(stamp)))
      .string();
}
}

TEST_CASE("Benchmark runner measures and persists results") {
  const auto instance = benchmark_instance();
  kdc::KontSolver solver;
  kdc::BenchmarkConfig config;
  config.measure_memory = true;
  config.verify_after = true;
  config.minmax_cfg.time_limit_per_ip = 10.0;
  config.minmax_cfg.gap_target = 0.01;
  config.minsum_cfg.time_limit_per_ip = 10.0;
  config.minsum_cfg.lb_num_samples = 2;

  SECTION("single") {
    const auto result = kdc::BenchmarkRunner::run_single(
        instance, solver, kdc::ObjectiveType::MIN_MAX, config);
    REQUIRE(result.verified);
    REQUIRE(result.verification_kind ==
            kdc::VerificationKind::CERTIFIED_CONTINUOUS);
    REQUIRE(result.verification_time_sec >= 0.0);
    REQUIRE(result.instance_name == instance.name);
    REQUIRE(result.n == instance.n);
    REQUIRE(result.m == instance.m);
    REQUIRE(result.objective == kdc::ObjectiveType::MIN_MAX);
    REQUIRE(result.wall_time_sec >= 0.0);
    REQUIRE(result.solve_time_sec >= 0.0);
    REQUIRE(result.solve_time_sec <= result.wall_time_sec + 1e-6);
    REQUIRE(result.cpu_time_sec >= 0.0);
    REQUIRE(result.ip_time_sec >= 0.0);
    REQUIRE(result.objective_value >= result.lower_bound - 1e-6);
  }

  SECTION("json save/load") {
    const auto result = kdc::BenchmarkRunner::run_single(
        instance, solver, kdc::ObjectiveType::MIN_MAX, config);
    const std::string json_path = unique_temp_stem() + ".json";
    const std::string csv_path = unique_temp_stem() + ".csv";
    kdc::BenchmarkRunner::save_json({result}, json_path);
    kdc::BenchmarkRunner::save_csv({result}, csv_path);
    const auto loaded = kdc::BenchmarkRunner::load_json(json_path);
    REQUIRE(std::filesystem::exists(json_path));
    REQUIRE(std::filesystem::exists(csv_path));
    REQUIRE(loaded.size() == 1U);
    REQUIRE(loaded.front().instance_name == result.instance_name);
    REQUIRE(loaded.front().n == result.n);
    REQUIRE(loaded.front().m == result.m);
    REQUIRE(loaded.front().objective == result.objective);
    REQUIRE(loaded.front().objective_value == result.objective_value);
    REQUIRE(loaded.front().lower_bound == result.lower_bound);
    REQUIRE(loaded.front().minsum_refinement_policy ==
            kdc::MinSumRefinementPolicy::HEURISTIC_ADAPTIVE);
    REQUIRE(loaded.front().verified == result.verified);
    REQUIRE(loaded.front().verification_kind == result.verification_kind);
    REQUIRE(loaded.front().verification_time_sec ==
            result.verification_time_sec);
    std::filesystem::remove(json_path);
    std::filesystem::remove(csv_path);
  }

  SECTION("MinSum policy is persisted") {
    config.minsum_cfg.refinement_policy =
        kdc::MinSumRefinementPolicy::CERTIFIED_BOUND;
    const auto result = kdc::BenchmarkRunner::run_single(
        instance, solver, kdc::ObjectiveType::MIN_SUM, config);
    REQUIRE(result.minsum_refinement_policy ==
            kdc::MinSumRefinementPolicy::CERTIFIED_BOUND);
    REQUIRE_FALSE(result.certified_gap.has_value());
    const std::string json_path = unique_temp_stem() + ".json";
    kdc::BenchmarkRunner::save_json({result}, json_path);
    const auto loaded = kdc::BenchmarkRunner::load_json(json_path);
    REQUIRE(loaded.front().minsum_refinement_policy ==
            kdc::MinSumRefinementPolicy::CERTIFIED_BOUND);
    std::filesystem::remove(json_path);
  }

  SECTION("memory") {
    REQUIRE(kdc::BenchmarkRunner::get_peak_memory_mb() > 0.0);
  }
}

TEST_CASE("Benchmark CLI workflow writes JSON and CSV outputs") {
  const std::string root = unique_temp_stem();
  const std::filesystem::path dataset =
      std::filesystem::path(root) / "dataset";
  const std::filesystem::path output =
      std::filesystem::path(root) / "output";
  std::filesystem::create_directories(dataset);
  kdc::DatasetReader::write_json(benchmark_instance(),
                                 (dataset / "instance.json").string());

  kdc::BenchmarkConfig config;
  config.dataset_dir = dataset.string();
  config.output_dir = output.string();
  config.both_objectives = true;
  config.measure_memory = false;
  config.minmax_cfg.time_limit_per_ip = 10.0;
  config.minsum_cfg.time_limit_per_ip = 10.0;
  config.minsum_cfg.lb_num_samples = 2;
  kdc::BenchmarkRunner::run_all(config);

  const auto json_path = output / "json" / "benchmark.json";
  const auto csv_path = output / "csv" / "benchmark.csv";
  REQUIRE(std::filesystem::exists(json_path));
  REQUIRE(std::filesystem::exists(csv_path));
  const auto manifest_path =
      output / "experiment_manifest.json";
  REQUIRE(std::filesystem::exists(manifest_path));
  std::ifstream manifest_input(manifest_path);
  const auto manifest = nlohmann::json::parse(manifest_input);
  REQUIRE(manifest.at("source_tree_dirty").is_boolean());
  const auto results =
      kdc::BenchmarkRunner::load_json(json_path.string());
  REQUIRE(results.size() == 2U);
  REQUIRE(results[0].objective == kdc::ObjectiveType::MIN_MAX);
  REQUIRE(results[1].objective == kdc::ObjectiveType::MIN_SUM);

  std::filesystem::remove_all(root);
}
