#include "kdc/benchmark_protocol.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <thread>

#ifndef KDC_GIT_COMMIT
#define KDC_GIT_COMMIT "unknown"
#endif
#ifndef KDC_GIT_DIRTY
#define KDC_GIT_DIRTY "unknown"
#endif
#ifndef KDC_BUILD_TYPE
#define KDC_BUILD_TYPE "unknown"
#endif
#ifndef KDC_COMPILER_FLAGS
#define KDC_COMPILER_FLAGS "unknown"
#endif

namespace kdc {
namespace {
std::string timestamp_now() {
  const auto now = std::chrono::system_clock::now();
  const auto value = std::chrono::system_clock::to_time_t(now);
  std::tm utc{};
#if defined(_WIN32)
  gmtime_s(&utc, &value);
#else
  gmtime_r(&value, &utc);
#endif
  std::ostringstream output;
  output << std::put_time(&utc, "%Y-%m-%dT%H:%M:%SZ");
  return output.str();
}

std::string dataset_fingerprint(const std::string& dataset_dir) {
  namespace fs = std::filesystem;
  const fs::path root(dataset_dir);
  std::vector<fs::path> files;
  if (fs::is_directory(root)) {
    for (const auto& entry : fs::recursive_directory_iterator(root)) {
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
    const std::string relative = fs::relative(file, root).generic_string();
    append(relative.data(), relative.size());
    append("\0", 1U);
    std::ifstream input(file, std::ios::binary);
    if (!input) {
      throw std::runtime_error("cannot read dataset file: " + file.string());
    }
    char buffer[4096];
    while (input) {
      input.read(buffer, sizeof(buffer));
      append(buffer, static_cast<std::size_t>(input.gcount()));
    }
    if (!input.eof()) {
      throw std::runtime_error("failed reading dataset file: " +
                               file.string());
    }
  }
  std::ostringstream output;
  output << std::hex << std::setfill('0') << std::setw(16) << hash;
  return output.str();
}
}  // namespace

std::string benchmark_profile_to_string(BenchmarkProfile profile) {
  switch (profile) {
    case BenchmarkProfile::FAST:
      return "FAST";
    case BenchmarkProfile::EXACT_REFERENCE:
      return "EXACT_REFERENCE";
    case BenchmarkProfile::DEBUG:
      return "DEBUG";
  }
  throw std::invalid_argument("unknown benchmark profile");
}

BenchmarkProfile benchmark_profile_from_string(const std::string& value) {
  if (value == "fast" || value == "FAST") {
    return BenchmarkProfile::FAST;
  }
  if (value == "exact-reference" || value == "EXACT_REFERENCE") {
    return BenchmarkProfile::EXACT_REFERENCE;
  }
  if (value == "debug" || value == "DEBUG") {
    return BenchmarkProfile::DEBUG;
  }
  throw std::invalid_argument("unknown benchmark profile: " + value);
}

std::vector<std::string> fast_benchmark_algorithms() {
  return {"nn",          "greedy",   "primal-dual", "local-search",
          "sa",          "genetic",  "lp-rounding",  "shifting"};
}

std::vector<std::string> comparison_benchmark_algorithms(
    const std::string& exact_backend) {
  auto algorithms = fast_benchmark_algorithms();
  algorithms.push_back(exact_backend);
  return algorithms;
}

nlohmann::json make_experiment_manifest(
    const std::string& experiment_id, const std::string& timestamp,
    const std::string& dataset_dir, const std::string& dataset_identity,
    BenchmarkProfile profile, const std::vector<std::string>& algorithms,
    const std::string& requested_backend, const std::string& actual_backend,
    const nlohmann::json& calibration, double fast_time_limit_sec,
    double exact_time_limit_sec, double per_static_time_limit_sec,
    unsigned seed, int repeats, const std::string& refinement_policy,
    bool verify_each_iteration, bool verify_after, bool handovers_enabled,
    const std::string& cache_policy, int thread_count) {
  nlohmann::json manifest = calibration.is_object()
                                ? calibration
                                : nlohmann::json::object();
  manifest["experiment_id"] = experiment_id;
  manifest["timestamp"] = timestamp.empty() ? timestamp_now() : timestamp;
  manifest["git_commit"] = KDC_GIT_COMMIT;
  manifest["source_tree_dirty"] =
      std::string(KDC_GIT_DIRTY) == "true";
  manifest["compiler"] = std::string(__VERSION__);
  manifest["compiler_id"] =
#if defined(__clang__)
      "Clang";
#elif defined(__GNUC__)
      "GNU";
#elif defined(_MSC_VER)
      "MSVC";
#else
      "unknown";
#endif
  manifest["build_type"] = KDC_BUILD_TYPE;
  manifest["compiler_flags"] = KDC_COMPILER_FLAGS;
  manifest["hardware"] = {
      {"architecture",
#if defined(__x86_64__) || defined(_M_X64)
       "x86_64"
#elif defined(__aarch64__) || defined(_M_ARM64)
       "aarch64"
#elif defined(__arm__) || defined(_M_ARM)
       "arm"
#else
       "unknown"
#endif
      },
      {"logical_cpu_count", std::thread::hardware_concurrency()},
      {"thread_count", thread_count}};
  manifest["dataset"] = {
      {"path", std::filesystem::absolute(dataset_dir).lexically_normal().string()},
      {"identity", dataset_identity.empty()
                       ? dataset_fingerprint(dataset_dir)
                       : dataset_identity},
      {"fingerprint_algorithm",
       "FNV-1a-64 over sorted relative JSON paths and raw bytes"}};
  manifest["algorithm_set"] = algorithms;
  manifest["benchmark_profile"] = benchmark_profile_to_string(profile);
  manifest["requested_backend"] = requested_backend;
  manifest["actual_backend"] = actual_backend;
  manifest["selected_exact_reference"] =
      manifest.value("selected_backend", std::string{});
  manifest["time_limits"] = {
      {"fast_global_sec", fast_time_limit_sec},
      {"exact_global_sec", exact_time_limit_sec},
      {"per_static_solve_sec", per_static_time_limit_sec}};
  manifest["seed_policy"] = {
      {"base_seed", seed},
      {"description",
       "deterministic per-instance/objective/algorithm/repeat derivation from "
       "the base seed; exact reference is run once per instance/objective"},
      {"repeats", repeats}};
  manifest["refinement_policy"] = refinement_policy;
  manifest["verification_policy"] = {
      {"verify_after", verify_after},
      {"verify_each_iteration", verify_each_iteration},
      {"verification_type", "certified_continuous"}};
  manifest["cache_policy"] = cache_policy;
  manifest["handovers_enabled"] = handovers_enabled;
  manifest["tolerances"] = {
      {"verification_absolute", 1e-6},
      {"comparison_relative", 1e-6}};
  return manifest;
}

void write_experiment_manifest(const std::string& path,
                               const nlohmann::json& manifest) {
  const std::filesystem::path output_path(path);
  if (!output_path.parent_path().empty()) {
    std::filesystem::create_directories(output_path.parent_path());
  }
  std::ofstream output(output_path);
  if (!output) {
    throw std::runtime_error("cannot write experiment manifest: " + path);
  }
  output << std::setw(2) << manifest << '\n';
  if (!output) {
    throw std::runtime_error("failed writing experiment manifest: " + path);
  }
}
}  // namespace kdc
