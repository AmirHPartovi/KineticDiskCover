#include "kdc/batch_runner.hpp"

#include "kdc/benchmark_protocol.hpp"
#include "kdc/benchmark.hpp"
#include "kdc/algorithms/brute_force_solver.hpp"
#include "kdc/candidate.hpp"
#include "kdc/exact_reference_selector.hpp"
#include "kdc/io.hpp"
#include "kdc/kont_solver.hpp"
#include "kdc/logging.hpp"
#include "kdc/minmax.hpp"
#include "kdc/minmaxsum.hpp"
#include "kdc/minsum.hpp"
#include "kdc/profiling.hpp"
#include "kdc/solution_serializer.hpp"
#include "kdc/static_solver_registry.hpp"
#include "kdc/thread_pool.hpp"
#include "kdc/temporal_pipeline.hpp"
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

bool is_temporal_method_name(const std::string& method) {
  return method == "temporal-interval-bnb" ||
         method == "b-grid" ||
         method == "b-nn" ||
         method == "b-static-hold" ||
         method == "b-minmax";
}

bool is_bound_temporal_method_name(const std::string& method) {
  return method == "temporal-interval-bnb" || method == "b-grid";
}

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
  if (is_temporal_method_name(record.solver_method)) {
    return Json{{"schema_version", 4},
                {"solver_method", record.solver_method},
                {"instance_name", record.instance_name},
                {"algorithm_name", record.algorithm_name},
                {"algorithm_category", record.algorithm_category},
                {"objective", record.objective},
                {"n", record.n},
                {"m", record.m},
                {"repeat", record.repeat},
                {"seed", record.seed},
                {"wall_time_sec", record.wall_time_sec},
                {"solve_time_sec", record.solve_time_sec},
                {"cpu_time_sec", record.cpu_time_sec},
                {"global_time_limit_sec", record.global_time_limit_sec},
                {"feasible", record.feasible},
                {"verified", record.verified},
                {"time_limited", record.time_limited},
                {"failed", record.failed},
                {"error_message", record.error_message},
                {"experiment_id", record.experiment_id},
                {"configuration", record.configuration},
                {"result_json_path", record.result_json_path},
                {"temporal_result_v2", record.temporal_result_v2},
                {"temporal_baseline_result_v1",
                 record.temporal_baseline_result_v1}};
  }
  return Json{{"schema_version", 3},
              {"solver_method", record.solver_method},
              {"instance_name", record.instance_name},
              {"algorithm_name", record.algorithm_name},
              {"algorithm_category", record.algorithm_category},
              {"requested_backend", record.requested_backend},
              {"selected_backend", record.selected_backend},
              {"actual_backend", record.actual_backend},
              {"native_kont", record.native_kont},
              {"fallback_used", record.fallback_used},
              {"native_solve_count", record.native_solve_count},
              {"fallback_solve_count", record.fallback_solve_count},
              {"failed_native_solve_count",
               record.failed_native_solve_count},
              {"kont_version", record.solver_version},
              {"solver_name", record.solver_name},
              {"solver_runtime_sec", record.solve_time_sec},
              {"objective", record.objective},
              {"repeat", record.repeat},
              {"n", record.n},
              {"m", record.m},
              {"wall_time_sec", record.wall_time_sec},
              {"solve_time_sec", record.solve_time_sec},
              {"peak_cost", record.peak_cost},
              {"peak_time", record.peak_time},
              {"initial_peak_cost", record.initial_peak_cost},
              {"heuristic_gap", record.heuristic_gap},
              {"peak_consistent", record.peak_consistent},
              {"integral_cost", record.integral_cost},
              {"minmaxsum_minmax_status",
               to_string(record.minmaxsum_minmax_status)},
              {"minmaxsum_minsum_status",
               to_string(record.minmaxsum_minsum_status)},
              {"minmaxsum_minmax_optimality",
               optimality_status_to_string(
                   record.minmaxsum_minmax_optimality)},
              {"minmaxsum_minsum_optimality",
               optimality_status_to_string(
                   record.minmaxsum_minsum_optimality)},
              {"dominates_minmax", record.dominates_minmax},
              {"dominates_minsum", record.dominates_minsum},
              {"dominance_invariants_ok",
               record.dominance_invariants_ok},
              {"objective_vector",
               record.objective == "minmaxsum"
                   ? Json{{"peak_cost", std::isfinite(record.peak_cost)
                                            ? Json(record.peak_cost)
                                            : Json(nullptr)},
                          {"integral_cost",
                           std::isfinite(record.integral_cost)
                               ? Json(record.integral_cost)
                               : Json(nullptr)}}
                   : Json(nullptr)},
              {"joint",
               record.objective == "minmaxsum"
                   ? Json{{"minmax_component_status",
                           to_string(record.minmaxsum_minmax_status)},
                          {"minsum_component_status",
                           to_string(record.minmaxsum_minsum_status)},
                          {"minmax_component_optimality",
                           optimality_status_to_string(
                               record.minmaxsum_minmax_optimality)},
                          {"minsum_component_optimality",
                           optimality_status_to_string(
                               record.minmaxsum_minsum_optimality)},
                          {"joint_optimality_status",
                           optimality_status_to_string(
                               record.optimality_status)},
                          {"dominates_minmax", record.dominates_minmax},
                          {"dominates_minsum", record.dominates_minsum},
                          {"dominance_invariants_ok",
                           record.dominance_invariants_ok},
                          {"minmax_component_peak",
                           record.minmax_component_peak
                               ? Json(*record.minmax_component_peak)
                               : Json(nullptr)},
                          {"minmax_component_integral",
                           record.minmax_component_integral
                               ? Json(*record.minmax_component_integral)
                               : Json(nullptr)},
                          {"minsum_component_peak",
                           record.minsum_component_peak
                               ? Json(*record.minsum_component_peak)
                               : Json(nullptr)},
                          {"minsum_component_integral",
                           record.minsum_component_integral
                               ? Json(*record.minsum_component_integral)
                               : Json(nullptr)},
                          {"minmax_source_run", record.minmax_source_run},
                          {"minsum_source_run", record.minsum_source_run}}
                   : Json(nullptr)},
              {"minmax_component_peak",
               record.minmax_component_peak
                   ? Json(*record.minmax_component_peak)
                   : Json(nullptr)},
              {"minmax_component_integral",
               record.minmax_component_integral
                   ? Json(*record.minmax_component_integral)
                   : Json(nullptr)},
              {"minsum_component_peak",
               record.minsum_component_peak
                   ? Json(*record.minsum_component_peak)
                   : Json(nullptr)},
              {"minsum_component_integral",
               record.minsum_component_integral
                   ? Json(*record.minsum_component_integral)
                   : Json(nullptr)},
              {"minmax_component_certified_gap",
               record.minmax_component_certified_gap
                   ? Json(*record.minmax_component_certified_gap)
                   : Json(nullptr)},
              {"minsum_component_certified_gap",
               record.minsum_component_certified_gap
                   ? Json(*record.minsum_component_certified_gap)
                   : Json(nullptr)},
              {"minmax_source_run", record.minmax_source_run},
              {"minsum_source_run", record.minsum_source_run},
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
              {"objective_value",
               std::isfinite(record.objective_value)
                   ? Json(record.objective_value)
                   : Json(nullptr)},
              {"lower_bound",
               std::isfinite(record.lower_bound)
                   ? Json(record.lower_bound)
                   : Json(nullptr)},
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
                   : (record.objective != "minmaxsum" &&
                              !record.exact_solver &&
                              record.optimality_status ==
                                  OptimalityStatus::OPTIMAL
                          ? OptimalityStatus::FEASIBLE
                          : record.optimality_status))},
              {"minsum_refinement_policy",
               minsum_refinement_policy_to_string(
                   record.minsum_refinement_policy)},
              {"exact_solver", record.exact_solver},
              {"certified_lower_bound",
               std::isfinite(record.certified_lower_bound)
                   ? Json(record.certified_lower_bound)
                   : Json(nullptr)},
              {"heuristic_lower_bound",
               std::isfinite(record.heuristic_lower_bound)
                   ? Json(record.heuristic_lower_bound)
                   : Json(nullptr)},
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
  const auto finite_string = [](double value) {
    return std::isfinite(value) ? std::to_string(value) : std::string{};
  };
  return {record.instance_name,
          record.algorithm_name,
          record.algorithm_category,
          record.requested_backend,
          record.selected_backend,
          record.actual_backend,
          record.solver_name,
          record.solver_version,
          record.native_kont ? "true" : "false",
          record.fallback_used ? "true" : "false",
          std::to_string(record.native_solve_count),
          std::to_string(record.fallback_solve_count),
          std::to_string(record.failed_native_solve_count),
          std::to_string(record.solve_time_sec),
          record.objective,
          std::to_string(record.repeat),
          std::to_string(record.n),
          std::to_string(record.m),
          std::to_string(record.wall_time_sec),
          std::to_string(record.solve_time_sec),
          std::to_string(record.cpu_time_sec),
          std::to_string(record.peak_memory_mb),
          std::to_string(record.time_limit_per_ip_sec),
          finite_string(record.peak_cost),
          finite_string(record.peak_time),
          finite_string(record.initial_peak_cost),
          finite_string(record.heuristic_gap),
          record.peak_consistent ? "true" : "false",
          finite_string(record.integral_cost),
          record.objective == "minmaxsum" ? finite_string(record.peak_cost) : "",
          record.objective == "minmaxsum"
              ? finite_string(record.integral_cost)
              : "",
          to_string(record.minmaxsum_minmax_status),
          to_string(record.minmaxsum_minsum_status),
          optimality_status_to_string(
              record.minmaxsum_minmax_optimality),
          optimality_status_to_string(
              record.minmaxsum_minsum_optimality),
          record.dominates_minmax ? "true" : "false",
          record.dominates_minsum ? "true" : "false",
          record.dominance_invariants_ok ? "true" : "false",
          record.minmax_component_peak
              ? std::to_string(*record.minmax_component_peak)
              : "",
          record.minmax_component_integral
              ? std::to_string(*record.minmax_component_integral)
              : "",
          record.minsum_component_peak
              ? std::to_string(*record.minsum_component_peak)
              : "",
          record.minsum_component_integral
              ? std::to_string(*record.minsum_component_integral)
              : "",
          record.minmax_component_certified_gap
              ? std::to_string(*record.minmax_component_certified_gap)
              : "",
          record.minsum_component_certified_gap
              ? std::to_string(*record.minsum_component_certified_gap)
              : "",
          record.minmax_source_run,
          record.minsum_source_run,
          record.empirical_ratio_to_exact
              ? std::to_string(*record.empirical_ratio_to_exact)
              : "",
          record.ratio_to_incumbent
              ? std::to_string(*record.ratio_to_incumbent)
              : "",
          std::isfinite(record.objective_value)
              ? std::to_string(record.objective_value)
              : "",
          std::isfinite(record.lower_bound)
              ? std::to_string(record.lower_bound)
              : "",
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
              : (record.objective != "minmaxsum" &&
                         !record.exact_solver &&
                         record.optimality_status == OptimalityStatus::OPTIMAL
                     ? OptimalityStatus::FEASIBLE
                     : record.optimality_status)),
          minsum_refinement_policy_to_string(
              record.minsum_refinement_policy),
          record.exact_solver ? "true" : "false",
          std::isfinite(record.gap) ? std::to_string(record.gap) : "",
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
  record.solver_method =
      pipeline_solver_method_name(config.solver_method);
  if (is_temporal_solver_method(config.solver_method)) {
    if (is_temporal_bound_solver_method(config.solver_method)) {
      record.temporal_result_v2 = temporal_thesis_failure_to_json(error);
    } else {
      const auto baseline =
          config.solver_method == PipelineSolverMethod::B_NN
              ? TemporalBaselineKindV1::B_NN
          : config.solver_method == PipelineSolverMethod::B_STATIC_HOLD
              ? TemporalBaselineKindV1::B_STATIC_HOLD
              : TemporalBaselineKindV1::B_MINMAX;
      record.temporal_baseline_result_v1 =
          temporal_baseline_failure_to_json(baseline, error);
    }
  }
  record.instance_name = path.stem().string();
  record.algorithm_name = algorithm;
  record.algorithm_category =
      is_temporal_solver_method(config.solver_method)
          ? "temporal"
          : algorithm == "brute-force"
          ? "diagnostic"
          : (algorithm == "ip-kont" || algorithm == "branch-and-bound")
                ? (algorithm == config.selected_backend
                       ? "exact_reference"
                       : "exact_candidate")
                : "heuristic";
  record.requested_backend = config.exact_reference;
  record.selected_backend = config.selected_backend;
  record.actual_backend =
      algorithm == "ip-kont"
          ? "native-backend-not-executed"
          : (record.algorithm_category == "exact_reference"
                 ? "branch-and-bound"
                 : algorithm);
  record.native_kont = false;
  record.fallback_used = false;
  record.solver_version = "not-applicable";
  record.solver_name = algorithm;
  record.objective = to_string(objective);
  if (objective == ObjectiveType::MIN_MAX_SUM) {
    record.objective_value = std::numeric_limits<double>::quiet_NaN();
    record.lower_bound = std::numeric_limits<double>::quiet_NaN();
    record.certified_lower_bound =
        std::numeric_limits<double>::quiet_NaN();
    record.heuristic_lower_bound =
        std::numeric_limits<double>::quiet_NaN();
    record.gap = std::numeric_limits<double>::quiet_NaN();
  }
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
  if (config.save_solutions &&
      !is_temporal_solver_method(config.solver_method)) {
    record.solution_json_path = (run_dir / "solution.json").string();
  }
  if (config.save_traces &&
      !is_temporal_solver_method(config.solver_method)) {
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

void require_fresh_batch_output(const std::filesystem::path& output) {
  for (const char* artifact : {"runs", "master_results.json",
                               "master_results.csv", "batch_summary.md"}) {
    if (std::filesystem::exists(output / artifact)) {
      throw std::invalid_argument(
          "batch output already contains " + std::string(artifact) +
          "; use a new output directory");
    }
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
      config.brute_force_max_assignments == 0U ||
      !std::isfinite(config.gap_target) || config.gap_target < 0.0 ||
      config.gap_target >= 1.0 || config.num_threads <= 0) {
    throw std::invalid_argument("batch runner configuration is invalid");
  }
  if (config.repeats <= 0) {
    throw std::invalid_argument("batch runner repeat count must be positive");
  }
  const bool temporal_method =
      is_temporal_solver_method(config.solver_method);
  if ((temporal_method ||
       config.solver_method == PipelineSolverMethod::MINSUM_ADAPTIVE) &&
      (config.objectives.size() != 1U ||
       config.objectives.front() != ObjectiveType::MIN_SUM)) {
    throw std::invalid_argument(
        "selected solver method requires only the minsum objective");
  }
  if (config.objectives.empty()) {
    throw std::invalid_argument("batch runner requires at least one objective");
  }
  if (std::find(config.objectives.begin(), config.objectives.end(),
                ObjectiveType::MIN_MAX_SUM) != config.objectives.end() &&
      (!std::isfinite(config.minmaxsum_config.minmax_budget_fraction) ||
       !std::isfinite(config.minmaxsum_config.minsum_budget_fraction) ||
       config.minmaxsum_config.minmax_budget_fraction <= 0.0 ||
       config.minmaxsum_config.minsum_budget_fraction <= 0.0 ||
       std::abs(config.minmaxsum_config.minmax_budget_fraction +
                    config.minmaxsum_config.minsum_budget_fraction -
                1.0) > 1e-9)) {
    throw std::invalid_argument(
        "MinMaxSum batch budget fractions must be positive and sum to one");
  }
  const bool explicitly_selects_both_exact_backends =
      std::find(config.algorithm_names.begin(), config.algorithm_names.end(),
                "ip-kont") != config.algorithm_names.end() &&
      std::find(config.algorithm_names.begin(), config.algorithm_names.end(),
                "branch-and-bound") != config.algorithm_names.end();
  const bool wants_all_registered =
      std::find(config.algorithm_names.begin(), config.algorithm_names.end(),
                "all-registered") != config.algorithm_names.end();
  if (explicitly_selects_both_exact_backends && !wants_all_registered) {
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
  ExactReferenceDecision exact_reference_decision;
  if (temporal_method) {
    exact_reference_decision.requested_backend = "not-applicable";
    exact_reference_decision.selected_backend = "not-applicable";
    exact_reference_decision.actual_backend =
        pipeline_solver_method_name(config.solver_method);
    exact_reference_decision.selection_rule =
        "dedicated temporal solver dispatch";
    exact_reference_decision.valid = true;
  } else {
    exact_reference_decision =
        ExactReferenceSelector::resolve(config.exact_reference,
                                        config.instances_dir,
                                        config.output_dir);
  }
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
  const bool wants_all_legacy =
      std::find(config.algorithm_names.begin(), config.algorithm_names.end(),
                "all") != config.algorithm_names.end() ||
      std::find(config.algorithm_names.begin(), config.algorithm_names.end(),
                "ALL") != config.algorithm_names.end();
  const bool wants_default_selection = config.algorithm_names.empty();

  const bool wants_fast =
      std::find(config.algorithm_names.begin(), config.algorithm_names.end(),
                "all-fast") != config.algorithm_names.end();
  const bool wants_comparison =
      std::find(config.algorithm_names.begin(), config.algorithm_names.end(),
                "all-comparison") != config.algorithm_names.end();
  if (temporal_method) {
    algorithms = {
        pipeline_solver_method_name(config.solver_method)};
  } else if (config.profile == BenchmarkProfile::EXACT_REFERENCE) {
    algorithms = {exact_reference_decision.selected_backend};
  } else if (wants_all_registered || wants_all_legacy) {
    for (const auto& name : registered) {
      if (!wants_all_registered &&
          (name == "ip-kont" || name == "branch-and-bound") &&
          name != exact_reference_decision.selected_backend) {
        continue;
      }
      algorithms.push_back(name);
    }
  } else if (wants_default_selection || wants_fast || wants_comparison) {
    algorithms = default_benchmark_algorithms(
        exact_reference_decision.selected_backend);
  } else if (config.algorithm_names.empty()) {
    algorithms = default_benchmark_algorithms(
        exact_reference_decision.selected_backend);
  } else {
    for (const auto& requested : config.algorithm_names) {
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
  if (!temporal_method && exact_count > 1 && !wants_all_registered) {
    throw std::invalid_argument(
        "strict benchmark mode accepts exactly one exact backend; remove "
        "either ip-kont or branch-and-bound");
  }
  const auto exact_position = std::find_if(
      algorithms.begin(), algorithms.end(), [](const std::string& name) {
        return name == "ip-kont" || name == "branch-and-bound";
      });
  if (!temporal_method && !wants_all_registered &&
      config.strict_exact_reference &&
      exact_position != algorithms.end() &&
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
  if (!temporal_method && config.strict_exact_reference &&
      exact_position == algorithms.end()) {
    algorithms.push_back(exact_reference_decision.selected_backend);
  }
  if (!temporal_method &&
      (wants_fast || wants_comparison || wants_default_selection)) {
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
  if (run_config.solver_method ==
      PipelineSolverMethod::MINSUM_ADAPTIVE) {
    run_config.minsum_refinement_policy =
        MinSumRefinementPolicy::HEURISTIC_ADAPTIVE;
  } else if (run_config.profile == BenchmarkProfile::FAST) {
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
  run_config.selected_backend = exact_reference_decision.selected_backend;
  run_config.native_kont =
      exact_reference_decision.actual_backend == "KONT/COPT";
  run_config.fallback_used = exact_reference_decision.fallback_used;
  run_config.solver_version =
      run_config.native_kont ? exact_reference_decision.kont_version
                             : "not-applicable";
  if (temporal_method) {
    run_config.selected_backend = "not-applicable";
    run_config.actual_backend =
        pipeline_solver_method_name(config.solver_method);
    run_config.native_kont = false;
    run_config.fallback_used = false;
  }
  if (run_config.experiment_id.empty()) {
    run_config.experiment_id = experiment_id_for(run_config);
  }
  const std::filesystem::path output(config.output_dir);
  require_fresh_batch_output(output);
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
      config.profile, algorithms,
      temporal_method ? "not-applicable" : config.exact_reference,
      exact_reference_decision.actual_backend, calibration,
      config.fast_time_limit_sec, config.exact_time_limit_sec,
      config.per_ip_time_limit_sec, config.seed, config.repeats,
      minsum_refinement_policy_to_string(
          run_config.minsum_refinement_policy),
      run_config.verify_each_iteration, run_config.verify_after, true, "auto",
      config.parallel ? config.num_threads : 1);
  manifest["solver_method"] =
      pipeline_solver_method_name(run_config.solver_method);
  manifest["resolved_solver_method"] =
      temporal_method
          ? pipeline_solver_method_name(run_config.solver_method)
          : run_config.solver_method ==
                    PipelineSolverMethod::MINSUM_ADAPTIVE
                ? "minsum-adaptive"
                : "legacy-static-pipeline";
  if (temporal_method) {
    if (is_temporal_bound_solver_method(run_config.solver_method)) {
      manifest["temporal_solver_config"] = {
          {"global_time_limit_sec", run_config.fast_time_limit_sec},
          {"oracle_time_limit_sec",
           run_config.temporal_config.execution.oracle_time_limit_seconds},
          {"delta_min", run_config.temporal_config.execution.delta_min},
          {"max_leaves", run_config.temporal_config.execution.max_leaves},
          {"max_oracle_calls",
           run_config.temporal_config.execution.max_oracle_calls},
          {"epsilon_abs", run_config.temporal_config.epsilon_abs},
          {"epsilon_rel", run_config.temporal_config.epsilon_rel},
          {"split_selection",
           run_config.solver_method == PipelineSolverMethod::B_GRID
               ? "LONGEST_INTERVAL"
               : "PRIORITY"},
          {"arithmetic", "FAST_DOUBLE"}};
    } else {
      manifest["temporal_baseline_config"] = {
          {"global_time_limit_sec", run_config.fast_time_limit_sec},
          {"oracle_time_limit_sec",
           run_config.temporal_baseline_config.oracle_time_limit_seconds},
          {"max_oracle_calls",
           run_config.temporal_baseline_config.max_oracle_calls},
          {"arithmetic", "FAST_FLOAT_ONLY"}};
    }
  }
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
        const int repeat_count = config.repeats;
        for (int repeat = 0; repeat < repeat_count; ++repeat) {
          phases[algorithm_phase(algorithm)].push_back(
              {path, algorithm, objective, repeat});
        }
      }
    }
  }

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
  record.solver_method =
      pipeline_solver_method_name(config.solver_method);
  record.instance_name = instance.name.empty() ? std::to_string(instance.id)
                                                : instance.name;
  record.algorithm_name = algorithm_name;
  record.algorithm_category =
      is_temporal_solver_method(config.solver_method)
          ? (is_temporal_bound_solver_method(config.solver_method)
                 ? "temporal"
                 : "temporal_baseline")
          : algorithm_name == "brute-force"
          ? "diagnostic"
          : (algorithm_name == "ip-kont" ||
                     algorithm_name == "branch-and-bound"
                 ? (algorithm_name == config.selected_backend
                        ? "exact_reference"
                        : "exact_candidate")
                 : "heuristic");
  record.requested_backend = config.exact_reference;
  record.selected_backend = config.selected_backend;
  record.actual_backend =
      algorithm_name == "ip-kont" ? "KONT/COPT"
      : algorithm_name == "branch-and-bound" ? "branch-and-bound"
                                             : algorithm_name;
  if (is_temporal_solver_method(config.solver_method)) {
    record.requested_backend = "not-applicable";
    record.selected_backend = "not-applicable";
    record.actual_backend =
        pipeline_solver_method_name(config.solver_method);
  }
  record.native_kont = algorithm_name == "ip-kont";
  record.fallback_used = false;
  record.solver_version = record.native_kont ? config.solver_version
                                              : "not-applicable";
  record.solver_name = algorithm_name;
  record.objective = to_string(objective);
  record.repeat = repeat;
  if (objective == ObjectiveType::MIN_SUM) {
    record.minsum_refinement_policy = config.minsum_refinement_policy;
  }
  record.n = instance.n;
  record.m = instance.m;
  record.time_limit_per_ip_sec = config.per_ip_time_limit_sec;
  const bool exact = algorithm_name == "ip-kont" ||
                     algorithm_name == "branch-and-bound";
  const bool diagnostic = algorithm_name == "brute-force";
  record.global_time_limit_sec =
      exact ? config.exact_time_limit_sec
            : diagnostic ? std::min(config.fast_time_limit_sec, 0.5)
                         : config.fast_time_limit_sec;
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
      {"solver_method", record.solver_method},
      {"resolved_solver_method",
       is_temporal_solver_method(config.solver_method)
           ? pipeline_solver_method_name(config.solver_method)
           : config.solver_method ==
                     PipelineSolverMethod::MINSUM_ADAPTIVE
                 ? "minsum-adaptive"
                 : "legacy-static-pipeline"},
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
      {"minmaxsum_minmax_budget_fraction",
       config.minmaxsum_config.minmax_budget_fraction},
      {"minmaxsum_minsum_budget_fraction",
       config.minmaxsum_config.minsum_budget_fraction},
      {"handovers_enabled", record.handovers_enabled},
      {"cache_policy", "auto"}};
  if (is_temporal_solver_method(config.solver_method)) {
    if (is_temporal_bound_solver_method(config.solver_method)) {
      record.configuration["temporal_solver_config"] = {
          {"global_time_limit_sec", record.global_time_limit_sec},
          {"oracle_time_limit_sec",
           config.temporal_config.execution.oracle_time_limit_seconds},
          {"delta_min", config.temporal_config.execution.delta_min},
          {"max_leaves", config.temporal_config.execution.max_leaves},
          {"max_oracle_calls",
           config.temporal_config.execution.max_oracle_calls},
          {"epsilon_abs", config.temporal_config.epsilon_abs},
          {"epsilon_rel", config.temporal_config.epsilon_rel},
          {"split_selection",
           config.solver_method == PipelineSolverMethod::B_GRID
               ? "LONGEST_INTERVAL"
               : "PRIORITY"},
          {"arithmetic", "FAST_DOUBLE"}};
    } else {
      record.configuration["temporal_baseline_config"] = {
          {"global_time_limit_sec", record.global_time_limit_sec},
          {"oracle_time_limit_sec",
           config.temporal_baseline_config.oracle_time_limit_seconds},
          {"max_oracle_calls",
           config.temporal_baseline_config.max_oracle_calls},
          {"arithmetic", "FAST_FLOAT_ONLY"}};
    }
  }
  if (diagnostic) {
    record.configuration["diagnostic_category"] = "validation_diagnostic";
    record.configuration["max_assignments"] =
        config.brute_force_max_assignments;
  }

  const std::filesystem::path run_dir(
      make_run_dir(config.output_dir + "/runs", record.instance_name,
                   algorithm_name, record.objective) +
      (repeat == 0 ? "" : "/repeat-" + std::to_string(repeat)));
  std::filesystem::create_directories(run_dir);
  record.solution_json_path =
      config.save_solutions &&
              !is_temporal_solver_method(config.solver_method)
          ? (run_dir / "solution.json").string()
          : "";
  record.trace_csv_path =
      config.save_traces &&
              !is_temporal_solver_method(config.solver_method)
          ? (run_dir / "trace.csv").string()
          : "";
  record.result_json_path = (run_dir / "result.json").string();

  const auto wall_start = Clock::now();
  const std::clock_t cpu_start = std::clock();
  auto* kont_solver = dynamic_cast<KontSolver*>(ilp);
  const KontExecutionProvenance provenance_before =
      kont_solver != nullptr ? kont_solver->thread_execution_provenance()
                             : KontExecutionProvenance{};
  try {
    if (is_temporal_solver_method(config.solver_method)) {
      if (is_temporal_bound_solver_method(config.solver_method)) {
        TemporalThesisConfigV2 temporal_config = config.temporal_config;
        temporal_config.execution.global_time_limit_seconds =
            record.global_time_limit_sec;
        temporal_config.execution.verify_snapshot = config.verify_after;
        if (config.solver_method == PipelineSolverMethod::B_GRID) {
          temporal_config.execution.split_selection =
              TemporalLeafSelectionV1::LONGEST_INTERVAL;
        }
        IntervalEvaluatorConfigV1 evaluator_config;
        evaluator_config.arithmetic =
            IntervalEvaluatorArithmeticV1::FAST_DOUBLE;
        TemporalThesisSolver temporal_solver(instance);
        const auto temporal_result =
            temporal_solver.solve_v2(evaluator_config, temporal_config);
        record.temporal_result_v2 =
            temporal_thesis_result_to_json(temporal_result);
        const auto& final = temporal_result.final;
        const auto& execution = temporal_result.execution;
        record.feasible =
            final.UB_valid && std::isfinite(final.UB) &&
            execution.global_bounds.ub_best.has_value() &&
            execution.global_bounds.ub_best->snapshot.complete;
        record.verified = execution.verified;
        record.verification_kind = VerificationKind::NONE;
        record.verification_time_sec = final.verify_time_seconds;
        record.solve_time_sec = execution.elapsed_seconds;
        record.time_limited =
            temporal_thesis_result_timed_out(temporal_result);
        record.failed = temporal_thesis_result_failed(temporal_result);
        record.actual_backend = record.solver_method;
        record.solver_name = "TemporalThesisSolver";
        if (record.failed) {
          record.error_message = execution.stop_reason;
        }
      } else {
        TemporalBaselineConfigV1 baseline_config =
            config.temporal_baseline_config;
        baseline_config.global_time_limit_seconds =
            record.global_time_limit_sec;
        TemporalBaselineSolver baseline_solver(instance);
        TemporalBaselineResultV1 baseline_result;
        switch (config.solver_method) {
          case PipelineSolverMethod::B_NN:
            baseline_result = baseline_solver.solve_nn(baseline_config);
            record.actual_backend = record.solver_method;
            break;
          case PipelineSolverMethod::B_STATIC_HOLD: {
            std::unique_ptr<IWeightedExactOracle> exact_oracle =
                ilp != nullptr
                    ? std::make_unique<ILPWeightedExactOracle>(
                          ilp,
                          ILPWeightedExactOracle::Config{
                              baseline_config.oracle_time_limit_seconds, 0.0})
                    : WeightedOracleFactory::unavailable_exact();
            auto heuristic_oracle =
                WeightedOracleFactory::greedy_heuristic();
            baseline_result = baseline_solver.solve_static_hold(
                exact_oracle.get(), heuristic_oracle.get(), baseline_config);
            record.requested_backend = "ilp-weighted-exact";
            record.selected_backend = "ilp-weighted-exact";
            record.actual_backend =
                ilp != nullptr ? ilp->name() : "unavailable";
            break;
          }
          case PipelineSolverMethod::B_MINMAX: {
            ColumnMinimumWeightedMinMaxOracle minmax_oracle;
            baseline_result =
                baseline_solver.solve_minmax(minmax_oracle, baseline_config);
            record.requested_backend = minmax_oracle.name();
            record.selected_backend = minmax_oracle.name();
            record.actual_backend = minmax_oracle.name();
            break;
          }
          default:
            throw std::logic_error(
                "unexpected temporal baseline method");
        }
        if (record.requested_backend.empty()) {
          record.requested_backend = "not-applicable";
          record.selected_backend = "not-applicable";
        }
        record.solver_name = record.actual_backend;
        record.temporal_baseline_result_v1 =
            temporal_baseline_result_to_json(baseline_result);
        record.feasible =
            baseline_result.program_complete &&
            baseline_result.program.complete &&
            baseline_result.cost.has_value() &&
            std::isfinite(*baseline_result.cost);
        record.verified = false;
        record.time_limited =
            baseline_result.status == TemporalBaselineStatusV1::TIME_LIMIT;
        record.failed =
            baseline_result.status == TemporalBaselineStatusV1::FAILED ||
            baseline_result.status == TemporalBaselineStatusV1::INDETERMINATE;
        record.num_ip_solves =
            static_cast<int>(baseline_result.oracle_call_count);
        record.solve_time_sec =
            std::chrono::duration<double>(Clock::now() - wall_start).count();
        if (record.failed) {
          record.error_message = baseline_result.failure_reason;
        }
        record.configuration["cost_role"] = baseline_result.cost_role;
      }
      record.timeout = record.time_limited;
      record.objective_value =
          std::numeric_limits<double>::quiet_NaN();
      record.lower_bound = std::numeric_limits<double>::quiet_NaN();
      record.upper_bound = std::numeric_limits<double>::quiet_NaN();
      record.gap = std::numeric_limits<double>::quiet_NaN();
      record.peak_memory_mb = BenchmarkRunner::get_peak_memory_mb();
      record.wall_time_sec =
          std::chrono::duration<double>(Clock::now() - wall_start).count();
      record.total_wall_time_sec = record.wall_time_sec;
      record.cpu_time_sec =
          static_cast<double>(std::clock() - cpu_start) / CLOCKS_PER_SEC;
      record.result_json_path = (run_dir / "result.json").string();
      write_result(record);
      static_cast<void>(ilp);
      return record;
    }

    auto solver = StaticSolverRegistry::create(algorithm_name, ilp);
    if (!solver) {
      throw std::runtime_error("unknown algorithm: " + algorithm_name);
    }
    if (auto* brute = dynamic_cast<BruteForceSolver*>(solver.get())) {
      brute->set_max_assignments(config.brute_force_max_assignments);
    }
    const double static_time_limit = diagnostic
        ? std::min(config.per_ip_time_limit_sec, 0.5)
        : config.per_ip_time_limit_sec;
    solver->set_time_limit(static_time_limit);
    if (diagnostic) {
      solver->set_time_limit(std::min(config.per_ip_time_limit_sec, 0.5));
    }
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
      record.initial_peak_cost = result.initial_peak_cost;
      record.peak_time = result.peak_time;
      record.heuristic_gap = result.heuristic_gap;
      record.peak_consistent = result.peak_consistent;
      record.verification_kind =
          result.verified ? VerificationKind::CERTIFIED_CONTINUOUS
                          : VerificationKind::NONE;
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
    } else if (objective == ObjectiveType::MIN_SUM) {
      MinSumSolver::Config solver_config;
      solver_config.time_limit_per_ip = config.per_ip_time_limit_sec;
      solver_config.global_time_limit_sec = global_limit;
      solver_config.gap_target = config.gap_target;
      solver_config.refinement_policy = config.minsum_refinement_policy;
      if (config.solver_method ==
          PipelineSolverMethod::MINSUM_ADAPTIVE) {
        solver_config.refinement_policy =
            MinSumRefinementPolicy::HEURISTIC_ADAPTIVE;
      }
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
    } else {
      auto solver_config = config.minmaxsum_config;
      solver_config.global_time_limit_sec = global_limit;
      solver_config.minmax_config.time_limit_per_ip =
          config.per_ip_time_limit_sec;
      solver_config.minsum_config.time_limit_per_ip =
          config.per_ip_time_limit_sec;
      solver_config.minmax_config.gap_target = config.gap_target;
      solver_config.minsum_config.gap_target = config.gap_target;
      solver_config.minsum_config.refinement_policy =
          config.minsum_refinement_policy;
      solver_config.minmax_config.verify_each_iteration =
          config.verify_each_iteration;
      solver_config.minsum_config.verify_each_iteration =
          config.verify_each_iteration;
      solver_config.seed = record.seed;
      solver_config.minmax_source_run =
          (run_dir / "component-minmax").string();
      solver_config.minsum_source_run =
          (run_dir / "component-minsum").string();
      const auto result =
          MinMaxSumSolver::solve(instance, *solver, solver_config, budget);
      record.objective_value =
          std::numeric_limits<double>::quiet_NaN();
      record.lower_bound = std::numeric_limits<double>::quiet_NaN();
      record.certified_lower_bound =
          std::numeric_limits<double>::quiet_NaN();
      record.heuristic_lower_bound =
          std::numeric_limits<double>::quiet_NaN();
      record.gap = std::numeric_limits<double>::quiet_NaN();
      record.peak_cost = result.peak_cost;
      record.initial_peak_cost =
          std::numeric_limits<double>::quiet_NaN();
      record.heuristic_gap = std::numeric_limits<double>::quiet_NaN();
      record.peak_time =
          result.feasible ? result.solution.peak_time()
                          : std::numeric_limits<double>::quiet_NaN();
      record.integral_cost = result.integral_cost;
      record.peak_consistent = result.peak_consistent;
      record.minmaxsum_minmax_status = result.minmax_component_status;
      record.minmaxsum_minsum_status = result.minsum_component_status;
      record.minmaxsum_minmax_optimality =
          result.minmax_component_optimality;
      record.minmaxsum_minsum_optimality =
          result.minsum_component_optimality;
      record.dominates_minmax = result.dominates_minmax;
      record.dominates_minsum = result.dominates_minsum;
      record.dominance_invariants_ok = result.dominance_invariants_ok;
      record.minmax_component_peak = result.minmax_component_peak;
      record.minmax_component_integral = result.minmax_component_integral;
      record.minsum_component_peak = result.minsum_component_peak;
      record.minsum_component_integral = result.minsum_component_integral;
      record.minmax_component_certified_gap =
          result.minmax_component_certified_gap;
      record.minsum_component_certified_gap =
          result.minsum_component_certified_gap;
      record.minmax_source_run = result.minmax_source_run;
      record.minsum_source_run = result.minsum_source_run;
      record.optimality_status = result.joint_optimality_status;
      record.solve_time_sec = result.total_time_sec;
      record.verification_time_sec = result.verification_time_sec;
      record.verified = result.verified;
      record.verification_kind =
          result.verified ? VerificationKind::CERTIFIED_CONTINUOUS
                          : VerificationKind::NONE;
      record.feasible = result.feasible;
      record.time_limited = result.time_limited;
      if (!result.minmax_error.empty() || !result.minsum_error.empty()) {
        record.error_message = "MinMax: " + result.minmax_error +
                               "; MinSum: " + result.minsum_error;
      }
      solution = result.solution;
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
    if (solution.is_well_formed()) {
      record.peak_cost = solution.peak_cost();
      record.integral_cost = solution.total_integral();
    }
    if ((objective == ObjectiveType::MIN_MAX ||
         objective == ObjectiveType::MIN_MAX_SUM) &&
        solution.is_well_formed()) {
      record.peak_time = solution.peak_time();
    }
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

  if (is_temporal_solver_method(config.solver_method)) {
    if (is_temporal_bound_solver_method(config.solver_method) &&
        record.temporal_result_v2.is_null()) {
      record.temporal_result_v2 =
          temporal_thesis_failure_to_json(record.error_message);
    } else if (!is_temporal_bound_solver_method(config.solver_method) &&
               record.temporal_baseline_result_v1.is_null()) {
      const auto baseline =
          config.solver_method == PipelineSolverMethod::B_NN
              ? TemporalBaselineKindV1::B_NN
          : config.solver_method == PipelineSolverMethod::B_STATIC_HOLD
              ? TemporalBaselineKindV1::B_STATIC_HOLD
              : TemporalBaselineKindV1::B_MINMAX;
      record.temporal_baseline_result_v1 =
          temporal_baseline_failure_to_json(baseline, record.error_message);
    }
  }

  if (kont_solver != nullptr) {
    const KontExecutionProvenance provenance =
        kont_solver->thread_execution_provenance();
    record.native_solve_count =
        provenance.native_solve_count - provenance_before.native_solve_count;
    record.fallback_solve_count =
        provenance.fallback_solve_count - provenance_before.fallback_solve_count;
    record.failed_native_solve_count = provenance.failed_native_solve_count -
                                       provenance_before.failed_native_solve_count;
    if (algorithm_name == "ip-kont") {
      record.native_kont = provenance.native_solve_count > 0;
      record.fallback_used = provenance.fallback_solve_count > 0;
      record.actual_backend =
          record.native_kont
              ? "KONT/COPT"
              : (record.fallback_used
                     ? "built-in-branch-and-bound-fallback"
                     : "native-backend-not-executed");
      record.solver_version =
          record.native_kont ? config.solver_version : "not-applicable";
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
           objective == ObjectiveType::MIN_MAX_SUM ? record.peak_cost
                                                    : record.objective_value,
           record.wall_time_sec, record.verified);
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
  const bool temporal_results =
      !records.empty() &&
      is_temporal_method_name(records.front().solver_method);
  const bool bound_temporal_results =
      temporal_results &&
      is_bound_temporal_method_name(records.front().solver_method);
  if (std::any_of(records.begin(), records.end(),
                  [&records, temporal_results](const BatchRunRecord& record) {
                    return is_temporal_method_name(record.solver_method) !=
                               temporal_results ||
                           (temporal_results &&
                            record.solver_method !=
                                records.front().solver_method);
                  })) {
    throw std::invalid_argument(
        "batch CSV cannot mix legacy and temporal result schemas");
  }
  if (temporal_results) {
    if (!bound_temporal_results) {
      const auto method =
          pipeline_solver_method_from_string(records.front().solver_method);
      if (method == PipelineSolverMethod::B_NN) {
        csv_output << "instance_name,solver_method,status,program_complete,"
                      "program_feasibility,cost,cost_role,cost_validity,"
                      "assignment_switch_count,oracle_call_count,wall_time_sec\n";
      } else if (method == PipelineSolverMethod::B_STATIC_HOLD) {
        csv_output << "instance_name,solver_method,status,program_complete,"
                      "program_feasibility,cost,cost_role,cost_validity,"
                      "static_termination,optimality_proved,objective_at_zero,"
                      "oracle_call_count,wall_time_sec\n";
      } else if (method == PipelineSolverMethod::B_MINMAX) {
        csv_output << "instance_name,solver_method,status,program_complete,"
                      "program_feasibility,cost,cost_role,cost_validity,"
                      "minmax_subproblem_count,optimal_subproblem_count,"
                      "feasible_subproblem_count,oracle_call_count,wall_time_sec\n";
      } else {
        throw std::logic_error("unexpected temporal baseline CSV method");
      }
      const auto write_value = [&csv_output](const Json& value) {
        if (value.is_number()) {
          csv_output << value.get<double>();
        }
      };
      for (const auto& record : records) {
        const Json& baseline = record.temporal_baseline_result_v1;
        csv_output << csv_escape(record.instance_name) << ','
                   << csv_escape(record.solver_method) << ','
                   << baseline.value("status", std::string{}) << ','
                   << baseline.value("program_complete", false) << ','
                   << baseline.value("program_feasibility", std::string{})
                   << ',';
        write_value(baseline.value("cost", Json(nullptr)));
        csv_output << ','
                   << baseline.value("cost_role", std::string{}) << ','
                   << baseline.value("cost_validity", std::string{}) << ',';
        if (method == PipelineSolverMethod::B_NN) {
          csv_output
              << baseline.value("assignment_switch_count", std::uint64_t{0})
              << ','
              << baseline.value("oracle_call_count", std::uint64_t{0}) << ','
              << record.wall_time_sec << '\n';
        } else if (method == PipelineSolverMethod::B_STATIC_HOLD) {
          const Json stat =
              baseline.value("static_at_zero", Json::object());
          csv_output << stat.value("termination", std::string{}) << ','
                     << stat.value("optimality_proved", false) << ',';
          write_value(stat.value("objective_at_zero", Json(nullptr)));
          csv_output << ','
                     << baseline.value("oracle_call_count", std::uint64_t{0})
                     << ',' << record.wall_time_sec << '\n';
        } else {
          const Json subproblems =
              baseline.value("minmax_subproblems", Json::array());
          std::uint64_t optimal = 0U;
          std::uint64_t feasible = 0U;
          for (const auto& subproblem : subproblems) {
            const auto termination =
                subproblem.value("termination", std::string{});
            optimal += termination == "OPTIMAL" ? 1U : 0U;
            feasible += termination == "FEASIBLE" ? 1U : 0U;
          }
          csv_output << subproblems.size() << ',' << optimal << ','
                     << feasible << ','
                     << baseline.value("oracle_call_count", std::uint64_t{0})
                     << ',' << record.wall_time_sec << '\n';
        }
      }
      if (!csv_output) {
        throw std::runtime_error(
            "failed writing temporal baseline master CSV: " + csv_path);
      }
      return;
    }
    csv_output
        << "instance_name,solver_method,algorithm_name,objective,n,m,repeat,"
           "status,stop_reason,LB,LB_valid,UB,UB_valid,absolute_gap,"
           "gamma_rep,gamma_cert,solve_time_sec,verify_time_sec,wall_time_sec,"
           "leaf_count,oracle_call_count,verified,verifier_status\n";
    for (const auto& record : records) {
      const Json& temporal = record.temporal_result_v2;
      const Json final = temporal.value("final", Json::object());
      const Json execution = temporal.value("execution", Json::object());
      const auto write_value = [&csv_output](const Json& value) {
        if (value.is_number()) {
          csv_output << value.get<double>();
        }
      };
      csv_output << csv_escape(record.instance_name) << ','
                 << csv_escape(record.solver_method) << ','
                 << csv_escape(record.algorithm_name) << ','
                 << csv_escape(record.objective) << ',' << record.n << ','
                 << record.m << ',' << record.repeat << ','
                 << csv_escape(final.value("status", std::string{})) << ','
                 << csv_escape(final.value("stop_reason", std::string{}))
                 << ',';
      write_value(final.value("LB", Json(nullptr)));
      csv_output << ','
                 << final.value("LB_valid", false) << ',';
      write_value(final.value("UB", Json(nullptr)));
      csv_output << ','
                 << final.value("UB_valid", false) << ',';
      write_value(final.value("absolute_gap", Json(nullptr)));
      csv_output << ',';
      write_value(final.value("gamma_rep", Json(nullptr)));
      csv_output << ',';
      write_value(final.value("gamma_cert", Json(nullptr)));
      csv_output << ',';
      write_value(final.value("solve_time_sec", Json(nullptr)));
      csv_output << ',';
      write_value(final.value("verify_time_sec", Json(nullptr)));
      csv_output << ',' << record.wall_time_sec << ','
                 << execution.value("leaf_count", std::uint64_t{0}) << ','
                 << execution.value("oracle_call_count", std::uint64_t{0})
                 << ',' << record.verified << ','
                 << csv_escape(execution.value("verifier_status",
                                               std::string{}))
                 << '\n';
    }
    if (!csv_output) {
      throw std::runtime_error("failed writing temporal batch CSV: " +
                               csv_path);
    }
    return;
  }
  csv_output
      << "instance_name,algorithm_name,algorithm_category,requested_backend,"
         "selected_backend,actual_backend,solver_name,kont_version,native_kont,"
         "fallback_used,native_solve_count,fallback_solve_count,"
         "failed_native_solve_count,solver_runtime_sec,objective,repeat,n,m,wall_time_sec,"
         "solve_time_sec,cpu_time_sec,peak_memory_mb,time_limit_per_ip_sec,"
         "peak_cost,peak_time,initial_peak_cost,heuristic_gap,peak_consistent,"
         "integral_cost,objective_vector_peak_cost,"
         "objective_vector_integral_cost,minmaxsum_minmax_status,"
         "minmaxsum_minsum_status,"
         "minmaxsum_minmax_optimality,minmaxsum_minsum_optimality,"
         "dominates_minmax,dominates_minsum,dominance_invariants_ok,"
         "minmax_component_peak,minmax_component_integral,"
         "minsum_component_peak,minsum_component_integral,"
         "minmax_component_certified_gap,minsum_component_certified_gap,"
         "minmax_source_run,"
         "minsum_source_run,empirical_ratio_to_exact,ratio_to_incumbent,"
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

  const bool temporal_results =
      !records.empty() &&
      is_temporal_method_name(records.front().solver_method);
  if (temporal_results) {
    if (!is_bound_temporal_method_name(records.front().solver_method)) {
      output << "# Temporal Baseline Batch Run Summary\n\n"
             << "| Instance | Baseline | Status | Program | Cost | Cost role | "
                "Wall time (s) |\n"
             << "|---|---|---|:---:|---:|---|---:|\n";
      for (const auto& record : records) {
        const Json baseline = record.temporal_baseline_result_v1;
        const Json cost = baseline.value("cost", Json(nullptr));
        output << '|' << record.instance_name << '|' << record.solver_method
               << '|' << baseline.value("status", std::string("FAILED"))
               << '|' << (baseline.value("program_complete", false) ? "yes"
                                                                     : "no")
               << '|';
        if (cost.is_number()) {
          output << cost.get<double>();
        } else {
          output << "n/a";
        }
        output << '|' << baseline.value("cost_role", std::string{}) << '|'
               << record.wall_time_sec << "|\n";
      }
      if (!output) {
        throw std::runtime_error(
            "failed writing temporal baseline batch summary: " + path);
      }
      return;
    }
    output << "# Temporal Batch Run Summary\n\n"
           << "| Instance | Status | Stop reason | LB | UB | Wall time (s) | "
              "Verified |\n"
           << "|---|---|---|---:|---:|---:|:---:|\n";
    for (const auto& record : records) {
      const Json temporal = record.temporal_result_v2;
      const Json final = temporal.value("final", Json::object());
      output << '|' << record.instance_name << '|'
             << final.value("status", std::string("INCOMPLETE")) << '|'
             << final.value("stop_reason", std::string("INVALID_INPUT"))
             << '|';
      for (const char* bound : {"LB", "UB"}) {
        const Json value = final.value(bound, Json(nullptr));
        if (value.is_number()) {
          output << value.get<double>();
        } else {
          output << "n/a";
        }
        output << '|';
      }
      output << record.wall_time_sec << '|'
             << (record.verified ? "yes" : "no") << "|\n";
    }
    if (!output) {
      throw std::runtime_error(
          "failed writing temporal batch summary: " + path);
    }
    return;
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
        values << record->objective << ": " << std::setprecision(6);
        if (record->objective == "minmaxsum") {
          values << '(' << record->peak_cost << ", "
                 << record->integral_cost << ')';
        } else {
          values << record->objective_value;
        }
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
