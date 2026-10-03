#include "kdc/algorithm_comparison.hpp"

#include "kdc/benchmark_protocol.hpp"
#include "kdc/exact_reference_selector.hpp"
#include "kdc/io.hpp"
#include "kdc/logging.hpp"
#include "kdc/minmax.hpp"
#include "kdc/minsum.hpp"
#include "kdc/static_solver_registry.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace kdc {
namespace {
using Clock = std::chrono::steady_clock;
using ReferenceKey = std::tuple<Index, int, double>;

unsigned comparison_seed(unsigned base, const std::string& instance,
                        const std::string& algorithm,
                        const std::string& objective, int repeat) {
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

std::string comparison_experiment_id(unsigned seed) {
  return "kdc-comparison-" +
         std::to_string(
             std::chrono::system_clock::now().time_since_epoch().count()) +
         "-seed" + std::to_string(seed);
}

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
  return {{"schema_version", 2},
          {"instance_name", result.instance_name},
          {"algorithm_name", result.algorithm_name},
          {"algorithm_category", result.algorithm_category},
          {"requested_backend", result.requested_backend},
          {"actual_backend", result.actual_backend},
          {"objective", result.objective},
          {"n", result.n},
          {"m", result.m},
          {"objective_value", result.objective_value},
          {"peak_cost", result.peak_cost},
          {"integral_cost", result.integral_cost},
          {"lower_bound", result.lower_bound},
          {"upper_bound", result.upper_bound},
          {"bound_status", bound_status_to_string(result.bound_status)},
          {"optimality_status",
           optimality_status_to_string(result.optimality_status)},
          {"certified_gap", result.certified_gap
                                ? nlohmann::json(*result.certified_gap)
                                : nlohmann::json(nullptr)},
          {"empirical_ratio_to_exact",
           result.empirical_ratio_to_exact
               ? nlohmann::json(*result.empirical_ratio_to_exact)
               : nlohmann::json(nullptr)},
          {"ratio_to_incumbent",
           result.ratio_to_incumbent
               ? nlohmann::json(*result.ratio_to_incumbent)
               : nlohmann::json(nullptr)},
          {"gap", result.gap},
          {"wall_time_sec", result.wall_time_sec},
          {"solve_time_sec", result.solve_time_sec},
          {"verification_time_sec", result.verification_time_sec},
          {"serialization_time_sec", result.serialization_time_sec},
          {"total_wall_time_sec", result.total_wall_time_sec},
          {"cpu_time_sec", result.cpu_time_sec},
          {"num_iterations", result.num_iterations},
          {"num_static_solves", result.num_static_solves},
          {"seed", result.seed},
          {"repeat", result.repeat},
          {"global_time_limit_sec", result.global_time_limit_sec},
          {"per_static_time_limit_sec", result.per_static_time_limit_sec},
          {"refinement_policy", result.refinement_policy},
          {"feasible", result.feasible},
          {"verified", result.verified},
          {"verify_each_iteration", result.verify_each_iteration},
          {"verify_after", result.verify_after},
          {"handovers_enabled", result.handovers_enabled},
          {"timeout", result.timeout},
          {"failed", result.failed},
          {"error_message", result.error_message},
          {"candidate_count", nullptr},
          {"coverage_nnz", nullptr},
          {"experiment_id", result.experiment_id},
          {"git_commit", KDC_GIT_COMMIT},
          {"compiler", __VERSION__},
          {"build_type", KDC_BUILD_TYPE},
          {"thread_count", 1},
          {"configuration", result.configuration},
          {"verification_kind",
           verification_kind_to_string(result.verification_kind)},
          {"verification_time_sec", result.verification_time_sec}};
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
      config.instance_paths.empty() || config.num_repeats <= 0 ||
      config.fast_time_limit_sec <= 0.0 ||
      config.exact_time_limit_sec <= 0.0 ||
      config.per_static_time_limit_sec <= 0.0) {
    throw std::invalid_argument("kinetic comparison configuration is invalid");
  }
  output.clear();
  const std::filesystem::path source_dataset_dir =
      std::filesystem::path(config.instance_paths.front()).parent_path();
  const std::filesystem::path dataset_dir =
      std::filesystem::path(config.output_dir) / "calibration-inputs";
  std::filesystem::create_directories(dataset_dir);
  for (Index index = 0; index < config.instance_paths.size(); ++index) {
    const auto& path = config.instance_paths[index];
    const std::filesystem::path destination =
        dataset_dir / (std::to_string(index) + "_" +
                       std::filesystem::path(path).filename().string());
    std::filesystem::copy_file(
        path, destination, std::filesystem::copy_options::overwrite_existing);
  }
  const auto exact_decision = ExactReferenceSelector::resolve(
      config.exact_reference, dataset_dir, config.output_dir);
  if (!exact_decision.valid) {
    throw std::invalid_argument("kinetic comparison exact reference is invalid");
  }
  std::vector<std::string> algorithms = config.algorithm_names;
  const bool use_default = algorithms.empty() ||
      std::find(algorithms.begin(), algorithms.end(), "all-fast") !=
          algorithms.end() ||
      std::find(algorithms.begin(), algorithms.end(), "all-comparison") !=
          algorithms.end() ||
      std::find(algorithms.begin(), algorithms.end(), "all") !=
          algorithms.end();
  if (config.profile == BenchmarkProfile::EXACT_REFERENCE) {
    algorithms = {exact_decision.selected_backend};
  } else if (use_default) {
    algorithms = comparison_benchmark_algorithms(
        exact_decision.selected_backend);
  } else {
    const auto requested_exact = std::count_if(
        algorithms.begin(), algorithms.end(), [](const std::string& name) {
          return name == "ip-kont" || name == "branch-and-bound";
        });
    if (requested_exact > 1 ||
        (requested_exact == 1 &&
         std::find(algorithms.begin(), algorithms.end(),
                   exact_decision.selected_backend) == algorithms.end())) {
      throw std::invalid_argument(
          "kinetic comparison must select exactly the configured exact backend");
    }
    if (requested_exact == 0) {
      algorithms.push_back(exact_decision.selected_backend);
    }
  }
  const auto registered = StaticSolverRegistry::list();
  for (const auto& algorithm : algorithms) {
    if (algorithm == "brute-force" ||
        std::find(registered.begin(), registered.end(), algorithm) ==
            registered.end()) {
      throw std::invalid_argument("unknown kinetic comparison algorithm: " +
                                  algorithm);
    }
  }
  const std::string experiment_id = comparison_experiment_id(config.seed);
  nlohmann::json calibration = nlohmann::json::object();
  const std::filesystem::path manifest_path =
      std::filesystem::path(config.output_dir) / "experiment_manifest.json";
  if (std::filesystem::is_regular_file(manifest_path)) {
    std::ifstream input(manifest_path);
    if (input) {
      input >> calibration;
    }
  }
  const auto manifest = make_experiment_manifest(
      experiment_id, {}, source_dataset_dir.string(),
      calibration.value("dataset_fingerprint", std::string{}), config.profile,
      algorithms, config.exact_reference, exact_decision.actual_backend,
      calibration, config.fast_time_limit_sec, config.exact_time_limit_sec,
      config.per_static_time_limit_sec, config.seed, config.num_repeats,
      config.profile == BenchmarkProfile::FAST
          ? "HEURISTIC_ADAPTIVE"
          : "CERTIFIED_BOUND",
      config.profile == BenchmarkProfile::DEBUG, true, true, "auto", 1);
  write_experiment_manifest(manifest_path.string(), manifest);

  std::stable_sort(algorithms.begin(), algorithms.end(),
                   [&exact_decision](const std::string& lhs,
                                     const std::string& rhs) {
                     return lhs == exact_decision.selected_backend &&
                            rhs != exact_decision.selected_backend;
                   });
  for (const auto& path : config.instance_paths) {
    const Instance instance = DatasetReader::read_json(path);
    const std::string instance_name =
        instance.name.empty() ? std::filesystem::path(path).stem().string()
                              : instance.name;
    for (const auto& algorithm : algorithms) {
      const bool exact = algorithm == "ip-kont" ||
                         algorithm == "branch-and-bound";
      const int repeat_count = exact ? 1 : config.num_repeats;
      for (int repeat = 0; repeat < repeat_count; ++repeat) {
        auto solver = StaticSolverRegistry::create(algorithm, ilp);
        if (!solver) {
          throw std::invalid_argument(
              "unknown kinetic comparison algorithm: " + algorithm);
        }
        KineticComparisonResult record;
        record.instance_name = instance_name;
        record.algorithm_name = solver->name();
        record.algorithm_category = exact ? "exact_reference" : "heuristic";
        record.requested_backend = config.exact_reference;
        record.actual_backend =
            exact ? exact_decision.actual_backend : algorithm;
        record.objective = objective;
        record.n = instance.n;
        record.m = instance.m;
        record.repeat = repeat;
        record.seed = comparison_seed(config.seed, instance_name, algorithm,
                                      objective, repeat);
        solver->set_seed(record.seed);
        record.global_time_limit_sec =
            exact ? config.exact_time_limit_sec : config.fast_time_limit_sec;
        record.per_static_time_limit_sec =
            config.per_static_time_limit_sec;
        record.verify_after = true;
        record.verify_each_iteration =
            config.profile == BenchmarkProfile::DEBUG;
        record.experiment_id = experiment_id;
        record.configuration = {
            {"profile", benchmark_profile_to_string(config.profile)},
            {"algorithm", algorithm},
            {"objective", objective},
            {"repeat", repeat},
            {"seed", record.seed},
            {"global_time_limit_sec", record.global_time_limit_sec},
            {"per_static_time_limit_sec",
             record.per_static_time_limit_sec},
            {"verify_after", true},
            {"verify_each_iteration", record.verify_each_iteration},
            {"cache_policy", "auto"},
            {"handovers_enabled", true}};
        const auto wall_start = Clock::now();
        const std::clock_t cpu_start = std::clock();
        SolverBudget budget(record.global_time_limit_sec);
        if (objective == "minmax") {
          MinMaxSolver::Config solver_config;
          solver_config.time_limit_per_ip = record.per_static_time_limit_sec;
          solver_config.global_time_limit_sec = record.global_time_limit_sec;
          solver_config.verify_after = true;
          solver_config.verify_each_iteration = record.verify_each_iteration;
          const auto result =
              MinMaxSolver::solve(instance, *solver, solver_config, budget);
          record.objective_value = result.peak_cost;
          record.peak_cost = result.peak_cost;
          record.integral_cost = result.solution.total_integral();
          record.lower_bound = result.lower_bound;
          record.upper_bound = result.upper_bound;
          record.bound_status = result.bound_status;
          record.optimality_status = result.optimality_status;
          record.certified_gap = result.certified_gap;
          record.gap = result.gap;
          record.solve_time_sec = result.total_time_sec;
          record.num_iterations = result.num_iterations;
          record.num_static_solves = result.num_ip_solves;
          record.feasible = result.feasible;
          record.verified = result.verified;
          record.verification_kind = result.verification_kind;
          record.verification_time_sec = result.verification_time_sec;
          record.timeout = result.time_limited;
          record.failed = !result.feasible && !result.time_limited;
        } else {
          MinSumSolver::Config solver_config;
          solver_config.time_limit_per_ip = record.per_static_time_limit_sec;
          solver_config.global_time_limit_sec = record.global_time_limit_sec;
          solver_config.refinement_policy =
              config.profile == BenchmarkProfile::FAST
                  ? MinSumRefinementPolicy::HEURISTIC_ADAPTIVE
                  : MinSumRefinementPolicy::CERTIFIED_BOUND;
          record.refinement_policy = minsum_refinement_policy_to_string(
              solver_config.refinement_policy);
          solver_config.verify_after = true;
          solver_config.verify_each_iteration = record.verify_each_iteration;
          const auto result =
              MinSumSolver::solve(instance, *solver, solver_config, budget);
          record.objective_value = result.total_integral;
          record.peak_cost = result.solution.peak_cost();
          record.integral_cost = result.total_integral;
          record.lower_bound = result.lower_bound_integral;
          record.upper_bound = result.upper_bound;
          record.bound_status = result.bound_status;
          record.optimality_status = result.optimality_status;
          record.certified_gap = result.certified_gap;
          record.gap = result.gap;
          record.solve_time_sec = result.total_time_sec;
          record.num_iterations = result.num_iterations;
          record.num_static_solves = result.num_ip_solves;
          record.feasible = result.feasible;
          record.verified = result.verified;
          record.verification_kind = result.verification_kind;
          record.verification_time_sec = result.verification_time_sec;
          record.timeout = result.time_limited;
          record.failed = !result.feasible && !result.time_limited;
        }
        record.optimality_status = record.timeout
                                       ? OptimalityStatus::TIME_LIMIT
                                       : record.optimality_status;
        record.wall_time_sec =
            std::chrono::duration<double>(Clock::now() - wall_start).count();
        record.total_wall_time_sec = record.wall_time_sec;
        record.cpu_time_sec =
            static_cast<double>(std::clock() - cpu_start) / CLOCKS_PER_SEC;
        output.push_back(std::move(record));
      }
    }
  }
  std::map<std::string, const KineticComparisonResult*> exact_results;
  std::map<std::string, double> incumbents;
  for (const auto& result : output) {
    if (!result.feasible) {
      continue;
    }
    const std::string key = result.instance_name + '\n' + result.objective;
    auto incumbent = incumbents.find(key);
    if (incumbent == incumbents.end() ||
        result.objective_value < incumbent->second) {
      incumbents[key] = result.objective_value;
    }
    if (result.algorithm_category == "exact_reference") {
      exact_results[key] = &result;
    }
  }
  for (auto& result : output) {
    if (!result.feasible) {
      continue;
    }
    const std::string key = result.instance_name + '\n' + result.objective;
    const auto exact = exact_results.find(key);
    if (exact != exact_results.end() &&
        exact->second->optimality_status == OptimalityStatus::OPTIMAL) {
      const double denominator = exact->second->objective_value;
      if (denominator > 0.0) {
        result.empirical_ratio_to_exact =
            result.objective_value / denominator;
      } else if (result.objective_value <= 1e-12) {
        result.empirical_ratio_to_exact = 1.0;
      }
    }
    const double incumbent = incumbents.at(key);
    if (incumbent > 0.0) {
      result.ratio_to_incumbent = result.objective_value / incumbent;
    } else if (result.objective_value <= 1e-12) {
      result.ratio_to_incumbent = 1.0;
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
  csv_file << "instance_name,algorithm_name,algorithm_category,"
              "requested_backend,actual_backend,objective,n,m,objective_value,"
              "peak_cost,integral_cost,lower_bound,upper_bound,bound_status,"
              "optimality_status,certified_gap,empirical_ratio_to_exact,"
              "ratio_to_incumbent,gap,solve_time_sec,verification_time_sec,"
              "serialization_time_sec,total_wall_time_sec,cpu_time_sec,"
              "num_iterations,num_static_solves,seed,repeat,"
              "global_time_limit_sec,per_static_time_limit_sec,"
              "refinement_policy,feasible,verified,verify_each_iteration,"
              "verify_after,handovers_enabled,timeout,failed,error_message,"
              "verification_kind\n";
  for (const auto& result : results) {
    csv_file << csv_escape(result.instance_name) << ','
             << csv_escape(result.algorithm_name) << ','
             << csv_escape(result.algorithm_category) << ','
             << csv_escape(result.requested_backend) << ','
             << csv_escape(result.actual_backend) << ','
             << csv_escape(result.objective) << ',' << result.n << ','
             << result.m << ',' << result.objective_value << ','
             << result.peak_cost << ',' << result.integral_cost << ','
             << result.lower_bound << ',' << result.upper_bound << ','
             << bound_status_to_string(result.bound_status) << ','
             << optimality_status_to_string(result.optimality_status) << ',';
    if (result.certified_gap) {
      csv_file << *result.certified_gap;
    }
    csv_file << ',';
    if (result.empirical_ratio_to_exact) {
      csv_file << *result.empirical_ratio_to_exact;
    }
    csv_file << ',';
    if (result.ratio_to_incumbent) {
      csv_file << *result.ratio_to_incumbent;
    }
    csv_file << ',' << result.gap << ',' << result.solve_time_sec << ','
             << result.verification_time_sec << ','
             << result.serialization_time_sec << ','
             << result.total_wall_time_sec << ',' << result.cpu_time_sec << ','
             << result.num_iterations << ',' << result.num_static_solves << ','
             << result.seed << ',' << result.repeat << ','
             << result.global_time_limit_sec << ','
             << result.per_static_time_limit_sec << ','
             << csv_escape(result.refinement_policy) << ','
             << (result.feasible ? "true" : "false") << ','
             << (result.verified ? "true" : "false") << ','
             << (result.verify_each_iteration ? "true" : "false") << ','
             << (result.verify_after ? "true" : "false") << ','
             << (result.handovers_enabled ? "true" : "false") << ','
             << (result.timeout ? "true" : "false") << ','
             << (result.failed ? "true" : "false") << ','
             << csv_escape(result.error_message) << ','
             << verification_kind_to_string(result.verification_kind) << '\n';
  }
  if (!csv_file) {
    throw std::runtime_error("failed writing kinetic comparison CSV: " +
                             csv_path);
  }
}
}
