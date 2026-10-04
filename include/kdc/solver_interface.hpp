#pragma once

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wsign-conversion"
#pragma clang diagnostic ignored "-Wunused-but-set-variable"
#endif
#include <Eigen/Core>
#include <Eigen/SparseCore>
#if defined(__clang__)
#pragma clang diagnostic pop
#endif

#include <string>
#include <vector>

namespace kdc {
struct ILPResult {
  enum class Status {
    OPTIMAL,
    FEASIBLE,
    INFEASIBLE,
    UNBOUNDED,
    TIME_LIMIT,
    ERROR
  };

  Status status{Status::ERROR};
  std::vector<double> x;
  double objective{0.0};
  double lower_bound{0.0};
  double solve_time_sec{0.0};
  double gap{0.0};
  std::string solver_message;
  std::string actual_backend;
  std::string solver_version;
  bool native_backend_used{false};
  bool fallback_used{false};
  bool license_runtime_initialization_passed{false};
};

class ILPSolver {
 public:
  virtual ~ILPSolver() = default;
  virtual ILPResult solve(const Eigen::VectorXd& c,
                          const Eigen::SparseMatrix<double>& A,
                          const Eigen::VectorXd& b,
                          const std::vector<int>& integer_vars,
                          double time_limit_sec, double gap_target) = 0;
  virtual std::string name() const = 0;
};
}
