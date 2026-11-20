#include "test_utils.hpp"

#include "kdc/batch_runner.hpp"
#include "kdc/benchmark.hpp"
#include "kdc/io.hpp"
#include "kdc/mock_ilp_solver.hpp"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace {
std::filesystem::path temporary_directory(const std::string& prefix) {
  const auto stamp =
      std::chrono::steady_clock::now().time_since_epoch().count();
  return std::filesystem::temp_directory_path() /
         (prefix + std::to_string(stamp));
}

kdc::Instance temporal_pipeline_instance() {
  auto instance = kdc::test::make_instance_linear(
      {{kdc::Point(0.0, 0.0), kdc::Point(1.0, 0.0)},
       {kdc::Point(2.0, 0.0), kdc::Point(2.0, 1.0)}},
      {{0.0, 0.0}, {2.0, 0.0}});
  instance.name = "pipeline-smoke";
  return instance;
}

std::string read_file(const std::filesystem::path& path) {
  std::ifstream input(path);
  return {std::istreambuf_iterator<char>(input),
          std::istreambuf_iterator<char>()};
}
}  // namespace

TEST_CASE("Benchmark preserves the existing adaptive MinSum path") {
  const auto instance = temporal_pipeline_instance();
  kdc::MockILPSolver ilp;
  kdc::BenchmarkConfig config;
  config.solver_method = kdc::PipelineSolverMethod::MINSUM_ADAPTIVE;
  config.profile = kdc::BenchmarkProfile::FAST;
  config.algorithm_name = "branch-and-bound";
  config.actual_backend = "branch-and-bound";
  config.fast_time_limit_sec = 2.0;
  config.exact_time_limit_sec = 2.0;
  config.per_static_time_limit_sec = 1.0;
  config.minsum_cfg.lb_num_samples = 2;

  const auto result = kdc::BenchmarkRunner::run_single(
      instance, ilp, kdc::ObjectiveType::MIN_SUM, config);
  REQUIRE(result.solver_method == "minsum-adaptive");
  REQUIRE(result.minsum_refinement_policy ==
          kdc::MinSumRefinementPolicy::HEURISTIC_ADAPTIVE);
  REQUIRE(result.configuration.at("resolved_solver_method") ==
          "minsum-adaptive");
}

TEST_CASE("Temporal interval B&B uses its own versioned benchmark schema") {
  const auto root = temporary_directory("kdc-temporal-benchmark-");
  const auto instance = temporal_pipeline_instance();
  kdc::MockILPSolver ilp;
  kdc::BenchmarkConfig config;
  config.solver_method =
      kdc::PipelineSolverMethod::TEMPORAL_INTERVAL_BNB;
  config.fast_time_limit_sec = 2.0;
  config.temporal_config.execution.delta_min = 0.1;
  config.temporal_config.execution.max_leaves = 2U;
  config.temporal_config.execution.oracle_time_limit_seconds = 1.0;
  config.measure_memory = false;
  config.output_dir = root.string();
  const auto result = kdc::BenchmarkRunner::run_single(
      instance, ilp, kdc::ObjectiveType::MIN_SUM, config);

  REQUIRE(result.algorithm_name == "temporal-interval-bnb");
  REQUIRE(result.temporal_result_v2.at("schema_version") == 2);
  REQUIRE(result.temporal_result_v2.at("execution").at("leaf_count") > 0U);
  REQUIRE(result.temporal_result_v2.at("execution").at("oracle_call_count") >
          0U);
  REQUIRE(result.temporal_result_v2.at("final").at("UB_valid") == false);
  REQUIRE(result.temporal_result_v2.at("final").at("status") ==
          "INCOMPLETE");
  REQUIRE(result.temporal_result_v2.at("incumbent_snapshot").is_null());
  REQUIRE_FALSE(result.failed);
  REQUIRE_FALSE(result.feasible);
  REQUIRE(result.temporal_result_v2.at("final").contains("LB_valid"));
  REQUIRE(result.temporal_result_v2.at("final").contains("UB_valid"));
  REQUIRE(result.temporal_result_v2.at("final").contains("gamma_cert"));
  REQUIRE(result.configuration.at("solver_method") ==
          "temporal-interval-bnb");
  REQUIRE(result.temporal_result_v2.dump().find("Infinity") ==
          std::string::npos);
  REQUIRE(result.temporal_result_v2.dump().find("NaN") ==
          std::string::npos);

  const auto json_path = root / "temporal.json";
  const auto csv_path = root / "temporal.csv";
  kdc::BenchmarkRunner::save_json({result}, json_path.string());
  kdc::BenchmarkRunner::save_csv({result}, csv_path.string());
  const auto loaded = kdc::BenchmarkRunner::load_json(json_path.string());
  REQUIRE(loaded.size() == 1U);
  REQUIRE(loaded.front().solver_method == "temporal-interval-bnb");
  REQUIRE(loaded.front().temporal_result_v2.at("schema_version") == 2);
  REQUIRE_FALSE(loaded.front().failed);
  REQUIRE_FALSE(loaded.front().feasible);
  const std::string csv = read_file(csv_path);
  REQUIRE(csv.find("absolute_gap") != std::string::npos);
  REQUIRE(csv.find("minsum_refinement_policy") == std::string::npos);

  auto malformed = nlohmann::json::parse(read_file(json_path));
  malformed.at(0).at("temporal_result_v2")["schema_version"] = 9;
  const auto invalid_path = root / "invalid.json";
  std::ofstream(invalid_path) << malformed.dump();
  REQUIRE_THROWS(kdc::BenchmarkRunner::load_json(invalid_path.string()));
  std::filesystem::remove_all(root);
}

TEST_CASE("B-grid selects the longest-interval temporal strategy") {
  const auto instance = temporal_pipeline_instance();
  kdc::MockILPSolver ilp;
  kdc::BenchmarkConfig config;
  config.solver_method = kdc::PipelineSolverMethod::B_GRID;
  config.fast_time_limit_sec = 2.0;
  config.temporal_config.execution.delta_min = 0.1;
  config.temporal_config.execution.max_leaves = 2U;
  config.temporal_config.execution.oracle_time_limit_seconds = 1.0;
  config.measure_memory = false;

  const auto result = kdc::BenchmarkRunner::run_single(
      instance, ilp, kdc::ObjectiveType::MIN_SUM, config);
  REQUIRE(result.solver_method == "b-grid");
  REQUIRE(result.temporal_result_v2.at("schema_version") == 2);
  REQUIRE(result.configuration.at("split_selection") ==
          "LONGEST_INTERVAL");
  REQUIRE(result.temporal_baseline_result_v1.is_null());
}

TEST_CASE("Chapter 7 baselines use their versioned benchmark payloads") {
  const auto instance = temporal_pipeline_instance();
  kdc::MockILPSolver ilp;
  kdc::BenchmarkConfig config;
  config.fast_time_limit_sec = 2.0;
  config.temporal_baseline_config.oracle_time_limit_seconds = 1.0;
  config.measure_memory = false;

  config.solver_method = kdc::PipelineSolverMethod::B_NN;
  const auto nearest = kdc::BenchmarkRunner::run_single(
      instance, ilp, kdc::ObjectiveType::MIN_SUM, config);
  REQUIRE(nearest.solver_method == "b-nn");
  REQUIRE(nearest.temporal_baseline_result_v1.at("schema_version") == 1);
  REQUIRE(nearest.temporal_baseline_result_v1.at("baseline") == "B-nn");
  REQUIRE(nearest.temporal_result_v2.is_null());
  REQUIRE(nearest.configuration.at("resolved_solver_method") == "b-nn");

  const auto root = temporary_directory("kdc-baseline-benchmark-");
  const auto json_path = root / "baseline.json";
  const auto csv_path = root / "baseline.csv";
  kdc::BenchmarkRunner::save_json({nearest}, json_path.string());
  kdc::BenchmarkRunner::save_csv({nearest}, csv_path.string());
  const auto loaded = kdc::BenchmarkRunner::load_json(json_path.string());
  REQUIRE(loaded.size() == 1U);
  REQUIRE(loaded.front().temporal_baseline_result_v1.at("baseline") ==
          "B-nn");
  const std::string csv = read_file(csv_path);
  REQUIRE(csv.find("cost_role") != std::string::npos);
  REQUIRE(csv.find("LB,") == std::string::npos);

  config.solver_method = kdc::PipelineSolverMethod::B_MINMAX;
  const auto minmax = kdc::BenchmarkRunner::run_single(
      instance, ilp, kdc::ObjectiveType::MIN_SUM, config);
  REQUIRE(minmax.solver_method == "b-minmax");
  REQUIRE(minmax.temporal_baseline_result_v1.at("baseline") == "B-minmax");
  REQUIRE(minmax.temporal_baseline_result_v1.at("cost_role") ==
          "MIN_SUM_COST_OF_MINMAX_CONSTRUCTED_PROGRAM");

  config.solver_method = kdc::PipelineSolverMethod::B_STATIC_HOLD;
  const auto static_hold = kdc::BenchmarkRunner::run_single(
      instance, ilp, kdc::ObjectiveType::MIN_SUM, config);
  REQUIRE(static_hold.solver_method == "b-static-hold");
  REQUIRE(static_hold.temporal_baseline_result_v1.at("baseline") ==
          "B-static-hold");
  std::filesystem::remove_all(root);
}

TEST_CASE("Temporal batch runs without an ILP backend and reports failures") {
  const auto root = temporary_directory("kdc-temporal-batch-");
  const auto instances = root / "instances";
  std::filesystem::create_directories(instances);
  kdc::DatasetReader::write_json(temporal_pipeline_instance(),
                                 (instances / "sample.json").string());
  kdc::BatchRunConfig config;
  config.instances_dir = instances.string();
  config.output_dir = (root / "out").string();
  config.solver_method =
      kdc::PipelineSolverMethod::TEMPORAL_INTERVAL_BNB;
  config.objectives = {kdc::ObjectiveType::MIN_SUM};
  config.profile = kdc::BenchmarkProfile::FAST;
  config.fast_time_limit_sec = 2.0;
  config.temporal_config.execution.delta_min = 0.1;
  config.temporal_config.execution.max_leaves = 2U;
  config.temporal_config.execution.oracle_time_limit_seconds = 1.0;
  config.save_solutions = true;
  config.save_traces = true;

  REQUIRE_NOTHROW(kdc::BatchRunner::run(config, nullptr));
  const auto master_path =
      std::filesystem::path(config.output_dir) / "master_results.json";
  const auto records = nlohmann::json::parse(read_file(master_path));
  REQUIRE(records.size() == 1U);
  REQUIRE(records.dump().find("Infinity") == std::string::npos);
  REQUIRE(records.dump().find("NaN") == std::string::npos);
  REQUIRE(records.at(0).at("schema_version") == 4);
  REQUIRE(records.at(0).at("solver_method") == "temporal-interval-bnb");
  REQUIRE_FALSE(records.at(0).at("failed").get<bool>());
  REQUIRE_FALSE(records.at(0).at("feasible").get<bool>());
  REQUIRE(records.at(0).at("temporal_result_v2").at("schema_version") == 2);
  REQUIRE(records.at(0).at("configuration").at("resolved_solver_method") ==
          "temporal-interval-bnb");
  REQUIRE_FALSE(std::filesystem::exists(
      std::filesystem::path(config.output_dir) / "runs" / "sample" /
      "temporal-interval-bnb" / "minsum" / "solution.json"));
  const std::string csv =
      read_file(std::filesystem::path(config.output_dir) /
                "master_results.csv");
  REQUIRE(csv.find("absolute_gap") != std::string::npos);
  REQUIRE(csv.find("objective_value") == std::string::npos);
  REQUIRE(std::filesystem::exists(
      std::filesystem::path(config.output_dir) / "batch_summary.md"));

  const auto failed_root = temporary_directory("kdc-temporal-failure-");
  config.output_dir = (failed_root / "out").string();
  config.temporal_config.execution.max_leaves = 0;
  REQUIRE_NOTHROW(kdc::BatchRunner::run(config, nullptr));
  const auto failed_records = nlohmann::json::parse(read_file(
      std::filesystem::path(config.output_dir) / "master_results.json"));
  REQUIRE(failed_records.size() == 1U);
  REQUIRE(failed_records.at(0).at("failed").get<bool>());
  REQUIRE(failed_records.at(0).at("temporal_result_v2").at("execution")
              .at("status") == "INVALID");
  std::filesystem::remove_all(root);
  std::filesystem::remove_all(failed_root);
}

TEST_CASE("Temporal baseline batch dispatch works without a static backend") {
  const auto root = temporary_directory("kdc-baseline-batch-");
  const auto instances = root / "instances";
  std::filesystem::create_directories(instances);
  kdc::DatasetReader::write_json(
      temporal_pipeline_instance(), (instances / "sample.json").string());
  kdc::BatchRunConfig config;
  config.instances_dir = instances.string();
  config.output_dir = (root / "out").string();
  config.solver_method = kdc::PipelineSolverMethod::B_NN;
  config.objectives = {kdc::ObjectiveType::MIN_SUM};
  config.profile = kdc::BenchmarkProfile::FAST;
  config.fast_time_limit_sec = 2.0;

  REQUIRE_NOTHROW(kdc::BatchRunner::run(config, nullptr));
  const auto master_path =
      std::filesystem::path(config.output_dir) / "master_results.json";
  const auto records = nlohmann::json::parse(read_file(master_path));
  REQUIRE(records.size() == 1U);
  REQUIRE(records.at(0).at("solver_method") == "b-nn");
  REQUIRE(records.at(0).at("temporal_baseline_result_v1")
              .at("schema_version") == 1);
  REQUIRE(records.at(0).at("configuration").at("resolved_solver_method") ==
          "b-nn");
  const std::string csv =
      read_file(std::filesystem::path(config.output_dir) /
                "master_results.csv");
  REQUIRE(csv.find("cost_role") != std::string::npos);
  REQUIRE(csv.find("LB,") == std::string::npos);
  const std::string summary =
      read_file(std::filesystem::path(config.output_dir) /
                "batch_summary.md");
  REQUIRE(summary.find("Temporal Baseline") != std::string::npos);

  config.solver_method = kdc::PipelineSolverMethod::B_STATIC_HOLD;
  config.output_dir = (root / "out-static-hold").string();
  REQUIRE_NOTHROW(kdc::BatchRunner::run(config, nullptr));
  const auto static_records = nlohmann::json::parse(read_file(
      std::filesystem::path(config.output_dir) / "master_results.json"));
  REQUIRE(static_records.size() == 1U);
  const auto& static_baseline =
      static_records.at(0).at("temporal_baseline_result_v1");
  REQUIRE(static_baseline.at("status") == "CANDIDATE");
  REQUIRE(static_baseline.at("program_complete") == true);
  REQUIRE(static_baseline.at("static_at_zero").at("termination") ==
          "HEURISTIC_FEASIBLE");
  std::filesystem::remove_all(root);
}
