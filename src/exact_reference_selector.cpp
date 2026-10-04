#include "kdc/exact_reference_selector.hpp"

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
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace kdc {
namespace {
using Json = nlohmann::json;
using Clock = std::chrono::steady_clock;

constexpr int kManifestVersion = 2;
constexpr const char* kFingerprintAlgorithm =
    "FNV-1a-64 over sorted relative JSON paths and raw bytes";
constexpr std::size_t kMaximumCalibrationInstances = 3U;
constexpr int kMaximumCalibrationPoints = 8;
constexpr int kMaximumCalibrationStations = 4;
constexpr double kCalibrationTimeLimitSec = 20.0;

struct CalibrationInstance {
  std::filesystem::path path;
  Instance instance;
};

std::string normalize_backend(const std::string& requested_backend) {
  if (requested_backend.empty()) {
    return "auto";
  }
  std::string value = requested_backend;
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char character) {
                   return static_cast<char>(std::tolower(character));
                 });
  return value == "kont" ? "ip-kont" : value;
}

bool is_backend(const std::string& backend) {
  return backend == "ip-kont" || backend == "branch-and-bound";
}

std::string absolute_path(const std::filesystem::path& path) {
  return std::filesystem::absolute(path).lexically_normal().string();
}

std::string dataset_fingerprint(const std::filesystem::path& dataset_dir) {
  std::vector<std::filesystem::path> files;
  if (std::filesystem::is_directory(dataset_dir)) {
    for (const auto& entry :
         std::filesystem::recursive_directory_iterator(dataset_dir)) {
      if (entry.is_regular_file() && entry.path().extension() == ".json") {
        files.push_back(entry.path());
      }
    }
  }
  std::sort(files.begin(), files.end());
  std::uint64_t hash = 14695981039346656037ULL;
  const auto append = [&hash](const char* data, std::size_t size) {
    for (std::size_t index = 0; index < size; ++index) {
      hash ^= static_cast<unsigned char>(data[index]);
      hash *= 1099511628211ULL;
    }
  };
  for (const auto& file : files) {
    const std::string relative =
        std::filesystem::relative(file, dataset_dir).generic_string();
    append(relative.data(), relative.size());
    append("\0", 1U);
    std::ifstream input(file, std::ios::binary);
    if (!input) {
      throw std::runtime_error("cannot read dataset file for fingerprint: " +
                               file.string());
    }
    char buffer[4096];
    while (input) {
      input.read(buffer, sizeof(buffer));
      append(buffer, static_cast<std::size_t>(input.gcount()));
    }
    if (!input.eof()) {
      throw std::runtime_error("failed reading dataset file for fingerprint: " +
                               file.string());
    }
  }
  std::ostringstream output;
  output << std::hex << std::setfill('0') << std::setw(16) << hash;
  return output.str();
}

Json load_manifest(const std::filesystem::path& path) {
  std::ifstream input(path);
  if (!input) {
    return Json::object();
  }
  try {
    Json manifest;
    input >> manifest;
    return manifest.is_object() ? manifest : Json::object();
  } catch (const Json::exception&) {
    return Json::object();
  }
}

bool reusable_calibration(const Json& manifest, const std::string& dataset,
                          const std::string& fingerprint,
                          ExactReferenceDecision& decision) {
  try {
    if (manifest.value("calibration_decision_version", 0) !=
            kManifestVersion ||
        manifest.value("requested_backend", std::string{}) != "auto" ||
        manifest.value("dataset_dir", std::string{}) != dataset ||
        manifest.value("dataset_fingerprint", std::string{}) != fingerprint ||
        manifest.value("dataset_fingerprint_algorithm", std::string{}) !=
            kFingerprintAlgorithm ||
        !manifest.contains("selected_backend") ||
        !manifest.at("selected_backend").is_string() ||
        !is_backend(manifest.at("selected_backend").get<std::string>()) ||
        !manifest.contains("actual_backend") ||
        !manifest.at("actual_backend").is_string() ||
        !manifest.contains("kont_runtime_available") ||
        !manifest.at("kont_runtime_available").is_boolean() ||
        !manifest.contains("calibration_instances") ||
        !manifest.at("calibration_instances").is_array() ||
        !manifest.contains("calibration_objectives") ||
        manifest.at("calibration_objectives") !=
            Json::array({"minmax", "minsum"}) ||
        !manifest.contains("successful_runs") ||
        !manifest.at("successful_runs").is_array() ||
        !manifest.contains("rejected_runs") ||
        !manifest.at("rejected_runs").is_array() ||
        !manifest.contains("runtime_statistics") ||
        !manifest.at("runtime_statistics").is_object() ||
        !manifest.at("runtime_statistics").contains("by_objective") ||
        !manifest.contains("selection_rule") ||
        !manifest.at("selection_rule").is_string()) {
      return false;
    }
    const std::string selected =
        manifest.at("selected_backend").get<std::string>();
    const std::string actual =
        manifest.at("actual_backend").get<std::string>();
    const bool kont_available =
        manifest.at("kont_runtime_available").get<bool>();
    if ((selected == "ip-kont" &&
         (!kont_available || actual != "KONT/COPT")) ||
        (selected == "branch-and-bound" &&
         actual != (kont_available
                        ? "branch-and-bound"
                        : "built-in-branch-and-bound-fallback"))) {
      return false;
    }
    const auto& successful = manifest.at("successful_runs");
    for (const auto& run : successful) {
      if (!run.is_object() || !run.value("accepted", false) ||
          !run.value("feasible", false) ||
          !run.value("continuously_verified", false) ||
          !run.value("optimality_proven", false)) {
        return false;
      }
    }
    decision.requested_backend = "auto";
    decision.selected_backend = selected;
    decision.actual_backend = actual;
    decision.selection_rule = manifest.at("selection_rule").get<std::string>();
    decision.calibration_instances =
        manifest.at("calibration_instances").get<std::vector<std::string>>();
    for (const auto& run : successful) {
      decision.successful_runs.push_back(run.dump());
    }
    for (const auto& run : manifest.at("rejected_runs")) {
      decision.rejected_runs.push_back(run.dump());
    }
    const Json& statistics = manifest.at("runtime_statistics");
    decision.ip_kont_median_runtime_sec =
        statistics.value("ip-kont_median_sec", -1.0);
    decision.branch_and_bound_median_runtime_sec =
        statistics.value("branch-and-bound_median_sec", -1.0);
    decision.uses_kont = decision.selected_backend == "ip-kont";
    decision.valid = true;
    return true;
  } catch (const Json::exception&) {
    return false;
  }
}

std::vector<CalibrationInstance> select_calibration_instances(
    const std::filesystem::path& dataset_dir) {
  std::vector<std::filesystem::path> paths;
  if (!std::filesystem::is_directory(dataset_dir)) {
    return {};
  }
  for (const auto& entry :
       std::filesystem::recursive_directory_iterator(dataset_dir)) {
    if (entry.is_regular_file() && entry.path().extension() == ".json") {
      paths.push_back(entry.path());
    }
  }
  std::sort(paths.begin(), paths.end());

  std::vector<CalibrationInstance> eligible;
  for (const auto& path : paths) {
    try {
      Instance instance = DatasetReader::read_json(path.string());
      CalibrationInstance candidate{path, std::move(instance)};
      if (candidate.instance.n > 0 && candidate.instance.m > 0 &&
          candidate.instance.n <= kMaximumCalibrationPoints &&
          candidate.instance.m <= kMaximumCalibrationStations) {
        eligible.push_back(std::move(candidate));
      }
    } catch (const std::exception& error) {
      LOG_WARN("ExactReferenceSelector: excluding invalid calibration input {}: {}",
               path.string(), error.what());
    }
  }
  std::sort(eligible.begin(), eligible.end(),
            [](const CalibrationInstance& lhs,
               const CalibrationInstance& rhs) {
              return std::tie(lhs.instance.n, lhs.instance.m, lhs.path) <
                     std::tie(rhs.instance.n, rhs.instance.m, rhs.path);
            });
  if (eligible.size() > kMaximumCalibrationInstances) {
    eligible.resize(kMaximumCalibrationInstances);
  }
  return eligible;
}

bool objective_is_proven_optimal(OptimalityStatus status, bool exact_solver,
                                 bool feasible, bool time_limited,
                                 BoundStatus bound_status, double lower_bound,
                                 double upper_bound) {
  if (!exact_solver || !feasible || time_limited ||
      bound_status != BoundStatus::CERTIFIED ||
      !std::isfinite(lower_bound) || !std::isfinite(upper_bound)) {
    return false;
  }
  if (status == OptimalityStatus::OPTIMAL) {
    return true;
  }
  // The certified zero lower bound proves optimality when a feasible exact
  // result also has zero objective, even if a legacy solver status is weaker.
  return lower_bound >= -1e-9 && upper_bound <= 1e-9;
}

Json run_calibration(const CalibrationInstance& item,
                     const std::string& backend, ObjectiveType objective,
                     ILPSolver& ilp) {
  Json run{{"instance", absolute_path(item.path)},
           {"backend", backend},
           {"objective", to_string(objective)},
           {"n", item.instance.n},
           {"m", item.instance.m}};
  const auto started = Clock::now();
  try {
    auto static_solver = StaticSolverRegistry::create(backend, &ilp);
    if (!static_solver || !static_solver->is_exact()) {
      run["accepted"] = false;
      run["rejection_reason"] = "backend is unavailable or not exact";
      return run;
    }
    bool feasible = false;
    bool time_limited = false;
    double lower_bound = 0.0;
    double upper_bound = std::numeric_limits<double>::infinity();
    double runtime = 0.0;
    OptimalityStatus status = OptimalityStatus::FAILED;
    BoundStatus bound_status = BoundStatus::NONE;
    KineticSolution solution;
    if (objective == ObjectiveType::MIN_MAX) {
      MinMaxSolver::Config config;
      config.time_limit_per_ip = kCalibrationTimeLimitSec;
      config.global_time_limit_sec = 2.0 * kCalibrationTimeLimitSec;
      config.gap_target = 0.0;
      config.max_iterations = 32;
      config.verify_after = true;
      const auto result =
          MinMaxSolver::solve(item.instance, *static_solver, config);
      feasible = result.feasible;
      time_limited = result.time_limited;
      lower_bound = result.lower_bound;
      upper_bound = result.upper_bound;
      status = result.optimality_status;
      bound_status = result.bound_status;
      runtime = result.total_time_sec;
      solution = result.solution;
    } else {
      MinSumSolver::Config config;
      config.time_limit_per_ip = kCalibrationTimeLimitSec;
      config.global_time_limit_sec = 2.0 * kCalibrationTimeLimitSec;
      config.gap_target = 0.0;
      config.max_iterations = 32;
      config.verify_after = true;
      const auto result =
          MinSumSolver::solve(item.instance, *static_solver, config);
      feasible = result.feasible;
      time_limited = result.time_limited;
      lower_bound = result.lower_bound;
      upper_bound = result.upper_bound;
      status = result.optimality_status;
      bound_status = result.bound_status;
      runtime = result.total_time_sec;
      solution = result.solution;
    }

    const VerificationReport verification =
        feasible ? Verifier::verify_continuous(item.instance, solution)
                 : VerificationReport{};
    const bool proven = objective_is_proven_optimal(
        status, static_solver->is_exact(), feasible, time_limited, bound_status,
        lower_bound, upper_bound);
    run["runtime_sec"] =
        std::chrono::duration<double>(Clock::now() - started).count();
    run["solver_runtime_sec"] = runtime;
    run["feasible"] = feasible;
    run["time_limited"] = time_limited;
    run["optimality_status"] = optimality_status_to_string(status);
    run["optimality_proven"] = proven;
    run["bound_status"] = bound_status_to_string(bound_status);
    run["lower_bound"] = lower_bound;
    run["upper_bound"] =
        std::isfinite(upper_bound) ? Json(upper_bound) : Json(nullptr);
    run["verification_kind"] =
        verification_kind_to_string(verification.kind);
    run["continuously_verified"] =
        verification.certified_continuous_verification &&
        verification.all_ok();
    const bool accepted =
        feasible && !time_limited && proven &&
        verification.certified_continuous_verification &&
        verification.all_ok() && std::isfinite(runtime);
    run["accepted"] = accepted;
    if (accepted) {
      return run;
    } else {
      run["rejection_reason"] =
          !feasible
              ? "no feasible kinetic solution"
              : (time_limited
                     ? "calibration run hit a time limit"
                     : (!verification.all_ok()
                            ? "continuous verification failed"
                            : "optimality was not proven"));
    }
    return run;
  } catch (const std::exception& error) {
    run["runtime_sec"] =
        std::chrono::duration<double>(Clock::now() - started).count();
    run["accepted"] = false;
    run["rejection_reason"] = error.what();
    return run;
  }
}

double median(std::vector<double> values) {
  if (values.empty()) {
    return -1.0;
  }
  std::sort(values.begin(), values.end());
  const std::size_t middle = values.size() / 2U;
  return values.size() % 2U == 0U
             ? (values[middle - 1U] + values[middle]) / 2.0
             : values[middle];
}

void write_manifest(const Json& manifest, const std::filesystem::path& path) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream output(path);
  if (!output) {
    throw std::runtime_error("cannot write experiment manifest: " +
                             path.string());
  }
  output << std::setw(2) << manifest << '\n';
  if (!output) {
    throw std::runtime_error("failed writing experiment manifest: " +
                             path.string());
  }
}

Json decision_manifest(const ExactReferenceDecision& decision,
                       const std::filesystem::path& dataset_dir,
                       const std::filesystem::path& output_dir,
                       const std::string& fingerprint,
                       bool kont_runtime_available, const Json& successful,
                       const Json& rejected, const Json& statistics) {
  Json manifest{{"calibration_decision_version", kManifestVersion},
                {"requested_backend", decision.requested_backend},
                {"actual_backend", decision.actual_backend},
                {"selected_backend", decision.selected_backend},
                {"selection_rule", decision.selection_rule},
                {"dataset_dir", absolute_path(dataset_dir)},
                {"output_dir", absolute_path(output_dir)},
                {"kont_runtime_available", kont_runtime_available},
                {"calibration_instances", decision.calibration_instances},
                {"successful_runs", successful},
                {"rejected_runs", rejected},
                {"runtime_statistics", statistics}};
  if (!fingerprint.empty()) {
    manifest["dataset_fingerprint"] = fingerprint;
    manifest["dataset_fingerprint_algorithm"] = kFingerprintAlgorithm;
    manifest["calibration_objectives"] = Json::array({"minmax", "minsum"});
  }
  return manifest;
}
}  // namespace

ExactReferenceDecision ExactReferenceSelector::resolve(
    const std::string& requested_backend,
    const std::filesystem::path& dataset_dir,
    const std::filesystem::path& output_dir) {
  ExactReferenceDecision decision;
  const std::string normalized = normalize_backend(requested_backend);
  if (normalized != "auto" && !is_backend(normalized)) {
    decision.requested_backend = normalized;
    decision.valid = false;
    return decision;
  }
  decision.requested_backend = normalized;
  const std::filesystem::path manifest_path =
      output_dir / "experiment_manifest.json";
  decision.manifest_path = manifest_path.string();
  const std::string dataset_absolute = absolute_path(dataset_dir);

  std::string fingerprint;
  if (normalized == "auto") {
    fingerprint = dataset_fingerprint(dataset_dir);
    const Json existing = load_manifest(manifest_path);
    if (reusable_calibration(existing, dataset_absolute, fingerprint,
                             decision)) {
      LOG_INFO("ExactReferenceSelector: reusing manifest decision '{}'",
               decision.selected_backend);
      return decision;
    }
  }

  const bool native_kont_available =
      normalized != "branch-and-bound" &&
      KontSolver::probe_native_backend();
  if (normalized != "auto") {
    decision.selected_backend =
        normalized == "ip-kont" && !native_kont_available
            ? "branch-and-bound"
            : normalized;
    decision.uses_kont = decision.selected_backend == "ip-kont";
    decision.actual_backend =
        decision.selected_backend == "ip-kont"
            ? "KONT/COPT"
            : (normalized == "ip-kont"
                   ? "built-in-branch-and-bound-fallback"
                   : "branch-and-bound");
    decision.selection_rule =
        normalized == "ip-kont" && !native_kont_available
            ? "explicit ip-kont requested; runtime probe unavailable, using "
              "built-in branch-and-bound fallback"
            : "explicit user selection";
    decision.valid = true;
    const Json empty = Json::array();
    const Json statistics{{"ip-kont_median_sec", -1.0},
                          {"branch-and-bound_median_sec", -1.0},
                          {"by_objective",
                           Json{{"minmax", Json{{"ip-kont_median_sec", -1.0},
                                               {"branch-and-bound_median_sec",
                                                -1.0}}},
                                {"minsum", Json{{"ip-kont_median_sec", -1.0},
                                               {"branch-and-bound_median_sec",
                                                -1.0}}}}}};
    write_manifest(decision_manifest(decision, dataset_dir, output_dir,
                                     {}, native_kont_available, empty, empty,
                                     statistics),
                   manifest_path);
    return decision;
  }

  Json successful = Json::array();
  Json rejected = Json::array();
  const auto calibration_instances =
      select_calibration_instances(dataset_dir);
  if (calibration_instances.empty()) {
    for (const auto backend : {"ip-kont", "branch-and-bound"}) {
      for (const auto objective : {ObjectiveType::MIN_MAX,
                                   ObjectiveType::MIN_SUM}) {
        rejected.push_back(
            Json{{"instance", nullptr},
                 {"backend", backend},
                 {"objective", to_string(objective)},
                 {"accepted", false},
                 {"rejection_reason",
                  "dataset contains no valid calibration instance within "
                  "the configured size cap"}});
      }
    }
  }
  for (const auto& item : calibration_instances) {
    decision.calibration_instances.push_back(absolute_path(item.path));
    if (!native_kont_available) {
      for (const auto objective : {ObjectiveType::MIN_MAX,
                                   ObjectiveType::MIN_SUM}) {
        rejected.push_back(
            Json{{"instance", absolute_path(item.path)},
                 {"backend", "ip-kont"},
                 {"objective", to_string(objective)},
                 {"n", item.instance.n},
                 {"m", item.instance.m},
                 {"accepted", false},
                 {"rejection_reason",
                  "native KONT/COPT failed the runtime capability probe"}});
      }
    } else {
      KontSolver kont;
      for (const auto objective : {ObjectiveType::MIN_MAX,
                                   ObjectiveType::MIN_SUM}) {
        const Json run =
            run_calibration(item, "ip-kont", objective, kont);
        (run.at("accepted").get<bool>() ? successful : rejected).push_back(run);
      }
    }

    KontSolver kont;
    for (const auto objective : {ObjectiveType::MIN_MAX,
                                 ObjectiveType::MIN_SUM}) {
      const Json run = run_calibration(item, "branch-and-bound", objective,
                                       kont);
      (run.at("accepted").get<bool>() ? successful : rejected).push_back(run);
    }
  }

  std::map<std::pair<std::string, std::string>, std::vector<double>>
      paired_runs;
  for (const auto& run : successful) {
    const std::string backend = run.at("backend").get<std::string>();
    const auto key = std::make_pair(run.at("instance").get<std::string>(),
                                    run.at("objective").get<std::string>());
    auto [iterator, inserted] =
        paired_runs.emplace(key, std::vector<double>(2U, -1.0));
    (void)inserted;
    auto& pair = iterator->second;
    const double runtime = run.at("solver_runtime_sec").get<double>();
    if (backend == "ip-kont") {
      pair[0] = runtime;
    } else {
      pair[1] = runtime;
    }
  }
  std::vector<double> paired_kont_runtimes;
  std::vector<double> paired_branch_runtimes;
  std::map<std::string, std::vector<double>> paired_kont_by_objective;
  std::map<std::string, std::vector<double>> paired_branch_by_objective;
  for (const auto& [key, runtimes] : paired_runs) {
    if (runtimes[0] < 0.0 || runtimes[1] < 0.0) {
      continue;
    }
    paired_kont_runtimes.push_back(runtimes[0]);
    paired_branch_runtimes.push_back(runtimes[1]);
    paired_kont_by_objective[key.second].push_back(runtimes[0]);
    paired_branch_by_objective[key.second].push_back(runtimes[1]);
  }
  decision.ip_kont_median_runtime_sec = median(paired_kont_runtimes);
  decision.branch_and_bound_median_runtime_sec =
      median(paired_branch_runtimes);
  if (native_kont_available && decision.ip_kont_median_runtime_sec >= 0.0 &&
      decision.branch_and_bound_median_runtime_sec >= 0.0) {
    decision.selected_backend =
        decision.ip_kont_median_runtime_sec <
                decision.branch_and_bound_median_runtime_sec
            ? "ip-kont"
            : "branch-and-bound";
    decision.selection_rule =
        "choose backend with lower median solver runtime over accepted, "
        "same-instance, same-objective continuously verified optimal runs; "
        "ties select branch-and-bound";
  } else {
    decision.selected_backend = "branch-and-bound";
    decision.selection_rule =
        "select branch-and-bound unless both backends have accepted, "
        "continuously verified, proven-optimal calibration runs";
  }
  decision.uses_kont = decision.selected_backend == "ip-kont";
  decision.actual_backend =
      !native_kont_available ? "built-in-branch-and-bound-fallback"
                             : decision.selected_backend;
  decision.valid = true;

  Json by_objective = Json::object();
  for (const auto objective : {ObjectiveType::MIN_MAX, ObjectiveType::MIN_SUM}) {
    const std::string name = to_string(objective);
    by_objective[to_string(objective)] =
        Json{{"ip-kont_median_sec", median(paired_kont_by_objective[name])},
             {"branch-and-bound_median_sec",
              median(paired_branch_by_objective[name])},
             {"paired_run_count", paired_kont_by_objective[name].size()}};
  }
  const Json statistics{
      {"ip-kont_median_sec", decision.ip_kont_median_runtime_sec},
      {"branch-and-bound_median_sec",
       decision.branch_and_bound_median_runtime_sec},
      {"paired_run_count", paired_kont_runtimes.size()},
      {"by_objective", std::move(by_objective)},
      {"runtime_metric",
       "median solver total_time_sec over paired accepted runs for the same "
       "instance and objective"}};
  decision.successful_runs.clear();
  decision.rejected_runs.clear();
  for (const auto& run : successful) {
    decision.successful_runs.push_back(run.dump());
  }
  for (const auto& run : rejected) {
    decision.rejected_runs.push_back(run.dump());
  }
  write_manifest(decision_manifest(decision, dataset_dir, output_dir,
                                   fingerprint, native_kont_available,
                                   successful, rejected, statistics),
                 manifest_path);
  LOG_INFO("ExactReferenceSelector: requested=auto selected={} actual={} "
           "KONT/COPT available={}",
           decision.selected_backend, decision.actual_backend,
           native_kont_available);
  return decision;
}

std::string ExactReferenceSelector::default_name(
    const std::string& requested_backend,
    const std::filesystem::path& dataset_dir,
    const std::filesystem::path& output_dir) {
  const auto decision =
      ExactReferenceSelector::resolve(requested_backend, dataset_dir,
                                      output_dir);
  return decision.selected_backend;
}
}  // namespace kdc
