#include "kdc/comparative_runner.hpp"

#include "kdc/logging.hpp"
#include "kdc/solution_serializer.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace kdc {
namespace {
using Clock = std::chrono::steady_clock;

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
  return {{"instance_name", result.instance_name},
          {"n", result.n},
          {"m", result.m},
          {"algorithm_name", result.algorithm_name},
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
          {"verified_minsum", result.verified_minsum}};
}
}  // namespace

ComparativeResult ComparativeRunner::run(const Instance& instance,
                                         IStaticSolver& static_solver,
                                         const Config& config) {
  LOG_INFO("ComparativeRunner: running on {} (n={}, m={}, algo={})",
           instance.name, instance.n, instance.m, static_solver.name());

  const auto minmax_start = Clock::now();
  const auto minmax_result =
      MinMaxSolver::solve(instance, static_solver, config.minmax_cfg);
  const double minmax_elapsed =
      std::chrono::duration<double>(Clock::now() - minmax_start).count();

  const auto minsum_start = Clock::now();
  const auto minsum_result =
      MinSumSolver::solve(instance, static_solver, config.minsum_cfg);
  const double minsum_elapsed =
      std::chrono::duration<double>(Clock::now() - minsum_start).count();

  ComparativeResult result;
  result.instance_name = instance.name;
  result.n = instance.n;
  result.m = instance.m;
  result.algorithm_name = static_solver.name();
  result.peak_cost_minmax = minmax_result.peak_cost;
  result.lb_minmax = minmax_result.lower_bound;
  result.gap_minmax = minmax_result.gap;
  result.time_minmax_sec = minmax_elapsed;
  result.iters_minmax = minmax_result.num_iterations;
  result.integral_minsum = minsum_result.total_integral;
  result.lb_minsum = minsum_result.lower_bound_integral;
  result.gap_minsum = minsum_result.gap;
  result.time_minsum_sec = minsum_elapsed;
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

  if (config.save_solutions) {
    std::filesystem::create_directories(config.output_dir);
    const std::filesystem::path output_dir(config.output_dir);
    SolutionSerializer::save_json(
        instance, minmax_result.solution,
        (output_dir / solution_filename(instance.name, "minmax")).string());
    SolutionSerializer::save_json(
        instance, minsum_result.solution,
        (output_dir / solution_filename(instance.name, "minsum")).string());
  }
  LOG_INFO("ComparativeRunner: peak_ratio={:.4f} integral_ratio={:.4f}",
           result.peak_ratio, result.integral_ratio);
  return result;
}

void ComparativeRunner::run_all(const std::vector<Instance>& instances,
                                IStaticSolver& static_solver,
                                const Config& config) {
  std::vector<ComparativeResult> results;
  results.reserve(instances.size());
  for (const auto& instance : instances) {
    results.push_back(run(instance, static_solver, config));
  }
  std::filesystem::create_directories(config.output_dir);
  const std::filesystem::path output_dir(config.output_dir);
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
            "integral_diff,verified_minmax,verified_minsum\n";
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
           << (result.verified_minsum ? "true" : "false") << '\n';
  }
  if (!output) {
    throw std::runtime_error("failed writing comparative CSV: " + path);
  }
}
}  // namespace kdc
