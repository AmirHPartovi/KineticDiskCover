#pragma once

#include "kdc/solver_budget.hpp"
#include "kdc/types.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace kdc {
enum class KineticEventEngine {
  REFERENCE_EXHAUSTIVE,
  KINETIC_TOURNAMENT
};

enum class HandoverEvaluation {
  REFERENCE_GLOBAL,
  LOCAL_EXACT
};

bool event_times_simultaneous(double first, double second) noexcept;

class KineticFarthestTournament {
 public:
  KineticFarthestTournament() = default;

  void initialize(const Instance& instance, int station_id,
                  const std::vector<int>& assigned_points, double time,
                  SolverBudget* budget = nullptr);
  void insert(int point_id, double time, SolverBudget* budget = nullptr);
  void erase(int point_id, double time, SolverBudget* budget = nullptr);
  void update_motion(int point_id, double time,
                    SolverBudget* budget = nullptr);
  int current_winner() const;
  int second_winner() const;
  int best_except(int point_id) const;
  double next_event_time(double time, bool forward = true,
                         SolverBudget* budget = nullptr) const;
  bool process_until(double time, SolverBudget* budget = nullptr);
  bool validate(double time, SolverBudget* budget = nullptr) const;

 private:
  struct Node {
    explicit Node(int point_id);
    int point_id{-1};
    int winner{-1};
    int second_winner{-1};
    std::unique_ptr<Node> left;
    std::unique_ptr<Node> right;
  };

  void rebuild_tree(double time, SolverBudget* budget = nullptr);
  void recompute_node(Node* node, double time, SolverBudget* budget = nullptr);

  const Instance* instance_{nullptr};
  int station_id_{-1};
  std::vector<int> points_;
  std::unique_ptr<Node> root_;
  int winner_{-1};
  int second_winner_{-1};
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
  std::uint64_t handover_local_support_points_inspected{0};
  std::uint64_t handover_local_acceptances{0};
  std::uint64_t handover_global_fallbacks{0};
  std::uint64_t handover_global_point_scans{0};
  std::uint64_t source_receiver_pair_count{0};
  std::uint64_t external_challenge_certificates{0};
  std::uint64_t external_challenge_updates{0};
  std::uint64_t handover_queue_pushes{0};
  std::uint64_t stale_handover_events{0};
  std::uint64_t local_support_queries{0};
  std::uint64_t second_support_queries{0};
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
  // Support comparison returns positive when first is farther, negative when
  // second is farther, and zero only when the point ids are identical.
  // Exact-time distances use a scale-aware 64*machine-epsilon tolerance and
  // then the lower point id. Directional comparison examines, in order, the
  // constant, linear, and quadratic coefficients of the local squared-
  // distance difference; the first coefficient distinguishable under that
  // same tolerance determines the winner. The linear coefficient is
  // multiplied by +1 forward or -1 backward. If linear terms tie, quadratic
  // terms decide; if all terms tie, the lower point id wins.
  static int compare_support_at_time(const Instance& instance, int station_id,
                                    int first_point, int second_point,
                                    double time,
                                    SolverBudget* budget = nullptr);
  static int compare_support_directional_limit(
      const Instance& instance, int station_id, int first_point,
      int second_point, double time, bool forward,
      SolverBudget* budget = nullptr);
  static int resolve_support_at_time(const Instance& instance, int station_id,
                                     const std::vector<int>& candidates,
                                     double time,
                                     SolverBudget* budget = nullptr);
  static int resolve_support_directional_limit(
      const Instance& instance, int station_id,
      const std::vector<int>& candidates, double time, bool forward,
      SolverBudget* budget = nullptr);

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
      KineticEventDiagnostics* diagnostics = nullptr,
      HandoverEvaluation evaluation = HandoverEvaluation::LOCAL_EXACT);
  static std::vector<HandoverEvent> find_handovers_from(
      const Instance& instance, int station_from,
      const std::vector<int>& current_supports,
      const std::vector<int>& assigned_points, double t_start, double t_end,
      bool forward, SolverBudget* budget = nullptr,
      KineticEventEngine engine =
          KineticEventEngine::REFERENCE_EXHAUSTIVE,
      KineticEventDiagnostics* diagnostics = nullptr,
      HandoverEvaluation evaluation = HandoverEvaluation::LOCAL_EXACT);

  static SupportChangeEvent find_external_challenge(
      const Instance& instance, int receiver_station, int receiver_support,
      int challenger, double t_start, double t_end, bool forward,
      SolverBudget* budget = nullptr,
      KineticEventEngine engine =
          KineticEventEngine::KINETIC_TOURNAMENT,
      KineticEventDiagnostics* diagnostics = nullptr);

  static HandoverEvent evaluate_handover_candidate(
      const Instance& instance, int station_from, int station_to,
      int point_id, int source_support_before, int receiver_support_before,
      int source_support_after_removal, int receiver_support_without_point,
      const std::vector<int>& assigned_points, double time, bool forward,
      SolverBudget* budget = nullptr,
      KineticEventDiagnostics* diagnostics = nullptr,
      HandoverEvaluation evaluation = HandoverEvaluation::LOCAL_EXACT);

  static HandoverEvent find_next_handover(
      const Instance& instance, const std::vector<int>& current_supports,
      const std::vector<int>& assigned_points, double t_start, double t_end,
      bool forward,
      SolverBudget* budget = nullptr,
      KineticEventEngine engine =
          KineticEventEngine::REFERENCE_EXHAUSTIVE,
      KineticEventDiagnostics* diagnostics = nullptr,
      HandoverEvaluation evaluation = HandoverEvaluation::LOCAL_EXACT);

  static int second_furthest_assigned(
      const Instance& instance, int station_id,
      const std::vector<int>& assigned_points, double t,
      SolverBudget* budget = nullptr);
};
}
