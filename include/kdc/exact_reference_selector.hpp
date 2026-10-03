#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace kdc {
struct ExactReferenceDecision {
  std::string requested_backend{};
  std::string actual_backend{};
  std::string manifest_path{};
  std::string selected_backend{};
  std::string selection_rule{};
  std::vector<std::string> calibration_instances;
  std::vector<std::string> successful_runs;
  std::vector<std::string> rejected_runs;
  double ip_kont_median_runtime_sec{-1.0};
  double branch_and_bound_median_runtime_sec{-1.0};
  bool uses_kont{false};
  bool valid{false};
};

class ExactReferenceSelector {
 public:
  static ExactReferenceDecision resolve(
      const std::string& requested_backend,
      const std::filesystem::path& dataset_dir = "data/instances",
      const std::filesystem::path& output_dir = "results/batch");

  static std::string default_name(const std::string& requested_backend = "auto",
                                  const std::filesystem::path& dataset_dir = "data/instances",
                                  const std::filesystem::path& output_dir = "results/batch");
};
}  // namespace kdc
