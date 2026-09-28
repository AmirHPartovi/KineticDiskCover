#include "kdc/benchmark.hpp"

#include "kdc/io.hpp"
#include "kdc/kont_solver.hpp"
#include "kdc/logging.hpp"
#include "kdc/thread_pool.hpp"
#include "kdc/verify.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <future>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <utility>

#if defined(__unix__) || defined(__APPLE__)
#include <sys/resource.h>
#include <sys/time.h>
#endif

namespace kdc {
namespace {
using Json = nlohmann::json;
using Clock = std::chrono::steady_clock;

class TimedSolver final : public ILPSolver {
 public:
  explicit TimedSolver(ILPSolver& inner) : inner_(inner) {}

  ILPResult solve(const Eigen::VectorXd& costs,
                  const Eigen::SparseMatrix<double>& constraints,
                  const Eigen::VectorXd& rhs,
                  const std::vector<int>& integer_vars,
                  double time_limit_sec, double gap_target) override {
    const auto start = Clock::now();
    ILPResult result = inner_.solve(costs, constraints, rhs, integer_vars,
                                    time_limit_sec, gap_target);
    elapsed_sec_ += std::chrono::duration<double>(Clock::now() - start).count();
    return result;
  }

  std::string name() const override { return inner_.name(); }
  double elapsed_sec() const noexcept { return elapsed_sec_; }

 private:
  ILPSolver& inner_;
  double elapsed_sec_{0.0};
};

std::string timestamp_now() {
  const auto now = std::chrono::system_clock::now();
  const std::time_t time = std::chrono::system_clock::to_time_t(now);
  std::tm local_time{};
#if defined(_WIN32)
  localtime_s(&local_time, &time);
#else
  localtime_r(&time, &local_time);
#endif
  std::ostringstream output;
  output << std::put_time(&local_time, "%Y-%m-%dT%H:%M:%S");
  return output.str();
}

std::string csv_escape(const std::string& value) {
  if (value.find_first_of(",\"\r\n") == std::string::npos) {
    return value;
  }
  std::string escaped{"\""};
  for (const char character : value) {
    if (character == '"') {
      escaped += "\"\"";
    } else {
      escaped += character;
    }
  }
  escaped += '"';
  return escaped;
}

void ensure_parent_directory(const std::string& file_path) {
  const std::filesystem::path path(file_path);
  if (path.has_parent_path()) {
    std::filesystem::create_directories(path.parent_path());
  }
}

Json result_to_json(const BenchmarkResult& result) {
  return Json{{"instance_name", result.instance_name},
              {"n", result.n},
              {"m", result.m},
              {"objective", to_string(result.objective)},
              {"wall_time_sec", result.wall_time_sec},
              {"cpu_time_sec", result.cpu_time_sec},
              {"ip_time_sec", result.ip_time_sec},
              {"peak_memory_mb", result.peak_memory_mb},
              {"objective_value", result.objective_value},
              {"lower_bound", result.lower_bound},
              {"gap", result.gap},
              {"num_ip_solves", result.num_ip_solves},
              {"num_iterations", result.num_iterations},
              {"verified", result.verified},
              {"timestamp", result.timestamp}};
}

BenchmarkResult result_from_json(const Json& json) {
  BenchmarkResult result;
  result.instance_name = json.at("instance_name").get<std::string>();
  result.n = json.at("n").get<int>();
  result.m = json.at("m").get<int>();
  result.objective = objective_from_string(json.at("objective").get<std::string>());
  result.wall_time_sec = json.at("wall_time_sec").get<double>();
  result.cpu_time_sec = json.at("cpu_time_sec").get<double>();
  result.ip_time_sec = json.at("ip_time_sec").get<double>();
  result.peak_memory_mb = json.at("peak_memory_mb").get<double>();
  result.objective_value = json.at("objective_value").get<double>();
  result.lower_bound = json.at("lower_bound").get<double>();
  result.gap = json.at("gap").get<double>();
  result.num_ip_solves = json.at("num_ip_solves").get<int>();
  result.num_iterations = json.at("num_iterations").get<int>();
  result.verified = json.at("verified").get<bool>();
  result.timestamp = json.at("timestamp").get<std::string>();
  return result;
}
}

BenchmarkResult BenchmarkRunner::run_single(const Instance& instance,
                                            ILPSolver& solver,
                                            ObjectiveType objective,
                                            const BenchmarkConfig& config) {
  if (instance.n < 0 || instance.m < 0 ||
      static_cast<Index>(instance.n) != instance.trajectories.size() ||
      static_cast<Index>(instance.m) != instance.stations.size() ||
      config.num_repeats <= 0) {
    throw std::invalid_argument("benchmark received invalid instance/config");
  }
  const auto wall_start = Clock::now();
  const std::clock_t cpu_start = std::clock();
  const double memory_before =
      config.measure_memory ? get_peak_memory_mb() : 0.0;
  TimedSolver timed_solver(solver);

  BenchmarkResult benchmark;
  KineticSolution computed_solution;
  benchmark.instance_name =
      instance.name.empty() ? std::to_string(instance.id) : instance.name;
  benchmark.n = instance.n;
  benchmark.m = instance.m;
  benchmark.objective = objective;
  if (objective == ObjectiveType::MIN_MAX) {
    auto solver_config = config.minmax_cfg;
    solver_config.verify_after = config.verify_after;
    const auto result =
        MinMaxSolver::solve(instance, timed_solver, solver_config);
    benchmark.objective_value = result.peak_cost;
    benchmark.lower_bound = result.lower_bound;
    benchmark.gap = result.gap;
    benchmark.num_ip_solves = result.num_ip_solves;
    benchmark.num_iterations = result.num_iterations;
    benchmark.verified = result.verified;
    computed_solution = result.solution;
  } else {
    auto solver_config = config.minsum_cfg;
    solver_config.verify_after = config.verify_after;
    const auto result =
        MinSumSolver::solve(instance, timed_solver, solver_config);
    benchmark.objective_value = result.total_integral;
    benchmark.lower_bound = result.lower_bound_integral;
    benchmark.gap = result.gap;
    benchmark.num_ip_solves = result.num_ip_solves;
    benchmark.num_iterations = result.num_iterations;
    benchmark.verified = result.verified;
    computed_solution = result.solution;
  }
  if (config.verify_after) {
    const VerificationReport report =
        Verifier::verify(instance, computed_solution);
    benchmark.verified = report.all_ok();
    if (!benchmark.verified) {
      throw std::runtime_error("benchmark result verification failed: " +
                               report.errors.front());
    }
  }

  benchmark.ip_time_sec = timed_solver.elapsed_sec();
  benchmark.cpu_time_sec =
      static_cast<double>(std::clock() - cpu_start) /
      static_cast<double>(CLOCKS_PER_SEC);
  benchmark.wall_time_sec =
      std::chrono::duration<double>(Clock::now() - wall_start).count();
  benchmark.peak_memory_mb =
      config.measure_memory ? std::max(memory_before, get_peak_memory_mb())
                            : 0.0;
  benchmark.timestamp = timestamp_now();
  return benchmark;
}

void BenchmarkRunner::run_all(const BenchmarkConfig& config) {
  if (config.num_repeats <= 0) {
    throw std::invalid_argument("benchmark repeat count must be positive");
  }
  if (config.solver_name != "KONT") {
    throw std::invalid_argument("unsupported benchmark solver: " +
                                config.solver_name);
  }
  const std::filesystem::path dataset_path(config.dataset_dir);
  if (!std::filesystem::is_directory(dataset_path)) {
    throw std::runtime_error("benchmark dataset directory does not exist: " +
                             config.dataset_dir);
  }

  std::vector<std::filesystem::path> files;
  for (const auto& entry : std::filesystem::directory_iterator(dataset_path)) {
    if (entry.is_regular_file() && entry.path().extension() == ".json") {
      files.push_back(entry.path());
    }
  }
  std::sort(files.begin(), files.end());

  std::vector<Instance> instances;
  for (const auto& file : files) {
    const Instance instance = DatasetReader::read_json(file.string());
    if (!config.instance_names.empty() &&
        std::find(config.instance_names.begin(), config.instance_names.end(),
                  file.stem().string()) == config.instance_names.end() &&
        std::find(config.instance_names.begin(), config.instance_names.end(),
                  instance.name) == config.instance_names.end()) {
      continue;
    }
    instances.push_back(instance);
  }
  if (instances.empty()) {
    throw std::runtime_error("no matching JSON instances found in " +
                             config.dataset_dir);
  }

  const std::vector<ObjectiveType> objectives =
      config.both_objectives
          ? std::vector<ObjectiveType>{ObjectiveType::MIN_MAX,
                                       ObjectiveType::MIN_SUM}
          : std::vector<ObjectiveType>{config.objective};
  std::vector<BenchmarkResult> results;
  if (config.parallel) {
    ThreadPool pool(config.num_threads);
    std::vector<std::future<std::vector<BenchmarkResult>>> futures;
    futures.reserve(instances.size());
    for (std::size_t instance_index = 0; instance_index < instances.size();
         ++instance_index) {
      futures.push_back(pool.submit([&, instance_index]() {
        auto solver = std::make_unique<KontSolver>();
        std::vector<BenchmarkResult> local;
        local.reserve(objectives.size() *
                      static_cast<std::size_t>(config.num_repeats));
        for (const auto objective : objectives) {
          for (int repeat = 0; repeat < config.num_repeats; ++repeat) {
            local.push_back(
                run_single(instances[instance_index], *solver, objective,
                           config));
          }
        }
        return local;
      }));
    }
    for (auto& future : futures) {
      auto local = future.get();
      results.insert(results.end(), local.begin(), local.end());
    }
    pool.wait_idle();
  } else {
    KontSolver solver;
    for (const auto& instance : instances) {
      for (const auto objective : objectives) {
        for (int repeat = 0; repeat < config.num_repeats; ++repeat) {
          results.push_back(run_single(instance, solver, objective, config));
        }
      }
    }
  }
  std::sort(results.begin(), results.end(),
            [](const BenchmarkResult& lhs, const BenchmarkResult& rhs) {
              if (lhs.instance_name != rhs.instance_name) {
                return lhs.instance_name < rhs.instance_name;
              }
              return lhs.objective < rhs.objective;
            });

  const std::filesystem::path output_dir(config.output_dir);
  save_json(results, (output_dir / "json" / "benchmark.json").string());
  save_csv(results, (output_dir / "csv" / "benchmark.csv").string());
  LOG_INFO("Benchmark: {} results saved.", results.size());
}

void BenchmarkRunner::save_json(const std::vector<BenchmarkResult>& results,
                                const std::string& path) {
  ensure_parent_directory(path);
  std::ofstream output(path);
  if (!output) {
    throw std::runtime_error("cannot open benchmark JSON output: " + path);
  }
  Json serialized = Json::array();
  for (const auto& result : results) {
    serialized.push_back(result_to_json(result));
  }
  output << std::setw(2) << serialized << '\n';
  if (!output) {
    throw std::runtime_error("failed writing benchmark JSON output: " + path);
  }
}

std::vector<BenchmarkResult> BenchmarkRunner::load_json(
    const std::string& path) {
  std::ifstream input(path);
  if (!input) {
    throw std::runtime_error("cannot open benchmark JSON input: " + path);
  }
  Json serialized;
  try {
    input >> serialized;
    if (!serialized.is_array()) {
      throw std::runtime_error("benchmark JSON root must be an array");
    }
    std::vector<BenchmarkResult> results;
    results.reserve(serialized.size());
    for (const auto& item : serialized) {
      results.push_back(result_from_json(item));
    }
    return results;
  } catch (const Json::exception& error) {
    throw std::runtime_error(std::string("invalid benchmark JSON: ") +
                             error.what());
  }
}

void BenchmarkRunner::save_csv(const std::vector<BenchmarkResult>& results,
                               const std::string& path) {
  ensure_parent_directory(path);
  std::ofstream output(path);
  if (!output) {
    throw std::runtime_error("cannot open benchmark CSV output: " + path);
  }
  output << "instance_name,n,m,objective,wall_time_sec,cpu_time_sec,"
            "ip_time_sec,peak_memory_mb,objective_value,lower_bound,gap,"
            "num_ip_solves,num_iterations,verified,timestamp\n";
  for (const auto& result : results) {
    output << csv_escape(result.instance_name) << ',' << result.n << ','
           << result.m << ',' << to_string(result.objective) << ','
           << result.wall_time_sec << ',' << result.cpu_time_sec << ','
           << result.ip_time_sec << ',' << result.peak_memory_mb << ','
           << result.objective_value << ',' << result.lower_bound << ','
           << result.gap << ',' << result.num_ip_solves << ','
           << result.num_iterations << ','
           << (result.verified ? "true" : "false") << ','
           << csv_escape(result.timestamp) << '\n';
  }
  if (!output) {
    throw std::runtime_error("failed writing benchmark CSV output: " + path);
  }
}

double BenchmarkRunner::get_peak_memory_mb() {
#if defined(__unix__) || defined(__APPLE__)
  struct rusage usage {};
  if (getrusage(RUSAGE_SELF, &usage) != 0) {
    throw std::runtime_error("getrusage failed while measuring peak memory");
  }
#if defined(__APPLE__)
  return static_cast<double>(usage.ru_maxrss) / (1024.0 * 1024.0);
#else
  return static_cast<double>(usage.ru_maxrss) / 1024.0;
#endif
#else
  return 0.0;
#endif
}
}
