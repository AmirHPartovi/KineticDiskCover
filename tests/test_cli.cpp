#include "test_utils.hpp"

#include "kdc/cli_utils.hpp"
#include "kdc/io.hpp"
#include "kdc/solution_serializer.hpp"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace {
std::filesystem::path temp_path(const std::string& suffix) {
  const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
  return std::filesystem::temp_directory_path() /
         ("kdc-cli-" + std::to_string(stamp) + suffix);
}

std::string shell_quote(const std::string& value) {
  std::string quoted = "'";
  for (const char character : value) {
    if (character == '\'') {
      quoted += "'\\''";
    } else {
      quoted += character;
    }
  }
  quoted += '\'';
  return quoted;
}
}

TEST_CASE("CliArgs: parses --key=value") {
  char arg0[] = "prog";
  char arg1[] = "--algorithm=nn";
  char arg2[] = "--mode";
  char arg3[] = "minsum";
  char* argv[] = {arg0, arg1, arg2, arg3};
  const auto args = kdc::CliArgs::parse(4, argv);
  REQUIRE(args.get("algorithm") == "nn");
  REQUIRE(args.get("mode") == "minsum");
}

TEST_CASE("CliArgs: parses --key value") {
  char arg0[] = "prog";
  char arg1[] = "--algorithm";
  char arg2[] = "ip-kont";
  char arg3[] = "--gap";
  char arg4[] = "0.001";
  char* argv[] = {arg0, arg1, arg2, arg3, arg4};
  const auto args = kdc::CliArgs::parse(5, argv);
  REQUIRE(args.get("algorithm") == "ip-kont");
  REQUIRE(std::abs(args.get_double("gap", 0.0) - 0.001) < 1e-12);
}

TEST_CASE("CliArgs: accepts negative numeric literals as flag values") {
  char arg0[] = "prog";
  char arg1[] = "--gap";
  char arg2[] = "-0.001";
  char arg3[] = "--threads";
  char arg4[] = "-2";
  char* argv[] = {arg0, arg1, arg2, arg3, arg4};
  const auto args = kdc::CliArgs::parse(5, argv);
  REQUIRE(std::abs(args.get_double("gap", 0.0) + 0.001) < 1e-12);
  REQUIRE(args.get_int("threads", 0) == -2);
}

TEST_CASE("CliArgs: parses boolean flags") {
  char arg0[] = "prog";
  char arg1[] = "--verify";
  char arg2[] = "--no-handovers";
  char* argv[] = {arg0, arg1, arg2};
  const auto args = kdc::CliArgs::parse(3, argv);
  REQUIRE(args.has("verify"));
  REQUIRE(args.has("no-handovers"));
  REQUIRE(args.get("verify").empty());
}

TEST_CASE("CliArgs: returns defaults for absent flags") {
  char arg0[] = "prog";
  char* argv[] = {arg0};
  const auto args = kdc::CliArgs::parse(1, argv);
  REQUIRE(args.get("algorithm", "ip-kont") == "ip-kont");
  REQUIRE(args.get_int("threads", 1) == 1);
}

TEST_CASE("CliArgs: preserves positional arguments") {
  char arg0[] = "prog";
  char arg1[] = "solve";
  char arg2[] = "--algorithm";
  char arg3[] = "nn";
  char arg4[] = "extra";
  char* argv[] = {arg0, arg1, arg2, arg3, arg4};
  const auto args = kdc::CliArgs::parse(5, argv);
  REQUIRE(args.positional().size() == 2U);
  REQUIRE(args.positional()[0] == "solve");
  REQUIRE(args.positional()[1] == "extra");
}

TEST_CASE("CLI: solve writes a serializable JSON solution") {
  const auto instance_path = temp_path("-instance.json");
  const auto solution_path = temp_path("-solution.json");
  const auto log_path = temp_path("-stdout.txt");
  const auto instance = kdc::test::make_dummy_instance(8, 3, 9U);
  kdc::DatasetReader::write_json(instance, instance_path.string());
  const std::string command =
      shell_quote(KDC_SOLVER_EXECUTABLE) + " solve --instance " +
      shell_quote(instance_path.string()) + " --mode minmax --algorithm nn "
      "--output " +
      shell_quote(solution_path.string()) + " >" + shell_quote(log_path.string()) +
      " 2>&1";

  REQUIRE(std::system(command.c_str()) == 0);
  REQUIRE(std::filesystem::exists(solution_path));
  const auto loaded =
      kdc::SolutionSerializer::load_json(instance, solution_path.string());
  REQUIRE(loaded.is_well_formed());
  std::filesystem::remove(instance_path);
  std::filesystem::remove(solution_path);
  std::filesystem::remove(log_path);
}

TEST_CASE("CLI: unknown algorithm reports available solvers") {
  const auto instance_path = temp_path("-unknown-instance.json");
  const auto log_path = temp_path("-unknown-output.txt");
  const auto instance = kdc::test::make_dummy_instance(3, 2, 4U);
  kdc::DatasetReader::write_json(instance, instance_path.string());
  const std::string command =
      shell_quote(KDC_SOLVER_EXECUTABLE) + " solve --instance " +
      shell_quote(instance_path.string()) +
      " --algorithm bogus >" + shell_quote(log_path.string()) + " 2>&1";

  REQUIRE(std::system(command.c_str()) != 0);
  std::ifstream output(log_path);
  const std::string text((std::istreambuf_iterator<char>(output)),
                         std::istreambuf_iterator<char>());
  REQUIRE(text.find("unknown algorithm 'bogus'") != std::string::npos);
  REQUIRE(text.find("nn") != std::string::npos);
  std::filesystem::remove(instance_path);
  std::filesystem::remove(log_path);
}

TEST_CASE("CLI help advertises temporal solver selection") {
  const auto log_path = temp_path("-help.txt");
  const std::string command =
      shell_quote(KDC_SOLVER_EXECUTABLE) + " benchmark --help >" +
      shell_quote(log_path.string()) + " 2>&1";
  REQUIRE(std::system(command.c_str()) == 0);
  const std::string output = [&] {
    std::ifstream input(log_path);
    return std::string(std::istreambuf_iterator<char>(input),
                       std::istreambuf_iterator<char>());
  }();
  REQUIRE(output.find("temporal-interval-bnb") != std::string::npos);
  REQUIRE(output.find("b-grid") != std::string::npos);
  REQUIRE(output.find("b-nn") != std::string::npos);
  REQUIRE(output.find("b-static-hold") != std::string::npos);
  REQUIRE(output.find("b-minmax") != std::string::npos);
  REQUIRE(output.find("--oracle-time-limit") != std::string::npos);
  std::filesystem::remove(log_path);
}

TEST_CASE("CLI parses and resolves the temporal solver method") {
  const auto root = temp_path("-temporal-run");
  const auto dataset = root / "dataset";
  const auto output_dir = root / "output";
  std::filesystem::create_directories(dataset);
  kdc::DatasetReader::write_json(
      kdc::test::make_dummy_instance(3, 2, 17U),
      (dataset / "tiny.json").string());
  const auto log_path = root / "stdout.txt";
  const std::string command =
      shell_quote(KDC_SOLVER_EXECUTABLE) +
      " benchmark --dataset " + shell_quote(dataset.string()) +
      " --output " + shell_quote(output_dir.string()) +
      " --mode minsum --profile fast --solver-method "
      "temporal-interval-bnb --fast-time-limit 2 "
      "--oracle-time-limit 1 --max-leaves 2 --delta-min 0.1 >" +
      shell_quote(log_path.string()) + " 2>&1";
  REQUIRE(std::system(command.c_str()) == 0);

  std::ifstream result_input(output_dir / "json" / "benchmark.json");
  const auto results = nlohmann::json::parse(result_input);
  REQUIRE(results.size() == 1U);
  REQUIRE(results.at(0).at("solver_method") == "temporal-interval-bnb");
  REQUIRE(results.at(0).at("temporal_result_v2").at("schema_version") == 2);
  REQUIRE(results.at(0).at("configuration").at("global_time_limit_sec") ==
          2.0);
  REQUIRE(results.at(0).at("configuration").at("oracle_time_limit_sec") ==
          1.0);
  std::ifstream manifest_input(output_dir / "experiment_manifest.json");
  const auto manifest = nlohmann::json::parse(manifest_input);
  REQUIRE(manifest.at("resolved_solver_method") == "temporal-interval-bnb");
  std::filesystem::remove_all(root);
}

TEST_CASE("CLI dispatches B-nn through the benchmark pipeline") {
  const auto root = temp_path("-b-nn-run");
  const auto dataset = root / "dataset";
  const auto output_dir = root / "output";
  std::filesystem::create_directories(dataset);
  kdc::DatasetReader::write_json(
      kdc::test::make_dummy_instance(3, 2, 19U),
      (dataset / "tiny.json").string());
  const auto log_path = root / "stdout.txt";
  const std::string command =
      shell_quote(KDC_SOLVER_EXECUTABLE) +
      " benchmark --dataset " + shell_quote(dataset.string()) +
      " --output " + shell_quote(output_dir.string()) +
      " --mode minsum --profile fast --solver-method b-nn "
      "--fast-time-limit 2 --oracle-time-limit 1 >" +
      shell_quote(log_path.string()) + " 2>&1";
  REQUIRE(std::system(command.c_str()) == 0);

  std::ifstream result_input(output_dir / "json" / "benchmark.json");
  const auto results = nlohmann::json::parse(result_input);
  REQUIRE(results.size() == 1U);
  REQUIRE(results.dump().find("NaN") == std::string::npos);
  REQUIRE(results.dump().find("Infinity") == std::string::npos);
  REQUIRE(results.at(0).at("solver_method") == "b-nn");
  REQUIRE(results.at(0).at("temporal_baseline_result_v1")
              .at("schema_version") == 1);
  REQUIRE(results.at(0).at("temporal_baseline_result_v1")
              .at("baseline") == "B-nn");
  REQUIRE(results.at(0).at("configuration").at("resolved_solver_method") ==
          "b-nn");
  std::ifstream manifest_input(output_dir / "experiment_manifest.json");
  const auto manifest = nlohmann::json::parse(manifest_input);
  REQUIRE(manifest.at("resolved_solver_method") == "b-nn");
  std::filesystem::remove_all(root);
}
