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
};

class TraceWriter {
 public:
  static void write_csv(const std::vector<IterTrace>& trace,
                        const std::string& path);
  static std::vector<IterTrace> read_csv(const std::string& path);
  static void log_summary(const std::vector<IterTrace>& trace);
};
}  // namespace kdc
