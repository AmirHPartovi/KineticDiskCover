#include "test_utils.hpp"

#include "kdc/benchmark.hpp"
#include "kdc/io.hpp"
#include "kdc/thread_pool.hpp"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <future>
#include <string>
#include <thread>
#include <vector>

namespace {
std::filesystem::path temporary_directory(const std::string& prefix) {
  const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
  return std::filesystem::temp_directory_path() /
         (prefix + std::to_string(stamp));
}

void create_dataset(const std::filesystem::path& path) {
  std::filesystem::create_directories(path);
  for (const unsigned seed : {1U, 2U, 3U}) {
    const auto instance = kdc::test::make_dummy_instance(15, 4, seed);
    kdc::DatasetReader::write_json(
        instance, (path / (instance.name + ".json")).string());
  }
}
}

TEST_CASE("Parallel benchmark: sequential and concurrent objectives agree") {
  const auto root = temporary_directory("kdc-parallel-benchmark-");
  const auto dataset = root / "dataset";
  create_dataset(dataset);

  kdc::BenchmarkConfig sequential;
  sequential.dataset_dir = dataset.string();
  sequential.output_dir = (root / "sequential").string();
  sequential.measure_memory = false;
  sequential.objective = kdc::ObjectiveType::MIN_MAX;
  kdc::BenchmarkRunner::run_all(sequential);

  kdc::BenchmarkConfig parallel = sequential;
  parallel.output_dir = (root / "parallel").string();
  parallel.parallel = true;
  parallel.num_threads = 2;
  kdc::BenchmarkRunner::run_all(parallel);

  const auto sequential_results = kdc::BenchmarkRunner::load_json(
      (root / "sequential/json/benchmark.json").string());
  const auto parallel_results = kdc::BenchmarkRunner::load_json(
      (root / "parallel/json/benchmark.json").string());
  REQUIRE(parallel_results.size() == sequential_results.size());
  for (std::size_t index = 0; index < sequential_results.size(); ++index) {
    REQUIRE(parallel_results[index].instance_name ==
            sequential_results[index].instance_name);
    REQUIRE(kdc::test::near(parallel_results[index].objective_value,
                            sequential_results[index].objective_value, 1e-6));
    REQUIRE(kdc::test::near(parallel_results[index].gap,
                            sequential_results[index].gap, 1e-6));
  }
  std::filesystem::remove_all(root);
}

TEST_CASE("Parallel benchmark: thread pool respects configured concurrency") {
  kdc::ThreadPool pool(3);
  std::atomic<int> current{0};
  std::atomic<int> peak{0};
  std::vector<std::future<void>> futures;
  for (int task = 0; task < 30; ++task) {
    futures.push_back(pool.submit([&current, &peak]() {
      const int running = current.fetch_add(1) + 1;
      int observed_peak = peak.load();
      while (running > observed_peak &&
             !peak.compare_exchange_weak(observed_peak, running)) {
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
      current.fetch_sub(1);
    }));
  }
  for (auto& future : futures) {
    future.get();
  }
  REQUIRE(peak.load() <= 3);
}

TEST_CASE("Parallel benchmark: CLI flags activate concurrent mode") {
  const auto root = temporary_directory("kdc-parallel-cli-");
  const auto dataset = root / "dataset";
  create_dataset(dataset);
  const auto output_dir = root / "results";
  const std::string command =
      std::string("\"") + KDC_SOLVER_EXECUTABLE +
      "\" benchmark --dataset \"" + dataset.string() + "\" --output \"" +
      output_dir.string() +
      "\" --mode minmax --parallel --threads 2 >/dev/null 2>&1";

  REQUIRE(std::system(command.c_str()) == 0);
  REQUIRE(std::filesystem::exists(output_dir / "json/benchmark.json"));
  const auto results = kdc::BenchmarkRunner::load_json(
      (output_dir / "json/benchmark.json").string());
  REQUIRE(results.size() == 3U);
  std::filesystem::remove_all(root);
}
