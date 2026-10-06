#pragma once

#include <string>
#include <vector>

namespace kdc {
struct IterTrace {
  int iter{0};
  double t_max{0.0};
  double objective_value{0.0};
  double lower_bound{0.0};
  double gap{0.0};
  double wall_time_sec{0.0};
  int num_ip_solves{0};
  double candidate_peak{0.0};
  double combined_peak{0.0};
  double peak_improvement{0.0};
  double static_cost_at_peak_time{0.0};
  double static_lower_bound{0.0};
  double static_upper_bound{0.0};
  double certified_gap{0.0};
  double heuristic_gap{0.0};
  std::string static_solver_status{"not_run"};
  std::string stop_reason{"pending"};
  bool static_solver_exact{false};
  bool candidate_accepted{false};
  bool has_certified_gap{false};
};

class TraceWriter {
 public:
  static void write_csv(const std::vector<IterTrace>& trace,
                        const std::string& path);
  static std::vector<IterTrace> read_csv(const std::string& path);
  static void log_summary(const std::vector<IterTrace>& trace);
};
}  // namespace kdc
