#include "test_utils.hpp"

#include "kdc/algorithms/nn_static_solver.hpp"
#include "kdc/minmax.hpp"
#include "kdc/minsum.hpp"
#include "kdc/solution_serializer.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <nlohmann/json.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>

namespace {
std::filesystem::path temporary_path(const std::string& suffix) {
  const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
  return std::filesystem::temp_directory_path() /
         ("kdc-solution-serializer-" + std::to_string(stamp) + suffix);
}
}

TEST_CASE("SolutionSerializer: round-trip minmax solution") {
  const auto instance = kdc::test::make_dummy_instance(20, 5, 7U);
  kdc::NNStaticSolver nn;
  const auto result =
      kdc::MinMaxSolver::solve(instance, nn, kdc::MinMaxSolver::Config{});
  const auto path = temporary_path(".json");

  kdc::SolutionSerializer::save_json(instance, result.solution, path.string());
  const auto loaded = kdc::SolutionSerializer::load_json(instance, path.string());

  REQUIRE(loaded.intervals.size() == result.solution.intervals.size());
  REQUIRE(kdc::test::near(loaded.peak_cost(), result.solution.peak_cost(),
                          1e-6));
  REQUIRE(kdc::test::near(loaded.total_integral(),
                          result.solution.total_integral(), 1e-6));
  std::filesystem::remove(path);
}

TEST_CASE("SolutionSerializer: round-trip minsum solution") {
  const auto instance = kdc::test::make_dummy_instance(15, 4, 3U);
  kdc::NNStaticSolver nn;
  const auto result =
      kdc::MinSumSolver::solve(instance, nn, kdc::MinSumSolver::Config{});
  const auto path = temporary_path("-minsum.json");

  kdc::SolutionSerializer::save_json(instance, result.solution, path.string());
  const auto loaded = kdc::SolutionSerializer::load_json(instance, path.string());

  REQUIRE(kdc::test::near(loaded.total_integral(),
                          result.solution.total_integral(), 1e-6));
  std::filesystem::remove(path);
}

TEST_CASE("SolutionSerializer: rejects non-well-formed solution") {
  const auto instance = kdc::test::make_dummy_instance(10, 3, 1U);
  const kdc::KineticSolution bad;
  REQUIRE_THROWS_AS(kdc::SolutionSerializer::save_json(
                        instance, bad, temporary_path("-empty.json").string()),
                    std::runtime_error);
}

TEST_CASE("SolutionSerializer: rejects mismatched instance dimensions") {
  const auto instance_a = kdc::test::make_dummy_instance(20, 5, 7U);
  const auto instance_b = kdc::test::make_dummy_instance(30, 6, 7U);
  kdc::NNStaticSolver nn;
  const auto result =
      kdc::MinMaxSolver::solve(instance_a, nn, kdc::MinMaxSolver::Config{});
  const auto path = temporary_path("-mismatch.json");

  kdc::SolutionSerializer::save_json(instance_a, result.solution, path.string());
  REQUIRE_THROWS_AS(
      kdc::SolutionSerializer::load_json(instance_b, path.string()),
      std::runtime_error);
  std::filesystem::remove(path);
}

TEST_CASE("SolutionSerializer: rejects invalid JSON") {
  const auto instance = kdc::test::make_dummy_instance(10, 3, 1U);
  const auto path = temporary_path("-bad.json");
  {
    std::ofstream output(path);
    output << "{ this is not json";
  }
  REQUIRE_THROWS_AS(
      kdc::SolutionSerializer::load_json(instance, path.string()),
      std::runtime_error);
  std::filesystem::remove(path);
}

TEST_CASE("SolutionSerializer: validation catches invalid support index") {
  const auto instance = kdc::test::make_dummy_instance(10, 3, 1U);
  kdc::NNStaticSolver nn;
  auto result =
      kdc::MinMaxSolver::solve(instance, nn, kdc::MinMaxSolver::Config{});
  result.solution.intervals.front().supporting_point.front() = 999;
  std::string error;

  REQUIRE_FALSE(kdc::SolutionSerializer::validate(instance, result.solution,
                                                    &error));
  REQUIRE(error.find("out of range") != std::string::npos);
}

TEST_CASE("SolutionSerializer: detects summary mismatch") {
  const auto instance = kdc::test::make_dummy_instance(10, 3, 1U);
  kdc::NNStaticSolver nn;
  const auto result =
      kdc::MinMaxSolver::solve(instance, nn, kdc::MinMaxSolver::Config{});
  const auto path = temporary_path("-summary.json");
  kdc::SolutionSerializer::save_json(instance, result.solution, path.string());
  {
    std::ifstream input(path);
    auto document = nlohmann::json::parse(input);
    document["summary"]["peak_cost"] = 999999.0;
    std::ofstream output(path);
    output << document.dump(4);
  }

  REQUIRE_THROWS_WITH(
      kdc::SolutionSerializer::load_json(instance, path.string()),
      Catch::Matchers::ContainsSubstring("summary mismatch"));
  std::filesystem::remove(path);
}

TEST_CASE("SolutionSerializer: writes human-readable JSON") {
  const auto instance = kdc::test::make_dummy_instance(10, 3, 8U);
  kdc::NNStaticSolver nn;
  const auto result =
      kdc::MinMaxSolver::solve(instance, nn, kdc::MinMaxSolver::Config{});
  const auto path = temporary_path("-formatted.json");
  kdc::SolutionSerializer::save_json(instance, result.solution, path.string());
  std::ifstream input(path);
  const std::string contents((std::istreambuf_iterator<char>(input)),
                             std::istreambuf_iterator<char>());

  REQUIRE(contents.find("\n    \"instance\"") != std::string::npos);
  std::filesystem::remove(path);
}
