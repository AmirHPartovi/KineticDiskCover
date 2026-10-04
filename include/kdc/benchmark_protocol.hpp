#pragma once

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

namespace kdc {
enum class BenchmarkProfile { FAST, EXACT_REFERENCE, DEBUG };

std::string benchmark_profile_to_string(BenchmarkProfile profile);
BenchmarkProfile benchmark_profile_from_string(const std::string& value);
std::vector<std::string> fast_benchmark_algorithms();
std::vector<std::string> comparison_benchmark_algorithms(
    const std::string& exact_backend);
nlohmann::json make_experiment_manifest(
    const std::string& experiment_id, const std::string& timestamp,
    const std::string& dataset_dir, const std::string& dataset_identity,
    BenchmarkProfile profile, const std::vector<std::string>& algorithms,
    const std::string& requested_backend, const std::string& actual_backend,
    const nlohmann::json& calibration, double fast_time_limit_sec,
    double exact_time_limit_sec, double per_static_time_limit_sec,
    unsigned seed, int repeats, const std::string& refinement_policy,
    bool verify_each_iteration, bool verify_after, bool handovers_enabled,
    const std::string& cache_policy, int thread_count);
void write_experiment_manifest(const std::string& path,
                               const nlohmann::json& manifest);
}  // namespace kdc
