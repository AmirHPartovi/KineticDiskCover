#include "kdc/sanity_check.hpp"

#include "kdc/kont_solver.hpp"
#include "kdc/static_solver_registry.hpp"

#include <catch2/catch_test_macros.hpp>

#include <fstream>
#include <string>

namespace {
const kdc::SanityCheckReport& report() {
  static const kdc::SanityCheckReport value = kdc::SanityChecker::run(nullptr);
  return value;
}

const kdc::SanityCheckItem* find_item(const std::string& name) {
  for (const auto& item : report().items) {
    if (item.name == name) {
      return &item;
    }
  }
  return nullptr;
}
}  // namespace

TEST_CASE("SanityCheck: registry populated") {
  const auto* item = find_item("registry_populated");
  REQUIRE(item != nullptr);
  REQUIRE(item->passed);
}

TEST_CASE("SanityCheck: registered algorithms run or report optional backend unavailable") {
  for (const auto& name : kdc::StaticSolverRegistry::list()) {
    const auto* item = find_item("algorithm:" + name);
    REQUIRE(item != nullptr);
    if (name == "ip-kont" &&
        (!kdc::KontSolver::probe_native_backend())) {
      REQUIRE(item->passed);
      REQUIRE(item->detail.find("skipped") != std::string::npos);
      REQUIRE_FALSE(item->detail.empty());
    } else {
      REQUIRE(item->passed);
    }
  }
}

TEST_CASE("SanityCheck: min-max pipeline works with Mock") {
  const auto* item = find_item("minmax_pipeline");
  REQUIRE(item != nullptr);
  REQUIRE(item->passed);
}

TEST_CASE("SanityCheck: min-sum pipeline works with Mock") {
  const auto* item = find_item("minsum_pipeline");
  REQUIRE(item != nullptr);
  REQUIRE(item->passed);
}

TEST_CASE("SanityCheck: serializer roundtrip") {
  const auto* item = find_item("serializer_roundtrip");
  REQUIRE(item != nullptr);
  REQUIRE(item->passed);
}

TEST_CASE("SanityCheck: markdown report is written") {
  const std::string path = "/tmp/kdc_preflight_test.md";
  report().write_markdown(path);
  REQUIRE(std::ifstream(path).good());
  std::ifstream input(path);
  std::string line;
  std::getline(input, line);
  REQUIRE(line.find("Pre-flight") != std::string::npos);
}

TEST_CASE("SanityCheck: report aggregates correctly") {
  REQUIRE(report().num_passed + report().num_failed ==
          static_cast<int>(report().items.size()));
}
