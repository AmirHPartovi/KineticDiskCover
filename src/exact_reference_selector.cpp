#include "kdc/exact_reference_selector.hpp"

#include "kdc/algorithms/branch_and_bound_solver.hpp"
#include "kdc/benchmark.hpp"
#include "kdc/io.hpp"
#include "kdc/kont_solver.hpp"
#include "kdc/logging.hpp"
#include "kdc/minmax.hpp"
#include "kdc/minsum.hpp"
#include "kdc/static_solver_registry.hpp"
#include "kdc/verify.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

namespace kdc {
namespace {
using Json = nlohmann::json;

std::string normalize_backend(const std::string& requested_backend) {
  if (requested_backend.empty()) {
    return "auto";
  }
  std::string value = requested_backend;
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  if (value == "kont") {
    return "ip-kont";
  }
  return value;
}

bool is_valid_exact_backend(const std::string& backend) {
  return backend == "ip-kont" || backend == "branch-and-bound";
}

bool solver_is_available_for_runtime(const std::string& backend) {
  if (backend == "branch-and-bound") {
    return StaticSolverRegistry::create("branch-and-bound", nullptr) != nullptr;
  }
  if (backend == "ip-kont") {
    KontSolver solver;
    return solver.name() == "KONT";
  }
  return false;
}

std::vector<std::filesystem::path> candidate_calibration_instances(
    const std::filesystem::path& dataset_dir) {
  std::vector<std::filesystem::path> instances;
  if (!std::filesystem::is_directory(dataset_dir)) {
    return instances;
  }
  for (const auto& entry : std::filesystem::directory_iterator(dataset_dir)) {
    if (entry.is_regular_file() && entry.path().extension() == ".json") {
      instances.push_back(entry.path());
    }
  }
  std::sort(instances.begin(), instances.end());
  if (instances.size() > 8U) {
    instances.resize(8U);
  }
  return instances;
}

bool is_successful_exact_run(const ILPResult& result,
                             const Instance& instance,
                             const KineticSolution& solution,
                             const std::string& objective_name) {
  if (instance.n <= 0 || instance.m <= 0) {
    return false;
  }
  if (!solution.is_well_formed()) {
    return false;
  }
  if (result.status == ILPResult::Status::OPTIMAL ||
      result.status == ILPResult::Status::FEASIBLE) {
    const VerificationReport report = Verifier::verify(instance, solution, 100, 1e-6);
    if (report.all_ok()) {
      return true;
    }
  }
  (void)objective_name;
  return false;
}

std::string actual_backend_name(bool has_kont_cpp_api) {
#ifdef KDC_HAS_COPT_CPP_API
  (void)has_kont_cpp_api;
  return "KONT-COPT";
#else
  (void)has_kont_cpp_api;
  return "built-in-branch-and-bound-fallback";
#endif
}

double runtime_for_backend(const std::string& backend,
                           const std::filesystem::path& dataset_dir) {
  std::vector<std::filesystem::path> instances =
      candidate_calibration_instances(dataset_dir);
  if (instances.empty()) {
    return std::numeric_limits<double>::infinity();
  }

  const auto start = std::chrono::steady_clock::now();
  double total_runtime = 0.0;
  int sample_count = 0;
  for (const auto& instance_path : instances) {
    try {
      const Instance instance = DatasetReader::read_json(instance_path.string());
      if (instance.n <= 0 || instance.m <= 0) {
        continue;
      }
      const auto backend_start = std::chrono::steady_clock::now();
      KontSolver kont;
      BranchAndBoundSolver branch_and_bound(&kont);
      if (backend == "ip-kont") {
        auto solver = StaticSolverRegistry::create("ip-kont", &kont);
        if (!solver) {
          continue;
        }
        (void)MinMaxSolver::solve(instance, *solver, MinMaxSolver::Config{});
      } else {
        auto solver = StaticSolverRegistry::create("branch-and-bound", &kont);
        if (!solver) {
          continue;
        }
        (void)MinSumSolver::solve(instance, *solver, MinSumSolver::Config{});
      }
      const double elapsed =
          std::chrono::duration<double>(std::chrono::steady_clock::now() - backend_start).count();
      total_runtime += elapsed;
      ++sample_count;
    } catch (const std::exception&) {
      continue;
    }
  }
  if (sample_count == 0) {
    return std::numeric_limits<double>::infinity();
  }
  return total_runtime / static_cast<double>(sample_count);
}
}  // namespace

ExactReferenceDecision ExactReferenceSelector::resolve(
    const std::string& requested_backend,
    const std::filesystem::path& dataset_dir,
    const std::filesystem::path& output_dir) {
  ExactReferenceDecision decision;
  const std::string normalized = normalize_backend(requested_backend);
  decision.requested_backend = normalized;
  if (normalized == "ip-kont" || normalized == "branch-and-bound") {
    decision.actual_backend = normalized;
    decision.valid = true;
    decision.uses_kont = normalized == "ip-kont";
    if (!solver_is_available_for_runtime(normalized)) {
      decision.actual_backend = normalized == "ip-kont"
                                    ? "built-in-branch-and-bound-fallback"
                                    : "branch-and-bound";
      decision.uses_kont = false;
    }
    std::filesystem::create_directories(output_dir);
    decision.manifest_path = (output_dir / "experiment_manifest.json").string();
    Json manifest = Json{{"requested_backend", decision.requested_backend},
                         {"actual_backend", decision.actual_backend},
                         {"dataset_dir", dataset_dir.string()},
                         {"output_dir", output_dir.string()}};
    std::ofstream output(decision.manifest_path);
    if (output) {
      output << std::setw(2) << manifest << '\n';
    }
    return decision;
  }

  if (normalized == "auto") {
#ifndef KDC_HAS_COPT_CPP_API
    decision.actual_backend = "branch-and-bound";
    decision.valid = true;
    decision.uses_kont = false;
#else
    const bool has_kont_runtime =
        solver_is_available_for_runtime("ip-kont");
    const bool has_branch_and_bound =
        solver_is_available_for_runtime("branch-and-bound");

    if (!has_kont_runtime && !has_branch_and_bound) {
      decision.actual_backend = "branch-and-bound";
      decision.valid = true;
      decision.uses_kont = false;
    } else if (!has_kont_runtime) {
      decision.actual_backend = "branch-and-bound";
      decision.valid = true;
      decision.uses_kont = false;
    } else {
      const double kont_runtime = runtime_for_backend("ip-kont", dataset_dir);
      const double branch_runtime = runtime_for_backend("branch-and-bound", dataset_dir);
      const bool kont_better = std::isfinite(kont_runtime) &&
                              (!std::isfinite(branch_runtime) || kont_runtime <= branch_runtime);
      decision.actual_backend = kont_better ? "ip-kont" : "branch-and-bound";
      decision.uses_kont = kont_better;
      decision.valid = true;
    }
    decision.actual_backend =
        decision.actual_backend == "ip-kont" ? actual_backend_name(true)
                                              : actual_backend_name(false);
#endif
    std::filesystem::create_directories(output_dir);
    decision.manifest_path = (output_dir / "experiment_manifest.json").string();
    Json manifest = Json{{"requested_backend", decision.requested_backend},
                         {"actual_backend", decision.actual_backend},
                         {"dataset_dir", dataset_dir.string()},
                         {"output_dir", output_dir.string()},
                         {"uses_kont", decision.uses_kont}};
    std::ofstream output(decision.manifest_path);
    if (output) {
      output << std::setw(2) << manifest << '\n';
    }
    return decision;
  }

  decision.actual_backend = "branch-and-bound";
  decision.valid = false;
  return decision;
}

std::string ExactReferenceSelector::default_name(
    const std::string& requested_backend,
    const std::filesystem::path& dataset_dir,
    const std::filesystem::path& output_dir) {
  return ExactReferenceSelector::resolve(requested_backend, dataset_dir,
                                        output_dir)
      .actual_backend == "KONT-COPT"
      ? "ip-kont"
      : "branch-and-bound";
}
}  // namespace kdc
