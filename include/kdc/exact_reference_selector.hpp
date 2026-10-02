#pragma once

#include <filesystem>
#include <string>

namespace kdc {
struct ExactReferenceDecision {
  std::string requested_backend{};
  std::string actual_backend{};
  std::string manifest_path{};
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
