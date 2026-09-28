#include "kdc/benchmark.hpp"
#include "kdc/batch_runner.hpp"
#include "kdc/algorithm_comparison.hpp"
#include "kdc/cli_utils.hpp"
#include "kdc/io.hpp"
#include "kdc/kont_solver.hpp"
#include "kdc/logging.hpp"
#include "kdc/minmax.hpp"
#include "kdc/minsum.hpp"
#include "kdc/objective.hpp"
#include "kdc/solution_serializer.hpp"
#include "kdc/sanity_check.hpp"
#include "kdc/stats.hpp"
#include "kdc/static_solver_registry.hpp"

#include <cmath>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {
void usage() {
  std::cout
      << "Usage: kdc-solver <command> [options]\n"
      << "\nCommands:\n"
      << "  solve     --instance FILE --mode minmax|minsum --output FILE "
         "[--algorithm nn|ip-kont|brute-force|branch-and-bound|greedy|"
         "lp-rounding|primal-dual|local-search|sa|genetic|shifting]\n"
      << "  verify    --instance FILE --solution FILE\n"
      << "  benchmark --dataset DIR --output DIR --mode both|minmax|minsum "
         "[--parallel] [--threads N]\n"
      << "  compare   --algorithms a,b --dataset DIR --output DIR "
         "[--mode static|minmax|minsum]\n"
      << "  preflight [--output FILE]\n"
      << "  batch     [--instances DIR] [--output DIR] [--algorithms LIST] "
         "[--modes minmax|minsum|both] [--parallel] [--threads N]\n"
      << "  --help    Show this help message\n";
}

void command_help(const std::string& command) {
  if (command == "solve") {
    kdc::print_usage("solve");
  } else if (command == "verify") {
    std::cout << "Usage: kdc-solver verify --instance FILE --solution FILE\n";
  } else if (command == "benchmark") {
    std::cout << "Usage: kdc-solver benchmark --dataset DIR --output DIR "
                 "--mode both|minmax|minsum [--parallel] [--threads N]\n"
                 "  --parallel               Run instances concurrently\n"
                 "  --threads N              Worker threads (default: 1)\n";
  } else if (command == "compare") {
    std::cout << "Usage: kdc-solver compare --algorithms a,b --dataset DIR "
                 "--output DIR [--mode static|minmax|minsum]\n";
  } else if (command == "preflight") {
    std::cout << "Usage: kdc-solver preflight "
                 "[--output results/preflight/preflight_report.md]\n";
  } else if (command == "batch") {
    std::cout
        << "Usage: kdc-solver batch [OPTIONS]\n"
        << "  --instances DIR      Instance directory (default: "
           "data/instances)\n"
        << "  --output DIR         Output directory (default: results/batch)\n"
        << "  --algorithms LIST    Comma-separated algorithm names (default: all)\n"
        << "  --modes MODE         minmax|minsum|both (default: both)\n"
        << "  --parallel           Run instances concurrently\n"
        << "  --threads N          Number of worker threads (default: 1)\n"
        << "  --time-limit SEC     Per static/IP solve timeout (default: 10)\n"
        << "  --no-verify          Skip solution verification\n"
        << "  --no-solutions       Do not save solution JSON files\n"
        << "  --no-traces          Do not save trace CSV files\n";
  }
}

std::vector<std::string> split_csv(const std::string& text) {
  std::vector<std::string> values;
  std::istringstream stream(text);
  std::string value;
  while (std::getline(stream, value, ',')) {
    if (!value.empty()) {
      values.push_back(value);
    }
  }
  return values;
}

int run_compare(int argc, char** argv) {
  kdc::AlgorithmComparisonConfig config;
  std::string dataset;
  std::string output;
  std::string mode = "static";
  bool algorithms_seen = false;
  for (int index = 2; index < argc; ++index) {
    const std::string option(argv[index]);
    if (index + 1 >= argc) {
      throw std::invalid_argument("missing value for " + option);
    }
    const std::string value(argv[++index]);
    if (option == "--algorithms") {
      config.algorithm_names = split_csv(value);
      algorithms_seen = true;
    } else if (option == "--dataset") {
      dataset = value;
    } else if (option == "--output") {
      output = value;
    } else if (option == "--mode") {
      mode = value;
    } else {
      throw std::invalid_argument("unknown compare option: " + option);
    }
  }
  if (!algorithms_seen || dataset.empty() || output.empty()) {
    throw std::invalid_argument(
        "compare requires --algorithms a,b --dataset DIR --output DIR");
  }
  if (mode != "static" && mode != "minmax" && mode != "minsum") {
    throw std::invalid_argument(
        "compare mode must be static, minmax, or minsum");
  }
  config.output_dir = output;
  for (const auto& entry : std::filesystem::directory_iterator(dataset)) {
    if (entry.is_regular_file() && entry.path().extension() == ".json") {
      config.instance_paths.push_back(entry.path().string());
    }
  }
  std::sort(config.instance_paths.begin(), config.instance_paths.end());
  if (config.instance_paths.empty()) {
    throw std::invalid_argument("compare dataset contains no .json instances");
  }
  kdc::KontSolver kont;
  const std::filesystem::path root(config.output_dir);
  if (mode != "static") {
    std::vector<kdc::KineticComparisonResult> kinetic_results;
    kdc::AlgorithmComparator::compare_kinetic(
        config, &kont, mode, kinetic_results);
    const std::string stem = mode + "_results";
    kdc::AlgorithmComparator::save_results(
        kinetic_results, (root / (stem + ".json")).string(),
        (root / (stem + ".csv")).string());

    std::ofstream verification(root / "verification_report.md");
    if (!verification) {
      throw std::runtime_error("cannot open comparison verification report");
    }
    verification << "# Verification report (" << mode << ")\n\n"
                 << "Records: " << kinetic_results.size() << "\n\n"
                 << "| Instance | Algorithm | Objective | Verified |\n"
                 << "|---|---|---:|:---:|\n";
    for (const auto& record : kinetic_results) {
      verification << '|' << record.instance_name << '|'
                   << record.algorithm_name << '|' << record.objective_value
                   << '|' << (record.verified ? "yes" : "no") << "|\n";
    }
    if (!verification) {
      throw std::runtime_error("failed writing verification report");
    }
    std::ofstream statistics(root / "statistical_analysis.md");
    if (!statistics) {
      throw std::runtime_error("cannot open statistical analysis report");
    }
    statistics << "# Statistical analysis (" << mode << ")\n\n"
               << "Kinetic comparison records are available in `" << stem
               << ".csv`; statistical tests are not computed for this "
                  "objective.\n";
    if (!statistics) {
      throw std::runtime_error("failed writing statistical analysis report");
    }
    LOG_INFO("Kinetic comparison: {} results saved under {}",
             kinetic_results.size(), config.output_dir);
    return 0;
  }
  std::vector<kdc::StaticComparisonResult> results;
  kdc::AlgorithmComparator::compare_static(config, &kont, results);
  const auto json_path = root / "static_results.json";
  const auto csv_path = root / "static_results.csv";
  kdc::AlgorithmComparator::save_results(results, json_path.string(),
                                         csv_path.string());

  std::ofstream verification(root / "verification_report.md");
  if (!verification) {
    throw std::runtime_error("cannot open comparison verification report");
  }
  verification << "# Verification report\n\n"
               << "Records: " << results.size() << "\n\n"
               << "| Instance | Algorithm | Time | Feasible | Verified |\n"
               << "|---|---|---:|:---:|:---:|\n";
  std::map<std::string, std::vector<double>> algorithm_costs;
  for (const auto& record : results) {
    verification << '|' << record.instance_name << '|'
                 << record.algorithm_name << '|' << record.t << '|'
                 << (record.feasible ? "yes" : "no") << '|'
                 << (record.verified ? "yes" : "no") << "|\n";
    if (record.feasible) {
      algorithm_costs[record.algorithm_name].push_back(record.cost);
    }
  }
  if (!verification) {
    throw std::runtime_error("failed writing verification report");
  }

  std::ofstream statistics(root / "statistical_analysis.md");
  if (!statistics) {
    throw std::runtime_error("cannot open statistical analysis report");
  }
  statistics << "# Statistical analysis\n\n"
             << "Paired Wilcoxon signed-rank tests compare each algorithm's "
                "costs with the configured reference (`"
             << config.reference_algorithm << "`). Samples are paired by "
                "instance and time.\n\n"
             << "| Algorithm | Samples | Median cost | Reference median | "
                "Wilcoxon W | p-value | Cliff's delta | Effect |\n"
             << "|---|---:|---:|---:|---:|---:|---:|---|\n";
  std::map<std::pair<std::string, double>, double> reference;
  for (const auto& record : results) {
    if (record.algorithm_name == config.reference_algorithm) {
      reference[{record.instance_name, record.t}] = record.cost;
    }
  }
  for (const auto& entry : algorithm_costs) {
    if (entry.first == config.reference_algorithm) {
      continue;
    }
    std::vector<double> sample;
    std::vector<double> reference_sample;
    for (const auto& record : results) {
      if (record.algorithm_name != entry.first || !record.feasible) {
        continue;
      }
      const auto ref = reference.find({record.instance_name, record.t});
      if (ref != reference.end()) {
        sample.push_back(record.cost);
        reference_sample.push_back(ref->second);
      }
    }
    const auto test =
        kdc::Stats::paired_wilcoxon(sample, reference_sample);
    statistics << '|' << entry.first << '|' << test.sample_size << '|'
               << test.median_a << '|' << test.median_b << '|'
               << test.wilcoxon_statistic << '|' << test.p_value << '|'
               << test.cliffs_delta << '|' << test.interpretation << "|\n";
  }
  if (!statistics) {
    throw std::runtime_error("failed writing statistical analysis report");
  }
  LOG_INFO("Comparison: {} results saved under {}", results.size(),
           config.output_dir);
  return 0;
}

kdc::BenchmarkConfig parse_benchmark_args(int argc, char** argv) {
  const kdc::CliArgs args = kdc::CliArgs::parse(argc, argv);
  kdc::BenchmarkConfig config;
  config.dataset_dir = args.get("dataset");
  config.output_dir = args.get("output");
  config.parallel = args.has("parallel");
  config.num_threads = args.get_int("threads", 1);
  const std::string mode = args.get("mode");
  if (mode == "both") {
    config.both_objectives = true;
  } else if (!mode.empty()) {
    config.objective = kdc::objective_from_string(mode);
  }
  if (config.dataset_dir.empty() || config.output_dir.empty() || mode.empty()) {
    throw std::invalid_argument(
        "benchmark requires --dataset DIR --output DIR --mode "
        "both|minmax|minsum");
  }
  if (mode != "both" && mode != "minmax" && mode != "minsum") {
    throw std::invalid_argument("benchmark mode must be both, minmax, or minsum");
  }
  if (config.parallel && config.num_threads <= 0) {
    config.num_threads = static_cast<int>(
        std::max(1U, std::thread::hardware_concurrency()));
  }
  return config;
}

int handle_solve(int argc, char** argv) {
  const kdc::CliArgs args = kdc::CliArgs::parse(argc, argv);
  if (args.has("help")) {
    kdc::print_usage("solve");
    return 0;
  }

  const std::string instance_path = args.get("instance");
  if (instance_path.empty()) {
    std::cerr << "error: --instance is required\n";
    kdc::print_usage("solve");
    return 1;
  }
  const std::string mode = args.get("mode", "minmax");
  const std::string algorithm = args.get("algorithm", "ip-kont");
  const std::string output_path = args.get("output");
  const double time_limit = args.get_double("time-limit", 600.0);
  const double gap_target = args.get_double("gap", 0.01);
  const bool verify_after = true;
  const bool use_handovers = !args.has("no-handovers");
  const bool use_no_dup = !args.has("no-dedup");
  const bool use_partial_ext = !args.has("no-partial");

  kdc::ObjectiveType objective;
  try {
    objective = kdc::objective_from_string(mode);
  } catch (const std::exception& error) {
    std::cerr << "error: " << error.what() << '\n';
    kdc::print_usage("solve");
    return 1;
  }
  if (!std::isfinite(time_limit) || time_limit <= 0.0 ||
      !std::isfinite(gap_target) || gap_target < 0.0) {
    std::cerr << "error: --time-limit must be positive and --gap "
                 "must be non-negative\n";
    return 1;
  }

  kdc::Instance instance;
  try {
    instance = kdc::DatasetReader::read_json(instance_path);
  } catch (const std::exception& error) {
    std::cerr << "error: failed to read instance: " << error.what() << '\n';
    return 1;
  }
  auto kont = std::make_unique<kdc::KontSolver>();
  auto static_solver =
      kdc::StaticSolverRegistry::create(algorithm, kont.get());
  if (!static_solver) {
    std::cerr << "error: unknown algorithm '" << algorithm << "'\n"
              << "available algorithms:\n";
    for (const auto& name : kdc::StaticSolverRegistry::list()) {
      std::cerr << "  " << name << '\n';
    }
    return 1;
  }

  kdc::KineticSolution solution;
  if (objective == kdc::ObjectiveType::MIN_MAX) {
    kdc::MinMaxSolver::Config config;
    config.time_limit_per_ip = time_limit;
    config.gap_target = gap_target;
    config.use_handovers = use_handovers;
    config.use_no_dup = use_no_dup;
    config.use_partial_ext = use_partial_ext;
    config.verify_after = verify_after;
    auto result = kdc::MinMaxSolver::solve(instance, *static_solver, config);
    std::cout << "min-max peak_cost=" << result.peak_cost
              << " LB=" << result.lower_bound << " gap=" << result.gap
              << " iters=" << result.num_iterations << " time="
              << result.total_time_sec << "s\n";
    solution = std::move(result.solution);
  } else {
    kdc::MinSumSolver::Config config;
    config.time_limit_per_ip = time_limit;
    config.gap_target = gap_target;
    config.use_handovers = use_handovers;
    config.use_no_dup = use_no_dup;
    config.use_partial_ext = use_partial_ext;
    config.verify_after = verify_after;
    auto result = kdc::MinSumSolver::solve(instance, *static_solver, config);
    std::cout << "min-sum integral=" << result.total_integral
              << " LB=" << result.lower_bound_integral << " gap="
              << result.gap << " iters=" << result.num_iterations
              << " time=" << result.total_time_sec << "s\n";
    solution = std::move(result.solution);
  }

  if (!output_path.empty()) {
    kdc::SolutionSerializer::save_json(instance, solution, output_path);
    std::cout << "solution saved to " << output_path << '\n';
  }
  return 0;
}

int handle_preflight(int argc, char** argv) {
  const kdc::CliArgs args = kdc::CliArgs::parse(argc, argv);
  const std::string output =
      args.get("output", "results/preflight/preflight_report.md");

  std::unique_ptr<kdc::ILPSolver> kont;
  kdc::ILPSolver* ilp = nullptr;
#if defined(KDC_HAS_KONT)
  try {
    kont = std::make_unique<kdc::KontSolver>();
    ilp = kont.get();
  } catch (const std::exception& error) {
    LOG_WARN("KONT unavailable ({}); using MockILPSolver", error.what());
  }
#else
  LOG_WARN("KONT unavailable; using MockILPSolver");
#endif

  const auto report = kdc::SanityChecker::run(ilp);
  const std::filesystem::path output_path(output);
  if (!output_path.parent_path().empty()) {
    std::filesystem::create_directories(output_path.parent_path());
  }
  report.write_markdown(output);
  std::cout << "preflight: " << report.num_passed << " passed, "
            << report.num_failed << " failed\n";
  return report.all_passed() ? 0 : 1;
}

int handle_batch(int argc, char** argv) {
  const kdc::CliArgs args = kdc::CliArgs::parse(argc, argv);
  kdc::BatchRunConfig config;
  config.instances_dir = args.get("instances", "data/instances");
  config.output_dir = args.get("output", "results/batch");
  config.parallel = args.has("parallel");
  config.num_threads = args.get_int("threads", 1);
  config.verify_after = !args.has("no-verify");
  config.save_solutions = !args.has("no-solutions");
  config.save_traces = !args.has("no-traces");
  config.algorithm_names = split_csv(args.get("algorithms"));
  config.per_ip_time_limit_sec = args.get_double("time-limit", 10.0);
  if (!std::isfinite(config.per_ip_time_limit_sec) ||
      config.per_ip_time_limit_sec <= 0.0) {
    throw std::invalid_argument("--time-limit must be positive");
  }

  const std::string modes = args.get("modes", "both");
  config.objectives.clear();
  if (modes == "minmax" || modes == "both") {
    config.objectives.push_back(kdc::ObjectiveType::MIN_MAX);
  }
  if (modes == "minsum" || modes == "both") {
    config.objectives.push_back(kdc::ObjectiveType::MIN_SUM);
  }
  if (config.objectives.empty()) {
    throw std::invalid_argument(
        "--modes must be minmax, minsum, or both");
  }
  if (config.parallel && config.num_threads <= 0) {
    config.num_threads = static_cast<int>(
        std::max(1U, std::thread::hardware_concurrency()));
  }

  kdc::KontSolver kont;
  kdc::BatchRunner::run(config, &kont);
  return 0;
}
}

int main(int argc, char** argv) {
  kdc::init_logging();
  if (argc < 2) {
    usage();
    return 0;
  }
  const std::string command(argv[1]);
  if (command == "--help" || command == "-h") {
    usage();
    return 0;
  }
  if (command != "solve" && command != "verify" &&
      command != "benchmark" && command != "compare" &&
      command != "preflight" && command != "batch") {
    usage();
    return 1;
  }
  if (argc > 2 &&
      (std::string(argv[2]) == "--help" ||
       std::string(argv[2]) == "-h")) {
    command_help(command);
    return 0;
  }
  if (command == "benchmark") {
    try {
      const auto config = parse_benchmark_args(argc, argv);
      kdc::BenchmarkRunner::run_all(config);
      return 0;
    } catch (const std::exception& error) {
      LOG_ERROR("benchmark failed: {}", error.what());
      return 1;
    }
  }
  if (command == "compare") {
    try {
      return run_compare(argc, argv);
    } catch (const std::exception& error) {
      LOG_ERROR("compare failed: {}", error.what());
      return 1;
    }
  }
  if (command == "preflight") {
    try {
      return handle_preflight(argc, argv);
    } catch (const std::exception& error) {
      LOG_ERROR("preflight failed: {}", error.what());
      return 1;
    }
  }
  if (command == "batch") {
    try {
      return handle_batch(argc, argv);
    } catch (const std::exception& error) {
      LOG_ERROR("batch failed: {}", error.what());
      return 1;
    }
  }
  if (command == "solve") {
    try {
      return handle_solve(argc, argv);
    } catch (const std::exception& error) {
      LOG_ERROR("solve failed: {}", error.what());
      return 1;
    }
  }
  LOG_INFO("{} not implemented yet", command);
  return 0;
}
