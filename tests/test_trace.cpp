#include "test_utils.hpp"

#include "kdc/algorithms/ip_static_solver.hpp"
#include "kdc/algorithms/nn_static_solver.hpp"
#include "kdc/minmax.hpp"
#include "kdc/mock_ilp_solver.hpp"
#include "kdc/trace.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

namespace {
std::filesystem::path temporary_path(const std::string& suffix) {
  const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
  return std::filesystem::temp_directory_path() /
         ("kdc-trace-" + std::to_string(stamp) + suffix);
}
}

TEST_CASE("TraceWriter: CSV round-trip preserves values") {
  std::vector<kdc::IterTrace> expected{
      {1, 0.5, 100.0, 90.0, 0.111, 1.2, 3},
      {2, 0.4, 95.0, 92.0, 0.0326, 2.5, 4}};
  expected[0].static_cost_at_peak_time = 91.0;
  expected[0].static_solver_status = "OPTIMAL";
  expected[0].static_solver_exact = true;
  expected[0].static_lower_bound = 90.0;
  expected[0].static_upper_bound = 91.0;
  expected[0].candidate_peak = 94.0;
  expected[0].combined_peak = 94.0;
  expected[0].peak_improvement = 6.0;
  expected[0].certified_gap = 0.044;
  expected[0].heuristic_gap = 0.05;
  expected[0].candidate_accepted = true;
  expected[0].has_certified_gap = true;
  expected[0].stop_reason = "candidate_accepted";
  const auto path = temporary_path(".csv");

  kdc::TraceWriter::write_csv(expected, path.string());
  const auto actual = kdc::TraceWriter::read_csv(path.string());

  REQUIRE(actual.size() == expected.size());
  for (std::size_t index = 0; index < expected.size(); ++index) {
    REQUIRE(actual[index].iter == expected[index].iter);
    REQUIRE(kdc::test::near(actual[index].t_max, expected[index].t_max,
                            1e-12));
    REQUIRE(kdc::test::near(actual[index].objective_value,
                            expected[index].objective_value, 1e-12));
    REQUIRE(kdc::test::near(actual[index].lower_bound,
                            expected[index].lower_bound, 1e-12));
    REQUIRE(kdc::test::near(actual[index].gap, expected[index].gap, 1e-12));
    REQUIRE(kdc::test::near(actual[index].wall_time_sec,
                            expected[index].wall_time_sec, 1e-12));
    REQUIRE(actual[index].num_ip_solves == expected[index].num_ip_solves);
    REQUIRE(kdc::test::near(actual[index].candidate_peak,
                            expected[index].candidate_peak, 1e-12));
    REQUIRE(actual[index].static_solver_status ==
            expected[index].static_solver_status);
    REQUIRE(actual[index].candidate_accepted ==
            expected[index].candidate_accepted);
    REQUIRE(actual[index].has_certified_gap ==
            expected[index].has_certified_gap);
    REQUIRE(actual[index].stop_reason == expected[index].stop_reason);
  }
  std::filesystem::remove(path);
}

TEST_CASE("TraceWriter reads the legacy seven-column format") {
  const auto path = temporary_path("-legacy.csv");
  {
    std::ofstream output(path);
    output << "iter,t_max,objective,lower_bound,gap,wall_time,num_ip_solves\n"
              "1,0.5,10,4,1.5,0.2,3\n";
  }
  const auto trace = kdc::TraceWriter::read_csv(path.string());
  REQUIRE(trace.size() == 1U);
  REQUIRE(trace.front().iter == 1);
  REQUIRE_FALSE(trace.front().has_certified_gap);
  REQUIRE(trace.front().stop_reason == "pending");
  std::filesystem::remove(path);
}

TEST_CASE("TraceWriter: rejects malformed CSV") {
  const auto path = temporary_path("-bad.csv");
  {
    std::ofstream output(path);
    output << "garbage\n1,2,x,y,z,a,b\n";
  }

  REQUIRE_THROWS(kdc::TraceWriter::read_csv(path.string()));
  std::filesystem::remove(path);
}

TEST_CASE("MinMax: trace rows and CSV are populated when enabled") {
  const auto instance = kdc::test::make_dummy_instance(20, 5, 7U);
  kdc::NNStaticSolver nn;
  kdc::MinMaxSolver::Config config;
  const auto path = temporary_path("-minmax.csv");
  config.trace_csv_path = path.string();

  const auto result = kdc::MinMaxSolver::solve(instance, nn, config);

  REQUIRE_FALSE(result.trace.empty());
  REQUIRE(result.trace.size() ==
          static_cast<std::size_t>(result.num_iterations));
  REQUIRE(std::filesystem::exists(path));
  const auto csv_trace = kdc::TraceWriter::read_csv(path.string());
  REQUIRE(csv_trace.size() == result.trace.size());
  REQUIRE(csv_trace.front().static_solver_status != "not_run");
  REQUIRE(csv_trace.front().stop_reason != "pending");
  REQUIRE(csv_trace.front().has_certified_gap);
  std::filesystem::remove(path);
}

TEST_CASE("MinMax: trace gap is non-increasing") {
  const auto instance = kdc::test::make_dummy_instance(30, 5, 11U);
  auto mock = std::make_unique<kdc::MockILPSolver>();
  kdc::IPStaticSolver ip(mock.get());
  kdc::MinMaxSolver::Config config;
  const auto path = temporary_path("-gap.csv");
  config.trace_csv_path = path.string();

  const auto result = kdc::MinMaxSolver::solve(instance, ip, config);

  for (std::size_t index = 1; index < result.trace.size(); ++index) {
    REQUIRE(result.trace[index].gap <= result.trace[index - 1U].gap + 1e-9);
  }
  std::filesystem::remove(path);
}

TEST_CASE("MinMax: trace objective is at least its lower bound") {
  const auto instance = kdc::test::make_dummy_instance(20, 5, 7U);
  kdc::NNStaticSolver nn;
  kdc::MinMaxSolver::Config config;
  const auto path = temporary_path("-bound.csv");
  config.trace_csv_path = path.string();

  const auto result = kdc::MinMaxSolver::solve(instance, nn, config);

  REQUIRE_FALSE(result.trace.empty());
  for (const auto& row : result.trace) {
    REQUIRE(row.objective_value >= row.lower_bound - 1e-9);
  }
  std::filesystem::remove(path);
}
