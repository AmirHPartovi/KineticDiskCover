#include "kdc/solution.hpp"

#include "kdc/stationary.hpp"
#include "kdc/verify.hpp"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <limits>
#include <random>
#include <stdexcept>
#include <string>
#include <thread>
#include <tuple>
#include <vector>

#ifndef KDC_BENCH_GIT_COMMIT
#define KDC_BENCH_GIT_COMMIT "unknown"
#endif
#ifndef KDC_BENCH_GIT_DIRTY
#define KDC_BENCH_GIT_DIRTY "unknown"
#endif
#ifndef KDC_BENCH_BUILD_TYPE
#define KDC_BENCH_BUILD_TYPE "unknown"
#endif

namespace {
using Clock = std::chrono::steady_clock;

struct Run {
  kdc::KineticSolution solution;
  kdc::KineticEventDiagnostics diagnostics;
  std::uint64_t extension_ns{0};
  std::uint64_t verification_ns{0};
  bool verified{false};
};

kdc::Instance make_instance(std::uint32_t seed, int point_count,
                            int station_count, int segment_count,
                            double spread) {
  std::mt19937 generator(seed);
  std::uniform_real_distribution<double> displacement(-spread, spread);

  kdc::Instance instance;
  instance.id = static_cast<int>(seed);
  instance.name = "handover-bench-" + std::to_string(seed);
  instance.n = point_count;
  instance.m = station_count;
  instance.T_end = 1.0;
  for (int station = 0; station < station_count; ++station) {
    instance.stations.push_back(
        {station, {20.0 * static_cast<double>(station), 0.0}});
  }

  std::vector<double> breaks;
  for (int segment = 0; segment <= segment_count; ++segment) {
    breaks.push_back(static_cast<double>(segment) / segment_count);
  }
  for (int point = 0; point < point_count; ++point) {
    const int group = point % station_count;
    const double center = 20.0 * static_cast<double>(group);
    std::vector<kdc::Point> waypoints;
    waypoints.reserve(breaks.size());
    for (std::size_t segment = 0; segment < breaks.size(); ++segment) {
      waypoints.push_back(
          {center + displacement(generator), displacement(generator)});
    }
    instance.trajectories.emplace_back(breaks, std::move(waypoints));
  }
  return instance;
}

kdc::Instance make_handover_heavy_instance(std::uint32_t seed) {
  constexpr int point_count = 20;
  constexpr int station_count = 2;
  const std::vector<double> breaks{0.0, 0.2, 0.45, 0.7, 1.0};
  kdc::Instance instance;
  instance.id = static_cast<int>(seed);
  instance.name = "handover-heavy-" + std::to_string(seed);
  instance.n = point_count;
  instance.m = station_count;
  instance.T_end = 1.0;
  instance.stations = {{0, {0.0, 0.0}}, {1, {10.0, 0.0}}};

  const std::vector<std::vector<kdc::Point>> source_paths{
      {{12.0, 0.0}, {10.5, 0.0}, {9.0, 0.0}, {9.0, 0.0}, {9.0, 0.0}},
      {{9.5, 0.0}, {9.5, 0.0}, {10.5, 0.0}, {9.0, 0.0}, {9.0, 0.0}},
      {{8.5, 0.0}, {8.5, 0.0}, {8.5, 0.0}, {10.5, 0.0}, {9.0, 0.0}},
      {{7.5, 0.0}, {7.5, 0.0}, {7.5, 0.0}, {7.5, 0.0}, {10.5, 0.0}}};
  for (const auto& path : source_paths) {
    instance.trajectories.emplace_back(breaks, path);
  }
  for (int point = 4; point < 10; ++point) {
    instance.trajectories.emplace_back(
        std::vector<double>{0.0, 1.0},
        std::vector<kdc::Point>{{1.0 + 0.1 * (point - 4), 0.0},
                                {1.0 + 0.1 * (point - 4), 0.0}});
  }
  instance.trajectories.emplace_back(
      std::vector<double>{0.0, 1.0},
      std::vector<kdc::Point>{{10.5, 0.0}, {10.5, 0.0}});
  for (int point = 11; point < point_count; ++point) {
    const double x = 10.1 + 0.05 * (point - 11);
    instance.trajectories.emplace_back(
        std::vector<double>{0.0, 1.0},
        std::vector<kdc::Point>{{x, 0.0}, {x, 0.0}});
  }
  return instance;
}

Run run(const kdc::Instance& instance, const kdc::StaticAssignment& assignment,
        kdc::HandoverEvaluation evaluation) {
  Run output;
  const auto start = Clock::now();
  output.solution = kdc::KineticSolution::extend(
      instance, assignment, 0.0, instance.T_end, true, true,
      kdc::ObjectiveType::MIN_SUM, nullptr,
      kdc::KineticEventEngine::REFERENCE_EXHAUSTIVE, &output.diagnostics,
      kdc::KineticIntervalEmission::EXACT_RELEVANT_BOUNDARIES, evaluation);
  output.extension_ns = static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - start)
          .count());

  const auto verification_start = Clock::now();
  output.verified =
      kdc::Verifier::verify_continuous(instance, output.solution).all_ok();
  output.verification_ns = static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::nanoseconds>(
          Clock::now() - verification_start)
          .count());
  return output;
}

bool same_solution(const kdc::KineticSolution& first,
                   const kdc::KineticSolution& second) {
  constexpr double tolerance = 1e-9;
  if (first.intervals.size() != second.intervals.size()) {
    return false;
  }
  for (std::size_t index = 0; index < first.intervals.size(); ++index) {
    const auto& lhs = first.intervals[index];
    const auto& rhs = second.intervals[index];
    if (std::abs(lhs.t_start - rhs.t_start) > tolerance ||
        std::abs(lhs.t_end - rhs.t_end) > tolerance ||
        lhs.supporting_point != rhs.supporting_point ||
        lhs.assigned_points != rhs.assigned_points ||
        std::abs(lhs.a - rhs.a) > tolerance ||
        std::abs(lhs.b - rhs.b) > tolerance ||
        std::abs(lhs.c - rhs.c) > tolerance) {
      return false;
    }
  }
  return std::abs(first.peak_cost() - second.peak_cost()) <= tolerance &&
         std::abs(first.peak_time() - second.peak_time()) <= tolerance &&
         std::abs(first.total_integral() - second.total_integral()) <=
             tolerance;
}

bool same_trace(const std::vector<kdc::KineticEventTraceEntry>& first,
                const std::vector<kdc::KineticEventTraceEntry>& second) {
  if (first.size() != second.size()) {
    return false;
  }
  for (std::size_t index = 0; index < first.size(); ++index) {
    const auto& lhs = first[index];
    const auto& rhs = second[index];
    if (!kdc::event_times_simultaneous(lhs.time, rhs.time) ||
        lhs.type != rhs.type || lhs.station_id != rhs.station_id ||
        lhs.old_support != rhs.old_support ||
        lhs.new_support != rhs.new_support ||
        lhs.from_station != rhs.from_station ||
        lhs.to_station != rhs.to_station ||
        lhs.affected_point != rhs.affected_point ||
        lhs.tie_breaking_outcome != rhs.tie_breaking_outcome ||
        lhs.from_station_support_after != rhs.from_station_support_after ||
        lhs.to_station_support_after != rhs.to_station_support_after) {
      return false;
    }
  }
  return true;
}

double ratio(std::uint64_t numerator, std::uint64_t denominator) {
  if (denominator == 0U) {
    return std::numeric_limits<double>::quiet_NaN();
  }
  return static_cast<double>(numerator) / static_cast<double>(denominator);
}

void print_header() {
  std::cout
      << "git_commit,git_dirty,build_type,platform,logical_cpus,seed,"
         "family,instance_id,n,m,T,total_trajectory_segments,repetitions,"
         "solver_config,tolerance_config,reference_runtime_ns,"
         "tournament_runtime_ns,geometric_engine_runtime_ns,"
         "local_handover_runtime_ns,reference_support_comparisons,"
         "optimized_support_comparisons,reference_certificate_count,"
         "optimized_certificate_count,certificate_queue_operations,"
         "reference_handover_checks,optimized_handover_checks,"
         "local_support_points_inspected,local_acceptances,"
         "global_fallbacks,reference_intervals,optimized_intervals,"
         "reference_event_detection_ns,optimized_event_detection_ns,"
         "reference_handover_ns,optimized_handover_ns,"
         "reference_interval_construction_ns,"
         "optimized_interval_construction_ns,reference_verification_ns,"
         "optimized_verification_ns,reference_peak,optimized_peak,"
         "reference_integral,optimized_integral,speedup,"
         "event_detection_speedup,handover_speedup,"
         "interval_construction_speedup,verification_overhead_ratio,"
         "correctness_status\n";
}

void print_row(const kdc::Instance& instance, std::uint32_t seed,
               const std::string& family, int repetitions,
               const Run& reference, const Run& optimized, bool correct) {
#if defined(__APPLE__)
  constexpr const char* platform = "macOS";
#elif defined(__linux__)
  constexpr const char* platform = "Linux";
#elif defined(_WIN32)
  constexpr const char* platform = "Windows";
#else
  constexpr const char* platform = "unknown";
#endif
  std::uint64_t segments = 0;
  for (const auto& trajectory : instance.trajectories) {
    segments += static_cast<std::uint64_t>(trajectory.t_breaks.size() - 1U);
  }
  std::cout << KDC_BENCH_GIT_COMMIT << ',' << KDC_BENCH_GIT_DIRTY << ','
            << KDC_BENCH_BUILD_TYPE << ',' << platform << ','
            << std::thread::hardware_concurrency() << ',' << seed << ','
            << family << ',' << instance.name << ',' << instance.n << ','
            << instance.m << ',' << instance.T_end << ',' << segments << ','
            << repetitions << ','
            << "MIN_SUM;handovers=on;interval_emission=exact"
            << ",abs_time=1e-9;relative_time=64eps;root_solver=KineticCore,"
            << reference.extension_ns << ",NA,NA," << optimized.extension_ns
            << ',' << reference.diagnostics.point_vs_support_comparisons << ','
            << optimized.diagnostics.point_vs_support_comparisons << ','
            << reference.diagnostics.quadratic_equations_solved << ','
            << optimized.diagnostics.quadratic_equations_solved
            << ",NA," << reference.diagnostics.handover_event_checks << ','
            << optimized.diagnostics.handover_event_checks << ','
            << optimized.diagnostics.handover_local_support_points_inspected
            << ',' << optimized.diagnostics.handover_local_acceptances << ','
            << optimized.diagnostics.handover_global_fallbacks << ','
            << reference.solution.intervals.size() << ','
            << optimized.solution.intervals.size() << ','
            << reference.diagnostics.event_detection_nanoseconds << ','
            << optimized.diagnostics.event_detection_nanoseconds << ','
            << reference.diagnostics.handover_detection_nanoseconds << ','
            << optimized.diagnostics.handover_detection_nanoseconds << ','
            << reference.diagnostics.interval_construction_nanoseconds << ','
            << optimized.diagnostics.interval_construction_nanoseconds << ','
            << reference.verification_ns << ',' << optimized.verification_ns
            << ',' << std::setprecision(17) << reference.solution.peak_cost()
            << ',' << optimized.solution.peak_cost() << ','
            << reference.solution.total_integral() << ','
            << optimized.solution.total_integral() << ','
            << ratio(reference.extension_ns, optimized.extension_ns) << ','
            << ratio(reference.diagnostics.event_detection_nanoseconds,
                     optimized.diagnostics.event_detection_nanoseconds)
            << ','
            << ratio(reference.diagnostics.handover_detection_nanoseconds,
                     optimized.diagnostics.handover_detection_nanoseconds)
            << ','
            << ratio(reference.diagnostics.interval_construction_nanoseconds,
                     optimized.diagnostics.interval_construction_nanoseconds)
            << ',' << ratio(optimized.verification_ns,
                           reference.verification_ns)
            << ',' << ((correct && reference.verified && optimized.verified)
                           ? "PASS"
                           : "FAIL")
            << '\n';
}
}  // namespace

int main(int argc, char** argv) {
  try {
    spdlog::set_level(spdlog::level::off);
    int instance_count = 10;
    int repetitions = 3;
    if (argc > 1) {
      instance_count = std::stoi(argv[1]);
    }
    if (argc > 2) {
      repetitions = std::stoi(argv[2]);
    }
    if (instance_count <= 0 || repetitions <= 0 || instance_count > 10000 ||
        repetitions > 1000) {
      throw std::invalid_argument("counts must be positive and within limits");
    }

    print_header();
    const std::vector<std::tuple<int, int, int, double, const char*>> families{
        {8, 2, 2, 1.0, "small"},
        {60, 6, 8, 7.0, "general"},
        {120, 2, 6, 7.0, "large_n_small_m"},
        {36, 12, 4, 7.0, "moderate_n_large_m"},
        {20, 2, 4, 0.0, "handover_heavy"}};
    for (int index = 0; index < instance_count; ++index) {
      const auto seed = static_cast<std::uint32_t>(20261006 + index);
      const auto& [point_count, station_count, segment_count, spread, family] =
          families[static_cast<std::size_t>(index) % families.size()];
      const bool handover_heavy =
          std::string(family) == "handover_heavy";
      const auto instance =
          handover_heavy
              ? make_handover_heavy_instance(seed)
              : make_instance(seed, point_count, station_count, segment_count,
                              spread);
      kdc::StaticAssignment assignment;
      if (handover_heavy) {
        std::vector<int> owners(static_cast<std::size_t>(point_count), 0);
        std::fill(owners.begin() + 10, owners.end(), 1);
        assignment = {{0, 10}, {12.0, 0.5},
                      std::acos(-1.0) * (12.0 * 12.0 + 0.5 * 0.5), true,
                      std::move(owners)};
      } else {
        assignment = kdc::StationarySolver::solve_nn(instance, 0.0);
      }
      if (!assignment.feasible) {
        throw std::runtime_error("benchmark assignment is infeasible");
      }
      std::vector<Run> reference_runs;
      std::vector<Run> optimized_runs;
      bool correct = true;
      for (int repeat = 0; repeat < repetitions; ++repeat) {
        const auto current_reference =
            run(instance, assignment, kdc::HandoverEvaluation::REFERENCE_GLOBAL);
        const auto current_optimized =
            run(instance, assignment, kdc::HandoverEvaluation::LOCAL_EXACT);
        correct = correct && current_reference.verified &&
                  current_optimized.verified &&
                  same_solution(current_reference.solution,
                                current_optimized.solution) &&
                  same_trace(current_reference.diagnostics.trace,
                             current_optimized.diagnostics.trace);
        reference_runs.push_back(current_reference);
        optimized_runs.push_back(current_optimized);
      }
      const auto median_run = [](std::vector<Run>& runs) -> const Run& {
        std::sort(runs.begin(), runs.end(),
                  [](const Run& first, const Run& second) {
                    return first.extension_ns < second.extension_ns;
                  });
        return runs[runs.size() / 2U];
      };
      const auto& reference = median_run(reference_runs);
      const auto& optimized = median_run(optimized_runs);
      print_row(instance, seed, family, repetitions, reference, optimized,
                correct);
      if (!correct) {
        return 1;
      }
    }
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "kinetic handover benchmark failed: " << error.what() << '\n';
    return 1;
  }
}
