#pragma once

#include "kdc/solver_interface.hpp"

namespace kdc {
class MockILPSolver final : public ILPSolver {
 public:
  struct Config {
    bool allow_optimal_claim{false};
  };

  MockILPSolver();
  explicit MockILPSolver(Config config);

  ILPResult solve(const Eigen::VectorXd& c,
                  const Eigen::SparseMatrix<double>& A,
                  const Eigen::VectorXd& b,
                  const std::vector<int>& integer_vars,
                  double time_limit_sec, double gap_target) override;
  std::string name() const override { return "mock"; }

 private:
  Config config_;
};
}  // namespace kdc
