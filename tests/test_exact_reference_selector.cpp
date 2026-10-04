#include "test_utils.hpp"

#include "kdc/batch_runner.hpp"
#include "kdc/exact_reference_selector.hpp"
#include "kdc/io.hpp"
#include "kdc/kont_solver.hpp"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <tuple>

namespace {
std::filesystem::path temp_root() {
  const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
  return std::filesystem::temp_directory_path() /
         ("kdc-exact-selector-" + std::to_string(stamp));
}

kdc::Instance zero_cost_instance() {
  auto instance = kdc::test::make_instance_linear(
      {{kdc::Point(0.0, 0.0), kdc::Point(0.0, 0.0)}},
      {{0.0, 0.0}});
  instance.name = "zero-cost";
  return instance;
}

nlohmann::json read_json(const std::filesystem::path& path) {
  std::ifstream input(path);
  return nlohmann::json::parse(input);
}
}  // namespace

TEST_CASE("AUTO calibrates exact backends on identical verified objectives") {
  const auto root = temp_root();
  const auto dataset = root / "dataset";
  const auto output = root / "output";
  std::filesystem::create_directories(dataset);
  kdc::DatasetReader::write_json(zero_cost_instance(),
                                 (dataset / "tiny.json").string());

  const auto decision = kdc::ExactReferenceSelector::resolve(
      "auto", dataset, output);
  REQUIRE(decision.valid);
  REQUIRE(decision.requested_backend == "auto");
  REQUIRE((decision.selected_backend == "ip-kont" ||
           decision.selected_backend == "branch-and-bound"));

  const auto manifest = read_json(output / "experiment_manifest.json");
  REQUIRE(manifest.at("actual_backend").get<std::string>() ==
          decision.actual_backend);
  REQUIRE(manifest.at("selected_backend").get<std::string>() ==
          decision.selected_backend);
  REQUIRE(manifest.at("calibration_instances").size() == 1U);
  REQUIRE(manifest.at("calibration_instances")[0] ==
          std::filesystem::absolute(dataset / "tiny.json").string());
  REQUIRE(manifest.at("calibration_objectives") ==
          nlohmann::json::array({"minmax", "minsum"}));
  REQUIRE(manifest.at("runtime_statistics").contains("by_objective"));
  REQUIRE(manifest.at("runtime_statistics").at("by_objective").contains(
      "minmax"));
  REQUIRE(manifest.at("runtime_statistics").at("by_objective").contains(
      "minsum"));

  bool observed_ip_kont = false;
  bool observed_branch_and_bound = false;
  std::set<std::tuple<std::string, std::string, std::string>> runs_seen;
  for (const auto* key : {"successful_runs", "rejected_runs"}) {
    for (const auto& run : manifest.at(key)) {
      const std::string backend = run.at("backend").get<std::string>();
      const std::string instance = run.at("instance").get<std::string>();
      const std::string objective = run.at("objective").get<std::string>();
      REQUIRE(instance ==
              std::filesystem::absolute(dataset / "tiny.json").string());
      observed_ip_kont = observed_ip_kont || backend == "ip-kont";
      observed_branch_and_bound =
          observed_branch_and_bound || backend == "branch-and-bound";
      runs_seen.emplace(instance, backend, objective);
      if (run.value("accepted", false)) {
        REQUIRE(run.at("feasible").get<bool>());
        REQUIRE(run.at("calibration_kind") ==
                "static_solver_performance");
        REQUIRE(run.at("solver_runtime_sec").is_number());
        REQUIRE(run.at("kinetic_optimality_proven").is_null());
      }
    }
  }
  REQUIRE(observed_ip_kont);
  REQUIRE(observed_branch_and_bound);
  for (const auto* backend : {"ip-kont", "branch-and-bound"}) {
    for (const auto* objective : {"minmax", "minsum"}) {
      REQUIRE(runs_seen.count(std::make_tuple(
                  std::filesystem::absolute(dataset / "tiny.json").string(),
                  std::string(backend), std::string(objective))) == 1U);
    }
  }

  const auto persisted_content = manifest.dump();
  const auto reused =
      kdc::ExactReferenceSelector::resolve("auto", dataset, output);
  REQUIRE(reused.selected_backend == decision.selected_backend);
  REQUIRE(read_json(output / "experiment_manifest.json").dump() ==
          persisted_content);

  auto changed = zero_cost_instance();
  changed.name = "changed";
  changed.stations[0].pos = kdc::Point(1.0, 0.0);
  kdc::DatasetReader::write_json(changed, (dataset / "tiny.json").string());
  const auto recalibrated =
      kdc::ExactReferenceSelector::resolve("auto", dataset, output);
  REQUIRE(recalibrated.valid);
  REQUIRE(read_json(output / "experiment_manifest.json")
              .at("dataset_fingerprint") !=
          manifest.at("dataset_fingerprint"));
  std::filesystem::remove_all(root);
}

TEST_CASE("AUTO benchmark profile schedules exactly one exact backend") {
  const auto root = temp_root();
  const auto dataset = root / "dataset";
  std::filesystem::create_directories(dataset);
  kdc::DatasetReader::write_json(zero_cost_instance(),
                                 (dataset / "tiny.json").string());

  kdc::BatchRunConfig config;
  config.instances_dir = dataset.string();
  config.output_dir = (root / "output").string();
  config.algorithm_names = {"nn", "greedy"};
  config.objectives = {kdc::ObjectiveType::MIN_MAX};
  config.save_solutions = false;
  config.save_traces = false;
  kdc::KontSolver ilp;

  kdc::BatchRunner::run(config, &ilp);
  const auto manifest_path =
      std::filesystem::path(config.output_dir) / "experiment_manifest.json";
  const auto first_manifest = read_json(manifest_path);
  kdc::BatchRunner::run(config, &ilp);
  const auto second_manifest = read_json(manifest_path);
  REQUIRE(first_manifest.at("dataset_fingerprint") ==
          second_manifest.at("dataset_fingerprint"));
  REQUIRE(first_manifest.at("selected_backend") ==
          second_manifest.at("selected_backend"));
  REQUIRE(first_manifest.at("successful_runs") ==
          second_manifest.at("successful_runs"));
  REQUIRE(first_manifest.at("experiment_id") !=
          second_manifest.at("experiment_id"));

  std::ifstream records_input(config.output_dir + "/master_results.json");
  const auto records = nlohmann::json::parse(records_input);
  int exact_runs = 0;
  for (const auto& record : records) {
    const std::string algorithm = record.at("algorithm_name");
    if (algorithm == "ip-kont" || algorithm == "branch-and-bound") {
      ++exact_runs;
      REQUIRE(algorithm ==
              first_manifest.at("selected_backend").get<std::string>());
    }
  }
  REQUIRE(exact_runs == 1);
  std::filesystem::remove_all(root);
}

TEST_CASE("strict benchmark rejects both exact backends before calibration") {
  const auto root = temp_root();
  kdc::BatchRunConfig config;
  config.instances_dir = (root / "missing").string();
  config.output_dir = (root / "output").string();
  config.algorithm_names = {"ip-kont", "branch-and-bound"};
  kdc::MockILPSolver ilp;
  REQUIRE_THROWS_AS(kdc::BatchRunner::run(config, &ilp),
                    std::invalid_argument);
  REQUIRE_FALSE(std::filesystem::exists(
      std::filesystem::path(config.output_dir) / "experiment_manifest.json"));
  std::filesystem::remove_all(root);
}

TEST_CASE("explicit KONT request fails instead of selecting a fallback") {
  if (kdc::KontSolver::probe_native_backend()) {
    SUCCEED("native KONT/COPT runtime is available");
    return;
  }
  const auto root = temp_root();
  REQUIRE_THROWS_AS(kdc::ExactReferenceSelector::resolve(
                        "ip-kont", root / "dataset", root / "output"),
                    std::runtime_error);
  std::filesystem::remove_all(root);
}
