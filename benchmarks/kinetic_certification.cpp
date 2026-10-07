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
#include <random>
#include <sstream>
#include <string>
#include <thread>
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
constexpr double kTolerance = 1e-9;

struct Family {
  const char* name;
  int n;
  int m;
  int segments;
  int pattern;
};

struct Run {
  kdc::KineticSolution solution;
  kdc::KineticEventDiagnostics diagnostics;
  std::uint64_t extension_ns{0};
  std::uint64_t verification_ns{0};
  bool verified{false};
  kdc::VerificationReport verification;
};

constexpr Family kFamilies[] = {
    {"A_many_support_events", 10, 2, 3, 0},
    {"B_large_n_small_m", 48, 2, 4, 1},
    {"C_moderate_n_large_m", 24, 8, 3, 2},
    {"D_irrelevant_waypoints", 12, 3, 12, 3},
    {"E_handover_heavy", 12, 2, 4, 4},
    {"F_near_zero", 10, 2, 3, 5},
    {"G_simultaneous", 12, 3, 4, 6},
    {"H_tangent_degenerate", 10, 2, 3, 7},
    {"I_long_piecewise", 12, 3, 16, 8},
    {"J_sparse_events", 16, 4, 2, 9},
};

kdc::Instance make_instance(std::uint32_t seed, const Family& family) {
  std::mt19937 generator(seed);
  std::uniform_real_distribution<double> jitter(-0.4, 0.4);
  kdc::Instance instance;
  instance.id = static_cast<int>(seed);
  instance.name = std::string(family.name) + "-" + std::to_string(seed);
  instance.n = family.n;
  instance.m = family.m;
  instance.T_end = 1.0;
  for (int station = 0; station < family.m; ++station) {
    instance.stations.push_back(
        {station, {8.0 * static_cast<double>(station), 0.0}});
  }
  std::vector<double> breaks;
  for (int index = 0; index <= family.segments; ++index) {
    double time = static_cast<double>(index) / family.segments;
    if (family.pattern == 5 && index == 1) {
      time = 1e-12;
    }
    breaks.push_back(time);
  }
  if (family.pattern == 5 && breaks.size() > 2U) {
    for (std::size_t index = 2; index < breaks.size(); ++index) {
      breaks[index] =
          1e-12 + (1.0 - 1e-12) * static_cast<double>(index - 1U) /
                      static_cast<double>(family.segments - 1);
    }
  }

  for (int point = 0; point < family.n; ++point) {
    const int group = point % family.m;
    const int ordinal = point / family.m;
    const double center = 8.0 * static_cast<double>(group);
    std::vector<kdc::Point> waypoints;
    waypoints.reserve(breaks.size());
    for (std::size_t index = 0; index < breaks.size(); ++index) {
      double x = center + jitter(generator);
      double y = jitter(generator);
      if (family.pattern == 0) {
        const bool first_support = ordinal == 0;
        const bool alternating = index % 2U == 0U;
        x = center + (first_support == alternating ? 1.0 : 2.0);
        y = 0.0;
      } else if (family.pattern == 3 || family.pattern == 8) {
        if (family.pattern == 3) {
          x = center + 0.15 * static_cast<double>(ordinal);
          y = 0.0;
        } else {
          x = center + jitter(generator) * 0.1;
          y = jitter(generator) * 0.1;
        }
      } else if (family.pattern == 4) {
        x = center + (point % 3 == 0 ? 9.5 - static_cast<double>(index) : 0.2);
        y = 0.0;
      } else if (family.pattern == 5) {
        const double time = breaks[index];
        const double offset = 1e6 * (time - 1e-12);
        x = center + (ordinal == 0 ? 1.0 + offset : 1.0 - offset);
        y = 0.0;
      } else if (family.pattern == 6) {
        const double time = breaks[index];
        const int perturbation = static_cast<int>(seed % 4U);
        double root_time =
            perturbation == 1 ? 0.5 - 2e-9
                              : (perturbation == 2 ? 0.5 + 2e-9 : 0.5);
        if (perturbation == 3 && group == 1) {
          root_time += 5e-10;
        }
        x = center + (ordinal == 0 ? 1.0 + 2.0 * (time - root_time)
                                   : 1.0 - 2.0 * (time - root_time));
        y = 0.0;
      } else if (family.pattern == 7) {
        const double tangent_offset = seed % 2U == 0U ? 0.0 : 1e-10;
        x = center + 1.0;
        if (ordinal == 1) {
          x -= tangent_offset;
          y = -1.0 + 2.0 * breaks[index];
        } else if (ordinal > 1) {
          x = center + 0.15 * static_cast<double>(ordinal - 1);
          y = 0.0;
        } else {
          y = 0.0;
        }
      } else if (family.pattern == 9) {
        x = center + jitter(generator) * 0.03;
        y = jitter(generator) * 0.03;
      }
      waypoints.emplace_back(x, y);
    }
    instance.trajectories.emplace_back(breaks, std::move(waypoints));
  }
  return instance;
}

kdc::Instance make_handover_instance(std::uint32_t seed) {
  const std::vector<double> breaks{0.0, 0.2, 0.45, 0.7, 1.0};
  kdc::Instance instance;
  instance.id = static_cast<int>(seed);
  instance.name = "E_handover_heavy-" + std::to_string(seed);
  instance.n = 20;
  instance.m = 2;
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
    const double x = 1.0 + 0.1 * static_cast<double>(point - 4);
    instance.trajectories.emplace_back(
        std::vector<double>{0.0, 1.0},
        std::vector<kdc::Point>{{x, 0.0}, {x, 0.0}});
  }
  instance.trajectories.emplace_back(
      std::vector<double>{0.0, 1.0},
      std::vector<kdc::Point>{{10.5, 0.0}, {10.5, 0.0}});
  for (int point = 11; point < instance.n; ++point) {
    const double x = 10.1 + 0.05 * static_cast<double>(point - 11);
    instance.trajectories.emplace_back(
        std::vector<double>{0.0, 1.0},
        std::vector<kdc::Point>{{x, 0.0}, {x, 0.0}});
  }
  return instance;
}

Run run(const kdc::Instance& instance,
        const kdc::StaticAssignment& assignment,
        kdc::KineticEventEngine engine, kdc::HandoverEvaluation handover,
        bool use_handovers) {
  Run result;
  const auto begin = Clock::now();
  result.solution = kdc::KineticSolution::extend(
      instance, assignment, 0.0, instance.T_end, true, use_handovers,
      kdc::ObjectiveType::MIN_SUM, nullptr, engine, &result.diagnostics,
      kdc::KineticIntervalEmission::EXACT_RELEVANT_BOUNDARIES, handover);
  result.extension_ns = static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - begin)
          .count());
  const auto verify_begin = Clock::now();
  result.verification =
      kdc::Verifier::verify_continuous(instance, result.solution);
  result.verified = result.verification.certified_continuous_verification &&
                    result.verification.all_ok();
  result.verification_ns = static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::nanoseconds>(
          Clock::now() - verify_begin)
          .count());
  return result;
}

bool near(double first, double second) {
  return std::abs(first - second) <=
         kTolerance * std::max({1.0, std::abs(first), std::abs(second)});
}

bool same_trace(const std::vector<kdc::KineticEventTraceEntry>& lhs,
                const std::vector<kdc::KineticEventTraceEntry>& rhs) {
  if (lhs.size() != rhs.size()) {
    return false;
  }
  for (std::size_t index = 0; index < lhs.size(); ++index) {
    const auto& a = lhs[index];
    const auto& b = rhs[index];
    if (!kdc::event_times_simultaneous(a.time, b.time) || a.type != b.type ||
        a.station_id != b.station_id || a.old_support != b.old_support ||
        a.new_support != b.new_support || a.from_station != b.from_station ||
        a.to_station != b.to_station ||
        a.affected_point != b.affected_point ||
        a.tie_breaking_outcome != b.tie_breaking_outcome ||
        a.from_station_support_after != b.from_station_support_after ||
        a.to_station_support_after != b.to_station_support_after) {
      return false;
    }
  }
  return true;
}

bool same_state(const kdc::Instance& instance,
                const kdc::KineticSolution& lhs,
                const kdc::KineticSolution& rhs,
                const std::vector<kdc::KineticEventTraceEntry>& trace) {
  if (lhs.intervals.size() != rhs.intervals.size()) {
    return false;
  }
  for (std::size_t index = 0; index < lhs.intervals.size(); ++index) {
    const auto& a = lhs.intervals[index];
    const auto& b = rhs.intervals[index];
    if (!near(a.t_start, b.t_start) || !near(a.t_end, b.t_end) ||
        a.supporting_point != b.supporting_point ||
        a.assigned_points != b.assigned_points || !near(a.a, b.a) ||
        !near(a.b, b.b) || !near(a.c, b.c)) {
      return false;
    }
  }
  std::vector<double> critical_times;
  critical_times.reserve(trace.size() + instance.trajectories.size() * 2U +
                         lhs.intervals.size() * 2U);
  for (const auto& event : trace) {
    critical_times.push_back(event.time);
  }
  for (const auto& trajectory : instance.trajectories) {
    critical_times.insert(critical_times.end(), trajectory.t_breaks.begin(),
                          trajectory.t_breaks.end());
  }
  for (const auto& interval : lhs.intervals) {
    critical_times.push_back(interval.t_start);
    critical_times.push_back(interval.t_end);
  }
  for (const auto& interval : rhs.intervals) {
    critical_times.push_back(interval.t_start);
    critical_times.push_back(interval.t_end);
  }
  const double domain_start = lhs.intervals.front().t_start;
  const double domain_end = lhs.intervals.back().t_end;
  for (const double time : critical_times) {
    for (const double candidate :
         {time, std::nextafter(time, 0.0), std::nextafter(time, 1.0)}) {
      if (candidate < domain_start || candidate > domain_end) {
        continue;
      }
      if (!near(lhs.cost_at(candidate), rhs.cost_at(candidate))) {
        return false;
      }
    }
  }
  return near(lhs.peak_cost(), rhs.peak_cost()) &&
         near(lhs.peak_time(), rhs.peak_time()) &&
         near(lhs.total_integral(), rhs.total_integral());
}

void header() {
  std::cout << "git_commit,git_dirty,compiler,build_type,logical_cpus,"
               "threads,seed,family,n,m,total_segments,objective,"
               "interval_emission,tolerance_config,solver_config,engine,"
               "handover,extension_ns,"
               "verification_ns,end_to_end_ns,support_event_ns,"
               "handover_event_ns,interval_construction_ns,"
               "point_support_comparisons,quadratic_solves,segment_pair_exams,"
               "support_certificate_creations,support_certificate_failures,"
               "support_queue_pushes,support_queue_pops,stale_support_events,"
               "tournament_tree_updates,external_challenge_certificates,"
               "external_challenge_updates,handover_queue_pushes,"
               "handover_queue_pops,stale_handover_events,"
               "local_support_queries,second_support_queries,global_fallbacks,"
               "global_points_scanned,interval_count,support_events,"
               "handover_events,peak,peak_time,"
               "integral,memory_bytes,speedup_vs_reference,correctness_status,"
               "event_trace\n";
}

void row(const kdc::Instance& instance, std::uint32_t seed, const Family& family,
         const char* engine_name, const char* handover_name, const Run& run,
         std::uint64_t reference_extension_ns, bool correct) {
  std::uint64_t segment_count = 0;
  for (const auto& trajectory : instance.trajectories) {
    segment_count += trajectory.t_breaks.size() - 1U;
  }
  std::uint64_t handovers = 0;
  for (const auto& event : run.diagnostics.trace) {
    handovers += event.type == kdc::KineticEventType::HANDOVER ? 1U : 0U;
  }
  std::ostringstream event_trace;
  event_trace << std::setprecision(17);
  std::size_t event_group = 0U;
  double previous_event_time = 0.0;
  bool have_previous_event = false;
  for (const auto& event : run.diagnostics.trace) {
    if (!have_previous_event ||
        !kdc::event_times_simultaneous(previous_event_time, event.time)) {
      ++event_group;
    }
    if (have_previous_event) {
      event_trace << ';';
    }
    event_trace << event_group << '@' << event.time << ':'
                << static_cast<int>(event.type) << ':' << event.station_id
                << ':' << event.old_support << ':' << event.new_support << ':'
                << event.from_station << ':' << event.to_station << ':'
                << event.affected_point << ':'
                << event.from_station_support_after << ':'
                << event.to_station_support_after;
    previous_event_time = event.time;
    have_previous_event = true;
  }
  const auto& d = run.diagnostics;
  std::cout << KDC_BENCH_GIT_COMMIT << ',' << KDC_BENCH_GIT_DIRTY
            << ",\"" << __VERSION__ << "\"," << KDC_BENCH_BUILD_TYPE << ','
            << std::thread::hardware_concurrency() << ",1," << seed << ','
            << family.name << ',' << instance.n << ',' << instance.m << ','
            << segment_count
            << ",MIN_SUM,EXACT_RELEVANT_BOUNDARIES,"
               "abs_event_1e-9_rel_64eps_interval_1e-9,"
               "nn_or_fixed_handover_assignment,"
            << engine_name << ',' << handover_name
            << ',' << run.extension_ns << ',' << run.verification_ns << ','
            << run.extension_ns + run.verification_ns << ','
            << d.support_event_detection_nanoseconds << ','
            << d.handover_detection_nanoseconds << ','
            << d.interval_construction_nanoseconds << ','
            << d.point_vs_support_comparisons << ','
            << d.quadratic_equations_solved << ','
            << d.trajectory_segment_pair_examinations << ",NA,NA,NA,NA,NA,NA,"
            << d.external_challenge_certificates << ','
            << d.external_challenge_updates << ',' << d.handover_queue_pushes
            << ",NA," << d.stale_handover_events << ','
            << d.local_support_queries << ',' << d.second_support_queries << ','
            << d.handover_global_fallbacks << ','
            << d.handover_global_point_scans << ','
            << run.solution.intervals.size() << ',' << d.selected_support_events
            << ',' << handovers << ',' << std::setprecision(17)
            << run.solution.peak_cost() << ',' << run.solution.peak_time() << ','
            << run.solution.total_integral() << ",NA,"
            << (correct && run.extension_ns > 0U
                    ? static_cast<double>(reference_extension_ns) /
                          static_cast<double>(run.extension_ns)
                    : 0.0)
            << ',' << (correct && run.verified ? "PASS" : "FAIL") << ",\""
            << event_trace.str() << "\"\n";
}
}  // namespace

int main(int argc, char** argv) {
  try {
    spdlog::set_level(spdlog::level::off);
    const int seed_count = argc > 1 ? std::stoi(argv[1]) : 100;
    const int repetitions = argc > 2 ? std::stoi(argv[2]) : 1;
    const int start_index = argc > 3 ? std::stoi(argv[3]) : 0;
    if (seed_count < 1 || repetitions < 1) {
      throw std::invalid_argument("seed and repetition counts must be positive");
    }
    header();
    constexpr std::uint32_t first_seed = 20261007U;
    bool campaign_passed = true;
    for (int index = 0; index < seed_count; ++index) {
      const int campaign_index = start_index + index;
      const auto seed =
          first_seed + static_cast<std::uint32_t>(campaign_index);
      auto family = kFamilies[static_cast<std::size_t>(campaign_index) %
                             (sizeof(kFamilies) / sizeof(kFamilies[0]))];
      const int scale =
          campaign_index /
          static_cast<int>(sizeof(kFamilies) / sizeof(kFamilies[0]));
      if (family.pattern == 1) {
        family.n = 8 + 8 * scale;
      } else if (family.pattern == 2) {
        family.m = std::min(2 + scale, 8);
        family.n = family.m * 3;
      } else if (family.pattern == 3) {
        family.segments = 2 + scale;
      } else if (family.pattern == 8) {
        family.segments = 2 + 2 * scale;
      } else if (family.pattern == 9) {
        family.n = 8 + 4 * scale;
      }
      const auto instance = family.pattern == 4
                                ? make_handover_instance(seed)
                                : make_instance(seed, family);
      kdc::StaticAssignment assignment;
      if (family.pattern == 4) {
        std::vector<int> owners(static_cast<std::size_t>(instance.n), 0);
        std::fill(owners.begin() + 10, owners.end(), 1);
        assignment = {{0, 10},
                      {12.0, 0.5},
                      std::acos(-1.0) * (12.0 * 12.0 + 0.5 * 0.5),
                      true,
                      std::move(owners)};
      } else {
        assignment = kdc::StationarySolver::solve_nn(instance, 0.0);
      }
      if (!assignment.feasible) {
        throw std::runtime_error("generated assignment is infeasible");
      }
      const std::vector<std::pair<const char*, kdc::KineticEventEngine>> engines{
          {"reference_global", kdc::KineticEventEngine::REFERENCE_EXHAUSTIVE},
          {"tournament_global", kdc::KineticEventEngine::KINETIC_TOURNAMENT},
          {"tournament_local", kdc::KineticEventEngine::KINETIC_TOURNAMENT}};
      std::vector<std::vector<Run>> runs(engines.size());
      const bool use_handovers = family.pattern == 4;
      for (int repeat = 0; repeat < repetitions; ++repeat) {
        for (std::size_t engine = 0; engine < engines.size(); ++engine) {
          const auto handover =
              engine == 0U ? kdc::HandoverEvaluation::REFERENCE_GLOBAL
                           : (engine == 1U
                                  ? kdc::HandoverEvaluation::REFERENCE_GLOBAL
                                  : kdc::HandoverEvaluation::LOCAL_EXACT);
          runs[engine].push_back(
              run(instance, assignment, engines[engine].second, handover,
                  use_handovers));
        }
      }
      const auto median_run = [](std::vector<Run>& values) -> Run {
        std::sort(values.begin(), values.end(),
                  [](const Run& lhs, const Run& rhs) {
                    return lhs.extension_ns < rhs.extension_ns;
                  });
        return values[values.size() / 2U];
      };
      std::vector<Run> representatives;
      for (auto& set : runs) {
        representatives.push_back(median_run(set));
      }
      bool correct = true;
      const auto& reference = runs[0].front();
      for (std::size_t engine = 0; engine < runs.size(); ++engine) {
        for (const auto& candidate : runs[engine]) {
          correct =
              correct && candidate.verified &&
              same_trace(reference.diagnostics.trace,
                         candidate.diagnostics.trace) &&
              same_state(instance, reference.solution, candidate.solution,
                         reference.diagnostics.trace);
        }
      }
      for (std::size_t engine = 0; engine < representatives.size(); ++engine) {
        const char* handover_name =
            !use_handovers ? "off"
                           : (engine == 0U || engine == 1U ? "global" : "local");
        row(instance, seed, family, engines[engine].first, handover_name,
            representatives[engine], representatives[0].extension_ns, correct);
      }
      if (!correct) {
        campaign_passed = false;
        std::cerr << "differential failure seed=" << seed
                  << " family=" << family.name;
        for (std::size_t engine = 0; engine < representatives.size(); ++engine) {
          std::cerr << " engine" << engine
                    << "_verified=" << representatives[engine].verified
                    << "_coverage="
                    << representatives[engine].verification.coverage_ok
                    << "_supports="
                    << representatives[engine]
                           .verification.supporting_points_ok
                    << "_cost="
                    << representatives[engine].verification.cost_consistent_ok
                    << "_integral="
                    << representatives[engine]
                           .verification.integral_consistent_ok
                    << "_assignment="
                    << representatives[engine]
                           .verification.assignment_consistent_ok;
          for (const auto& error :
               representatives[engine].verification.errors) {
            std::cerr << " error=\"" << error << '"';
          }
        }
        for (std::size_t engine = 1; engine < representatives.size(); ++engine) {
          std::cerr << " trace" << engine << "="
                    << same_trace(representatives[0].diagnostics.trace,
                                  representatives[engine].diagnostics.trace)
                    << " state" << engine << "="
                    << same_state(instance, representatives[0].solution,
                                  representatives[engine].solution,
                                  representatives[0].diagnostics.trace);
          if (engine == 2U) {
            const auto& expected = representatives[0].diagnostics.trace;
            const auto& actual = representatives[engine].diagnostics.trace;
            std::cerr << " trace_sizes=" << expected.size() << '/'
                      << actual.size();
            const auto trace_count = std::min(expected.size(), actual.size());
            for (std::size_t event = 0; event < trace_count; ++event) {
              if (!kdc::event_times_simultaneous(expected[event].time,
                                                 actual[event].time) ||
                  expected[event].type != actual[event].type ||
                  expected[event].station_id != actual[event].station_id ||
                  expected[event].affected_point !=
                      actual[event].affected_point) {
                std::cerr << " first_trace_difference=" << event << ':'
                          << expected[event].time << '/'
                          << actual[event].time << ':'
                          << static_cast<int>(expected[event].type) << '/'
                          << static_cast<int>(actual[event].type) << ':'
                          << expected[event].affected_point << '/'
                          << actual[event].affected_point;
                break;
              }
            }
            const auto& expected_intervals =
                representatives[0].solution.intervals;
            const auto& actual_intervals =
                representatives[engine].solution.intervals;
            std::cerr << " interval_sizes=" << expected_intervals.size() << '/'
                      << actual_intervals.size();
            for (std::size_t event = 0; event < expected.size(); ++event) {
              std::cerr << " ref_event[" << event << "]="
                        << expected[event].time << ':'
                        << static_cast<int>(expected[event].type) << ':'
                        << expected[event].from_station << ':'
                        << expected[event].to_station << ':'
                        << expected[event].affected_point;
            }
            for (std::size_t event = 0; event < actual.size(); ++event) {
              std::cerr << " local_event[" << event << "]="
                        << actual[event].time << ':'
                        << static_cast<int>(actual[event].type) << ':'
                        << actual[event].from_station << ':'
                        << actual[event].to_station << ':'
                        << actual[event].affected_point;
            }
          }
        }
        std::cerr << '\n';
      }
    }
    return campaign_passed ? 0 : 1;
  } catch (const std::exception& error) {
    std::cerr << "kinetic certification failed: " << error.what() << '\n';
    return 1;
  }
}
