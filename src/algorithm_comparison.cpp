#include "kdc/algorithm_comparison.hpp"

#include "kdc/io.hpp"
#include "kdc/logging.hpp"
#include "kdc/minmax.hpp"
#include "kdc/minsum.hpp"
#include "kdc/static_solver_registry.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <map>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace kdc {
namespace {
using Clock = std::chrono::steady_clock;
using ReferenceKey = std::tuple<Index, int, double>;

std::string csv_escape(const std::string& value) {
  std::string escaped = "\"";
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

nlohmann::json result_to_json(const StaticComparisonResult& result) {
  return {{"instance_name", result.instance_name},
          {"algorithm_name", result.algorithm_name},
          {"n", result.n},
          {"m", result.m},
          {"t", result.t},
          {"cost", result.cost},
          {"lower_bound", result.lower_bound},
          {"upper_bound", result.upper_bound},
          {"gap", result.gap},
          {"wall_time_sec", result.wall_time_sec},
          {"feasible", result.feasible},
          {"verified", result.verified},
          {"matches_reference", result.matches_reference},
          {"ratio_to_reference", result.ratio_to_reference}};
}

nlohmann::json result_to_json(const KineticComparisonResult& result) {
  return {{"instance_name", result.instance_name},
          {"algorithm_name", result.algorithm_name},
          {"objective", result.objective},
          {"n", result.n},
          {"m", result.m},
          {"objective_value", result.objective_value},
          {"lower_bound", result.lower_bound},
          {"gap", result.gap},
          {"wall_time_sec", result.wall_time_sec},
          {"num_iterations", result.num_iterations},
          {"verified", result.verified}};
}
}

bool AlgorithmComparator::verify_static_solution(const Instance& instance,
                                                double time,
                                                const StaticSolution& solution,
                                                double tolerance) {
  if (!solution.feasible || !std::isfinite(tolerance) || tolerance < 0.0 ||
      instance.n < 0 || instance.m < 0 ||
      static_cast<Index>(instance.n) != instance.trajectories.size() ||
      static_cast<Index>(instance.m) != instance.stations.size() ||
      solution.supporting_point.size() !=
          static_cast<Index>(instance.m) ||
      solution.radius.size() != static_cast<Index>(instance.m) ||
      !std::isfinite(solution.cost)) {
    return false;
  }
  const double pi = std::acos(-1.0);
  double computed_cost = 0.0;
  for (int station = 0; station < instance.m; ++station) {
    const Index station_index = static_cast<Index>(station);
    const int support = solution.supporting_point[station_index];
    const double radius = solution.radius[station_index];
    if (!std::isfinite(radius) || radius < 0.0) {
      return false;
    }
    computed_cost += pi * radius * radius;
    if (support == -1) {
      if (radius > tolerance) {
        return false;
      }
      continue;
    }
    if (support < 0 || support >= instance.n) {
      return false;
    }
    const double support_distance =
        (instance.stations[station_index].pos -
         instance.trajectories[static_cast<Index>(support)].position(time))
            .norm();
    if (std::abs(radius - support_distance) >
        tolerance * std::max(1.0, support_distance)) {
      return false;
    }
  }
  if (std::abs(computed_cost - solution.cost) >
      tolerance * std::max(1.0, std::abs(solution.cost))) {
    return false;
  }
  for (const auto& trajectory : instance.trajectories) {
    const Point point = trajectory.position(time);
    bool covered = false;
    for (int station = 0; station < instance.m; ++station) {
      const Index station_index = static_cast<Index>(station);
      if (solution.supporting_point[station_index] >= 0 &&
          (instance.stations[station_index].pos - point).norm() <=
              solution.radius[station_index] + tolerance) {
        covered = true;
        break;
      }
    }
    if (!covered) {
      return false;
    }
  }
  return true;
}

void AlgorithmComparator::compare_static(
    const AlgorithmComparisonConfig& config, ILPSolver* ilp,
    std::vector<StaticComparisonResult>& output) {
  if (config.algorithm_names.empty() || config.instance_paths.empty() ||
      config.reference_algorithm.empty() || config.num_repeats <= 0 ||
      !std::isfinite(config.reference_tol) || config.reference_tol < 0.0 ||
      config.time_points.empty()) {
    throw std::invalid_argument("algorithm comparison configuration is invalid");
  }
  output.clear();
  std::vector<Instance> instances;
  instances.reserve(config.instance_paths.size());
  for (const auto& path : config.instance_paths) {
    instances.push_back(DatasetReader::read_json(path));
  }
  for (const double time : config.time_points) {
    if (!std::isfinite(time) || time < 0.0) {
      throw std::invalid_argument("comparison time points must be finite and >= 0");
    }
    for (const auto& instance : instances) {
      if (time > instance.T_end) {
        throw std::invalid_argument(
            "comparison time point exceeds an instance T_end");
      }
    }
  }

  std::map<ReferenceKey, double> reference_costs;
  for (Index instance_index = 0; instance_index < instances.size();
       ++instance_index) {
    auto solver =
        StaticSolverRegistry::create(config.reference_algorithm, ilp);
    if (!solver) {
      throw std::invalid_argument("unknown reference algorithm: " +
                                  config.reference_algorithm);
    }
    for (int repeat = 0; repeat < config.num_repeats; ++repeat) {
      for (const double time : config.time_points) {
        const StaticSolution solution =
            solver->solve(instances[instance_index], time);
        if (!solution.feasible || !std::isfinite(solution.cost)) {
          throw std::runtime_error("reference solver failed to produce a "
                                   "feasible solution for " +
                                   config.instance_paths[instance_index]);
        }
        reference_costs[{instance_index, repeat, time}] = solution.cost;
      }
    }
  }

  std::vector<std::string> algorithms = config.algorithm_names;
  if (std::find(algorithms.begin(), algorithms.end(),
                config.reference_algorithm) == algorithms.end()) {
    algorithms.push_back(config.reference_algorithm);
  }
  for (Index instance_index = 0; instance_index < instances.size();
       ++instance_index) {
    const Instance& instance = instances[instance_index];
    for (const std::string& algorithm : algorithms) {
      auto solver = StaticSolverRegistry::create(algorithm, ilp);
      if (!solver) {
        throw std::invalid_argument("unknown comparison algorithm: " +
                                    algorithm);
      }
      for (int repeat = 0; repeat < config.num_repeats; ++repeat) {
        for (const double time : config.time_points) {
          const auto started = Clock::now();
          const StaticSolution solution = solver->solve(instance, time);
          const double elapsed =
              std::chrono::duration<double>(Clock::now() - started).count();
          const double reference =
              reference_costs.at({instance_index, repeat, time});
          StaticComparisonResult record;
          record.instance_name =
              instance.name.empty()
                  ? std::filesystem::path(
                        config.instance_paths[instance_index])
                        .stem()
                        .string()
                  : instance.name;
          record.algorithm_name = algorithm;
          record.n = instance.n;
          record.m = instance.m;
          record.t = time;
          record.cost = solution.cost;
          record.lower_bound = solution.lower_bound;
          record.upper_bound = solution.upper_bound;
          record.gap = solution.lower_bound > 0.0
                           ? std::max(0.0, (solution.cost - solution.lower_bound) /
                                               solution.lower_bound)
                           : 0.0;
          record.wall_time_sec = elapsed;
          record.feasible = solution.feasible;
          record.verified =
              config.verify_solutions &&
              verify_static_solution(instance, time, solution);
          record.matches_reference =
              solution.feasible &&
              std::abs(solution.cost - reference) <=
                  config.reference_tol *
                      std::max(1.0, std::abs(reference));
          record.ratio_to_reference =
              reference > 1e-12
                  ? solution.cost / reference
                  : (solution.cost <= 1e-12
                         ? 1.0
                         : std::numeric_limits<double>::infinity());
          output.push_back(std::move(record));
        }
      }
    }
  }
  LOG_INFO("compare_static: {} records", output.size());
}

void AlgorithmComparator::compare_kinetic(
    const AlgorithmComparisonConfig& config, ILPSolver* ilp,
    const std::string& objective,
    std::vector<KineticComparisonResult>& output) {
  if ((objective != "minmax" && objective != "minsum") ||
      config.algorithm_names.empty() || config.instance_paths.empty()) {
    throw std::invalid_argument("kinetic comparison configuration is invalid");
  }
  output.clear();
  for (const auto& path : config.instance_paths) {
    const Instance instance = DatasetReader::read_json(path);
    for (const auto& algorithm : config.algorithm_names) {
      auto solver = StaticSolverRegistry::create(algorithm, ilp);
      if (!solver) {
        throw std::invalid_argument("unknown kinetic comparison algorithm: " +
                                    algorithm);
      }
      KineticComparisonResult record;
      record.instance_name =
          instance.name.empty() ? std::filesystem::path(path).stem().string()
                                : instance.name;
      record.algorithm_name = solver->name();
      record.objective = objective;
      record.n = instance.n;
      record.m = instance.m;
      if (objective == "minmax") {
        MinMaxSolver::Config solver_config;
        solver_config.verify_after = config.verify_solutions;
        const MinMaxSolver::Result result =
            MinMaxSolver::solve(instance, *solver, solver_config);
        record.objective_value = result.peak_cost;
        record.lower_bound = result.lower_bound;
        record.gap = result.gap;
        record.wall_time_sec = result.total_time_sec;
        record.num_iterations = result.num_iterations;
        record.verified = result.verified;
      } else {
        MinSumSolver::Config solver_config;
        solver_config.verify_after = config.verify_solutions;
        const MinSumSolver::Result result =
            MinSumSolver::solve(instance, *solver, solver_config);
        record.objective_value = result.total_integral;
        record.lower_bound = result.lower_bound_integral;
        record.gap = result.gap;
        record.wall_time_sec = result.total_time_sec;
        record.num_iterations = result.num_iterations;
        record.verified = result.verified;
      }
      output.push_back(std::move(record));
    }
  }
  LOG_INFO("compare_kinetic: {} {} records", output.size(), objective);
}

void AlgorithmComparator::save_results(
    const std::vector<StaticComparisonResult>& results,
    const std::string& json_path, const std::string& csv_path) {
  ensure_parent_directory(json_path);
  ensure_parent_directory(csv_path);
  nlohmann::json json = nlohmann::json::array();
  for (const auto& result : results) {
    json.push_back(result_to_json(result));
  }
  std::ofstream json_file(json_path);
  if (!json_file) {
    throw std::runtime_error("cannot open comparison JSON: " + json_path);
  }
  json_file << std::setw(2) << json << '\n';
  if (!json_file) {
    throw std::runtime_error("failed writing comparison JSON: " + json_path);
  }

  std::ofstream csv_file(csv_path);
  if (!csv_file) {
    throw std::runtime_error("cannot open comparison CSV: " + csv_path);
  }
  csv_file << "instance_name,algorithm_name,n,m,t,cost,lower_bound,"
              "upper_bound,gap,wall_time_sec,feasible,verified,"
              "matches_reference,ratio_to_reference\n";
  for (const auto& result : results) {
    csv_file << csv_escape(result.instance_name) << ','
             << csv_escape(result.algorithm_name) << ',' << result.n << ','
             << result.m << ',' << result.t << ',' << result.cost << ','
             << result.lower_bound << ',' << result.upper_bound << ','
             << result.gap << ',' << result.wall_time_sec << ','
             << (result.feasible ? "true" : "false") << ','
             << (result.verified ? "true" : "false") << ','
             << (result.matches_reference ? "true" : "false") << ','
             << result.ratio_to_reference << '\n';
  }
  if (!csv_file) {
    throw std::runtime_error("failed writing comparison CSV: " + csv_path);
  }
}

void AlgorithmComparator::save_results(
    const std::vector<KineticComparisonResult>& results,
    const std::string& json_path, const std::string& csv_path) {
  ensure_parent_directory(json_path);
  ensure_parent_directory(csv_path);
  nlohmann::json json = nlohmann::json::array();
  for (const auto& result : results) {
    json.push_back(result_to_json(result));
  }
  std::ofstream json_file(json_path);
  if (!json_file) {
    throw std::runtime_error("cannot open kinetic comparison JSON: " +
                             json_path);
  }
  json_file << std::setw(2) << json << '\n';
  if (!json_file) {
    throw std::runtime_error("failed writing kinetic comparison JSON: " +
                             json_path);
  }

  std::ofstream csv_file(csv_path);
  if (!csv_file) {
    throw std::runtime_error("cannot open kinetic comparison CSV: " +
                             csv_path);
  }
  csv_file << "instance_name,algorithm_name,objective,n,m,objective_value,"
              "lower_bound,gap,wall_time_sec,num_iterations,verified\n";
  for (const auto& result : results) {
    csv_file << csv_escape(result.instance_name) << ','
             << csv_escape(result.algorithm_name) << ','
             << csv_escape(result.objective) << ',' << result.n << ','
             << result.m << ',' << result.objective_value << ','
             << result.lower_bound << ',' << result.gap << ','
             << result.wall_time_sec << ',' << result.num_iterations << ','
             << (result.verified ? "true" : "false") << '\n';
  }
  if (!csv_file) {
    throw std::runtime_error("failed writing kinetic comparison CSV: " +
                             csv_path);
  }
}
}
