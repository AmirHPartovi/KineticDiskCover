#include "kdc/comparative_runner.hpp"

#include "kdc/benchmark_protocol.hpp"
#include "kdc/logging.hpp"
#include "kdc/solution_serializer.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <chrono>
#include <ctime>
#include <vector>

namespace kdc {
namespace {
std::string csv_escape(const std::string& value) {
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

void ensure_parent_directory(const std::string& path) {
  const std::filesystem::path target(path);
  if (target.has_parent_path()) {
    std::filesystem::create_directories(target.parent_path());
  }
}

std::string solution_filename(const std::string& name,
                              const char* objective) {
  std::string safe_name;
  safe_name.reserve(name.size());
  for (const char raw_character : name) {
    const auto character = static_cast<unsigned char>(raw_character);
    const bool safe = (character >= 'a' && character <= 'z') ||
                      (character >= 'A' && character <= 'Z') ||
                      (character >= '0' && character <= '9') ||
                      character == '-' || character == '_' || character == '.';
    safe_name += safe ? static_cast<char>(character) : '_';
  }
  if (safe_name.empty() || safe_name == "." || safe_name == "..") {
    safe_name = "instance";
  }
  return safe_name + "_" + objective + ".json";
}

nlohmann::json to_json(const ComparativeResult& result) {
  return {{"schema_version", 2},
          {"instance_name", result.instance_name},
          {"n", result.n},
          {"m", result.m},
          {"algorithm_name", result.algorithm_name},
          {"algorithm_category", result.algorithm_category},
          {"requested_backend", result.requested_backend},
          {"actual_backend", result.actual_backend},
          {"objective", "both"},
          {"seed", result.seed},
          {"repeat", result.repeat},
          {"experiment_id", result.experiment_id},
          {"peak_cost_minmax", result.peak_cost_minmax},
          {"lb_minmax", result.lb_minmax},
          {"gap_minmax", result.gap_minmax},
          {"time_minmax_sec", result.time_minmax_sec},
          {"iters_minmax", result.iters_minmax},
          {"integral_minsum", result.integral_minsum},
          {"lb_minsum", result.lb_minsum},
          {"gap_minsum", result.gap_minsum},
          {"time_minsum_sec", result.time_minsum_sec},
          {"iters_minsum", result.iters_minsum},
          {"peak_ratio", result.peak_ratio},
          {"integral_ratio", result.integral_ratio},
          {"peak_diff_abs", result.peak_diff_abs},
          {"integral_diff_abs", result.integral_diff_abs},
          {"verified_minmax", result.verified_minmax},
          {"verified_minsum", result.verified_minsum},
          {"verification_kind_minmax",
           verification_kind_to_string(result.verification_kind_minmax)},
          {"verification_kind_minsum",
           verification_kind_to_string(result.verification_kind_minsum)},
          {"verification_time_minmax_sec",
           result.verification_time_minmax_sec},
          {"verification_time_minsum_sec",
           result.verification_time_minsum_sec},
          {"serialization_time_sec", result.serialization_time_sec},
          {"total_wall_time_sec", result.total_wall_time_sec},
          {"feasible_minmax", result.feasible_minmax},
          {"feasible_minsum", result.feasible_minsum},
          {"timeout_minmax", result.timeout_minmax},
          {"timeout_minsum", result.timeout_minsum},
          {"optimality_status_minmax",
           optimality_status_to_string(result.optimality_status_minmax)},
          {"optimality_status_minsum",
           optimality_status_to_string(result.optimality_status_minsum)},
          {"git_commit", KDC_GIT_COMMIT},
          {"compiler", __VERSION__},
          {"build_type", KDC_BUILD_TYPE},
          {"thread_count", 1},
          {"configuration", result.configuration}};
}
}  // namespace

ComparativeResult ComparativeRunner::run(const Instance& instance,
                                         IStaticSolver& static_solver,
                                         const Config& config) {
  LOG_INFO("ComparativeRunner: running on {} (n={}, m={}, algo={})",
           instance.name, instance.n, instance.m, static_solver.name());

  const auto wall_start = std::chrono::steady_clock::now();
  std::uint32_t seed_hash = 2166136261U ^ config.seed;
  const std::string seed_key =
      instance.name + std::to_string(instance.id);
  for (const unsigned char character : seed_key) {
    seed_hash = (seed_hash ^ character) * 16777619U;
  }
  const unsigned seed = seed_hash;
  static_solver.set_seed(seed);
  auto minmax_config = config.minmax_cfg;
  auto minsum_config = config.minsum_cfg;
  const double global_limit = static_solver.is_exact()
                                  ? config.exact_time_limit_sec
                                  : config.fast_time_limit_sec;
  minmax_config.time_limit_per_ip = config.per_static_time_limit_sec;
  minsum_config.time_limit_per_ip = config.per_static_time_limit_sec;
  minmax_config.global_time_limit_sec = global_limit;
  minsum_config.global_time_limit_sec = global_limit;
  minmax_config.verify_after = true;
  minsum_config.verify_after = true;
  if (config.profile == BenchmarkProfile::FAST) {
    minsum_config.refinement_policy =
        MinSumRefinementPolicy::HEURISTIC_ADAPTIVE;
  } else {
    minsum_config.refinement_policy =
        MinSumRefinementPolicy::CERTIFIED_BOUND;
  }
  if (config.profile == BenchmarkProfile::DEBUG) {
    minmax_config.verify_each_iteration = true;
    minsum_config.verify_each_iteration = true;
  }
  const auto minmax_result =
      MinMaxSolver::solve(instance, static_solver, minmax_config);

  const auto minsum_result =
      MinSumSolver::solve(instance, static_solver, minsum_config);

  ComparativeResult result;
  result.instance_name = instance.name;
  result.n = instance.n;
  result.m = instance.m;
  result.algorithm_name = static_solver.name();
  result.algorithm_category =
      static_solver.is_exact() ? "exact_reference" : "heuristic";
  result.requested_backend = config.requested_backend;
  result.actual_backend = config.actual_backend.empty()
                              ? static_solver.name()
                              : config.actual_backend;
  result.seed = seed;
  result.peak_cost_minmax = minmax_result.peak_cost;
  result.lb_minmax = minmax_result.lower_bound;
  result.gap_minmax = minmax_result.gap;
  result.time_minmax_sec = minmax_result.total_time_sec;
  result.iters_minmax = minmax_result.num_iterations;
  result.integral_minsum = minsum_result.total_integral;
  result.lb_minsum = minsum_result.lower_bound_integral;
  result.gap_minsum = minsum_result.gap;
  result.time_minsum_sec = minsum_result.total_time_sec;
  result.iters_minsum = minsum_result.num_iterations;
  const double minsum_peak = minsum_result.solution.peak_cost();
  const double minmax_integral = minmax_result.solution.total_integral();
  result.peak_ratio =
      minsum_peak > 0.0 ? minmax_result.peak_cost / minsum_peak : 1.0;
  result.integral_ratio =
      minmax_integral > 0.0
          ? minsum_result.total_integral / minmax_integral
          : 1.0;
  result.peak_diff_abs = minmax_result.peak_cost - minsum_peak;
  result.integral_diff_abs =
      minsum_result.total_integral - minmax_integral;
  result.verified_minmax = minmax_result.verified;
  result.verified_minsum = minsum_result.verified;
  result.verification_kind_minmax = minmax_result.verification_kind;
  result.verification_kind_minsum = minsum_result.verification_kind;
  result.verification_time_minmax_sec =
      minmax_result.verification_time_sec;
  result.verification_time_minsum_sec =
      minsum_result.verification_time_sec;
  result.feasible_minmax = minmax_result.feasible;
  result.feasible_minsum = minsum_result.feasible;
  result.timeout_minmax = minmax_result.time_limited;
  result.timeout_minsum = minsum_result.time_limited;
  result.optimality_status_minmax = minmax_result.optimality_status;
  result.optimality_status_minsum = minsum_result.optimality_status;
  result.configuration = {
      {"profile", benchmark_profile_to_string(config.profile)},
      {"seed", result.seed},
      {"global_time_limit_sec", global_limit},
      {"per_static_time_limit_sec", config.per_static_time_limit_sec},
      {"verify_after", true},
      {"verify_each_iteration",
       config.profile == BenchmarkProfile::DEBUG},
      {"minsum_refinement_policy",
       minsum_refinement_policy_to_string(minsum_config.refinement_policy)},
      {"handovers_enabled", true},
      {"cache_policy", "auto"}};

  if (config.save_solutions) {
    const auto serialization_started = std::chrono::steady_clock::now();
    std::filesystem::create_directories(config.output_dir);
    const std::filesystem::path output_dir(config.output_dir);
    SolutionSerializer::save_json(
        instance, minmax_result.solution,
        (output_dir / solution_filename(instance.name, "minmax")).string());
    SolutionSerializer::save_json(
        instance, minsum_result.solution,
        (output_dir / solution_filename(instance.name, "minsum")).string());
    result.serialization_time_sec =
        std::chrono::duration<double>(std::chrono::steady_clock::now() -
                                      serialization_started)
            .count();
  }
  result.total_wall_time_sec =
      std::chrono::duration<double>(std::chrono::steady_clock::now() -
                                    wall_start)
          .count();
  LOG_INFO("ComparativeRunner: peak_ratio={:.4f} integral_ratio={:.4f}",
           result.peak_ratio, result.integral_ratio);
  return result;
}

void ComparativeRunner::run_all(const std::vector<Instance>& instances,
                                IStaticSolver& static_solver,
                                const Config& config) {
  std::vector<ComparativeResult> results;
  if (config.repeats <= 0) {
    throw std::invalid_argument("comparative repeat count must be positive");
  }
  results.reserve(instances.size() *
                  static_cast<std::size_t>(config.repeats));
  const std::string experiment_id =
      "kdc-comparative-" +
      std::to_string(
          std::chrono::system_clock::now().time_since_epoch().count()) +
      "-seed" + std::to_string(config.seed);
  for (const auto& instance : instances) {
    const int repeat_count =
        static_solver.is_exact() ? 1 : config.repeats;
    for (int repeat = 0; repeat < repeat_count; ++repeat) {
      auto repeat_config = config;
      repeat_config.seed = config.seed + static_cast<unsigned>(repeat);
      auto result = run(instance, static_solver, repeat_config);
      result.repeat = repeat;
      result.experiment_id = experiment_id;
      results.push_back(std::move(result));
    }
  }
  std::filesystem::create_directories(config.output_dir);
  const std::filesystem::path output_dir(config.output_dir);
  const auto manifest = make_experiment_manifest(
      experiment_id, {}, "in-memory",
      std::to_string(instances.size()) + ":" +
          (instances.empty() ? std::string{} : instances.front().name),
      config.profile, {static_solver.name()}, config.requested_backend,
      config.actual_backend.empty() ? static_solver.name()
                                    : config.actual_backend,
      nlohmann::json{{"selected_backend", static_solver.name()},
                     {"successful_runs", nlohmann::json::array()},
                     {"rejected_runs", nlohmann::json::array()}},
      config.fast_time_limit_sec, config.exact_time_limit_sec,
      config.per_static_time_limit_sec, config.seed, config.repeats,
      config.profile == BenchmarkProfile::FAST
          ? "HEURISTIC_ADAPTIVE"
          : "CERTIFIED_BOUND",
      config.profile == BenchmarkProfile::DEBUG, true, true, "auto", 1);
  write_experiment_manifest(
      (output_dir / "experiment_manifest.json").string(), manifest);
  save_json(results, (output_dir / "comparative.json").string());
  save_csv(results, (output_dir / "comparative.csv").string());

  std::cout << "Instance, algorithm, minmax peak, minsum integral, "
               "peak ratio, integral ratio\n";
  for (const auto& result : results) {
    std::cout << result.instance_name << ", " << result.algorithm_name << ", "
              << result.peak_cost_minmax << ", " << result.integral_minsum
              << ", " << result.peak_ratio << ", "
              << result.integral_ratio << '\n';
  }
}

void ComparativeRunner::save_json(
    const std::vector<ComparativeResult>& results, const std::string& path) {
  ensure_parent_directory(path);
  nlohmann::json document = nlohmann::json::array();
  for (const auto& result : results) {
    document.push_back(to_json(result));
  }
  std::ofstream output(path);
  if (!output) {
    throw std::runtime_error("cannot open comparative JSON: " + path);
  }
  output << std::setw(4) << document << '\n';
  if (!output) {
    throw std::runtime_error("failed writing comparative JSON: " + path);
  }
}

void ComparativeRunner::save_csv(
    const std::vector<ComparativeResult>& results, const std::string& path) {
  ensure_parent_directory(path);
  std::ofstream output(path);
  if (!output) {
    throw std::runtime_error("cannot open comparative CSV: " + path);
  }
  output << "instance,n,m,algorithm,peak_minmax,lb_minmax,gap_minmax,"
            "time_minmax,iters_minmax,integral_minsum,lb_minsum,gap_minsum,"
            "time_minsum,iters_minsum,peak_ratio,integral_ratio,peak_diff,"
            "integral_diff,verified_minmax,verified_minsum,"
            "verification_kind_minmax,verification_kind_minsum,"
            "verification_time_minmax_sec,verification_time_minsum_sec\n";
  for (const auto& result : results) {
    output << csv_escape(result.instance_name) << ',' << result.n << ','
           << result.m << ',' << csv_escape(result.algorithm_name) << ','
           << result.peak_cost_minmax << ',' << result.lb_minmax << ','
           << result.gap_minmax << ',' << result.time_minmax_sec << ','
           << result.iters_minmax << ',' << result.integral_minsum << ','
           << result.lb_minsum << ',' << result.gap_minsum << ','
           << result.time_minsum_sec << ',' << result.iters_minsum << ','
           << result.peak_ratio << ',' << result.integral_ratio << ','
           << result.peak_diff_abs << ',' << result.integral_diff_abs << ','
           << (result.verified_minmax ? "true" : "false") << ','
           << (result.verified_minsum ? "true" : "false") << ','
           << verification_kind_to_string(result.verification_kind_minmax)
           << ','
           << verification_kind_to_string(result.verification_kind_minsum)
           << ',' << result.verification_time_minmax_sec << ','
           << result.verification_time_minsum_sec << '\n';
  }
  if (!output) {
    throw std::runtime_error("failed writing comparative CSV: " + path);
  }
}
}  // namespace kdc
