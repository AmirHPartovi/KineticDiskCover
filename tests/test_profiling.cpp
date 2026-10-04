#include "kdc/profiling.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

TEST_CASE("Optional phase profiling records requested phases") {
  const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
  const auto output_path =
      std::filesystem::temp_directory_path() /
      ("kdc-profile-" + std::to_string(stamp) + ".json");
  REQUIRE(setenv("KDC_PROFILE_PHASES", output_path.c_str(), 1) == 0);
  {
    kdc::ProfileSession session;
    REQUIRE(session.enabled());
    {
      KDC_PROFILE_PHASE(kdc::ProfilePhase::CANDIDATE_BUILD);
    }
    {
      KDC_PROFILE_PHASE(kdc::ProfilePhase::VERIFICATION);
    }
  }
  REQUIRE(unsetenv("KDC_PROFILE_PHASES") == 0);

  std::ifstream input(output_path);
  const auto report = nlohmann::json::parse(input);
  REQUIRE(report.at("unit") == "seconds");
  REQUIRE(report.at("phases").at("candidate_build").at("calls") == 1U);
  REQUIRE(report.at("phases").at("verification").at("calls") == 1U);
  REQUIRE(report.at("phases").at("serialization").at("calls") == 0U);
  for (const char* phase :
       {"trajectory_position", "distance_matrix", "coverage_matrix",
        "model_build", "lp_ilp_build", "lp_ilp_solve",
        "support_event_detection", "handover_detection", "kinetic_extension",
        "combination", "local_improvement"}) {
    REQUIRE(report.at("phases").contains(phase));
  }
  std::filesystem::remove(output_path);
}
