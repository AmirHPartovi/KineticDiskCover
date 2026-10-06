#include "kdc/solution.hpp"

#include "kdc/stationary.hpp"
#include "kdc/verify.hpp"

#include <cstdint>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
kdc::Instance make_instance(std::uint32_t seed, int point_count,
                            int station_count) {
  std::mt19937 generator(seed);
  std::uniform_real_distribution<double> offset(-1.5, 1.5);
  constexpr int trajectory_segments = 16;
  std::vector<double> times;
  times.reserve(trajectory_segments + 1U);
  for (int index = 0; index <= trajectory_segments; ++index) {
    times.push_back(static_cast<double>(index) / trajectory_segments);
  }

  kdc::Instance instance;
  instance.id = static_cast<int>(seed);
  instance.name = "kinetic-interval-" + std::to_string(seed);
  instance.n = point_count;
  instance.m = station_count;
  instance.T_end = 1.0;
  for (int station = 0; station < station_count; ++station) {
    instance.stations.push_back(
        {station, {static_cast<double>(station) * 100.0, 0.0}});
  }
  int point = 0;
  for (int station = 0; station < station_count; ++station) {
    const double center = static_cast<double>(station) * 100.0;
    std::vector<kdc::Point> anchor_waypoints{
        {center + 10.0, 0.0}, {center + 10.0, 0.0}};
    instance.trajectories.emplace_back(
        std::vector<double>{0.0, 1.0}, std::move(anchor_waypoints));
    ++point;

    const int remaining_points = point_count - point;
    const int remaining_stations = station_count - station;
    const int group_size =
        (remaining_points + remaining_stations) / remaining_stations;
    for (int local = 1; local < group_size && point < point_count;
         ++local, ++point) {
      std::vector<kdc::Point> waypoints;
      waypoints.reserve(times.size());
      for (kdc::Index index = 0; index < times.size(); ++index) {
        waypoints.emplace_back(center + offset(generator), offset(generator));
      }
      instance.trajectories.emplace_back(times, std::move(waypoints));
    }
  }
  return instance;
}

void print_header() {
  std::cout
      << "instance,seed,n,m,raw_trajectory_breakpoints,"
         "before_interval_count,after_interval_count,"
         "before_extension_runtime_ns,after_extension_runtime_ns,"
         "before_event_detection_runtime_ns,"
         "after_event_detection_runtime_ns,"
         "before_interval_construction_runtime_ns,"
         "after_interval_construction_runtime_ns,"
         "interval_reduction_percent\n";
}

void print_record(const kdc::Instance& instance, std::uint32_t seed,
                  const kdc::KineticSolution& reference,
                  const kdc::KineticSolution& optimized,
                  const kdc::KineticEventDiagnostics& before,
                  const kdc::KineticEventDiagnostics& after) {
  const double reduction =
      reference.intervals.empty()
          ? 0.0
          : 100.0 *
                (static_cast<double>(reference.intervals.size()) -
                 static_cast<double>(optimized.intervals.size())) /
                static_cast<double>(reference.intervals.size());
  std::cout << instance.name << ',' << seed << ',' << instance.n << ','
            << instance.m << ',' << before.raw_trajectory_breakpoints << ','
            << reference.intervals.size() << ',' << optimized.intervals.size()
            << ',' << before.total_extension_nanoseconds << ','
            << after.total_extension_nanoseconds << ','
            << before.event_detection_nanoseconds << ','
            << after.event_detection_nanoseconds << ','
            << before.interval_construction_nanoseconds << ','
            << after.interval_construction_nanoseconds << ',' << reduction
            << '\n';
}
}  // namespace

int main(int argc, char** argv) {
  try {
    int instance_count = 10;
    if (argc > 1) {
      char* end = nullptr;
      const long parsed = std::strtol(argv[1], &end, 10);
      if (end == argv[1] || *end != '\0' || parsed <= 0 || parsed > 10000) {
        throw std::invalid_argument(
            "instance count must be an integer in [1, 10000]");
      }
      instance_count = static_cast<int>(parsed);
    }

    print_header();
    for (int index = 0; index < instance_count; ++index) {
      const auto seed = static_cast<std::uint32_t>(20261006 + index);
      const auto instance = make_instance(seed, 50, 7);
      const auto assignment = kdc::StationarySolver::solve_nn(instance, 0.0);
      if (!assignment.feasible) {
        throw std::runtime_error("benchmark initial assignment is infeasible");
      }

      kdc::KineticEventDiagnostics before;
      const auto reference = kdc::KineticSolution::extend(
          instance, assignment, 0.0, instance.T_end, true, true,
          kdc::ObjectiveType::MIN_MAX, nullptr,
          kdc::KineticEventEngine::REFERENCE_EXHAUSTIVE, &before,
          kdc::KineticIntervalEmission::REFERENCE_ALL_TRAJECTORY_BREAKPOINTS);
      kdc::KineticEventDiagnostics after;
      const auto optimized = kdc::KineticSolution::extend(
          instance, assignment, 0.0, instance.T_end, true, true,
          kdc::ObjectiveType::MIN_MAX, nullptr,
          kdc::KineticEventEngine::REFERENCE_EXHAUSTIVE, &after,
          kdc::KineticIntervalEmission::EXACT_RELEVANT_BOUNDARIES);
      if (!kdc::Verifier::verify_continuous(instance, reference).all_ok() ||
          !kdc::Verifier::verify_continuous(instance, optimized).all_ok()) {
        throw std::runtime_error(
            "benchmark detected an invalid emitted kinetic solution");
      }
      print_record(instance, seed, reference, optimized, before, after);
    }
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "kinetic interval benchmark failed: " << error.what() << '\n';
    return 1;
  }
}
