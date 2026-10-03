#include "kdc/benchmark.hpp"

#include "kdc/benchmark_protocol.hpp"
#include "kdc/batch_runner.hpp"
#include "kdc/io.hpp"
#include "kdc/exact_reference_selector.hpp"
#include "kdc/kont_solver.hpp"
#include "kdc/logging.hpp"
#include "kdc/profiling.hpp"
#include "kdc/static_solver_registry.hpp"
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
#include <map>
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
  return Json{{"schema_version", 2},
              {"instance_name", result.instance_name},
              {"algorithm_name", result.algorithm_name},
              {"algorithm_category", result.algorithm_category},
              {"requested_backend", result.requested_backend},
              {"actual_backend", result.actual_backend},
              {"repeat", result.repeat},
              {"n", result.n},
              {"m", result.m},
              {"objective", to_string(result.objective)},
              {"wall_time_sec", result.wall_time_sec},
              {"solve_time_sec", result.solve_time_sec},
              {"peak_cost", result.peak_cost},
              {"integral_cost", result.integral_cost},
              {"empirical_ratio_to_exact",
               result.empirical_ratio_to_exact
                   ? Json(*result.empirical_ratio_to_exact)
                   : Json(nullptr)},
              {"ratio_to_incumbent",
               result.ratio_to_incumbent ? Json(*result.ratio_to_incumbent)
                                         : Json(nullptr)},
              {"cpu_time_sec", result.cpu_time_sec},
              {"ip_time_sec", result.ip_time_sec},
              {"peak_memory_mb", result.peak_memory_mb},
              {"objective_value", result.objective_value},
              {"lower_bound", result.lower_bound},
              {"bound_status", bound_status_to_string(result.bound_status)},
              {"upper_bound", std::isfinite(result.upper_bound)
                                  ? Json(result.upper_bound)
                                  : Json(nullptr)},
              {"certified_gap", result.certified_gap.has_value()
                                    && result.exact_solver && result.feasible &&
                                            result.bound_status ==
                                                BoundStatus::CERTIFIED
                                    ? Json(*result.certified_gap)
                                    : Json(nullptr)},
              {"optimality_status",
               optimality_status_to_string(
                   result.time_limited ? OptimalityStatus::TIME_LIMIT
                   : (!result.exact_solver &&
                              result.optimality_status ==
                                  OptimalityStatus::OPTIMAL
                          ? OptimalityStatus::FEASIBLE
                          : result.optimality_status))},
              {"minsum_refinement_policy",
               minsum_refinement_policy_to_string(
                   result.minsum_refinement_policy)},
              {"exact_solver", result.exact_solver},
              {"feasible", result.feasible},
              {"certified_lower_bound", result.certified_lower_bound},
              {"heuristic_lower_bound", result.heuristic_lower_bound},
              {"gap", result.gap},
              {"num_ip_solves", result.num_ip_solves},
              {"num_iterations", result.num_iterations},
              {"verified", result.verified},
              {"verification_kind",
               verification_kind_to_string(result.verification_kind)},
              {"verification_time_sec", result.verification_time_sec},
              {"serialization_time_sec", result.serialization_time_sec},
              {"total_wall_time_sec", result.total_wall_time_sec},
              {"num_static_solves", result.num_static_solves},
              {"seed", result.seed},
              {"global_time_limit_sec", result.global_time_limit_sec},
              {"per_static_time_limit_sec",
               result.per_static_time_limit_sec},
              {"refinement_policy",
               minsum_refinement_policy_to_string(
                   result.minsum_refinement_policy)},
              {"verify_each_iteration", result.verify_each_iteration},
              {"verify_after", result.verify_after},
              {"handovers_enabled", result.handovers_enabled},
              {"candidate_count", nullptr},
              {"coverage_nnz", nullptr},
              {"time_limited", result.time_limited},
              {"timeout", result.timeout},
              {"failed", result.failed},
              {"error_message", ""},
              {"git_commit", result.git_commit},
              {"compiler", result.compiler},
              {"build_type", result.build_type},
              {"thread_count", result.thread_count},
              {"experiment_id", result.experiment_id},
              {"configuration", result.configuration},
              {"timestamp", result.timestamp}};
}

BenchmarkResult result_from_json(const Json& json) {
  BenchmarkResult result;
  result.instance_name = json.at("instance_name").get<std::string>();
  result.algorithm_name =
      json.value("algorithm_name", std::string("exact-reference"));
  result.algorithm_category =
      json.value("algorithm_category", std::string("exact"));
  result.requested_backend =
      json.value("requested_backend", std::string{});
  result.actual_backend = json.value("actual_backend", std::string{});
  result.repeat = json.value("repeat", 0);
  result.n = json.at("n").get<int>();
  result.m = json.at("m").get<int>();
  result.objective = objective_from_string(json.at("objective").get<std::string>());
  result.wall_time_sec = json.at("wall_time_sec").get<double>();
  result.solve_time_sec =
      json.value("solve_time_sec", result.wall_time_sec);
  result.cpu_time_sec = json.at("cpu_time_sec").get<double>();
  result.ip_time_sec = json.at("ip_time_sec").get<double>();
  result.peak_memory_mb = json.at("peak_memory_mb").get<double>();
  result.objective_value = json.at("objective_value").get<double>();
  result.peak_cost = json.value("peak_cost", result.objective_value);
  result.integral_cost = json.value("integral_cost", result.objective_value);
  result.lower_bound = json.at("lower_bound").get<double>();
  result.bound_status =
      json.contains("bound_status")
          ? bound_status_from_string(json.at("bound_status").get<std::string>())
          : BoundStatus::NONE;
  result.feasible =
      json.value("feasible", std::isfinite(result.objective_value));
  result.exact_solver = json.value("exact_solver", false);
  result.upper_bound =
      json.contains("upper_bound") && !json.at("upper_bound").is_null()
          ? json.at("upper_bound").get<double>()
          : (result.feasible ? result.objective_value
                             : std::numeric_limits<double>::infinity());
  result.optimality_status =
      json.contains("optimality_status")
          ? optimality_status_from_string(
                json.at("optimality_status").get<std::string>())
          : (result.feasible ? OptimalityStatus::FEASIBLE
                             : OptimalityStatus::FAILED);
  result.minsum_refinement_policy =
      json.contains("minsum_refinement_policy")
          ? minsum_refinement_policy_from_string(
                json.at("minsum_refinement_policy").get<std::string>())
          : MinSumRefinementPolicy::HEURISTIC_ADAPTIVE;
  if (json.contains("certified_gap") && json.at("certified_gap").is_number() &&
      result.exact_solver && result.bound_status == BoundStatus::CERTIFIED &&
      result.feasible) {
    const double gap = json.at("certified_gap").get<double>();
    if (std::isfinite(gap) && gap >= 0.0) {
      result.certified_gap = gap;
    }
  }
  result.certified_lower_bound =
      json.contains("certified_lower_bound")
          ? json.at("certified_lower_bound").get<double>()
          : result.lower_bound;
  result.heuristic_lower_bound =
      json.contains("heuristic_lower_bound")
          ? json.at("heuristic_lower_bound").get<double>()
          : result.lower_bound;
  result.gap = json.at("gap").get<double>();
  result.num_ip_solves = json.at("num_ip_solves").get<int>();
  result.num_iterations = json.at("num_iterations").get<int>();
  result.verified = json.at("verified").get<bool>();
  result.verification_kind =
      json.contains("verification_kind")
          ? verification_kind_from_string(
                json.at("verification_kind").get<std::string>())
          : VerificationKind::NONE;
  result.verification_time_sec =
      json.value("verification_time_sec", 0.0);
  result.time_limited =
      json.value("time_limited", false);
  result.timeout = json.value("timeout", result.time_limited);
  result.failed = json.value("failed", false);
  result.serialization_time_sec =
      json.value("serialization_time_sec", 0.0);
  result.total_wall_time_sec =
      json.value("total_wall_time_sec", result.wall_time_sec);
  result.num_static_solves =
      json.value("num_static_solves", result.num_ip_solves);
  result.seed = json.value("seed", 42U);
  result.global_time_limit_sec =
      json.value("global_time_limit_sec", 0.0);
  result.per_static_time_limit_sec =
      json.value("per_static_time_limit_sec", 60.0);
  result.verify_each_iteration =
      json.value("verify_each_iteration", false);
  result.verify_after = json.value("verify_after", true);
  result.handovers_enabled = json.value("handovers_enabled", true);
  result.git_commit = json.value("git_commit", std::string{});
  result.compiler = json.value("compiler", std::string{});
  result.build_type = json.value("build_type", std::string{});
  result.thread_count = json.value("thread_count", 1);
  result.experiment_id = json.value("experiment_id", std::string{});
  result.configuration = json.value("configuration", Json::object());
  if (json.contains("empirical_ratio_to_exact") &&
      json.at("empirical_ratio_to_exact").is_number()) {
    result.empirical_ratio_to_exact =
        json.at("empirical_ratio_to_exact").get<double>();
  }
  if (json.contains("ratio_to_incumbent") &&
      json.at("ratio_to_incumbent").is_number()) {
    result.ratio_to_incumbent = json.at("ratio_to_incumbent").get<double>();
  }
  if (result.time_limited) {
    result.optimality_status = OptimalityStatus::TIME_LIMIT;
  }
  if (!result.exact_solver &&
      result.optimality_status == OptimalityStatus::OPTIMAL) {
    result.optimality_status = OptimalityStatus::FEASIBLE;
  }
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
  std::string selected_backend = config.algorithm_name.empty()
                                     ? config.exact_reference
                                     : config.algorithm_name;
  std::string actual_backend = config.actual_backend;
  if ((selected_backend == "auto" || selected_backend.empty()) &&
      config.exact_reference != "auto") {
    selected_backend = config.exact_reference;
  }
  const bool exact_backend_requested =
      selected_backend == "ip-kont" ||
      selected_backend == "branch-and-bound" || selected_backend == "auto";
  if (selected_backend == "auto" ||
      (exact_backend_requested && actual_backend.empty())) {
    const auto decision = ExactReferenceSelector::resolve(
        selected_backend == "auto" ? config.exact_reference : selected_backend,
        config.dataset_dir, config.output_dir);
    selected_backend = decision.selected_backend;
    actual_backend = decision.actual_backend;
  }
  auto static_solver =
      StaticSolverRegistry::create(selected_backend, &timed_solver);
  if (!static_solver) {
    throw std::invalid_argument("unknown benchmark algorithm: " +
                                selected_backend);
  }
  const bool is_exact = static_solver->is_exact();
  const double global_limit =
      is_exact ? config.exact_time_limit_sec : config.fast_time_limit_sec;
  SolverBudget budget(global_limit);

  BenchmarkResult benchmark;
  KineticSolution computed_solution;
  benchmark.instance_name =
      instance.name.empty() ? std::to_string(instance.id) : instance.name;
  benchmark.n = instance.n;
  benchmark.m = instance.m;
  benchmark.algorithm_name = selected_backend;
  benchmark.algorithm_category = is_exact ? "exact_reference" : "heuristic";
  benchmark.requested_backend = config.exact_reference;
  benchmark.actual_backend =
      is_exact ? (actual_backend.empty() ? selected_backend : actual_backend)
               : selected_backend;
  benchmark.repeat = config.repeat_index;
  benchmark.seed = run_seed(config.seed, benchmark.instance_name,
                            selected_backend, to_string(objective),
                            benchmark.repeat);
  static_solver->set_seed(benchmark.seed);
  benchmark.global_time_limit_sec = global_limit;
  benchmark.per_static_time_limit_sec =
      config.per_static_time_limit_sec;
  benchmark.verify_each_iteration = config.verify_each_iteration;
  benchmark.verify_after = config.verify_after;
  benchmark.handovers_enabled = true;
  benchmark.thread_count = config.parallel ? config.num_threads : 1;
  benchmark.git_commit = KDC_GIT_COMMIT;
  benchmark.compiler = __VERSION__;
  benchmark.build_type = KDC_BUILD_TYPE;
  benchmark.experiment_id = config.experiment_id;
  benchmark.configuration = {
      {"profile", benchmark_profile_to_string(config.profile)},
      {"algorithm", selected_backend},
      {"objective", to_string(objective)},
      {"repeat", benchmark.repeat},
      {"seed", benchmark.seed},
      {"global_time_limit_sec", global_limit},
      {"per_static_time_limit_sec", config.per_static_time_limit_sec},
      {"verify_each_iteration", benchmark.verify_each_iteration},
      {"verify_after", benchmark.verify_after},
      {"refinement_policy",
       minsum_refinement_policy_to_string(
           config.profile == BenchmarkProfile::FAST
               ? MinSumRefinementPolicy::HEURISTIC_ADAPTIVE
               : MinSumRefinementPolicy::CERTIFIED_BOUND)},
      {"handovers_enabled", benchmark.handovers_enabled},
      {"cache_policy", "auto"}};
  benchmark.objective = objective;
  if (objective == ObjectiveType::MIN_SUM) {
    benchmark.minsum_refinement_policy =
        config.minsum_cfg.refinement_policy;
  }
  if (objective == ObjectiveType::MIN_MAX) {
    auto solver_config = config.minmax_cfg;
    solver_config.time_limit_per_ip = config.per_static_time_limit_sec;
    solver_config.verify_after = config.verify_after;
    solver_config.verify_each_iteration = config.verify_each_iteration;
    solver_config.global_time_limit_sec = global_limit;
    const auto result =
        MinMaxSolver::solve(instance, *static_solver, solver_config, budget);
    benchmark.objective_value = result.peak_cost;
    benchmark.solve_time_sec = result.total_time_sec;
    benchmark.lower_bound = result.lower_bound;
    benchmark.bound_status = result.bound_status;
    benchmark.upper_bound = result.upper_bound;
    benchmark.certified_gap = result.certified_gap;
    benchmark.optimality_status = result.optimality_status;
    benchmark.exact_solver = result.exact_solver;
    benchmark.certified_lower_bound = result.certified_lower_bound;
    benchmark.heuristic_lower_bound = result.heuristic_lower_bound;
    benchmark.gap = result.gap;
    benchmark.num_ip_solves = result.num_ip_solves;
    benchmark.num_iterations = result.num_iterations;
    benchmark.verified = result.verified;
    benchmark.verification_kind = result.verification_kind;
    benchmark.verification_time_sec = result.verification_time_sec;
    benchmark.time_limited = result.time_limited;
    computed_solution = result.solution;
    benchmark.feasible = result.feasible;
    benchmark.num_static_solves =
        static_cast<std::size_t>(result.num_ip_solves);
    benchmark.minsum_refinement_policy =
        MinSumRefinementPolicy::HEURISTIC_ADAPTIVE;
  } else {
    auto solver_config = config.minsum_cfg;
    solver_config.time_limit_per_ip = config.per_static_time_limit_sec;
    solver_config.verify_after = config.verify_after;
    solver_config.verify_each_iteration = config.verify_each_iteration;
    solver_config.global_time_limit_sec = global_limit;
    if (config.profile == BenchmarkProfile::FAST) {
      solver_config.refinement_policy =
          MinSumRefinementPolicy::HEURISTIC_ADAPTIVE;
    } else {
      solver_config.refinement_policy =
          MinSumRefinementPolicy::CERTIFIED_BOUND;
    }
    const auto result =
        MinSumSolver::solve(instance, *static_solver, solver_config, budget);
    benchmark.objective_value = result.total_integral;
    benchmark.solve_time_sec = result.total_time_sec;
    benchmark.lower_bound = result.lower_bound;
    benchmark.bound_status = result.bound_status;
    benchmark.upper_bound = result.upper_bound;
    benchmark.certified_gap = result.certified_gap;
    benchmark.optimality_status = result.optimality_status;
    benchmark.minsum_refinement_policy = result.refinement_policy;
    benchmark.exact_solver = result.exact_solver;
    benchmark.certified_lower_bound =
        result.certified_lower_bound_integral;
    benchmark.heuristic_lower_bound =
        result.heuristic_lower_bound_integral;
    benchmark.gap = result.gap;
    benchmark.num_ip_solves = result.num_ip_solves;
    benchmark.num_iterations = result.num_iterations;
    benchmark.verified = result.verified;
    benchmark.verification_kind = result.verification_kind;
    benchmark.verification_time_sec = result.verification_time_sec;
    benchmark.time_limited = result.time_limited;
    computed_solution = result.solution;
    benchmark.feasible = result.feasible;
    benchmark.num_static_solves =
        static_cast<std::size_t>(result.num_ip_solves);
  }
  if (config.verify_after && benchmark.feasible && !benchmark.time_limited &&
      !benchmark.verified) {
    const auto verification_start = Clock::now();
    try {
      const VerificationReport report =
          Verifier::verify(instance, computed_solution, 100, 1e-6, &budget);
      benchmark.verified = report.all_ok();
      benchmark.verification_kind = report.kind;
      benchmark.feasible = benchmark.verified;
      if (!benchmark.verified) {
        throw std::runtime_error("benchmark result verification failed: " +
                                 report.errors.front());
      }
    } catch (const SolverBudgetExpired&) {
      benchmark.time_limited = true;
      benchmark.verified = false;
      benchmark.optimality_status = OptimalityStatus::TIME_LIMIT;
    }
    benchmark.verification_time_sec +=
        std::chrono::duration<double>(Clock::now() - verification_start).count();
  }
  benchmark.time_limited = benchmark.time_limited || budget.expired();
  if (benchmark.time_limited) {
    benchmark.optimality_status = OptimalityStatus::TIME_LIMIT;
  }

  benchmark.ip_time_sec = timed_solver.elapsed_sec();
  benchmark.cpu_time_sec =
      static_cast<double>(std::clock() - cpu_start) /
      static_cast<double>(CLOCKS_PER_SEC);
  benchmark.wall_time_sec =
      std::chrono::duration<double>(Clock::now() - wall_start).count();
  benchmark.total_wall_time_sec = benchmark.wall_time_sec;
  benchmark.timeout = benchmark.time_limited;
  benchmark.failed = !benchmark.feasible && !benchmark.time_limited;
  benchmark.peak_cost = computed_solution.peak_cost();
  benchmark.integral_cost = computed_solution.total_integral();
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
  const auto exact_decision = ExactReferenceSelector::resolve(
      config.exact_reference, config.dataset_dir, config.output_dir);
  if (!exact_decision.valid) {
    throw std::invalid_argument("benchmark exact-reference selection failed");
  }
  BenchmarkConfig resolved_config = config;
  resolved_config.exact_reference = exact_decision.selected_backend;
  resolved_config.actual_backend = exact_decision.actual_backend;
  resolved_config.verify_after = true;
  if (resolved_config.profile == BenchmarkProfile::DEBUG) {
    resolved_config.verify_each_iteration = true;
  }
  resolved_config.experiment_id =
      "kdc-" + timestamp_now() + "-seed" + std::to_string(config.seed);
  std::vector<std::string> algorithms;
  if (config.profile == BenchmarkProfile::EXACT_REFERENCE) {
    algorithms = {exact_decision.selected_backend};
  } else if (config.profile == BenchmarkProfile::FAST) {
    algorithms = comparison_benchmark_algorithms(
        exact_decision.selected_backend);
  } else {
    algorithms = comparison_benchmark_algorithms(
        exact_decision.selected_backend);
  }
  if (!config.algorithm_names.empty()) {
    algorithms.clear();
    const bool all_fast =
        std::find(config.algorithm_names.begin(), config.algorithm_names.end(),
                  "all-fast") != config.algorithm_names.end();
    const bool all_comparison =
        std::find(config.algorithm_names.begin(), config.algorithm_names.end(),
                  "all-comparison") != config.algorithm_names.end();
    if (all_fast || all_comparison) {
      algorithms = comparison_benchmark_algorithms(
          exact_decision.selected_backend);
    } else {
      algorithms = config.algorithm_names;
      const auto exact_count = std::count_if(
          algorithms.begin(), algorithms.end(), [](const std::string& name) {
            return name == "ip-kont" || name == "branch-and-bound";
          });
      if (exact_count > 1 ||
          (exact_count == 1 &&
           std::find(algorithms.begin(), algorithms.end(),
                     exact_decision.selected_backend) == algorithms.end())) {
        throw std::invalid_argument(
            "benchmark must select exactly the configured exact backend");
      }
      if (exact_count == 0) {
        algorithms.push_back(exact_decision.selected_backend);
      }
    }
  }
  if (config.profile == BenchmarkProfile::EXACT_REFERENCE) {
    algorithms = {exact_decision.selected_backend};
  }
  const auto registered_algorithms = StaticSolverRegistry::list();
  for (const auto& algorithm : algorithms) {
    if (std::find(registered_algorithms.begin(), registered_algorithms.end(),
                  algorithm) == registered_algorithms.end() ||
        algorithm == "brute-force") {
      throw std::invalid_argument("invalid benchmark algorithm: " + algorithm);
    }
  }
  std::stable_sort(algorithms.begin(), algorithms.end(),
                   [](const std::string& lhs, const std::string& rhs) {
                     const auto exact = [](const std::string& value) {
                       return value == "ip-kont" ||
                              value == "branch-and-bound";
                     };
                     return exact(lhs) && !exact(rhs);
                   });
  const std::filesystem::path manifest_path =
      std::filesystem::path(config.output_dir) / "experiment_manifest.json";
  Json calibration = Json::object();
  if (std::filesystem::is_regular_file(manifest_path)) {
    std::ifstream input(manifest_path);
    if (input) {
      input >> calibration;
    }
  }
  const Json experiment_manifest = make_experiment_manifest(
      resolved_config.experiment_id, {}, config.dataset_dir,
      calibration.value("dataset_fingerprint", std::string{}), config.profile,
      algorithms, config.exact_reference, exact_decision.actual_backend,
      calibration, config.fast_time_limit_sec, config.exact_time_limit_sec,
      config.per_static_time_limit_sec, config.seed, config.num_repeats,
      minsum_refinement_policy_to_string(
          config.profile == BenchmarkProfile::FAST
              ? MinSumRefinementPolicy::HEURISTIC_ADAPTIVE
              : config.profile == BenchmarkProfile::EXACT_REFERENCE
                    ? MinSumRefinementPolicy::CERTIFIED_BOUND
                    : config.minsum_cfg.refinement_policy),
      config.profile == BenchmarkProfile::DEBUG || config.verify_each_iteration,
      true, true, "auto", config.parallel ? config.num_threads : 1);
  write_experiment_manifest(manifest_path.string(), experiment_manifest);
  LOG_INFO("BenchmarkRunner: exact reference selected '{}' (actual={}, "
           "manifest={})",
           exact_decision.selected_backend, exact_decision.actual_backend,
           exact_decision.manifest_path);

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
        local.reserve(objectives.size() * algorithms.size() *
                      static_cast<std::size_t>(config.num_repeats));
        for (const auto objective : objectives) {
          for (const auto& algorithm : algorithms) {
            const bool exact = algorithm == "ip-kont" ||
                               algorithm == "branch-and-bound";
            const int repeat_count = exact ? 1 : config.num_repeats;
            for (int repeat = 0; repeat < repeat_count; ++repeat) {
              auto algorithm_config = resolved_config;
              algorithm_config.algorithm_name = algorithm;
              algorithm_config.repeat_index = repeat;
              if (algorithm_config.profile == BenchmarkProfile::DEBUG) {
                algorithm_config.verify_each_iteration = true;
              }
              local.push_back(run_single(instances[instance_index], *solver,
                                         objective, algorithm_config));
            }
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
        for (const auto& algorithm : algorithms) {
          const bool exact = algorithm == "ip-kont" ||
                             algorithm == "branch-and-bound";
          const int repeat_count = exact ? 1 : config.num_repeats;
          for (int repeat = 0; repeat < repeat_count; ++repeat) {
            auto algorithm_config = resolved_config;
            algorithm_config.algorithm_name = algorithm;
            algorithm_config.repeat_index = repeat;
            if (algorithm_config.profile == BenchmarkProfile::DEBUG) {
              algorithm_config.verify_each_iteration = true;
            }
            results.push_back(
                run_single(instance, solver, objective, algorithm_config));
          }
        }
      }
    }
  }
  using ResultKey = std::pair<std::string, ObjectiveType>;
  std::map<ResultKey, const BenchmarkResult*> exact_results;
  std::map<ResultKey, double> incumbents;
  for (const auto& result : results) {
    if (!result.feasible || !std::isfinite(result.objective_value)) {
      continue;
    }
    const ResultKey key{result.instance_name, result.objective};
    const auto current = incumbents.find(key);
    if (current == incumbents.end() ||
        result.objective_value < current->second) {
      incumbents[key] = result.objective_value;
    }
    if (result.algorithm_category == "exact_reference") {
      exact_results[key] = &result;
    }
  }
  for (auto& result : results) {
    if (!result.feasible || !std::isfinite(result.objective_value)) {
      continue;
    }
    const ResultKey key{result.instance_name, result.objective};
    const auto incumbent = incumbents.find(key);
    if (incumbent != incumbents.end() && incumbent->second > 0.0) {
      result.ratio_to_incumbent =
          result.objective_value / incumbent->second;
    } else if (incumbent != incumbents.end() &&
               result.objective_value <= 1e-12) {
      result.ratio_to_incumbent = 1.0;
    }
    const auto exact = exact_results.find(key);
    if (exact != exact_results.end() &&
        exact->second->optimality_status == OptimalityStatus::OPTIMAL) {
      const double exact_value = exact->second->objective_value;
      if (exact_value > 0.0) {
        result.empirical_ratio_to_exact =
            result.objective_value / exact_value;
      } else if (result.objective_value <= 1e-12) {
        result.empirical_ratio_to_exact = 1.0;
      }
    }
  }
  std::sort(results.begin(), results.end(),
            [](const BenchmarkResult& lhs, const BenchmarkResult& rhs) {
              if (lhs.instance_name != rhs.instance_name) {
                return lhs.instance_name < rhs.instance_name;
              }
              if (lhs.objective != rhs.objective) {
                return lhs.objective < rhs.objective;
              }
              if (lhs.algorithm_name != rhs.algorithm_name) {
                return lhs.algorithm_name < rhs.algorithm_name;
              }
              return lhs.repeat < rhs.repeat;
            });

  const std::filesystem::path output_dir(config.output_dir);
  save_json(results, (output_dir / "json" / "benchmark.json").string());
  save_csv(results, (output_dir / "csv" / "benchmark.csv").string());
  LOG_INFO("Benchmark: {} results saved.", results.size());
}

void BenchmarkRunner::save_json(const std::vector<BenchmarkResult>& results,
                                const std::string& path) {
  KDC_PROFILE_PHASE(ProfilePhase::SERIALIZATION);
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
  output << "instance_name,n,m,algorithm_name,algorithm_category,"
            "requested_backend,actual_backend,objective,repeat,objective_value,"
            "peak_cost,integral_cost,lower_bound,upper_bound,bound_status,"
            "optimality_status,certified_gap,empirical_ratio_to_exact,"
            "ratio_to_incumbent,feasible,verified,solve_time_sec,"
            "verification_time_sec,serialization_time_sec,total_wall_time_sec,"
            "cpu_time_sec,num_iterations,num_static_solves,seed,"
            "global_time_limit_sec,per_static_time_limit_sec,refinement_policy,"
            "verify_each_iteration,verify_after,handovers_enabled,"
            "candidate_count,coverage_nnz,timeout,failed,error_message,"
            "git_commit,compiler,build_type,thread_count,experiment_id,"
            "configuration,timestamp\n";
  for (const auto& result : results) {
    output << csv_escape(result.instance_name) << ',' << result.n << ','
           << result.m << ',' << csv_escape(result.algorithm_name) << ','
           << csv_escape(result.algorithm_category) << ','
           << csv_escape(result.requested_backend) << ','
           << csv_escape(result.actual_backend) << ','
           << to_string(result.objective) << ',' << result.repeat << ','
           << result.objective_value << ',' << result.peak_cost << ','
           << result.integral_cost << ',' << result.lower_bound << ',';
    if (std::isfinite(result.upper_bound)) {
      output << result.upper_bound;
    }
    output << ',' << bound_status_to_string(result.bound_status) << ','
           << optimality_status_to_string(result.optimality_status) << ',';
    if (result.certified_gap.has_value() && result.exact_solver &&
        result.feasible && result.bound_status == BoundStatus::CERTIFIED) {
      output << *result.certified_gap;
    }
    output << ',';
    if (result.empirical_ratio_to_exact) {
      output << *result.empirical_ratio_to_exact;
    }
    output << ',';
    if (result.ratio_to_incumbent) {
      output << *result.ratio_to_incumbent;
    }
    output << ',' << (result.feasible ? "true" : "false") << ','
           << (result.verified ? "true" : "false") << ','
           << result.solve_time_sec << ',' << result.verification_time_sec
           << ',' << result.serialization_time_sec << ','
           << result.total_wall_time_sec << ',' << result.cpu_time_sec << ','
           << result.num_iterations << ',' << result.num_static_solves << ','
           << result.seed << ',' << result.global_time_limit_sec << ','
           << result.per_static_time_limit_sec << ','
           << minsum_refinement_policy_to_string(
                  result.minsum_refinement_policy)
           << ',' << (result.verify_each_iteration ? "true" : "false")
           << ',' << (result.verify_after ? "true" : "false")
           << ',' << (result.handovers_enabled ? "true" : "false")
           << ",,," << (result.timeout ? "true" : "false") << ','
           << (result.failed ? "true" : "false") << ",,"
           << csv_escape(result.git_commit) << ','
           << csv_escape(result.compiler) << ','
           << csv_escape(result.build_type) << ',' << result.thread_count
           << ',' << csv_escape(result.experiment_id) << ','
           << csv_escape(result.configuration.dump()) << ','
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
