#include "kdc/batch_runner.hpp"

#include "kdc/benchmark_protocol.hpp"
#include "kdc/benchmark.hpp"
#include "kdc/candidate.hpp"
#include "kdc/exact_reference_selector.hpp"
#include "kdc/io.hpp"
#include "kdc/kont_solver.hpp"
#include "kdc/logging.hpp"
#include "kdc/minmax.hpp"
#include "kdc/minsum.hpp"
#include "kdc/profiling.hpp"
#include "kdc/solution_serializer.hpp"
#include "kdc/static_solver_registry.hpp"
#include "kdc/thread_pool.hpp"
#include "kdc/trace.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <future>
#include <iomanip>
#include <limits>
#include <map>
#include <memory>
#include <numeric>
#include <sstream>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <spdlog/spdlog.h>

namespace kdc {
namespace {
using Json = nlohmann::json;
using Clock = std::chrono::steady_clock;

unsigned run_seed(unsigned base, const std::string& instance,
                  const std::string& algorithm, const std::string& objective,
                  int repeat) {
  std::uint32_t hash = 2166136261U ^ base;
  const auto append = [&hash](const std::string& text) {
    for (const unsigned char character : text) {
      hash = (hash ^ character) * 16777619U;
    }
    hash = (hash ^ 0xffU) * 16777619U;
  };
  append(instance);
  append(algorithm);
  append(objective);
  append(std::to_string(repeat));
  return hash;
}

std::string experiment_timestamp() {
  const auto now = std::chrono::system_clock::now();
  const auto value = std::chrono::system_clock::to_time_t(now);
  std::tm utc{};
#if defined(_WIN32)
  gmtime_s(&utc, &value);
#else
  gmtime_r(&value, &utc);
#endif
  std::ostringstream output;
  output << std::put_time(&utc, "%Y%m%dT%H%M%SZ");
  return output.str();
}

std::string experiment_id_for(const BatchRunConfig& config) {
  std::ostringstream output;
  output << "kdc-" << experiment_timestamp() << "-seed" << config.seed << '-'
         << std::chrono::steady_clock::now().time_since_epoch().count();
  return output.str();
}

class ScopedLogLevel {
 public:
  explicit ScopedLogLevel(spdlog::level::level_enum level)
      : previous_(spdlog::get_level()) {
    spdlog::set_level(level);
  }
  ~ScopedLogLevel() { spdlog::set_level(previous_); }

 private:
  spdlog::level::level_enum previous_;
};

Json record_json(const BatchRunRecord& record) {
  return Json{{"schema_version", 2},
              {"instance_name", record.instance_name},
              {"algorithm_name", record.algorithm_name},
              {"algorithm_category", record.algorithm_category},
              {"requested_backend", record.requested_backend},
              {"actual_backend", record.actual_backend},
              {"objective", record.objective},
              {"repeat", record.repeat},
              {"n", record.n},
              {"m", record.m},
              {"wall_time_sec", record.wall_time_sec},
              {"solve_time_sec", record.solve_time_sec},
              {"peak_cost", record.peak_cost},
              {"integral_cost", record.integral_cost},
              {"empirical_ratio_to_exact",
               record.empirical_ratio_to_exact
                   ? Json(*record.empirical_ratio_to_exact)
                   : Json(nullptr)},
              {"ratio_to_incumbent",
               record.ratio_to_incumbent ? Json(*record.ratio_to_incumbent)
                                         : Json(nullptr)},
              {"cpu_time_sec", record.cpu_time_sec},
              {"peak_memory_mb", record.peak_memory_mb},
              {"time_limit_per_ip_sec", record.time_limit_per_ip_sec},
              {"objective_value", record.objective_value},
              {"lower_bound", record.lower_bound},
              {"bound_status", bound_status_to_string(record.bound_status)},
              {"upper_bound", std::isfinite(record.upper_bound)
                                  ? Json(record.upper_bound)
                                  : Json(nullptr)},
              {"certified_gap", record.certified_gap.has_value()
                                    && record.exact_solver && record.feasible &&
                                            record.bound_status ==
                                                BoundStatus::CERTIFIED
                                    ? Json(*record.certified_gap)
                                    : Json(nullptr)},
              {"optimality_status",
               optimality_status_to_string(
                   record.time_limited ? OptimalityStatus::TIME_LIMIT
                   : (!record.exact_solver &&
                              record.optimality_status ==
                                  OptimalityStatus::OPTIMAL
                          ? OptimalityStatus::FEASIBLE
                          : record.optimality_status))},
              {"minsum_refinement_policy",
               minsum_refinement_policy_to_string(
                   record.minsum_refinement_policy)},
              {"exact_solver", record.exact_solver},
              {"certified_lower_bound", record.certified_lower_bound},
              {"heuristic_lower_bound", record.heuristic_lower_bound},
              {"gap", record.gap},
              {"num_iterations", record.num_iterations},
              {"num_ip_solves", record.num_ip_solves},
              {"verified", record.verified},
              {"verification_kind",
               verification_kind_to_string(record.verification_kind)},
              {"verification_time_sec", record.verification_time_sec},
              {"serialization_time_sec", record.serialization_time_sec},
              {"total_wall_time_sec", record.total_wall_time_sec},
              {"num_static_solves", record.num_ip_solves},
              {"seed", record.seed},
              {"global_time_limit_sec", record.global_time_limit_sec},
              {"per_static_time_limit_sec", record.time_limit_per_ip_sec},
              {"refinement_policy",
               minsum_refinement_policy_to_string(
                   record.minsum_refinement_policy)},
              {"verify_each_iteration", record.verify_each_iteration},
              {"verify_after", record.verify_after},
              {"handovers_enabled", record.handovers_enabled},
              {"candidate_count",
               record.candidate_count ? Json(*record.candidate_count)
                                      : Json(nullptr)},
              {"coverage_nnz", record.coverage_nnz
                                   ? Json(*record.coverage_nnz)
                                   : Json(nullptr)},
              {"feasible", record.feasible},
              {"time_limited", record.time_limited},
              {"timeout", record.timeout},
              {"failed", record.failed},
              {"error_message", record.error_message},
              {"git_commit", record.git_commit},
              {"compiler", record.compiler},
              {"build_type", record.build_type},
              {"thread_count", record.thread_count},
              {"experiment_id", record.experiment_id},
              {"configuration", record.configuration},
              {"solution_json_path", record.solution_json_path},
              {"trace_csv_path", record.trace_csv_path},
              {"result_json_path", record.result_json_path}};
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

std::string sanitize_path_component(const std::string& value) {
  std::string sanitized;
  sanitized.reserve(value.size());
  for (const char raw_character : value) {
    const auto character = static_cast<unsigned char>(raw_character);
    if (character == '/' || character == '\\' ||
        std::isspace(character) != 0) {
      sanitized.push_back('_');
    } else if (std::isalnum(character) != 0 || character == '-' ||
               character == '_' || character == '.') {
      sanitized.push_back(static_cast<char>(character));
    } else {
      sanitized.push_back('_');
    }
  }
  while (!sanitized.empty() && sanitized.front() == '.') {
    sanitized.erase(sanitized.begin());
  }
  return sanitized.empty() ? "unnamed" : sanitized;
}

void write_result(BatchRunRecord& record, bool track_time = true) {
  KDC_PROFILE_PHASE(ProfilePhase::SERIALIZATION);
  const std::filesystem::path path(record.result_json_path);
  std::filesystem::create_directories(path.parent_path());
  const auto serialization_started = Clock::now();
  Json document = record_json(record);
  std::string serialized = document.dump(2);
  if (track_time) {
    const double serialization_time =
        std::chrono::duration<double>(Clock::now() - serialization_started)
            .count();
    record.serialization_time_sec += serialization_time;
    record.total_wall_time_sec += serialization_time;
    record.wall_time_sec += serialization_time;
    document["serialization_time_sec"] = record.serialization_time_sec;
    document["total_wall_time_sec"] = record.total_wall_time_sec;
    document["wall_time_sec"] = record.wall_time_sec;
    serialized = document.dump(2);
  }
  std::ofstream output(path);
  if (!output) {
    throw std::runtime_error("cannot open batch result JSON: " + path.string());
  }
  output << serialized << '\n';
  if (!output) {
    throw std::runtime_error("failed writing batch result JSON: " +
                             path.string());
  }
}

std::string median_text(std::vector<double> values) {
  if (values.empty()) {
    return "n/a";
  }
  std::sort(values.begin(), values.end());
  const std::size_t middle = values.size() / 2U;
  const double median =
      values.size() % 2U == 0U
          ? (values[middle - 1U] + values[middle]) / 2.0
          : values[middle];
  std::ostringstream output;
  output << std::setprecision(6) << median;
  return output.str();
}

std::vector<std::string> csv_fields(const BatchRunRecord& record) {
  return {record.instance_name,
          record.algorithm_name,
          record.algorithm_category,
          record.requested_backend,
          record.actual_backend,
          record.objective,
          std::to_string(record.repeat),
          std::to_string(record.n),
          std::to_string(record.m),
          std::to_string(record.wall_time_sec),
          std::to_string(record.solve_time_sec),
          std::to_string(record.peak_cost),
          std::to_string(record.integral_cost),
          record.empirical_ratio_to_exact
              ? std::to_string(*record.empirical_ratio_to_exact)
              : "",
          record.ratio_to_incumbent
              ? std::to_string(*record.ratio_to_incumbent)
              : "",
          std::to_string(record.cpu_time_sec),
          std::to_string(record.peak_memory_mb),
          std::to_string(record.time_limit_per_ip_sec),
          std::to_string(record.objective_value),
          std::to_string(record.lower_bound),
          bound_status_to_string(record.bound_status),
          std::isfinite(record.upper_bound) ? std::to_string(record.upper_bound)
                                            : "",
          record.certified_gap.has_value() && record.exact_solver &&
                  record.feasible &&
                  record.bound_status == BoundStatus::CERTIFIED
              ? std::to_string(*record.certified_gap)
              : "",
          optimality_status_to_string(
              record.time_limited ? OptimalityStatus::TIME_LIMIT
              : (!record.exact_solver &&
                         record.optimality_status == OptimalityStatus::OPTIMAL
                     ? OptimalityStatus::FEASIBLE
                     : record.optimality_status)),
          minsum_refinement_policy_to_string(
              record.minsum_refinement_policy),
          record.exact_solver ? "true" : "false",
          std::to_string(record.gap),
          std::to_string(record.num_iterations),
          std::to_string(record.num_ip_solves),
          record.verified ? "true" : "false",
          verification_kind_to_string(record.verification_kind),
          std::to_string(record.verification_time_sec),
          std::to_string(record.serialization_time_sec),
          std::to_string(record.total_wall_time_sec),
          std::to_string(record.seed),
          std::to_string(record.global_time_limit_sec),
          std::to_string(record.time_limit_per_ip_sec),
          record.verify_each_iteration ? "true" : "false",
          record.verify_after ? "true" : "false",
          record.handovers_enabled ? "true" : "false",
          record.candidate_count ? std::to_string(*record.candidate_count) : "",
          record.coverage_nnz ? std::to_string(*record.coverage_nnz) : "",
          record.feasible ? "true" : "false",
          record.time_limited ? "true" : "false",
          record.timeout ? "true" : "false",
          record.failed ? "true" : "false",
          record.git_commit,
          record.compiler,
          record.build_type,
          std::to_string(record.thread_count),
          record.experiment_id,
          record.configuration.dump(),
          record.error_message};
}

BatchRunRecord failed_instance_record(const std::filesystem::path& path,
                                      const std::string& algorithm,
                                      ObjectiveType objective,
                                      const std::string& error,
                                      const BatchRunConfig& config,
                                      int repeat) {
  BatchRunRecord record;
  record.instance_name = path.stem().string();
  record.algorithm_name = algorithm;
  record.algorithm_category =
      algorithm == "ip-kont" || algorithm == "branch-and-bound"
          ? "exact_reference"
          : "heuristic";
  record.requested_backend = config.exact_reference;
  record.actual_backend = record.algorithm_category == "exact_reference"
                              ? config.actual_backend
                              : algorithm;
  record.objective = to_string(objective);
  record.repeat = repeat;
  record.failed = true;
  record.error_message = error;
  record.seed = run_seed(config.seed, record.instance_name, algorithm,
                         record.objective, repeat);
  record.global_time_limit_sec =
      record.algorithm_category == "exact_reference"
          ? config.exact_time_limit_sec
          : config.fast_time_limit_sec;
  record.verify_each_iteration = config.verify_each_iteration;
  record.verify_after = config.verify_after;
  record.experiment_id = config.experiment_id;
  record.git_commit = KDC_GIT_COMMIT;
  record.compiler = __VERSION__;
  record.build_type = KDC_BUILD_TYPE;
  record.thread_count = config.parallel ? config.num_threads : 1;
  record.time_limit_per_ip_sec = config.per_ip_time_limit_sec;
  record.error_message = error;
  const std::filesystem::path run_dir =
      std::filesystem::path(config.output_dir) / "runs" /
      sanitize_path_component(record.instance_name) /
      sanitize_path_component(algorithm) /
      sanitize_path_component(record.objective);
  std::filesystem::create_directories(run_dir);
  if (config.save_solutions) {
    record.solution_json_path = (run_dir / "solution.json").string();
  }
  if (config.save_traces) {
    record.trace_csv_path = (run_dir / "trace.csv").string();
    try {
      TraceWriter::write_csv({}, record.trace_csv_path);
    } catch (const std::exception& trace_error) {
      LOG_ERROR("BatchRunner: unable to write empty failure trace: {}",
                trace_error.what());
    }
  }
  record.result_json_path = (run_dir / "result.json").string();
  write_result(record);
  LOG_ERROR("BatchRunner: failed loading {} / {} / {}: {}",
            record.instance_name, algorithm, record.objective, error);
  return record;
}

std::vector<std::string> default_benchmark_algorithms(
    const std::string& selected_exact_reference) {
  std::vector<std::string> algorithms = {"nn", "greedy", "primal-dual",
                                         "local-search", "sa", "genetic",
                                         "lp-rounding", "shifting"};
  if (std::find(algorithms.begin(), algorithms.end(),
                selected_exact_reference) ==
      algorithms.end()) {
    algorithms.push_back(selected_exact_reference);
  }
  return algorithms;
}

std::size_t algorithm_phase(const std::string& algorithm) {
  if (algorithm == "ip-kont" || algorithm == "branch-and-bound") {
    return 0U;
  }
  if (algorithm == "nn" || algorithm == "greedy") {
    return 1U;
  }
  return 2U;
}

void clear_previous_batch_output(const std::filesystem::path& output) {
  for (const char* artifact : {"runs", "master_results.json",
                               "master_results.csv", "batch_summary.md"}) {
    std::filesystem::remove_all(output / artifact);
  }
}
}  // namespace

void BatchRunner::run(const BatchRunConfig& config, ILPSolver* ilp) {
  ScopedLogLevel log_level(config.profile == BenchmarkProfile::DEBUG
                               ? spdlog::level::debug
                               : spdlog::level::warn);
  if ((config.exact_reference != "auto" &&
       config.exact_reference != "ip-kont" &&
       config.exact_reference != "branch-and-bound") ||
      !std::isfinite(config.per_ip_time_limit_sec) ||
      config.per_ip_time_limit_sec <= 0.0 ||
      !std::isfinite(config.fast_time_limit_sec) ||
      config.fast_time_limit_sec <= 0.0 ||
      !std::isfinite(config.exact_time_limit_sec) ||
      config.exact_time_limit_sec <= 0.0 ||
      !std::isfinite(config.gap_target) || config.gap_target < 0.0 ||
      config.gap_target >= 1.0 || config.num_threads <= 0) {
    throw std::invalid_argument("batch runner configuration is invalid");
  }
  if (config.repeats <= 0) {
    throw std::invalid_argument("batch runner repeat count must be positive");
  }
  if (config.objectives.empty()) {
    throw std::invalid_argument("batch runner requires at least one objective");
  }
  const bool explicitly_selects_both_exact_backends =
      std::find(config.algorithm_names.begin(), config.algorithm_names.end(),
                "ip-kont") != config.algorithm_names.end() &&
      std::find(config.algorithm_names.begin(), config.algorithm_names.end(),
                "branch-and-bound") != config.algorithm_names.end();
  if (explicitly_selects_both_exact_backends) {
    throw std::invalid_argument(
        "strict benchmark mode cannot select both ip-kont and "
        "branch-and-bound");
  }

  LOG_INFO("BatchRunner: scanning {} for instances", config.instances_dir);
  const std::filesystem::path instances_dir(config.instances_dir);
  if (!std::filesystem::is_directory(instances_dir)) {
    LOG_ERROR("BatchRunner: instances directory does not exist: {}",
              config.instances_dir);
    return;
  }

  std::vector<std::filesystem::path> instance_paths;
  for (const auto& entry :
       std::filesystem::recursive_directory_iterator(instances_dir)) {
    if (entry.is_regular_file() && entry.path().extension() == ".json") {
      instance_paths.push_back(entry.path());
    }
  }
  std::sort(instance_paths.begin(), instance_paths.end());
  if (instance_paths.empty()) {
    LOG_ERROR("BatchRunner: no JSON instances found in {}",
              config.instances_dir);
    return;
  }

  if (!config.strict_exact_reference && config.exact_reference == "auto") {
    throw std::invalid_argument(
        "benchmark fragments must reuse an explicit experiment exact-reference "
        "decision, not calibrate independently");
  }
  const auto exact_reference_decision =
      ExactReferenceSelector::resolve(config.exact_reference,
                                      config.instances_dir, config.output_dir);
  if (!exact_reference_decision.valid) {
    throw std::invalid_argument("exact-reference selection is invalid");
  }
  LOG_INFO("BatchRunner: exact reference selected '{}' (actual={}, "
           "requested={}, manifest={})",
           exact_reference_decision.selected_backend,
           exact_reference_decision.actual_backend,
           exact_reference_decision.requested_backend,
           exact_reference_decision.manifest_path);

  const auto registered = StaticSolverRegistry::list();
  std::vector<std::string> algorithms;
  const bool wants_default_selection =
      config.algorithm_names.empty() ||
      std::any_of(config.algorithm_names.begin(), config.algorithm_names.end(),
                  [](const std::string& value) {
                    return value == "all" || value == "ALL";
                  });

  const bool wants_fast =
      std::find(config.algorithm_names.begin(), config.algorithm_names.end(),
                "all-fast") != config.algorithm_names.end();
  const bool wants_comparison =
      std::find(config.algorithm_names.begin(), config.algorithm_names.end(),
                "all-comparison") != config.algorithm_names.end();
  if (config.profile == BenchmarkProfile::EXACT_REFERENCE) {
    algorithms = {exact_reference_decision.selected_backend};
  } else if (wants_default_selection || wants_fast || wants_comparison) {
    algorithms = default_benchmark_algorithms(
        exact_reference_decision.selected_backend);
  } else if (config.algorithm_names.empty()) {
    algorithms = default_benchmark_algorithms(
        exact_reference_decision.selected_backend);
  } else {
    for (const auto& requested : config.algorithm_names) {
      if (requested == "brute-force") {
        LOG_WARN("BatchRunner: ignoring validation-only algorithm '{}'",
                 requested);
        continue;
      }
      if (std::find(registered.begin(), registered.end(), requested) ==
          registered.end()) {
        LOG_WARN("BatchRunner: ignoring unregistered algorithm '{}'",
                 requested);
      } else if (std::find(algorithms.begin(), algorithms.end(), requested) ==
                 algorithms.end()) {
        algorithms.push_back(requested);
      }
    }
  }

  const auto exact_count = static_cast<int>(std::count_if(
      algorithms.begin(), algorithms.end(), [](const std::string& name) {
        return name == "ip-kont" || name == "branch-and-bound";
      }));
  if (exact_count > 1) {
    throw std::invalid_argument(
        "strict benchmark mode accepts exactly one exact backend; remove "
        "either ip-kont or branch-and-bound");
  }
  const auto exact_position = std::find_if(
      algorithms.begin(), algorithms.end(), [](const std::string& name) {
        return name == "ip-kont" || name == "branch-and-bound";
      });
  if (exact_position != algorithms.end() &&
      *exact_position != exact_reference_decision.selected_backend) {
    if (config.exact_reference == "ip-kont" &&
        exact_reference_decision.selected_backend == "branch-and-bound") {
      *exact_position = exact_reference_decision.selected_backend;
    } else {
      throw std::invalid_argument(
          "selected exact algorithm conflicts with the experiment manifest "
          "decision; set --exact-reference to the requested backend");
    }
  }
  if (config.strict_exact_reference && exact_position == algorithms.end()) {
    algorithms.push_back(exact_reference_decision.selected_backend);
  }
  if (wants_fast || wants_comparison || wants_default_selection) {
    algorithms = default_benchmark_algorithms(
        exact_reference_decision.selected_backend);
  }

  if (algorithms.empty()) {
    LOG_ERROR("BatchRunner: no registered algorithms selected");
  }
  std::stable_sort(algorithms.begin(), algorithms.end(),
                   [](const std::string& lhs, const std::string& rhs) {
                     return algorithm_phase(lhs) < algorithm_phase(rhs);
                   });
  BatchRunConfig run_config = config;
  if (run_config.profile == BenchmarkProfile::FAST) {
    run_config.minsum_refinement_policy =
        MinSumRefinementPolicy::HEURISTIC_ADAPTIVE;
    run_config.verify_after = true;
    run_config.verify_each_iteration = false;
    run_config.save_traces = false;
  } else if (run_config.profile == BenchmarkProfile::EXACT_REFERENCE) {
    run_config.minsum_refinement_policy =
        MinSumRefinementPolicy::CERTIFIED_BOUND;
    run_config.verify_after = true;
    run_config.verify_each_iteration = false;
  } else {
    run_config.minsum_refinement_policy =
        MinSumRefinementPolicy::CERTIFIED_BOUND;
    run_config.verify_after = true;
    run_config.verify_each_iteration = true;
  }
  run_config.actual_backend = exact_reference_decision.actual_backend;
  if (run_config.experiment_id.empty()) {
    run_config.experiment_id = experiment_id_for(run_config);
  }
  const std::filesystem::path output(config.output_dir);
  std::filesystem::create_directories(output);
  Json calibration = Json::object();
  const std::filesystem::path manifest_path =
      output / "experiment_manifest.json";
  if (std::filesystem::is_regular_file(manifest_path)) {
    std::ifstream prior(manifest_path);
    if (prior) {
      prior >> calibration;
    }
  }
  const std::string dataset_identity =
      calibration.value("dataset_fingerprint", std::string{});
  Json manifest = make_experiment_manifest(
      run_config.experiment_id, {}, config.instances_dir, dataset_identity,
      config.profile, algorithms, config.exact_reference,
      exact_reference_decision.actual_backend, calibration,
      config.fast_time_limit_sec, config.exact_time_limit_sec,
      config.per_ip_time_limit_sec, config.seed, config.repeats,
      minsum_refinement_policy_to_string(
          run_config.minsum_refinement_policy),
      run_config.verify_each_iteration, run_config.verify_after, true, "auto",
      config.parallel ? config.num_threads : 1);
  write_experiment_manifest(manifest_path.string(), manifest);

  struct WorkItem {
    std::filesystem::path instance_path;
    std::string algorithm;
    ObjectiveType objective;
    int repeat{0};
  };
  std::vector<std::vector<WorkItem>> phases(3U);
  for (const auto& path : instance_paths) {
    for (const auto& algorithm : algorithms) {
      for (const auto objective : config.objectives) {
        const bool exact = algorithm == "ip-kont" ||
                           algorithm == "branch-and-bound";
        const int repeat_count = exact ? 1 : config.repeats;
        for (int repeat = 0; repeat < repeat_count; ++repeat) {
          phases[algorithm_phase(algorithm)].push_back(
              {path, algorithm, objective, repeat});
        }
      }
    }
  }

  clear_previous_batch_output(output);

  std::vector<BatchRunRecord> records;
  std::size_t total_work = 0U;
  for (const auto& phase : phases) {
    total_work += phase.size();
  }
  records.reserve(total_work);
  for (const auto& phase : phases) {
    if (config.parallel && !phase.empty()) {
      ThreadPool pool(config.num_threads);
      std::vector<std::future<BatchRunRecord>> futures;
      futures.reserve(phase.size());
      for (const auto& item : phase) {
        futures.push_back(pool.submit([item, run_config]() {
          try {
            const Instance instance =
                DatasetReader::read_json(item.instance_path.string());
            KontSolver task_ilp;
            return BatchRunner::run_single(instance, item.algorithm,
                                           item.objective, &task_ilp, run_config,
                                           item.repeat);
          } catch (const std::exception& error) {
            return failed_instance_record(item.instance_path, item.algorithm,
                                          item.objective, error.what(), run_config,
                                          item.repeat);
          }
        }));
      }
      for (auto& future : futures) {
        records.push_back(future.get());
      }
      pool.wait_idle();
    } else {
      for (const auto& item : phase) {
        try {
          const Instance instance =
              DatasetReader::read_json(item.instance_path.string());
          records.push_back(run_single(instance, item.algorithm,
                                       item.objective, ilp, run_config,
                                       item.repeat));
        } catch (const std::exception& error) {
          records.push_back(failed_instance_record(
              item.instance_path, item.algorithm, item.objective, error.what(),
              run_config, item.repeat));
        }
      }
    }
  }

  std::sort(records.begin(), records.end(),
            [](const BatchRunRecord& lhs, const BatchRunRecord& rhs) {
              return std::tie(lhs.instance_name, lhs.algorithm_name,
                              lhs.objective) <
                     std::tie(rhs.instance_name, rhs.algorithm_name,
                              rhs.objective);
            });

  std::map<std::pair<std::string, std::string>, const BatchRunRecord*>
      exact_results;
  std::map<std::pair<std::string, std::string>, double> incumbents;
  for (const auto& record : records) {
    if (!record.feasible || !std::isfinite(record.objective_value)) {
      continue;
    }
    const auto key = std::make_pair(record.instance_name, record.objective);
    const auto incumbent = incumbents.find(key);
    if (incumbent == incumbents.end() ||
        record.objective_value < incumbent->second) {
      incumbents[key] = record.objective_value;
    }
    if (record.algorithm_category == "exact_reference" &&
        record.repeat == 0) {
      exact_results[key] = &record;
    }
  }
  for (auto& record : records) {
    if (!record.feasible || !std::isfinite(record.objective_value)) {
      continue;
    }
    const auto key = std::make_pair(record.instance_name, record.objective);
    const auto incumbent = incumbents.find(key);
    if (incumbent != incumbents.end()) {
      if (incumbent->second > 0.0) {
        record.ratio_to_incumbent =
            record.objective_value / incumbent->second;
      } else if (record.objective_value <= 1e-12) {
        record.ratio_to_incumbent = 1.0;
      }
    }
    const auto exact = exact_results.find(key);
    if (exact != exact_results.end() &&
        exact->second->optimality_status == OptimalityStatus::OPTIMAL &&
        exact->second->feasible &&
        std::isfinite(exact->second->objective_value)) {
      const double exact_value = exact->second->objective_value;
      if (exact_value > 0.0) {
        record.empirical_ratio_to_exact =
            record.objective_value / exact_value;
      } else if (record.objective_value <= 1e-12) {
        record.empirical_ratio_to_exact = 1.0;
      }
    }
  }
  for (auto& record : records) {
    write_result(record, false);
  }

  std::filesystem::create_directories(output);
  save_master(records, (output / "master_results.json").string(),
              (output / "master_results.csv").string());
  write_summary(records, (output / "batch_summary.md").string());
  LOG_INFO("BatchRunner: {} runs completed", records.size());
}

BatchRunRecord BatchRunner::run_single(const Instance& instance,
                                       const std::string& algorithm_name,
                                       ObjectiveType objective, ILPSolver* ilp,
                                       const BatchRunConfig& config,
                                       int repeat) {
  BatchRunRecord record;
  record.instance_name = instance.name.empty() ? std::to_string(instance.id)
                                                : instance.name;
  record.algorithm_name = algorithm_name;
  record.algorithm_category =
      algorithm_name == "ip-kont" || algorithm_name == "branch-and-bound"
          ? "exact_reference"
          : "heuristic";
  record.requested_backend = config.exact_reference;
  record.actual_backend =
      record.algorithm_category == "exact_reference"
          ? config.actual_backend
          : algorithm_name;
  record.objective = to_string(objective);
  record.repeat = repeat;
  if (objective == ObjectiveType::MIN_SUM) {
    record.minsum_refinement_policy = config.minsum_refinement_policy;
  }
  record.n = instance.n;
  record.m = instance.m;
  record.time_limit_per_ip_sec = config.per_ip_time_limit_sec;
  const bool exact = record.algorithm_category == "exact_reference";
  record.global_time_limit_sec =
      exact ? config.exact_time_limit_sec : config.fast_time_limit_sec;
  record.seed = run_seed(config.seed, record.instance_name, algorithm_name,
                         record.objective, repeat);
  record.verify_each_iteration = config.verify_each_iteration;
  record.verify_after = config.verify_after;
  record.handovers_enabled = true;
  record.git_commit = KDC_GIT_COMMIT;
  record.compiler = __VERSION__;
  record.build_type = KDC_BUILD_TYPE;
  record.thread_count = config.parallel ? config.num_threads : 1;
  record.experiment_id = config.experiment_id;
  record.configuration = {
      {"profile", benchmark_profile_to_string(config.profile)},
      {"algorithm", algorithm_name},
      {"objective", record.objective},
      {"repeat", repeat},
      {"seed", record.seed},
      {"per_static_time_limit_sec", config.per_ip_time_limit_sec},
      {"global_time_limit_sec", record.global_time_limit_sec},
      {"verify_each_iteration", record.verify_each_iteration},
      {"verify_after", record.verify_after},
      {"minsum_refinement_policy",
       minsum_refinement_policy_to_string(config.minsum_refinement_policy)},
      {"handovers_enabled", record.handovers_enabled},
      {"cache_policy", "auto"}};

  const std::filesystem::path run_dir(
      make_run_dir(config.output_dir + "/runs", record.instance_name,
                   algorithm_name, record.objective) +
      (repeat == 0 ? "" : "/repeat-" + std::to_string(repeat)));
  std::filesystem::create_directories(run_dir);
  record.solution_json_path =
      config.save_solutions ? (run_dir / "solution.json").string() : "";
  record.trace_csv_path =
      config.save_traces ? (run_dir / "trace.csv").string() : "";
  record.result_json_path = (run_dir / "result.json").string();

  const auto wall_start = Clock::now();
  const std::clock_t cpu_start = std::clock();
  try {
    auto solver = StaticSolverRegistry::create(algorithm_name, ilp);
    if (!solver) {
      throw std::runtime_error("unknown algorithm: " + algorithm_name);
    }
    solver->set_time_limit(config.per_ip_time_limit_sec);
    solver->set_seed(record.seed);
    const double global_limit = record.global_time_limit_sec;
    SolverBudget budget(global_limit);

    KineticSolution solution;
    std::vector<IterTrace> trace_rows;
    if (objective == ObjectiveType::MIN_MAX) {
      MinMaxSolver::Config solver_config;
      solver_config.time_limit_per_ip = config.per_ip_time_limit_sec;
      solver_config.global_time_limit_sec = global_limit;
      solver_config.gap_target = config.gap_target;
      solver_config.verify_after = config.verify_after;
      solver_config.verify_each_iteration = config.verify_each_iteration;
      solver_config.trace_csv_path.clear();
      const auto result =
          MinMaxSolver::solve(instance, *solver, solver_config, budget);
      record.objective_value = result.peak_cost;
      record.solve_time_sec = result.total_time_sec;
      record.lower_bound = result.lower_bound;
      record.bound_status = result.bound_status;
      record.upper_bound = result.upper_bound;
      record.certified_gap = result.certified_gap;
      record.optimality_status = result.optimality_status;
      record.exact_solver = result.exact_solver;
      record.certified_lower_bound = result.certified_lower_bound;
      record.heuristic_lower_bound = result.heuristic_lower_bound;
      record.gap = result.gap;
      record.num_iterations = result.num_iterations;
      record.num_ip_solves = result.num_ip_solves;
      record.verified = result.verified;
      record.verification_kind = result.verification_kind;
      record.verification_time_sec = result.verification_time_sec;
      record.time_limited = result.time_limited;
      record.feasible = result.feasible;
      solution = result.solution;
      trace_rows = result.trace;
    } else {
      MinSumSolver::Config solver_config;
      solver_config.time_limit_per_ip = config.per_ip_time_limit_sec;
      solver_config.global_time_limit_sec = global_limit;
      solver_config.gap_target = config.gap_target;
      solver_config.refinement_policy = config.minsum_refinement_policy;
      solver_config.verify_after = config.verify_after;
      solver_config.verify_each_iteration = config.verify_each_iteration;
      const auto result =
          MinSumSolver::solve(instance, *solver, solver_config, budget);
      record.objective_value = result.total_integral;
      record.solve_time_sec = result.total_time_sec;
      record.lower_bound = result.lower_bound;
      record.bound_status = result.bound_status;
      record.upper_bound = result.upper_bound;
      record.certified_gap = result.certified_gap;
      record.optimality_status = result.optimality_status;
      record.minsum_refinement_policy = result.refinement_policy;
      record.exact_solver = result.exact_solver;
      record.certified_lower_bound =
          result.certified_lower_bound_integral;
      record.heuristic_lower_bound =
          result.heuristic_lower_bound_integral;
      record.gap = result.gap;
      record.num_iterations = result.num_iterations;
      record.num_ip_solves = result.num_ip_solves;
      record.verified = result.verified;
      record.verification_kind = result.verification_kind;
      record.verification_time_sec = result.verification_time_sec;
      record.time_limited = result.time_limited;
      record.feasible = result.feasible;
      solution = result.solution;
      trace_rows = result.trace;
    }

    if (!record.feasible) {
      if (record.error_message.empty()) {
        record.error_message =
            record.optimality_status == OptimalityStatus::INFEASIBLE
                ? "solver proved the instance infeasible"
                : (record.time_limited
                       ? "solver timed out without a feasible incumbent"
                       : "solver returned no feasible solution");
      }
      if (!record.time_limited) {
        record.optimality_status = OptimalityStatus::FAILED;
      }
      record.certified_gap.reset();
      record.upper_bound = std::numeric_limits<double>::infinity();
      record.failed = !record.time_limited;
    }
    const auto serialization_started = Clock::now();
    if (config.save_traces) {
      TraceWriter::write_csv(trace_rows, record.trace_csv_path);
    }
    record.peak_cost = solution.peak_cost();
    record.integral_cost = solution.total_integral();
    record.candidate_count =
        CandidateSet::precompute(instance)->candidates.size();
    if (config.save_solutions && record.feasible) {
      SolutionSerializer::save_json(instance, solution,
                                    record.solution_json_path);
    }
    record.serialization_time_sec =
        std::chrono::duration<double>(Clock::now() - serialization_started)
            .count();
  } catch (const SolverBudgetExpired& error) {
    record.feasible = false;
    record.verified = false;
    record.optimality_status = OptimalityStatus::FAILED;
    record.time_limited = true;
    record.timeout = true;
    record.failed = false;
    record.optimality_status = OptimalityStatus::TIME_LIMIT;
    record.error_message = error.what();
    record.failed = true;
    LOG_WARN("BatchRunner: {} / {} / {} exhausted its budget",
             record.instance_name, algorithm_name, record.objective);
  } catch (const std::exception& error) {
    record.feasible = false;
    record.verified = false;
    record.error_message = error.what();
    LOG_ERROR("BatchRunner: {} / {} / {} failed: {}", record.instance_name,
              algorithm_name, record.objective, error.what());
    if (config.save_traces && !record.trace_csv_path.empty() &&
        !std::filesystem::exists(record.trace_csv_path)) {
      try {
        TraceWriter::write_csv({}, record.trace_csv_path);
      } catch (const std::exception& trace_error) {
        LOG_ERROR("BatchRunner: unable to write empty failure trace: {}",
                  trace_error.what());
      }
    }
  }

  record.wall_time_sec =
      std::chrono::duration<double>(Clock::now() - wall_start).count();
  record.timeout = record.time_limited;
  record.total_wall_time_sec = record.wall_time_sec;
  record.cpu_time_sec =
      static_cast<double>(std::clock() - cpu_start) / CLOCKS_PER_SEC;
  record.peak_memory_mb = BenchmarkRunner::get_peak_memory_mb();
  write_result(record);
  LOG_INFO("BatchRunner: {} / {} / {} cost={:.6f} t={:.3f}s verified={}",
           record.instance_name, algorithm_name, record.objective,
           record.objective_value, record.wall_time_sec, record.verified);
  return record;
}

std::string BatchRunner::make_run_dir(const std::string& base,
                                      const std::string& instance,
                                      const std::string& algorithm,
                                      const std::string& objective) {
  return (std::filesystem::path(base) / sanitize_path_component(instance) /
          sanitize_path_component(algorithm) /
          sanitize_path_component(objective))
      .string();
}

void BatchRunner::save_master(const std::vector<BatchRunRecord>& records,
                              const std::string& json_path,
                              const std::string& csv_path) {
  const std::filesystem::path json_file(json_path);
  const std::filesystem::path csv_file(csv_path);
  if (!json_file.parent_path().empty()) {
    std::filesystem::create_directories(json_file.parent_path());
  }
  if (!csv_file.parent_path().empty()) {
    std::filesystem::create_directories(csv_file.parent_path());
  }

  Json json = Json::array();
  for (const auto& record : records) {
    json.push_back(record_json(record));
  }
  std::ofstream json_output(json_file);
  if (!json_output) {
    throw std::runtime_error("cannot open batch master JSON: " + json_path);
  }
  json_output << std::setw(2) << json << '\n';
  if (!json_output) {
    throw std::runtime_error("failed writing batch master JSON: " + json_path);
  }

  std::ofstream csv_output(csv_file);
  if (!csv_output) {
    throw std::runtime_error("cannot open batch master CSV: " + csv_path);
  }
  csv_output
      << "instance_name,algorithm_name,algorithm_category,requested_backend,"
         "actual_backend,objective,repeat,n,m,wall_time_sec,"
         "solve_time_sec,cpu_time_sec,peak_memory_mb,time_limit_per_ip_sec,"
         "peak_cost,integral_cost,empirical_ratio_to_exact,ratio_to_incumbent,"
         "objective_value,lower_bound,bound_status,upper_bound,certified_gap,"
         "optimality_status,minsum_refinement_policy,exact_solver,gap,"
         "num_iterations,num_ip_solves,verified,verification_kind,"
         "verification_time_sec,serialization_time_sec,total_wall_time_sec,"
         "seed,global_time_limit_sec,per_static_time_limit_sec,"
         "verify_each_iteration,verify_after,handovers_enabled,candidate_count,"
         "coverage_nnz,feasible,time_limited,timeout,failed,git_commit,"
         "compiler,build_type,thread_count,experiment_id,configuration,"
         "error_message\n";
  for (const auto& record : records) {
    const auto fields = csv_fields(record);
    for (std::size_t index = 0; index < fields.size(); ++index) {
      if (index != 0U) {
        csv_output << ',';
      }
      csv_output << csv_escape(fields[index]);
    }
    csv_output << '\n';
  }
  if (!csv_output) {
    throw std::runtime_error("failed writing batch master CSV: " + csv_path);
  }
}

void BatchRunner::write_summary(const std::vector<BatchRunRecord>& records,
                                const std::string& path) {
  const std::filesystem::path summary_path(path);
  if (!summary_path.parent_path().empty()) {
    std::filesystem::create_directories(summary_path.parent_path());
  }
  std::ofstream output(summary_path);
  if (!output) {
    throw std::runtime_error("cannot open batch summary: " + path);
  }

  const std::size_t successful =
      static_cast<std::size_t>(std::count_if(
          records.begin(), records.end(),
          [](const BatchRunRecord& record) { return record.feasible; }));
  output << "# Batch Run Summary\n\n"
         << "- **Total runs:** " << records.size() << "\n"
         << "- **Successful runs:** " << successful << "\n"
         << "- **Failed runs:** " << records.size() - successful << "\n\n"
         << "## Median wall time and gap by algorithm\n\n"
         << "| Algorithm | MinMax median wall time (s) | MinMax median gap | "
            "MinSum median wall time (s) | MinSum median gap |\n"
         << "|---|---:|---:|---:|---:|\n";
  std::map<std::pair<std::string, std::string>,
           std::vector<const BatchRunRecord*>>
      grouped;
  for (const auto& record : records) {
    grouped[{record.algorithm_name, record.objective}].push_back(&record);
  }
  std::vector<std::string> summary_algorithms;
  for (const auto& entry : grouped) {
    if (std::find(summary_algorithms.begin(), summary_algorithms.end(),
                  entry.first.first) == summary_algorithms.end()) {
      summary_algorithms.push_back(entry.first.first);
    }
  }
  std::sort(summary_algorithms.begin(), summary_algorithms.end());
  for (const auto& algorithm : summary_algorithms) {
    output << '|' << algorithm;
    for (const char* objective : {"minmax", "minsum"}) {
      std::vector<double> times;
      std::vector<double> gaps;
      const auto found = grouped.find({algorithm, objective});
      if (found != grouped.end()) {
        for (const auto* record : found->second) {
          if (record->feasible) {
            times.push_back(record->wall_time_sec);
            gaps.push_back(record->gap);
          }
        }
      }
      output << '|' << median_text(std::move(times)) << '|'
             << median_text(std::move(gaps));
    }
    output << "|\n";
  }

  output << "\n## Objective value by instance and algorithm\n\n";
  std::vector<std::string> algorithms;
  for (const auto& record : records) {
    if (std::find(algorithms.begin(), algorithms.end(),
                  record.algorithm_name) == algorithms.end()) {
      algorithms.push_back(record.algorithm_name);
    }
  }
  std::sort(algorithms.begin(), algorithms.end());
  output << "| Instance |";
  for (const auto& algorithm : algorithms) {
    output << ' ' << algorithm << " |";
  }
  output << "\n|---|";
  for (std::size_t index = 0; index < algorithms.size(); ++index) {
    output << "---:|";
  }
  output << '\n';

  std::map<std::string, std::map<std::string, std::vector<const BatchRunRecord*>>>
      by_instance;
  for (const auto& record : records) {
    by_instance[record.instance_name][record.algorithm_name].push_back(&record);
  }
  for (const auto& instance : by_instance) {
    output << '|' << instance.first << '|';
    for (const auto& algorithm : algorithms) {
      const auto found = instance.second.find(algorithm);
      if (found == instance.second.end()) {
        output << " n/a |";
        continue;
      }
      std::ostringstream values;
      bool first = true;
      for (const auto* record : found->second) {
        if (!record->feasible) {
          continue;
        }
        if (!first) {
          values << "; ";
        }
        values << record->objective << ": " << std::setprecision(6)
               << record->objective_value;
        first = false;
      }
      output << ' ' << (first ? "n/a" : values.str()) << " |";
    }
    output << '\n';
  }

  output << "\n## Failures\n\n";
  bool has_failures = false;
  for (const auto& record : records) {
    if (record.feasible && record.error_message.empty()) {
      continue;
    }
    has_failures = true;
    output << "- `" << record.instance_name << " / " << record.algorithm_name
           << " / " << record.objective << "`: "
           << (record.error_message.empty() ? "solution was not feasible"
                                            : record.error_message)
           << '\n';
  }
  if (!has_failures) {
    output << "None.\n";
  }
  if (!output) {
    throw std::runtime_error("failed writing batch summary: " + path);
  }
}

}  // namespace kdc
