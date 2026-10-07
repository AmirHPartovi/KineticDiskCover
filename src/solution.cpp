#include "kdc/solution.hpp"

#include "kdc/candidate.hpp"
#include "kdc/kinetic.hpp"
#include "kdc/logging.hpp"
#include "kdc/profiling.hpp"
#include "kdc/stationary.hpp"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

namespace kdc {
namespace {
Value evaluate(const SolutionInterval& interval, Value time) {
  return (interval.a * time + interval.b) * time + interval.c;
}

void validate_interval_request(const KineticSolution& solution, Value start,
                               Value end) {
  if (solution.intervals.empty()) {
    throw std::runtime_error("kinetic solution has no intervals");
  }
  if (!std::isfinite(start) || !std::isfinite(end) || end < start) {
    throw std::invalid_argument("integration bounds must be finite and ordered");
  }
  if (start < solution.intervals.front().t_start ||
      end > solution.intervals.back().t_end) {
    throw std::out_of_range("requested time range is outside the solution");
  }
}

constexpr Value kIntervalTolerance = 1e-9;

class ExtensionDiagnosticsTimer {
 public:
  explicit ExtensionDiagnosticsTimer(
      KineticEventDiagnostics* diagnostics) noexcept
      : diagnostics_(diagnostics) {
    if (diagnostics_ != nullptr) {
      start_ = std::chrono::steady_clock::now();
    }
  }

  ~ExtensionDiagnosticsTimer() {
    if (diagnostics_ != nullptr) {
      const auto elapsed =
          std::chrono::duration_cast<std::chrono::nanoseconds>(
              std::chrono::steady_clock::now() - start_)
              .count();
      diagnostics_->total_extension_nanoseconds +=
          static_cast<std::uint64_t>(elapsed);
    }
  }

 private:
  KineticEventDiagnostics* diagnostics_;
  std::chrono::steady_clock::time_point start_{};
};

class IntervalConstructionTimer {
 public:
  explicit IntervalConstructionTimer(
      KineticEventDiagnostics* diagnostics) noexcept
      : diagnostics_(diagnostics) {
    if (diagnostics_ != nullptr) {
      start_ = std::chrono::steady_clock::now();
    }
  }

  ~IntervalConstructionTimer() {
    stop();
  }

  void stop() noexcept {
    if (diagnostics_ != nullptr) {
      const auto elapsed =
          std::chrono::duration_cast<std::chrono::nanoseconds>(
              std::chrono::steady_clock::now() - start_)
              .count();
      diagnostics_->interval_construction_nanoseconds +=
          static_cast<std::uint64_t>(elapsed);
      diagnostics_ = nullptr;
    }
  }

 private:
  KineticEventDiagnostics* diagnostics_;
  std::chrono::steady_clock::time_point start_{};
};

bool same_coefficients(const SolutionInterval& lhs,
                       const SolutionInterval& rhs) {
  return lhs.supporting_point == rhs.supporting_point &&
         lhs.assigned_points == rhs.assigned_points &&
         std::abs(lhs.a - rhs.a) <= kIntervalTolerance &&
         std::abs(lhs.b - rhs.b) <= kIntervalTolerance &&
         std::abs(lhs.c - rhs.c) <= kIntervalTolerance;
}

const SolutionInterval& interval_at(const KineticSolution& solution,
                                   Value time) {
  auto upper = std::upper_bound(
      solution.intervals.begin(), solution.intervals.end(), time,
      [](Value value, const SolutionInterval& interval) {
        return value < interval.t_start;
      });
  if (upper == solution.intervals.begin()) {
    return solution.intervals.front();
  }
  --upper;
  if (time > upper->t_end + kIntervalTolerance) {
    throw std::invalid_argument("solution intervals do not cover a breakpoint");
  }
  return *upper;
}

void append_interval(KineticSolution& solution,
                     const SolutionInterval& source, Value start, Value end) {
  if (end <= start) {
    return;
  }
  SolutionInterval interval = source;
  interval.t_start = start;
  interval.t_end = end;
  solution.intervals.push_back(std::move(interval));
}

std::vector<Value> common_breakpoints(const KineticSolution& first,
                                      const KineticSolution& second) {
  if (!first.is_well_formed() || !second.is_well_formed()) {
    throw std::invalid_argument("combine requires well-formed solutions");
  }
  std::vector<Value> breakpoints;
  breakpoints.reserve(first.intervals.size() + second.intervals.size() + 2U);
  for (const auto& interval : first.intervals) {
    breakpoints.push_back(interval.t_start);
    breakpoints.push_back(interval.t_end);
  }
  for (const auto& interval : second.intervals) {
    breakpoints.push_back(interval.t_start);
    breakpoints.push_back(interval.t_end);
  }
  std::sort(breakpoints.begin(), breakpoints.end());
  std::vector<Value> unique_breakpoints;
  unique_breakpoints.reserve(breakpoints.size());
  for (const Value breakpoint : breakpoints) {
    if (unique_breakpoints.empty() ||
        std::abs(breakpoint - unique_breakpoints.back()) >
            kIntervalTolerance) {
      unique_breakpoints.push_back(breakpoint);
    }
  }
  if (unique_breakpoints.size() < 2U) {
    throw std::invalid_argument("solutions have no positive duration");
  }
  return unique_breakpoints;
}

void compute_quadratic_coeffs_precomputed(
    const Instance& instance, const InstancePrecompute& precompute,
    const std::vector<int>& supporting_points, SolutionInterval& output,
    SolverBudget* budget) {
  Value a = 0.0;
  Value b = 0.0;
  Value c = 0.0;
  const Value pi = std::acos(-1.0);
  const Value midpoint =
      output.t_start + (output.t_end - output.t_start) / 2.0;
  for (Index station_index = 0; station_index < supporting_points.size();
       ++station_index) {
    if (budget != nullptr) {
      budget->checkpoint();
    }
    const int point_index = supporting_points[station_index];
    if (point_index == -1) {
      continue;
    }
    if (point_index < 0 || point_index >= instance.n) {
      throw std::out_of_range("supporting point is outside the instance");
    }
    const auto& segment =
        precompute.trajectory_segments[static_cast<Index>(point_index)]
                                      [precompute.segment_index(
                                          static_cast<Index>(point_index),
                                          midpoint)];
    const Point displacement =
        instance.stations[station_index].pos - segment.affine_origin;
    a += pi * segment.velocity.dot(segment.velocity);
    b += pi * (-2.0 * displacement.dot(segment.velocity));
    c += pi * displacement.dot(displacement);
  }
  output.supporting_point = supporting_points;
  output.a = a;
  output.b = b;
  output.c = c;
}
}

void KineticSolution::compute_quadratic_coeffs(
    const Instance& instance, const std::vector<int>& supporting_points,
    SolutionInterval& output, SolverBudget* budget) {
  LOG_DEBUG("compute_quadratic_coeffs: m={}", supporting_points.size());
  if (instance.n < 0 || instance.m < 0 ||
      static_cast<Index>(instance.n) != instance.trajectories.size() ||
      static_cast<Index>(instance.m) != instance.stations.size()) {
    throw std::invalid_argument(
        "quadratic coefficient calculation received inconsistent dimensions");
  }
  if (supporting_points.size() != static_cast<Index>(instance.m)) {
    throw std::invalid_argument(
        "supporting_points size must match the number of stations");
  }
  if (!std::isfinite(instance.T_end) || instance.T_end <= 0.0) {
    throw std::invalid_argument(
        "quadratic coefficients require a positive finite T_end");
  }
  if (!std::isfinite(output.t_start) || !std::isfinite(output.t_end) ||
      output.t_start < 0.0 || output.t_end > instance.T_end ||
      output.t_start >= output.t_end) {
    throw std::invalid_argument(
        "quadratic coefficients require a positive interval within [0, T_end]");
  }

  const auto precomputed = CandidateSet::precompute(instance, budget);
  compute_quadratic_coeffs_precomputed(instance, *precomputed,
                                       supporting_points, output, budget);
}

KineticSolution KineticSolution::extend(
    const Instance& instance, const StaticAssignment& init_assignment,
    double t_start, double t_end, bool forward, bool use_handovers,
    ObjectiveType objective_type, SolverBudget* budget,
    KineticEventEngine event_engine,
    KineticEventDiagnostics* diagnostics,
    KineticIntervalEmission interval_emission,
    HandoverEvaluation handover_evaluation) {
  KDC_PROFILE_PHASE(ProfilePhase::KINETIC_EXTENSION);
  ExtensionDiagnosticsTimer diagnostics_timer(diagnostics);
  if (event_engine != KineticEventEngine::REFERENCE_EXHAUSTIVE &&
      event_engine != KineticEventEngine::KINETIC_TOURNAMENT) {
    throw std::invalid_argument("unsupported kinetic event engine");
  }
  if (interval_emission !=
          KineticIntervalEmission::REFERENCE_ALL_TRAJECTORY_BREAKPOINTS &&
      interval_emission !=
          KineticIntervalEmission::EXACT_RELEVANT_BOUNDARIES) {
    throw std::invalid_argument("unsupported kinetic interval emission mode");
  }
  LOG_DEBUG("extend: t_start={}, t_end={}, forward={}", t_start, t_end,
            forward);
  if (!std::isfinite(t_start) || !std::isfinite(t_end) ||
      t_start < 0.0 || t_start > instance.T_end || t_end < 0.0 ||
      t_end > instance.T_end) {
    throw std::out_of_range("extension bounds are outside [0, T_end]");
  }
  if ((forward && t_end < t_start) || (!forward && t_end > t_start)) {
    throw std::invalid_argument("extension bounds disagree with direction");
  }
  if (!init_assignment.feasible ||
      init_assignment.supporting_point.size() !=
          static_cast<Index>(instance.m) ||
      init_assignment.radius.size() != static_cast<Index>(instance.m) ||
      init_assignment.assigned_points.size() !=
          static_cast<Index>(instance.n) ||
      instance.n < 0 || instance.m < 0 ||
      instance.trajectories.size() != static_cast<Index>(instance.n) ||
      instance.stations.size() != static_cast<Index>(instance.m)) {
    throw std::invalid_argument("extension requires a feasible assignment");
  }

  std::vector<int> supports = init_assignment.supporting_point;
  std::vector<int> owners = init_assignment.assigned_points;
  for (const int support : supports) {
    if (budget != nullptr) {
      budget->checkpoint();
    }
    if (support < -1 || support >= instance.n) {
      throw std::out_of_range("initial supporting point is invalid");
    }
  }
  const auto precomputed = CandidateSet::precompute(instance, budget);
  const auto initial_geometry =
      CandidateSet::build_geometry(instance, *precomputed, t_start, budget);
  std::vector<bool> station_has_points(static_cast<Index>(instance.m), false);
  std::vector<std::vector<int>> station_points(
      static_cast<Index>(instance.m));
  for (int point_id = 0; point_id < instance.n; ++point_id) {
    if (budget != nullptr) {
      budget->checkpoint();
    }
    const int owner = owners[static_cast<Index>(point_id)];
    if (owner < 0 || owner >= instance.m) {
      throw std::invalid_argument("initial assignment has an invalid owner");
    }
    station_has_points[static_cast<Index>(owner)] = true;
    station_points[static_cast<Index>(owner)].push_back(point_id);
  }
  std::vector<KineticFarthestTournament> tournaments(
      static_cast<Index>(instance.m));
  for (int station_id = 0; station_id < instance.m; ++station_id) {
    tournaments[static_cast<Index>(station_id)].initialize(
        instance, station_id,
        station_points[static_cast<Index>(station_id)], t_start, budget);
  }
  for (int station_id = 0; station_id < instance.m; ++station_id) {
    if (budget != nullptr) {
      budget->checkpoint();
    }
    const Index station_index = static_cast<Index>(station_id);
    if (!std::isfinite(init_assignment.radius[station_index]) ||
        init_assignment.radius[station_index] < 0.0) {
      throw std::invalid_argument("initial assignment has an invalid radius");
    }
    const int support = supports[station_index];
    if ((support < 0 && station_has_points[station_index]) ||
        (support >= 0 &&
         owners[static_cast<Index>(support)] != station_id)) {
      throw std::invalid_argument(
          "initial support and explicit ownership do not agree");
    }
    for (int point_id = 0; point_id < instance.n; ++point_id) {
      if (owners[static_cast<Index>(point_id)] != station_id) {
        continue;
      }
      const Value distance_squared = initial_geometry.distance_squared(
          station_index, static_cast<Index>(point_id),
          static_cast<Index>(instance.n));
      const Value radius = init_assignment.radius[station_index];
      if (distance_squared >
          radius * radius + 2e-9 * radius + 1e-18) {
        throw std::invalid_argument(
            "initial ownership assigns a point outside its disk");
      }
    }
  }
  KineticSolution solution;
  solution.objective = objective_type;
  Value current_time = t_start;
  const Value direction = forward ? 1.0 : -1.0;
  std::vector<Value> event_breakpoints;
  for (const auto& trajectory : precomputed->trajectories) {
    if (budget != nullptr) {
      budget->checkpoint();
    }
    if (diagnostics != nullptr) {
      diagnostics->raw_trajectory_breakpoints +=
          static_cast<std::uint64_t>(trajectory.t_breaks.size());
    }
    event_breakpoints.insert(event_breakpoints.end(),
                             trajectory.t_breaks.begin(),
                             trajectory.t_breaks.end());
  }
  std::sort(event_breakpoints.begin(), event_breakpoints.end());
  event_breakpoints.erase(
      std::unique(event_breakpoints.begin(), event_breakpoints.end()),
      event_breakpoints.end());
  const auto resolve_owner_supports =
      [&](const std::vector<int>& assignment, Value time,
          bool direction_forward, bool directional_state) {
        std::vector<std::vector<int>> points_by_station(
            static_cast<Index>(instance.m));
        for (int point_id = 0; point_id < instance.n; ++point_id) {
          if (budget != nullptr) {
            budget->checkpoint();
          }
          points_by_station[static_cast<Index>(
              assignment[static_cast<Index>(point_id)])]
              .push_back(point_id);
        }
        std::vector<int> resolved(static_cast<Index>(instance.m), -1);
        for (int station_id = 0; station_id < instance.m; ++station_id) {
          const Index station = static_cast<Index>(station_id);
          resolved[station] =
              directional_state
                  ? KineticCore::resolve_support_directional_limit(
                        instance, station_id, points_by_station[station], time,
                        direction_forward, budget)
                  : KineticCore::resolve_support_at_time(
                        instance, station_id, points_by_station[station], time,
                        budget);
        }
        return resolved;
      };
  std::size_t event_count = 0U;
      struct ExternalChallengeCertificate {
        HandoverEvent event;
        int source_support{-1};
        int receiver_support{-1};
        Index source_segment{0};
        Index receiver_segment{0};
        bool initialized{false};
        bool evaluated{false};
        bool valid{false};
      };
      std::vector<std::vector<ExternalChallengeCertificate>> challenge_certificates(
          static_cast<Index>(instance.m),
          std::vector<ExternalChallengeCertificate>(
              static_cast<Index>(instance.m)));
  while (direction * (t_end - current_time) > 0.0) {
    if (budget != nullptr) {
      budget->checkpoint();
    }
    const std::vector<int> previous_supports = supports;
    for (auto& tournament : tournaments) {
      tournament.process_until(current_time, budget);
    }
    const std::vector<int> exact_supports =
        resolve_owner_supports(owners, current_time, forward, false);
    supports = exact_supports;
    supports = resolve_owner_supports(owners, current_time, forward, true);
    bool support_changed_at_current_time = false;
    for (Index station = 0; station < supports.size(); ++station) {
      if (supports[station] != previous_supports[station]) {
        support_changed_at_current_time = true;
        if (diagnostics != nullptr) {
          ++diagnostics->selected_support_events;
          diagnostics->trace.push_back(
              {current_time, KineticEventType::SUPPORT_CHANGE,
               static_cast<int>(station), previous_supports[station],
               supports[station], -1, -1, supports[station],
               "directional quadratic ordering; exact ties retain the lowest point id"});
        }
      }
    }
    if (use_handovers) {
      std::vector<HandoverEvent> immediate_handovers;
      for (int station_from = 0; station_from < instance.m; ++station_from) {
        if (budget != nullptr) {
          budget->checkpoint();
        }
        const int support_from =
            supports[static_cast<Index>(station_from)];
        if (support_from < 0) {
          continue;
        }
        if (diagnostics != nullptr) {
          ++diagnostics->second_support_queries;
        }
        const int second_support =
            tournaments[static_cast<Index>(station_from)].best_except(
                support_from);
        if (second_support < 0) {
          continue;
        }
        if (KineticCore::compare_support_at_time(
                instance, station_from, second_support, support_from,
                current_time, budget) >= 0) {
          continue;
        }
        for (int station_to = 0; station_to < instance.m; ++station_to) {
          if (budget != nullptr) {
            budget->checkpoint();
          }
          if (diagnostics != nullptr) {
            ++diagnostics->source_receiver_pair_count;
          }
          const int support_to = supports[static_cast<Index>(station_to)];
          if (station_to == station_from || support_to < 0) {
            continue;
          }
          const bool receiver_accepts =
              KineticCore::compare_support_directional_limit(
                  instance, station_to, support_from, support_to,
                  current_time, forward, budget) < 0;
          if (receiver_accepts) {
            immediate_handovers.push_back(
                {current_time, station_from, station_to, support_from,
                 second_support, support_to, true});
          }
        }
      }

      std::sort(immediate_handovers.begin(), immediate_handovers.end(),
                [](const HandoverEvent& lhs, const HandoverEvent& rhs) {
                  if (lhs.from_station != rhs.from_station) {
                    return lhs.from_station < rhs.from_station;
                  }
                  if (lhs.to_station != rhs.to_station) {
                    return lhs.to_station < rhs.to_station;
                  }
                  return lhs.point_id < rhs.point_id;
                });
      std::vector<HandoverEvent> applied_immediate_handovers;
      for (const auto& event : immediate_handovers) {
        if (owners[static_cast<Index>(event.point_id)] != event.from_station) {
          continue;
        }
        const auto current_supports =
            resolve_owner_supports(owners, current_time, forward, true);
        if (current_supports[static_cast<Index>(event.from_station)] !=
                event.point_id ||
            current_supports[static_cast<Index>(event.to_station)] < 0) {
          if (diagnostics != nullptr) {
            ++diagnostics->stale_handover_events;
          }
          continue;
        }
        if (diagnostics != nullptr) {
          ++diagnostics->second_support_queries;
        }
        const int current_second_support =
            tournaments[static_cast<Index>(event.from_station)].best_except(
                event.point_id);
        if (current_second_support < 0) {
          if (diagnostics != nullptr) {
            ++diagnostics->stale_handover_events;
          }
          continue;
        }
        const auto evaluation_at_current_time =
            handover_evaluation == HandoverEvaluation::LOCAL_EXACT &&
                    support_changed_at_current_time
                ? HandoverEvaluation::REFERENCE_GLOBAL
                : handover_evaluation;
        // Keep source eligibility on the reference approach-side at a tie.
        if (evaluation_at_current_time == HandoverEvaluation::REFERENCE_GLOBAL &&
            handover_evaluation == HandoverEvaluation::LOCAL_EXACT &&
            diagnostics != nullptr) {
          ++diagnostics->handover_global_fallbacks;
        }
        const auto accepted = KineticCore::evaluate_handover_candidate(
            instance, event.from_station, event.to_station, event.point_id,
            event.point_id,
            current_supports[static_cast<Index>(event.to_station)],
            current_second_support,
            current_supports[static_cast<Index>(event.to_station)], owners,
            current_time, forward, budget, diagnostics,
            evaluation_at_current_time);
        if (accepted.valid) {
          owners[static_cast<Index>(event.point_id)] = event.to_station;
          tournaments[static_cast<Index>(event.from_station)].erase(
              event.point_id, current_time, budget);
          tournaments[static_cast<Index>(event.to_station)].insert(
              event.point_id, current_time, budget);
          applied_immediate_handovers.push_back(event);
        } else if (diagnostics != nullptr) {
          ++diagnostics->stale_handover_events;
        }
      }
      if (!applied_immediate_handovers.empty()) {
        supports = resolve_owner_supports(owners, current_time, forward, true);
        if (diagnostics != nullptr) {
          for (const auto& event : applied_immediate_handovers) {
            diagnostics->trace.push_back(
                {current_time, KineticEventType::HANDOVER, -1, -1, -1,
                 event.from_station, event.to_station, event.point_id,
                 "simultaneous initial handovers are applied by ascending source, receiver, and point id",
                 supports[static_cast<Index>(event.from_station)],
                 supports[static_cast<Index>(event.to_station)]});
          }
        }
        if (++event_count > 100000U) {
          throw std::runtime_error("kinetic extension exceeded event limit");
        }
      }
    }

    Value next_time = t_end;
    const auto select_previous_breakpoint = [&](const auto& breaks) {
      if (forward) {
        const auto breakpoint = std::upper_bound(
            breaks.begin(), breaks.end(), current_time);
        if (breakpoint != breaks.end() &&
            *breakpoint < next_time) {
          next_time = *breakpoint;
        }
      } else {
        const auto breakpoint = std::lower_bound(
            breaks.begin(), breaks.end(), current_time);
        if (breakpoint != breaks.begin()) {
          const Value previous = *std::prev(breakpoint);
          if (previous > next_time) {
            next_time = previous;
          }
        }
      }
    };
    if (interval_emission ==
        KineticIntervalEmission::REFERENCE_ALL_TRAJECTORY_BREAKPOINTS) {
      select_previous_breakpoint(event_breakpoints);
    } else {
      for (Index station = 0; station < supports.size(); ++station) {
        const int support = supports[station];
        if (support < 0) {
          continue;
        }
        select_previous_breakpoint(
            precomputed
                ->trajectories[static_cast<Index>(support)]
                .t_breaks);
      }
    }
    const auto event_detection_start =
        diagnostics == nullptr ? std::chrono::steady_clock::time_point{}
                               : std::chrono::steady_clock::now();
    bool support_event_at_next_time = false;
    for (int station_id = 0; station_id < instance.m; ++station_id) {
      const int support = supports[static_cast<Index>(station_id)];
      if (support < 0) {
        continue;
      }
      const auto events = KineticCore::find_support_changes(
          instance, station_id, support, current_time, t_end, forward,
          budget, event_engine, diagnostics);
      for (const auto& event : events) {
        if (budget != nullptr) {
          budget->checkpoint();
        }
        if (owners[static_cast<Index>(event.new_supporting_point)] !=
            station_id) {
          if (diagnostics != nullptr) {
            ++diagnostics->candidate_roots_rejected;
          }
          continue;
        }
        if (KineticCore::compare_support_directional_limit(
                instance, station_id, event.new_supporting_point, support,
                event.time, forward, budget) <= 0) {
          if (diagnostics != nullptr) {
            ++diagnostics->candidate_roots_rejected;
          }
          continue;
        }
        if (direction * (event.time - current_time) > 0.0 &&
            direction * (event.time - next_time) < 0.0) {
          next_time = event.time;
          support_event_at_next_time = true;
        } else if (event_times_simultaneous(event.time, next_time)) {
          support_event_at_next_time = true;
        }
        break;
      }
    }
    std::vector<HandoverEvent> handover_events;
    const auto handover_detection_start =
        diagnostics == nullptr ? std::chrono::steady_clock::time_point{}
                               : std::chrono::steady_clock::now();
    const auto evaluate_external_candidate =
        [&](ExternalChallengeCertificate& certificate, int station_from,
            int station_to) {
          auto& source_tournament =
              tournaments[static_cast<Index>(station_from)];
          source_tournament.process_until(certificate.event.time, budget);
          const int source_second =
              source_tournament.best_except(certificate.event.point_id);
          source_tournament.process_until(current_time, budget);
          if (diagnostics != nullptr) {
            ++diagnostics->second_support_queries;
            ++diagnostics->handover_event_checks;
          }
          if (source_second < 0) {
            return HandoverEvent{};
          }
          return KineticCore::evaluate_handover_candidate(
              instance, station_from, station_to, certificate.event.point_id,
              certificate.source_support, certificate.receiver_support,
              source_second, certificate.receiver_support, owners,
              certificate.event.time, forward, budget, diagnostics,
              handover_evaluation);
        };
    const auto find_next_external_candidate =
        [&](ExternalChallengeCertificate& certificate, int station_from,
            int station_to, Value challenge_start) {
          certificate.valid = false;
          certificate.evaluated = false;
          while (direction * (t_end - challenge_start) > 0.0) {
            const auto challenge = KineticCore::find_external_challenge(
                instance, station_to, certificate.receiver_support,
                certificate.source_support, challenge_start, t_end, forward,
                budget, event_engine, diagnostics);
            if (!challenge.valid) {
              return;
            }
            certificate.event =
                {challenge.time, station_from, station_to,
                 certificate.source_support, -1, certificate.receiver_support,
                 true};
            certificate.valid = true;
            if (diagnostics != nullptr) {
              ++diagnostics->handover_queue_pushes;
            }
            if (direction * (next_time - challenge.time) > 0.0 &&
                !event_times_simultaneous(challenge.time, next_time)) {
              const auto accepted = evaluate_external_candidate(
                  certificate, station_from, station_to);
              if (accepted.valid) {
                certificate.event = accepted;
                certificate.evaluated = true;
                return;
              }
              certificate.valid = false;
              challenge_start = challenge.time;
              if (diagnostics != nullptr) {
                ++diagnostics->stale_handover_events;
              }
              continue;
            }
            return;
          }
        };
    if (use_handovers) {
      for (int station_from = 0; station_from < instance.m; ++station_from) {
        const int source_support =
            supports[static_cast<Index>(station_from)];
        for (int station_to = 0; station_to < instance.m; ++station_to) {
          if (station_to == station_from) {
            continue;
          }
          if (diagnostics != nullptr) {
            ++diagnostics->source_receiver_pair_count;
          }
          auto& certificate =
              challenge_certificates[static_cast<Index>(station_from)]
                                   [static_cast<Index>(station_to)];
          const int receiver_support =
              supports[static_cast<Index>(station_to)];
          if (source_support < 0 || receiver_support < 0 ||
              tournaments[static_cast<Index>(station_from)].best_except(
                  source_support) < 0) {
            if (certificate.valid && diagnostics != nullptr) {
              ++diagnostics->stale_handover_events;
            }
            certificate.valid = false;
            certificate.initialized = false;
            continue;
          }
          const Index source_segment =
              precomputed->directional_segment_index(
                  static_cast<Index>(source_support), current_time, forward);
          const Index receiver_segment =
              precomputed->directional_segment_index(
                  static_cast<Index>(receiver_support), current_time, forward);
          const bool changed =
              !certificate.initialized ||
              certificate.source_support != source_support ||
              certificate.receiver_support != receiver_support ||
              certificate.source_segment != source_segment ||
              certificate.receiver_segment != receiver_segment ||
              (certificate.valid &&
               direction * (certificate.event.time - current_time) <= 0.0);
          if (changed) {
            if (certificate.valid && diagnostics != nullptr) {
              ++diagnostics->stale_handover_events;
            }
            certificate.source_support = source_support;
            certificate.receiver_support = receiver_support;
            certificate.source_segment = source_segment;
            certificate.receiver_segment = receiver_segment;
            certificate.initialized = true;
            find_next_external_candidate(certificate, station_from,
                                         station_to, current_time);
          } else if (certificate.valid && !certificate.evaluated &&
                     direction * (next_time - certificate.event.time) > 0.0 &&
                     !event_times_simultaneous(certificate.event.time,
                                               next_time)) {
            const auto accepted =
                evaluate_external_candidate(certificate, station_from,
                                            station_to);
            if (accepted.valid) {
              certificate.event = accepted;
              certificate.evaluated = true;
            } else {
              const Value rejected_time = certificate.event.time;
              if (diagnostics != nullptr) {
                ++diagnostics->stale_handover_events;
              }
              find_next_external_candidate(certificate, station_from,
                                           station_to, rejected_time);
            }
          }
          if (certificate.valid) {
            handover_events.push_back(certificate.event);
          }
        }
      }
      std::sort(
          handover_events.begin(), handover_events.end(),
          [forward](const HandoverEvent& lhs, const HandoverEvent& rhs) {
            if (lhs.time != rhs.time) {
              return forward ? lhs.time < rhs.time : lhs.time > rhs.time;
            }
            if (lhs.from_station != rhs.from_station) {
              return lhs.from_station < rhs.from_station;
            }
            if (lhs.to_station != rhs.to_station) {
              return lhs.to_station < rhs.to_station;
            }
            return lhs.point_id < rhs.point_id;
          });
      handover_events.erase(
          std::unique(handover_events.begin(), handover_events.end(),
                      [](const HandoverEvent& lhs, const HandoverEvent& rhs) {
                        return lhs.from_station == rhs.from_station &&
                               lhs.to_station == rhs.to_station &&
                               lhs.point_id == rhs.point_id &&
                               event_times_simultaneous(lhs.time, rhs.time);
                      }),
          handover_events.end());
      if (!handover_events.empty() &&
          direction * (handover_events.front().time - current_time) > 0.0 &&
          direction * (handover_events.front().time - next_time) < 0.0) {
        next_time = handover_events.front().time;
      }
    }
    if (diagnostics != nullptr) {
      diagnostics->handover_detection_nanoseconds +=
          static_cast<std::uint64_t>(
              std::chrono::duration_cast<std::chrono::nanoseconds>(
                  std::chrono::steady_clock::now() -
                  handover_detection_start)
                  .count());
      diagnostics->event_detection_nanoseconds +=
          static_cast<std::uint64_t>(
              std::chrono::duration_cast<std::chrono::nanoseconds>(
                  std::chrono::steady_clock::now() - event_detection_start)
                  .count());
    }

    const Value interval_start = std::min(current_time, next_time);
    const Value interval_end = std::max(current_time, next_time);
    IntervalConstructionTimer interval_timer(diagnostics);
    SolutionInterval interval;
    interval.t_start = interval_start;
    interval.t_end = interval_end;
    interval.assigned_points = owners;
    compute_quadratic_coeffs_precomputed(instance, *precomputed, supports,
                                         interval, budget);
    assert(interval.t_start < interval.t_end);
    assert(std::isfinite(interval.a) && std::isfinite(interval.b) &&
           std::isfinite(interval.c));
    solution.intervals.push_back(std::move(interval));
    interval_timer.stop();
    if (diagnostics != nullptr) {
      ++diagnostics->solution_intervals_generated;
      if (direction * (next_time - t_end) < -kIntervalTolerance) {
        for (Index station = 0; station < supports.size(); ++station) {
          const int support = supports[station];
          if (support < 0) {
            continue;
          }
          const auto& trajectory =
              precomputed->trajectories[static_cast<Index>(support)];
          if (std::find(trajectory.t_breaks.begin(),
                        trajectory.t_breaks.end(), next_time) !=
              trajectory.t_breaks.end()) {
            diagnostics->trace.push_back(
                {next_time,
                 KineticEventType::ACTIVE_SUPPORT_MOTION_BREAKPOINT,
                 static_cast<int>(station), support, support, -1, -1, support,
                 "active support trajectory changes linear segment"});
          }
        }
      }
    }

    std::vector<HandoverEvent> simultaneous_handovers;
    for (const auto& event : handover_events) {
      if (event_times_simultaneous(event.time, next_time)) {
        simultaneous_handovers.push_back(event);
      }
    }
    std::sort(simultaneous_handovers.begin(), simultaneous_handovers.end(),
              [](const HandoverEvent& lhs, const HandoverEvent& rhs) {
                if (lhs.from_station != rhs.from_station) {
                  return lhs.from_station < rhs.from_station;
                }
                if (lhs.to_station != rhs.to_station) {
                  return lhs.to_station < rhs.to_station;
                }
                return lhs.point_id < rhs.point_id;
              });
    std::vector<HandoverEvent> applied_handovers;
    if (!simultaneous_handovers.empty()) {
      std::vector<int> batch_supports_before =
          resolve_owner_supports(owners, next_time, !forward, true);
      std::vector<int> batch_supports_after =
          resolve_owner_supports(owners, next_time, forward, true);
      for (auto& tournament : tournaments) {
        tournament.process_until(next_time, budget);
      }
      for (const auto& event : simultaneous_handovers) {
        if (event.point_id < 0 || event.point_id >= instance.n ||
            event.from_station < 0 || event.from_station >= instance.m ||
            event.to_station < 0 || event.to_station >= instance.m ||
            owners[static_cast<Index>(event.point_id)] != event.from_station) {
          if (diagnostics != nullptr) {
            ++diagnostics->stale_handover_events;
          }
          continue;
        }
        challenge_certificates[static_cast<Index>(event.from_station)]
                               [static_cast<Index>(event.to_station)]
                                   .valid = false;
        challenge_certificates[static_cast<Index>(event.from_station)]
                              [static_cast<Index>(event.to_station)]
                                  .initialized = false;
        if (diagnostics != nullptr) {
          ++diagnostics->handover_event_checks;
        }
        if (batch_supports_before[static_cast<Index>(event.from_station)] !=
            event.point_id) {
          if (diagnostics != nullptr) {
            ++diagnostics->stale_handover_events;
          }
          continue;
        }
        if (diagnostics != nullptr) {
          ++diagnostics->second_support_queries;
        }
        const int source_second =
            tournaments[static_cast<Index>(event.from_station)].best_except(
                event.point_id);
        if (source_second < 0) {
          if (diagnostics != nullptr) {
            ++diagnostics->stale_handover_events;
          }
          continue;
        }
        const auto evaluation_at_boundary =
            handover_evaluation == HandoverEvaluation::LOCAL_EXACT &&
                    support_event_at_next_time
                ? HandoverEvaluation::REFERENCE_GLOBAL
                : handover_evaluation;
        // A coincident support root must use the same approach-side check.
        if (evaluation_at_boundary == HandoverEvaluation::REFERENCE_GLOBAL &&
            handover_evaluation == HandoverEvaluation::LOCAL_EXACT &&
            diagnostics != nullptr) {
          ++diagnostics->handover_global_fallbacks;
        }
        const auto accepted = KineticCore::evaluate_handover_candidate(
            instance, event.from_station, event.to_station, event.point_id,
            batch_supports_before[static_cast<Index>(event.from_station)],
            batch_supports_before[static_cast<Index>(event.to_station)],
            source_second,
            batch_supports_after[static_cast<Index>(event.to_station)], owners,
            next_time, forward, budget, diagnostics, evaluation_at_boundary);
        if (!accepted.valid) {
          if (diagnostics != nullptr) {
            ++diagnostics->stale_handover_events;
          }
          continue;
        }
        owners[static_cast<Index>(event.point_id)] = event.to_station;
        tournaments[static_cast<Index>(event.from_station)].erase(
            event.point_id, next_time, budget);
        tournaments[static_cast<Index>(event.to_station)].insert(
            event.point_id, next_time, budget);
        batch_supports_before[static_cast<Index>(event.from_station)] =
            accepted.new_support_from;
        batch_supports_after[static_cast<Index>(event.from_station)] =
            accepted.new_support_from;
        batch_supports_before[static_cast<Index>(event.to_station)] =
            accepted.new_support_to;
        batch_supports_after[static_cast<Index>(event.to_station)] =
            accepted.new_support_to;
        applied_handovers.push_back(accepted);
      }
    }
    if (diagnostics != nullptr && !applied_handovers.empty()) {
      for (const auto& event : applied_handovers) {
        diagnostics->trace.push_back(
            {next_time, KineticEventType::HANDOVER, -1, -1, -1,
             event.from_station, event.to_station, event.point_id,
             "simultaneous handovers are applied by ascending source, receiver, and point id",
             event.new_support_from, event.new_support_to});
      }
    }
    if (next_time == current_time) {
      throw std::runtime_error("kinetic extension failed to advance time");
    }
    current_time = next_time;
    if (++event_count > 100000U) {
      throw std::runtime_error("kinetic extension exceeded event limit");
    }
  }
  if (solution.intervals.empty() && t_start != t_end) {
    IntervalConstructionTimer interval_timer(diagnostics);
    SolutionInterval interval;
    interval.t_start = std::min(t_start, t_end);
    interval.t_end = std::max(t_start, t_end);
    interval.assigned_points = owners;
    compute_quadratic_coeffs_precomputed(instance, *precomputed, supports,
                                         interval, budget);
    assert(interval.t_start < interval.t_end);
    assert(std::isfinite(interval.a) && std::isfinite(interval.b) &&
           std::isfinite(interval.c));
    solution.intervals.push_back(std::move(interval));
    interval_timer.stop();
    if (diagnostics != nullptr) {
      ++diagnostics->solution_intervals_generated;
    }
  }
  std::sort(solution.intervals.begin(), solution.intervals.end(),
            [](const SolutionInterval& lhs, const SolutionInterval& rhs) {
              return lhs.t_start < rhs.t_start;
            });
  if (diagnostics != nullptr) {
    std::stable_sort(
        diagnostics->trace.begin(), diagnostics->trace.end(),
        [](const KineticEventTraceEntry& lhs,
           const KineticEventTraceEntry& rhs) {
          if (lhs.time != rhs.time) {
            return lhs.time < rhs.time;
          }
          if (lhs.type != rhs.type) {
            return lhs.type < rhs.type;
          }
          if (lhs.station_id != rhs.station_id) {
            return lhs.station_id < rhs.station_id;
          }
          if (lhs.from_station != rhs.from_station) {
            return lhs.from_station < rhs.from_station;
          }
          if (lhs.to_station != rhs.to_station) {
            return lhs.to_station < rhs.to_station;
          }
          return lhs.affected_point < rhs.affected_point;
        });
  }
  LOG_INFO("extend: {} intervals over [{}, {}]", solution.intervals.size(),
           std::min(t_start, t_end), std::max(t_start, t_end));
  return solution;
}

KineticSolution KineticSolution::combine(const KineticSolution& s1,
                                         const KineticSolution& s2,
                                         ObjectiveType objective_type,
                                         SolverBudget* budget) {
  KDC_PROFILE_PHASE(ProfilePhase::COMBINATION);
  LOG_DEBUG("combine: {} + {} intervals", s1.intervals.size(),
            s2.intervals.size());
  const auto breakpoints = common_breakpoints(s1, s2);
  KineticSolution result;
  result.objective = objective_type;
  for (Index index = 0; index + 1U < breakpoints.size(); ++index) {
    if (budget != nullptr) {
      budget->checkpoint();
    }
    const Value start = breakpoints[index];
    const Value end = breakpoints[index + 1U];
    if (end <= start) {
      continue;
    }
    const Value midpoint = start + (end - start) / 2.0;
    const bool active1 =
        midpoint >= s1.intervals.front().t_start - kIntervalTolerance &&
        midpoint <= s1.intervals.back().t_end + kIntervalTolerance;
    const bool active2 =
        midpoint >= s2.intervals.front().t_start - kIntervalTolerance &&
        midpoint <= s2.intervals.back().t_end + kIntervalTolerance;
    if (!active1 && !active2) {
      throw std::invalid_argument("combined solutions leave a time gap");
    }
    if (!active1 || !active2) {
      const auto& active_solution = active1 ? s1 : s2;
      append_interval(result, interval_at(active_solution, midpoint), start,
                      end);
      continue;
    }
    const auto& interval1 = interval_at(s1, midpoint);
    const auto& interval2 = interval_at(s2, midpoint);
    std::vector<Value> partitions{start, end};
    const auto roots = KineticCore::solve_quadratic(
        interval1.a - interval2.a, interval1.b - interval2.b,
        interval1.c - interval2.c);
    for (const Value root : roots) {
      if (root > start + kIntervalTolerance &&
          root < end - kIntervalTolerance) {
        partitions.push_back(root);
      }
    }
    std::sort(partitions.begin(), partitions.end());
    partitions.erase(
        std::unique(partitions.begin(), partitions.end(),
                    [](Value lhs, Value rhs) {
                      return std::abs(lhs - rhs) <= kIntervalTolerance;
                    }),
        partitions.end());
    for (Index partition = 0; partition + 1U < partitions.size();
         ++partition) {
      const Value sub_start = partitions[partition];
      const Value sub_end = partitions[partition + 1U];
      const Value sample = sub_start + (sub_end - sub_start) / 2.0;
      const auto& selected =
          evaluate(interval1, sample) <= evaluate(interval2, sample)
              ? interval1
              : interval2;
      append_interval(result, selected, sub_start, sub_end);
    }
  }
  result.remove_duplicates();
  LOG_INFO("combine: {} intervals", result.intervals.size());
  return result;
}

void KineticSolution::remove_duplicates() {
  if (intervals.empty()) {
    return;
  }
  std::vector<SolutionInterval> merged;
  merged.reserve(intervals.size());
  for (const auto& interval : intervals) {
    if (!merged.empty() &&
        std::abs(merged.back().t_end - interval.t_start) <=
            kIntervalTolerance &&
        same_coefficients(merged.back(), interval)) {
      merged.back().t_end = interval.t_end;
    } else {
      merged.push_back(interval);
    }
  }
  intervals = std::move(merged);
}

KineticSolution KineticSolution::partial_extend(
    const KineticSolution& new_solution, const KineticSolution& current,
    ObjectiveType objective_type, SolverBudget* budget) {
  if (!new_solution.is_well_formed() || !current.is_well_formed()) {
    throw std::invalid_argument("partial_extend requires well-formed solutions");
  }
  const Value start = std::max(new_solution.intervals.front().t_start,
                               current.intervals.front().t_start);
  const Value end = std::min(new_solution.intervals.back().t_end,
                             current.intervals.back().t_end);
  KineticSolution result;
  result.objective = objective_type;
  if (objective_type == ObjectiveType::MIN_MAX ||
      objective_type == ObjectiveType::MIN_MAX_SUM) {
    // MinMax refinement is peak-based: the actual decision is made later by
    // `combine`, which constructs the pointwise lower envelope.
    // Carrying the whole candidate through the common domain preserves the
    // correct MinMax semantics and avoids rejecting a candidate solely because
    // its cumulative integral is larger than the incumbent's.
    result = new_solution;
    result.objective = objective_type;
    if (start > result.intervals.front().t_start ||
        end < result.intervals.back().t_end) {
      KineticSolution clipped;
      clipped.objective = objective_type;
      for (const auto& interval : result.intervals) {
        if (budget != nullptr) {
          budget->checkpoint();
        }
        const Value interval_start = std::max(start, interval.t_start);
        const Value interval_end = std::min(end, interval.t_end);
        if (interval_end <= interval_start) {
          continue;
        }
        append_interval(clipped, interval, interval_start, interval_end);
      }
      clipped.remove_duplicates();
      return clipped;
    }
    return result;
  }
  if (objective_type == ObjectiveType::MIN_SUM) {
    result = new_solution;
    result.objective = objective_type;
    return result;
  }
  throw std::invalid_argument("partial_extend received an unknown objective");
}

double KineticSolution::cost_at(double time) const {
  if (intervals.empty()) {
    throw std::runtime_error("kinetic solution has no intervals");
  }
  if (!std::isfinite(time) || time < intervals.front().t_start ||
      time > intervals.back().t_end) {
    throw std::out_of_range("requested time is outside the solution");
  }
  auto upper = std::upper_bound(
      intervals.begin(), intervals.end(), time,
      [](Value value, const SolutionInterval& interval) {
        return value < interval.t_start;
      });
  if (upper == intervals.begin()) {
    upper = intervals.begin();
  } else {
    --upper;
  }
  if (time > upper->t_end) {
    throw std::out_of_range("requested time is outside solution intervals");
  }
  return evaluate(*upper, time);
}

double KineticSolution::integral_on(double t_start, double t_end) const {
  validate_interval_request(*this, t_start, t_end);
  Value integral = 0.0;
  for (const auto& interval : intervals) {
    const Value lower = std::max(t_start, interval.t_start);
    const Value upper = std::min(t_end, interval.t_end);
    if (upper <= lower) {
      continue;
    }
    integral += (interval.a / 3.0) *
                    (upper * upper * upper - lower * lower * lower) +
                (interval.b / 2.0) *
                    (upper * upper - lower * lower) +
                interval.c * (upper - lower);
  }
  return integral;
}

double KineticSolution::total_integral() const {
  if (intervals.empty()) {
    throw std::runtime_error("kinetic solution has no intervals");
  }
  return integral_on(intervals.front().t_start, intervals.back().t_end);
}

double KineticSolution::peak_cost() const {
  if (intervals.empty()) {
    throw std::runtime_error("kinetic solution has no intervals");
  }
  Value maximum = -std::numeric_limits<Value>::infinity();
  for (const auto& interval : intervals) {
    maximum = std::max(maximum, evaluate(interval, interval.t_start));
    maximum = std::max(maximum, evaluate(interval, interval.t_end));
    if (interval.a < 0.0) {
      const Value vertex = -interval.b / (2.0 * interval.a);
      if (vertex > interval.t_start && vertex < interval.t_end) {
        maximum = std::max(maximum, evaluate(interval, vertex));
      }
    }
  }
  return maximum;
}

double KineticSolution::peak_time() const {
  if (intervals.empty()) {
    throw std::runtime_error("kinetic solution has no intervals");
  }
  Value maximum = -std::numeric_limits<Value>::infinity();
  Value time_of_maximum = intervals.front().t_start;
  const auto consider = [&maximum, &time_of_maximum](
                            const SolutionInterval& interval, Value time) {
    const Value cost = evaluate(interval, time);
    if (cost > maximum) {
      maximum = cost;
      time_of_maximum = time;
    }
  };
  for (const auto& interval : intervals) {
    consider(interval, interval.t_start);
    consider(interval, interval.t_end);
    if (interval.a < 0.0) {
      const Value vertex = -interval.b / (2.0 * interval.a);
      if (vertex > interval.t_start && vertex < interval.t_end) {
        consider(interval, vertex);
      }
    }
  }
  return time_of_maximum;
}

bool KineticSolution::is_well_formed() const {
  if (intervals.empty()) {
    return false;
  }
  const Index station_count = intervals.front().supporting_point.size();
  const Index point_count = intervals.front().assigned_points.size();
  for (Index index = 0; index < intervals.size(); ++index) {
    const auto& interval = intervals[index];
    if (!std::isfinite(interval.t_start) || !std::isfinite(interval.t_end) ||
        interval.t_start >= interval.t_end ||
        interval.supporting_point.size() != station_count ||
        interval.assigned_points.size() != point_count ||
        !std::isfinite(interval.a) || !std::isfinite(interval.b) ||
        !std::isfinite(interval.c)) {
      return false;
    }
    if (index > 0U &&
        intervals[index - 1U].t_end != interval.t_start) {
      return false;
    }
  }
  return true;
}

void KineticSolution::dump() const {
  for (const auto& interval : intervals) {
    LOG_INFO(
        "solution interval [{:.9g}, {:.9g}]: a={:.9g}, b={:.9g}, c={:.9g}",
        interval.t_start, interval.t_end, interval.a, interval.b, interval.c);
  }
}
}
