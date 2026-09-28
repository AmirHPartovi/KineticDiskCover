#pragma once

#include "kdc/types.hpp"

#include <vector>

namespace kdc {
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
      double t_start, double t_end, bool forward);

  static SupportChangeEvent find_next_event(
      const Instance& instance, const std::vector<int>& current_supports,
      double t_start, double t_end, bool forward);

  static int resolve_degeneracy(const Instance& instance, int station_id,
                                const std::vector<int>& candidates, double t);

  static std::vector<HandoverEvent> find_handovers(
      const Instance& instance, int station_from, int station_to,
      const std::vector<int>& current_supports, double t_start, double t_end,
      bool forward);

  static HandoverEvent find_next_handover(
      const Instance& instance, const std::vector<int>& current_supports,
      double t_start, double t_end, bool forward);

  static int second_furthest_assigned(
      const Instance& instance, int station_id,
      const std::vector<int>& assigned_points, double t);
};
}
