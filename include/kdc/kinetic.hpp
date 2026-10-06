#pragma once

#include "kdc/solver_budget.hpp"
#include "kdc/types.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace kdc {
enum class KineticEventEngine {
  REFERENCE_EXHAUSTIVE
};

enum class KineticEventType {
  SUPPORT_CHANGE,
  HANDOVER,
  ACTIVE_SUPPORT_MOTION_BREAKPOINT
};

struct KineticEventTraceEntry {
  double time{0.0};
  KineticEventType type{KineticEventType::SUPPORT_CHANGE};
  int station_id{-1};
  int old_support{-1};
  int new_support{-1};
  int from_station{-1};
  int to_station{-1};
  int affected_point{-1};
  std::string tie_breaking_outcome;
  int from_station_support_after{-1};
  int to_station_support_after{-1};
};

struct KineticEventDiagnostics {
  std::uint64_t station_support_event_searches{0};
  std::uint64_t point_vs_support_comparisons{0};
  std::uint64_t trajectory_segment_pair_examinations{0};
  std::uint64_t quadratic_equations_solved{0};
  std::uint64_t real_roots_found{0};
  std::uint64_t candidate_roots_rejected{0};
  std::uint64_t selected_support_events{0};
  std::uint64_t handover_event_checks{0};
  std::uint64_t solution_intervals_generated{0};
  std::uint64_t total_extension_nanoseconds{0};
  std::uint64_t support_event_detection_nanoseconds{0};
  std::uint64_t handover_detection_nanoseconds{0};
  std::uint64_t event_detection_nanoseconds{0};
  std::uint64_t interval_construction_nanoseconds{0};
  std::uint64_t raw_trajectory_breakpoints{0};
  std::vector<KineticEventTraceEntry> trace;
};

struct SupportChangeEvent {
  double time{-1.0};
  int station_id{-1};
  int new_supporting_point{-1};
  bool valid{false};
};

struct HandoverEvent {
  double time{-1.0};
  int from_station{-1};
  int to_station{-1};
  int point_id{-1};
  int new_support_from{-1};
  int new_support_to{-1};
  bool valid{false};
};

class KineticCore {
 public:
  static std::vector<double> solve_quadratic(
      double a, double b, double c, double eps_a = 1e-12,
      double eps_b = 1e-12, double eps_disc = 1e-12);

  static std::vector<SupportChangeEvent> find_support_changes(
      const Instance& instance, int station_id, int current_support,
      double t_start, double t_end, bool forward,
      SolverBudget* budget = nullptr,
      KineticEventEngine engine =
          KineticEventEngine::REFERENCE_EXHAUSTIVE,
      KineticEventDiagnostics* diagnostics = nullptr);

  static SupportChangeEvent find_next_event(
      const Instance& instance, const std::vector<int>& current_supports,
      double t_start, double t_end, bool forward,
      SolverBudget* budget = nullptr,
      KineticEventEngine engine =
          KineticEventEngine::REFERENCE_EXHAUSTIVE,
      KineticEventDiagnostics* diagnostics = nullptr);

  static int resolve_degeneracy(const Instance& instance, int station_id,
                                const std::vector<int>& candidates, double t,
                                SolverBudget* budget = nullptr);

  static std::vector<HandoverEvent> find_handovers(
      const Instance& instance, int station_from, int station_to,
      const std::vector<int>& current_supports,
      const std::vector<int>& assigned_points, double t_start, double t_end,
      bool forward, SolverBudget* budget = nullptr,
      KineticEventEngine engine =
          KineticEventEngine::REFERENCE_EXHAUSTIVE,
      KineticEventDiagnostics* diagnostics = nullptr);
  static std::vector<HandoverEvent> find_handovers_from(
      const Instance& instance, int station_from,
      const std::vector<int>& current_supports,
      const std::vector<int>& assigned_points, double t_start, double t_end,
      bool forward, SolverBudget* budget = nullptr,
      KineticEventEngine engine =
          KineticEventEngine::REFERENCE_EXHAUSTIVE,
      KineticEventDiagnostics* diagnostics = nullptr);

  static HandoverEvent find_next_handover(
      const Instance& instance, const std::vector<int>& current_supports,
      const std::vector<int>& assigned_points, double t_start, double t_end,
      bool forward,
      SolverBudget* budget = nullptr,
      KineticEventEngine engine =
          KineticEventEngine::REFERENCE_EXHAUSTIVE,
      KineticEventDiagnostics* diagnostics = nullptr);

  static int second_furthest_assigned(
      const Instance& instance, int station_id,
      const std::vector<int>& assigned_points, double t,
      SolverBudget* budget = nullptr);
};
}
