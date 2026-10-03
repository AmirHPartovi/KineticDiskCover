#include "test_utils.hpp"

#include "kdc/batch_runner.hpp"
#include "kdc/io.hpp"
#include "kdc/mock_ilp_solver.hpp"
#include "kdc/trace.hpp"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <string>

namespace {
std::filesystem::path temporary_directory(const std::string& prefix) {
  const auto stamp =
      std::chrono::steady_clock::now().time_since_epoch().count();
  return std::filesystem::temp_directory_path() /
         (prefix + std::to_string(stamp));
}

void write_instance(const std::filesystem::path& directory) {
  std::filesystem::create_directories(directory);
  auto instance = kdc::test::make_dummy_instance(15, 4, 1U);
  instance.name = "inst1";
  kdc::DatasetReader::write_json(instance,
                                 (directory / "inst1.json").string());
}

kdc::BatchRunConfig one_run_config(const std::filesystem::path& root,
                                   const std::string& output_name) {
  kdc::BatchRunConfig config;
  config.instances_dir = (root / "instances").string();
  config.output_dir = (root / output_name).string();
  config.algorithm_names = {"nn"};
  config.profile = kdc::BenchmarkProfile::DEBUG;
  config.objectives = {kdc::ObjectiveType::MIN_MAX};
  config.per_ip_time_limit_sec = 10.0;
  return config;
}
}  // namespace

TEST_CASE("BatchRunner: single instance single algorithm") {
  const auto root = temporary_directory("kdc-batch-single-");
  write_instance(root / "instances");

  auto config = one_run_config(root, "output");
  kdc::MockILPSolver mock;
  kdc::BatchRunner::run(config, &mock);

  REQUIRE(std::ifstream(config.output_dir + "/master_results.json").good());
  REQUIRE(std::ifstream(config.output_dir + "/master_results.csv").good());
  REQUIRE(std::ifstream(config.output_dir + "/batch_summary.md").good());
  const auto run_dir =
      std::filesystem::path(config.output_dir) / "runs/inst1/nn/minmax";
  REQUIRE(std::filesystem::exists(run_dir / "result.json"));
  REQUIRE(std::filesystem::exists(run_dir / "solution.json"));
  REQUIRE(std::filesystem::exists(run_dir / "trace.csv"));
  std::ifstream result_input(config.output_dir + "/master_results.json");
  const auto records = nlohmann::json::parse(result_input);
  REQUIRE(records.is_array());
  REQUIRE(records.size() == 2U);
  REQUIRE(records.back().at("algorithm_name") == "nn");
  REQUIRE(records.back().at("feasible").get<bool>());
  REQUIRE(records.back().at("bound_status").get<std::string>() ==
          "CERTIFIED");
  REQUIRE(records.back().at("optimality_status").get<std::string>() ==
          "FEASIBLE");
  REQUIRE_FALSE(records.back().at("exact_solver").get<bool>());
  REQUIRE(records.back().at("minsum_refinement_policy").get<std::string>() ==
          "HEURISTIC_ADAPTIVE");
  REQUIRE(records.back().at("upper_bound").is_number());
  REQUIRE(records.back().at("certified_gap").is_null());
  REQUIRE(records.back().at("time_limit_per_ip_sec").get<double>() == 10.0);
  REQUIRE(records.back().at("solution_json_path").get<std::string>() != "");
  std::filesystem::remove_all(root);
}

TEST_CASE("BatchRunConfig: defaults to a 60-second IP limit") {
  const kdc::BatchRunConfig config;
  REQUIRE(config.per_ip_time_limit_sec == 60.0);
  REQUIRE(config.num_threads >= 1);
}

TEST_CASE("BatchRunner: clears previous results but preserves other output files") {
  const auto root = temporary_directory("kdc-batch-clean-");
  write_instance(root / "instances");
  auto config = one_run_config(root, "output");
  const auto output = std::filesystem::path(config.output_dir);
  const auto stale_run = output / "runs" / "old" / "nn" / "minmax";
  std::filesystem::create_directories(stale_run);
  std::ofstream(stale_run / "result.json") << "{\"stale\": true}\n";
  std::ofstream(output / "master_results.json") << "[{\"stale\": true}]\n";
  std::ofstream(output / "unrelated.txt") << "keep\n";

  kdc::MockILPSolver mock;
  kdc::BatchRunner::run(config, &mock);

  REQUIRE_FALSE(std::filesystem::exists(stale_run));
  REQUIRE(std::filesystem::exists(output / "unrelated.txt"));
  std::ifstream result_input(output / "master_results.json");
  const auto records = nlohmann::json::parse(result_input);
  REQUIRE(records.size() == 2U);
  REQUIRE(records.back().at("algorithm_name") == "nn");
  REQUIRE(records.back().at("time_limit_per_ip_sec").get<double>() == 10.0);
  std::filesystem::remove_all(root);
}

TEST_CASE("BatchRunner: multiple algorithms and modes") {
  const auto root = temporary_directory("kdc-batch-multiple-");
  write_instance(root / "instances");
  auto config = one_run_config(root, "output");
  config.algorithm_names = {"nn", "greedy", "ip-kont"};
  config.exact_reference = "ip-kont";
  config.objectives = {kdc::ObjectiveType::MIN_MAX,
                       kdc::ObjectiveType::MIN_SUM};
  config.minsum_refinement_policy =
      kdc::MinSumRefinementPolicy::CERTIFIED_BOUND;
  kdc::MockILPSolver mock;
  kdc::BatchRunner::run(config, &mock);

  std::ifstream input(config.output_dir + "/master_results.json");
  const auto records = nlohmann::json::parse(input);
  REQUIRE(records.size() == 6U);
  std::map<std::string, int> exact_count_by_objective;
  std::map<std::string, std::string> exact_status_by_objective;
  for (const auto& record : records) {
    REQUIRE(record.at("result_json_path").get<std::string>() != "");
    REQUIRE(std::filesystem::exists(
        record.at("result_json_path").get<std::string>()));
    if (record.at("objective") == "minsum") {
      REQUIRE(record.at("minsum_refinement_policy").get<std::string>() ==
              "CERTIFIED_BOUND");
    }
    if (record.at("algorithm_category") == "exact_reference") {
      ++exact_count_by_objective[record.at("objective").get<std::string>()];
      exact_status_by_objective[record.at("objective").get<std::string>()] =
          record.at("optimality_status").get<std::string>();
    }
    if (record.at("algorithm_category") == "heuristic") {
      REQUIRE(record.at("optimality_status").get<std::string>() != "OPTIMAL");
      REQUIRE(record.at("certified_gap").is_null());
    }
    REQUIRE(std::filesystem::exists(record.at("trace_csv_path").get<std::string>()));
    const auto trace =
        kdc::TraceWriter::read_csv(
            record.at("trace_csv_path").get<std::string>());
    if (record.at("feasible").get<bool>()) {
      REQUIRE_FALSE(trace.empty());
    }
    for (const auto& record : records) {
      if (record.at("algorithm_category") == "heuristic" &&
          !record.at("empirical_ratio_to_exact").is_null()) {
        REQUIRE(exact_status_by_objective[record.at("objective")
                                              .get<std::string>()] ==
                "OPTIMAL");
      }
    }
    for (const auto& row : trace) {
      REQUIRE(row.objective_value >= row.lower_bound - 1e-9);
    }
    if (record.at("feasible").get<bool>()) {
      REQUIRE(std::filesystem::exists(
          record.at("solution_json_path").get<std::string>()));
    }
  }
  REQUIRE(exact_count_by_objective["minmax"] == 1);
  REQUIRE(exact_count_by_objective["minsum"] == 1);
  std::ifstream csv(config.output_dir + "/master_results.csv");
  std::string header;
  std::getline(csv, header);
  REQUIRE(header.find("error_message") != std::string::npos);
  REQUIRE(header.find("time_limit_per_ip_sec") != std::string::npos);
  std::filesystem::remove_all(root);
}

TEST_CASE("BatchRunner: parallel matches sequential") {
  const auto root = temporary_directory("kdc-batch-parallel-");
  write_instance(root / "instances");
  auto sequential = one_run_config(root, "sequential");
  auto parallel = one_run_config(root, "parallel");
  parallel.parallel = true;
  parallel.num_threads = 2;

  kdc::MockILPSolver mock;
  kdc::BatchRunner::run(sequential, &mock);
  kdc::BatchRunner::run(parallel, &mock);

  std::ifstream seq_input(sequential.output_dir + "/master_results.json");
  std::ifstream par_input(parallel.output_dir + "/master_results.json");
  const auto seq = nlohmann::json::parse(seq_input);
  const auto par = nlohmann::json::parse(par_input);
  REQUIRE(seq.size() == 2U);
  REQUIRE(par.size() == 2U);
  REQUIRE(seq.back().at("algorithm_name") == "nn");
  REQUIRE(par.back().at("algorithm_name") == "nn");
  REQUIRE(kdc::test::near(seq.back().at("objective_value").get<double>(),
                          par.back().at("objective_value").get<double>(), 1e-6));
  REQUIRE(kdc::test::near(seq.back().at("gap").get<double>(),
                          par.back().at("gap").get<double>(), 1e-6));
  REQUIRE(seq.back().at("seed") == par.back().at("seed"));
  std::filesystem::remove_all(root);
}

TEST_CASE("BatchRunner: handles missing instances directory") {
  kdc::BatchRunConfig config;
  config.instances_dir =
      (temporary_directory("kdc-batch-missing-") / "absent").string();
  config.output_dir =
      (std::filesystem::temp_directory_path() / "kdc-batch-missing-output")
          .string();
  kdc::MockILPSolver mock;
  REQUIRE_NOTHROW(kdc::BatchRunner::run(config, &mock));
}

TEST_CASE("BatchRunner: records errors and keeps processing") {
  const auto root = temporary_directory("kdc-batch-failure-");
  const auto instances = root / "instances";
  std::filesystem::create_directories(instances);
  std::ofstream(instances / "invalid.json") << "{ invalid json\n";
  auto config = one_run_config(root, "output");
  kdc::MockILPSolver mock;
  kdc::BatchRunner::run(config, &mock);

  std::ifstream input(config.output_dir + "/master_results.json");
  const auto records = nlohmann::json::parse(input);
  REQUIRE(records.size() == 2U);
  REQUIRE(records.back().at("algorithm_name") == "nn");
  REQUIRE_FALSE(records.back().at("error_message").get<std::string>().empty());
  REQUIRE_FALSE(records.back().at("feasible").get<bool>());
  REQUIRE(std::filesystem::exists(
      records.back().at("result_json_path").get<std::string>()));
  std::ifstream summary(config.output_dir + "/batch_summary.md");
  const std::string contents((std::istreambuf_iterator<char>(summary)),
                             std::istreambuf_iterator<char>());
  REQUIRE(contents.find("## Failures") != std::string::npos);
  REQUIRE(contents.find("invalid") != std::string::npos);
  std::filesystem::remove_all(root);
}
