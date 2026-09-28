#pragma once

#include "kdc/solver_interface.hpp"

#include <memory>
#include <string>
#include <vector>

namespace kdc {
class KontSolver final : public ILPSolver {
 public:
  KontSolver();
  ~KontSolver() override;

  ILPResult solve(const Eigen::VectorXd& c,
                  const Eigen::SparseMatrix<double>& A,
                  const Eigen::VectorXd& b,
                  const std::vector<int>& integer_vars,
                  double time_limit_sec, double gap_target) override;
  std::string name() const override { return "KONT"; }
};

class DummyLPAdapter final : public ILPSolver {
 public:
  ILPResult solve(const Eigen::VectorXd& c,
                  const Eigen::SparseMatrix<double>& A,
                  const Eigen::VectorXd& b,
                  const std::vector<int>& integer_vars,
                  double time_limit_sec, double gap_target) override;
  std::string name() const override { return "DummyLP"; }
};
}
