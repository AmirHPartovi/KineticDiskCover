#include "kdc/sanity_check.hpp"

#include "kdc/io.hpp"
#include "kdc/logging.hpp"
#include "kdc/minmax.hpp"
#include "kdc/minsum.hpp"
#include "kdc/mock_ilp_solver.hpp"
#include "kdc/solution_serializer.hpp"
#include "kdc/static_solver_registry.hpp"
#include "kdc/stationary.hpp"
#include "kdc/verify.hpp"
#include "kdc/algorithms/nn_static_solver.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>

namespace kdc {
namespace {
using Clock = std::chrono::steady_clock;

SanityCheckItem make_item(std::string name, bool passed, std::string detail) {
  return {std::move(name), passed, std::move(detail), 0.0};
}

template <typename Check>
SanityCheckItem run_timed(const std::string& name, Check&& check) {
  const auto start = Clock::now();
  try {
    SanityCheckItem result = check();
    result.name = name;
    result.duration_sec =
        std::chrono::duration<double>(Clock::now() - start).count();
    return result;
  } catch (const std::exception& error) {
    SanityCheckItem result =
        make_item(name, false, std::string("exception: ") + error.what());
    result.duration_sec =
        std::chrono::duration<double>(Clock::now() - start).count();
    return result;
  } catch (...) {
    SanityCheckItem result = make_item(name, false, "unknown exception");
    result.duration_sec =
        std::chrono::duration<double>(Clock::now() - start).count();
    return result;
  }
}

std::string markdown_cell(std::string value) {
  std::replace(value.begin(), value.end(), '|', '/');
  std::replace(value.begin(), value.end(), '\n', ' ');
  std::replace(value.begin(), value.end(), '\r', ' ');
  return value;
}

Instance make_check_instance(int n, int m, unsigned seed) {
  return DatasetReader::generate_random(n, m, seed);
}

std::filesystem::path resolve_data_directory(const std::string& directory) {
  const std::filesystem::path requested(directory);
  if (std::filesystem::is_directory(requested)) {
    return requested;
  }
  const auto from_parent = std::filesystem::current_path().parent_path() /
                           requested;
  if (std::filesystem::is_directory(from_parent)) {
    return from_parent;
  }
  return requested;
}
}  // namespace

void SanityCheckReport::write_markdown(const std::string& path) const {
  const std::filesystem::path report_path(path);
  if (!report_path.parent_path().empty()) {
    std::filesystem::create_directories(report_path.parent_path());
  }
  std::ofstream output(report_path);
  if (!output) {
    throw std::runtime_error("cannot open preflight report: " + path);
  }

  std::vector<SanityCheckItem> sorted_items = items;
  std::sort(sorted_items.begin(), sorted_items.end(),
            [](const SanityCheckItem& lhs, const SanityCheckItem& rhs) {
              return lhs.name < rhs.name;
            });
  double total_duration = 0.0;
  for (const auto& item : items) {
    total_duration += item.duration_sec;
  }

  output << "# Pre-flight Sanity Check Report\n\n"
         << "- **Summary:** " << num_passed << " / " << items.size()
         << " checks passed (" << num_failed << " failed)\n"
         << "- **Total check duration:** " << total_duration << " s\n\n"
         << "| Check | Status | Duration (s) | Detail |\n"
         << "|---|---|---:|---|\n";
  for (const auto& item : sorted_items) {
    output << '|' << markdown_cell(item.name) << '|'
           << (item.passed ? "PASS" : "FAIL") << '|' << item.duration_sec
           << '|' << markdown_cell(item.detail) << "|\n";
  }
  if (!output) {
    throw std::runtime_error("failed writing preflight report: " + path);
  }
}

SanityCheckReport SanityChecker::run(ILPSolver* ilp) {
  SanityCheckReport report;
  report.items.push_back(run_timed("registry_populated", [] {
    return check_registry_populated();
  }));

  std::unique_ptr<ILPSolver> default_ilp;
  ILPSolver* active_ilp = ilp;
  if (active_ilp == nullptr) {
    default_ilp = std::make_unique<MockILPSolver>();
    active_ilp = default_ilp.get();
    LOG_WARN("SanityChecker: KONT unavailable; using MockILPSolver");
    report.items.push_back(make_item(
        "ilp_backend", true, "KONT not provided; using MockILPSolver"));
  } else {
    report.items.push_back(
        make_item("ilp_backend", true, "using " + active_ilp->name()));
  }

  for (const auto& name : StaticSolverRegistry::list()) {
    report.items.push_back(run_timed("algorithm:" + name, [name, active_ilp] {
      return check_algorithm_callable(name, active_ilp);
    }));
  }
  report.items.push_back(run_timed("all_algorithms_on_dummy", [active_ilp] {
    return check_all_algorithms_on_dummy(active_ilp);
  }));
  report.items.push_back(run_timed("verifier_accepts_all", [active_ilp] {
    return check_verifier_accepts_all(active_ilp);
  }));
  report.items.push_back(
      run_timed("minmax_pipeline", [active_ilp] {
        return check_minmax_pipeline(active_ilp);
      }));
  report.items.push_back(
      run_timed("minsum_pipeline", [active_ilp] {
        return check_minsum_pipeline(active_ilp);
      }));
  report.items.push_back(
      run_timed("serializer_roundtrip", [] {
        return check_serializer_roundtrip();
      }));
  report.items.push_back(run_timed("data_instances_directory", [] {
    return check_data_instances_directory("data/instances");
  }));

  for (const auto& item : report.items) {
    if (item.passed) {
      ++report.num_passed;
    } else {
      ++report.num_failed;
    }
  }
  LOG_INFO("SanityCheck: {} passed, {} failed", report.num_passed,
           report.num_failed);
  return report;
}

SanityCheckItem SanityChecker::check_registry_populated() {
  const auto names = StaticSolverRegistry::list();
  std::ostringstream detail;
  detail << "registered: ";
  for (std::size_t index = 0; index < names.size(); ++index) {
    if (index != 0U) {
      detail << ", ";
    }
    detail << names[index];
  }
  return make_item("registry_populated", names.size() >= 2U, detail.str());
}

SanityCheckItem SanityChecker::check_algorithm_callable(const std::string& name,
                                                        ILPSolver* ilp) {
  auto solver = StaticSolverRegistry::create(name, ilp);
  if (!solver) {
    return make_item("algorithm:" + name, false, "create() returned nullptr");
  }
  const Instance instance = make_check_instance(10, 3, 42U);
  const StaticSolution solution = solver->solve(instance, 0.5);
  const bool passed = solution.feasible && std::isfinite(solution.cost) &&
                      solution.cost >= 0.0;
  return make_item("algorithm:" + name, passed,
                   passed ? "cost=" + std::to_string(solution.cost)
                          : "infeasible or invalid cost");
}

SanityCheckItem SanityChecker::check_all_algorithms_on_dummy(ILPSolver* ilp) {
  const Instance instance = make_check_instance(8, 3, 7U);
  for (const auto& name : StaticSolverRegistry::list()) {
    auto solver = StaticSolverRegistry::create(name, ilp);
    if (!solver) {
      return make_item("all_algorithms_on_dummy", false,
                       "algorithm " + name + " could not be created");
    }
    const StaticSolution solution = solver->solve(instance, 0.5);
    if (!solution.feasible || !std::isfinite(solution.cost) ||
        solution.cost < 0.0) {
      return make_item("all_algorithms_on_dummy", false,
                       "algorithm " + name + " returned an invalid solution");
    }
  }
  return make_item("all_algorithms_on_dummy", true,
                   "all algorithms produced feasible solutions");
}

SanityCheckItem SanityChecker::check_verifier_accepts_all(ILPSolver* ilp) {
  (void)ilp;
  const Instance instance = make_check_instance(10, 3, 7U);
  NNStaticSolver nearest;
  MinMaxSolver::Config config;
  const auto result = MinMaxSolver::solve(instance, nearest, config);
  const auto verification = Verifier::verify(instance, result.solution, 100,
                                               1e-6);
  if (verification.all_ok()) {
    return make_item("verifier_accepts_all", true,
                     "verifier accepts the NN kinetic solution");
  }
  const std::string detail = verification.errors.empty()
                                 ? "verifier rejected the NN solution"
                                 : "verifier rejected: " +
                                       verification.errors.front();
  return make_item("verifier_accepts_all", false, detail);
}

SanityCheckItem SanityChecker::check_minmax_pipeline(ILPSolver* ilp) {
  const Instance instance = make_check_instance(8, 3, 7U);
  auto solver = StaticSolverRegistry::create("ip-kont", ilp);
  if (!solver) {
    return make_item("minmax_pipeline", false,
                     "could not create the ip-kont static solver");
  }
  MinMaxSolver::Config config;
  config.gap_target = 0.99;
  config.time_limit_per_ip = 10.0;
  const auto result = MinMaxSolver::solve(instance, *solver, config);
  return make_item(
      "minmax_pipeline", result.verified,
      result.verified
          ? "min-max pipeline OK, peak=" + std::to_string(result.peak_cost)
          : "min-max solution failed verification");
}

SanityCheckItem SanityChecker::check_minsum_pipeline(ILPSolver* ilp) {
  const Instance instance = make_check_instance(8, 3, 7U);
  auto solver = StaticSolverRegistry::create("ip-kont", ilp);
  if (!solver) {
    return make_item("minsum_pipeline", false,
                     "could not create the ip-kont static solver");
  }
  MinSumSolver::Config config;
  config.gap_target = 0.99;
  config.time_limit_per_ip = 10.0;
  config.lb_num_samples = 2;
  const auto result = MinSumSolver::solve(instance, *solver, config);
  return make_item(
      "minsum_pipeline", result.verified,
      result.verified
          ? "min-sum pipeline OK, integral=" +
                std::to_string(result.total_integral)
          : "min-sum solution failed verification");
}

SanityCheckItem SanityChecker::check_serializer_roundtrip() {
  const Instance instance = make_check_instance(10, 3, 1U);
  NNStaticSolver nearest;
  MinMaxSolver::Config config;
  const auto result = MinMaxSolver::solve(instance, nearest, config);
  const auto path = std::filesystem::temp_directory_path() /
                    "kdc_preflight_roundtrip.json";
  SolutionSerializer::save_json(instance, result.solution, path.string());
  const auto loaded = SolutionSerializer::load_json(instance, path.string());
  std::error_code remove_error;
  std::filesystem::remove(path, remove_error);
  const double original_peak = result.solution.peak_cost();
  const double loaded_peak = loaded.peak_cost();
  const bool passed = std::isfinite(original_peak) && std::isfinite(loaded_peak) &&
                      std::abs(original_peak - loaded_peak) <=
                          1e-6 * std::max(1.0, std::abs(original_peak));
  return make_item("serializer_roundtrip", passed,
                   passed ? "serializer roundtrip OK" : "peak mismatch");
}

SanityCheckItem SanityChecker::check_data_instances_directory(
    const std::string& dir) {
  const auto directory = resolve_data_directory(dir);
  if (!std::filesystem::is_directory(directory)) {
    return make_item("data_instances_directory", false,
                     "directory does not exist: " + directory.string());
  }

  std::vector<std::filesystem::path> json_files;
  for (const auto& entry : std::filesystem::directory_iterator(directory)) {
    if (entry.is_regular_file() && entry.path().extension() == ".json") {
      json_files.push_back(entry.path());
    }
  }
  std::sort(json_files.begin(), json_files.end());
  if (json_files.empty()) {
    return make_item("data_instances_directory", false,
                     "directory contains no JSON instances: " +
                         directory.string());
  }

  try {
    (void)DatasetReader::read_json(json_files.front().string());
  } catch (const std::exception& error) {
    return make_item("data_instances_directory", false,
                     "cannot load " + json_files.front().string() + ": " +
                         error.what());
  }
  return make_item("data_instances_directory", true,
                   std::to_string(json_files.size()) +
                       " JSON instances found in " + directory.string());
}

}  // namespace kdc
