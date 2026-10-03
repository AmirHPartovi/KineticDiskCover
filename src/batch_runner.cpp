#include "kdc/batch_runner.hpp"

#include "kdc/benchmark.hpp"
#include "kdc/exact_reference_selector.hpp"
#include "kdc/io.hpp"
#include "kdc/kont_solver.hpp"
#include "kdc/logging.hpp"
#include "kdc/minmax.hpp"
#include "kdc/minsum.hpp"
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

namespace kdc {
namespace {
using Json = nlohmann::json;
using Clock = std::chrono::steady_clock;

Json record_json(const BatchRunRecord& record) {
  return Json{{"instance_name", record.instance_name},
              {"algorithm_name", record.algorithm_name},
              {"objective", record.objective},
              {"n", record.n},
              {"m", record.m},
              {"wall_time_sec", record.wall_time_sec},
              {"cpu_time_sec", record.cpu_time_sec},
              {"peak_memory_mb", record.peak_memory_mb},
              {"time_limit_per_ip_sec", record.time_limit_per_ip_sec},
              {"objective_value", record.objective_value},
              {"lower_bound", record.lower_bound},
              {"certified_lower_bound", record.certified_lower_bound},
              {"heuristic_lower_bound", record.heuristic_lower_bound},
              {"gap", record.gap},
              {"num_iterations", record.num_iterations},
              {"num_ip_solves", record.num_ip_solves},
              {"verified", record.verified},
              {"feasible", record.feasible},
              {"solution_json_path", record.solution_json_path},
              {"trace_csv_path", record.trace_csv_path},
              {"result_json_path", record.result_json_path},
              {"error_message", record.error_message}};
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

void write_result(const BatchRunRecord& record) {
  const std::filesystem::path path(record.result_json_path);
  std::filesystem::create_directories(path.parent_path());
  std::ofstream output(path);
  if (!output) {
    throw std::runtime_error("cannot open batch result JSON: " + path.string());
  }
  output << std::setw(2) << record_json(record) << '\n';
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
          record.objective,
          std::to_string(record.n),
          std::to_string(record.m),
          std::to_string(record.wall_time_sec),
          std::to_string(record.cpu_time_sec),
          std::to_string(record.peak_memory_mb),
          std::to_string(record.time_limit_per_ip_sec),
          std::to_string(record.objective_value),
          std::to_string(record.lower_bound),
          std::to_string(record.gap),
          std::to_string(record.num_iterations),
          std::to_string(record.num_ip_solves),
          record.verified ? "true" : "false",
          record.feasible ? "true" : "false",
          record.error_message};
}

BatchRunRecord failed_instance_record(const std::filesystem::path& path,
                                      const std::string& algorithm,
                                      ObjectiveType objective,
                                      const std::string& error,
                                      const BatchRunConfig& config) {
  BatchRunRecord record;
  record.instance_name = path.stem().string();
  record.algorithm_name = algorithm;
  record.objective = to_string(objective);
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

std::string exact_reference_name(const std::string& requested) {
  if (requested == "ip-kont" || requested == "branch-and-bound") {
    return requested;
  }
  const auto decision =
      ExactReferenceSelector::resolve(requested, "data/instances",
                                      "results/batch");
  return decision.actual_backend == "KONT-COPT" ? "ip-kont" : "branch-and-bound";
}

std::vector<std::string> default_benchmark_algorithms(
    const std::string& requested_exact_reference) {
  std::vector<std::string> algorithms = {"nn", "greedy", "primal-dual",
                                         "local-search", "sa", "genetic",
                                         "lp-rounding", "shifting"};
  const std::string exact_reference =
      requested_exact_reference.empty() || requested_exact_reference == "auto"
          ? exact_reference_name(requested_exact_reference)
          : requested_exact_reference;
  if (std::find(algorithms.begin(), algorithms.end(), exact_reference) ==
      algorithms.end()) {
    algorithms.push_back(exact_reference);
  }
  return algorithms;
}

std::size_t algorithm_phase(const std::string& algorithm) {
  if (algorithm == "nn" || algorithm == "greedy") {
    return 0U;
  }
  if (algorithm == "ip-kont" || algorithm == "branch-and-bound") {
    return 2U;
  }
  return 1U;
}

void clear_previous_batch_output(const std::filesystem::path& output) {
  for (const char* artifact : {"runs", "master_results.json",
                               "master_results.csv", "batch_summary.md"}) {
    std::filesystem::remove_all(output / artifact);
  }
}
}  // namespace

void BatchRunner::run(const BatchRunConfig& config, ILPSolver* ilp) {
  if ((config.exact_reference != "auto" &&
       config.exact_reference != "ip-kont" &&
       config.exact_reference != "branch-and-bound") ||
      !std::isfinite(config.per_ip_time_limit_sec) ||
      config.per_ip_time_limit_sec <= 0.0 ||
      !std::isfinite(config.gap_target) || config.gap_target < 0.0 ||
      config.gap_target >= 1.0 || config.num_threads <= 0) {
    throw std::invalid_argument("batch runner configuration is invalid");
  }
  if (config.objectives.empty()) {
    throw std::invalid_argument("batch runner requires at least one objective");
  }

  const auto exact_reference_decision =
      ExactReferenceSelector::resolve(config.exact_reference,
                                      config.instances_dir, config.output_dir);
  if (config.exact_reference == "auto") {
    LOG_INFO("BatchRunner: exact reference auto-selected '{}' (requested={}, manifest={})",
             exact_reference_decision.actual_backend,
             exact_reference_decision.requested_backend,
             exact_reference_decision.manifest_path);
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

  const auto registered = StaticSolverRegistry::list();
  std::vector<std::string> algorithms;
  const bool wants_default_selection =
      config.algorithm_names.empty() ||
      std::any_of(config.algorithm_names.begin(), config.algorithm_names.end(),
                  [](const std::string& value) {
                    return value == "all" || value == "ALL";
                  });

  if (wants_default_selection) {
    algorithms = default_benchmark_algorithms(config.exact_reference);
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

  if (algorithms.empty()) {
    LOG_ERROR("BatchRunner: no registered algorithms selected");
  }
  std::stable_sort(algorithms.begin(), algorithms.end(),
                   [](const std::string& lhs, const std::string& rhs) {
                     return algorithm_phase(lhs) < algorithm_phase(rhs);
                   });

  struct WorkItem {
    std::filesystem::path instance_path;
    std::string algorithm;
    ObjectiveType objective;
  };
  std::vector<std::vector<WorkItem>> phases(3U);
  for (const auto& path : instance_paths) {
    for (const auto& algorithm : algorithms) {
      for (const auto objective : config.objectives) {
        phases[algorithm_phase(algorithm)].push_back(
            {path, algorithm, objective});
      }
    }
  }

  const std::filesystem::path output(config.output_dir);
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
        futures.push_back(pool.submit([item, config]() {
          try {
            const Instance instance =
                DatasetReader::read_json(item.instance_path.string());
            KontSolver task_ilp;
            return BatchRunner::run_single(instance, item.algorithm,
                                           item.objective, &task_ilp, config);
          } catch (const std::exception& error) {
            return failed_instance_record(item.instance_path, item.algorithm,
                                          item.objective, error.what(), config);
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
                                       item.objective, ilp, config));
        } catch (const std::exception& error) {
          records.push_back(failed_instance_record(
              item.instance_path, item.algorithm, item.objective, error.what(),
              config));
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

  std::filesystem::create_directories(output);
  save_master(records, (output / "master_results.json").string(),
              (output / "master_results.csv").string());
  write_summary(records, (output / "batch_summary.md").string());
  LOG_INFO("BatchRunner: {} runs completed", records.size());
}

BatchRunRecord BatchRunner::run_single(const Instance& instance,
                                       const std::string& algorithm_name,
                                       ObjectiveType objective, ILPSolver* ilp,
                                       const BatchRunConfig& config) {
  BatchRunRecord record;
  record.instance_name = instance.name.empty() ? std::to_string(instance.id)
                                                : instance.name;
  record.algorithm_name = algorithm_name;
  record.objective = to_string(objective);
  record.n = instance.n;
  record.m = instance.m;
  record.time_limit_per_ip_sec = config.per_ip_time_limit_sec;

  const std::filesystem::path run_dir(
      make_run_dir(config.output_dir + "/runs", record.instance_name,
                   algorithm_name, record.objective));
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

    KineticSolution solution;
    if (objective == ObjectiveType::MIN_MAX) {
      MinMaxSolver::Config solver_config;
      solver_config.time_limit_per_ip = config.per_ip_time_limit_sec;
      solver_config.gap_target = config.gap_target;
      solver_config.verify_after = config.verify_after;
      solver_config.trace_csv_path = record.trace_csv_path;
      const auto result = MinMaxSolver::solve(instance, *solver, solver_config);
      record.objective_value = result.peak_cost;
      record.lower_bound = result.lower_bound;
      record.gap = result.gap;
      record.num_iterations = result.num_iterations;
      record.num_ip_solves = result.num_ip_solves;
      record.verified = result.verified;
      record.feasible = result.solution.is_well_formed();
      solution = result.solution;
    } else {
      MinSumSolver::Config solver_config;
      solver_config.time_limit_per_ip = config.per_ip_time_limit_sec;
      solver_config.gap_target = config.gap_target;
      solver_config.verify_after = config.verify_after;
      const auto result = MinSumSolver::solve(instance, *solver, solver_config);
      record.objective_value = result.total_integral;
      record.lower_bound = result.lower_bound_integral;
      record.gap = result.gap;
      record.num_iterations = result.num_iterations;
      record.num_ip_solves = result.num_ip_solves;
      record.verified = result.verified;
      record.feasible = result.solution.is_well_formed();
      solution = result.solution;
      if (config.save_traces) {
        TraceWriter::write_csv(result.trace, record.trace_csv_path);
      }
    }

    if (!record.feasible) {
      record.error_message = "solver returned a malformed solution";
    }
    if (config.save_solutions && record.feasible) {
      SolutionSerializer::save_json(instance, solution,
                                    record.solution_json_path);
    }
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
      << "instance_name,algorithm_name,objective,n,m,wall_time_sec,"
         "cpu_time_sec,peak_memory_mb,time_limit_per_ip_sec,"
         "objective_value,lower_bound,gap,"
         "num_iterations,num_ip_solves,verified,feasible,error_message\n";
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
