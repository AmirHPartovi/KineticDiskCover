#pragma once

#include "kdc/solver_interface.hpp"

#include <memory>
#include <string>
#include <vector>

namespace kdc {
struct KontBackendDiagnostics {
  std::string root;
  std::string root_source;
  std::string copt_header;
  std::string copt_library;
  std::string kont_header;
  std::string kont_cpp_header;
  std::string kont_cpp_library;
  std::string include_directory;
  std::string library;
  std::string architecture;
  std::string native_api;
  std::string compile_check;
  std::string compile_failure_reason;
  std::string runtime_probe_failure_reason;
  std::string version;
  bool runtime_probe_passed{false};
  bool license_runtime_initialization_passed{false};
  bool native_backend_available{false};
};

class KontSolver final : public ILPSolver {
 public:
  KontSolver();
  ~KontSolver() override;
  static KontBackendDiagnostics backend_diagnostics();
  static bool native_backend_compiled();
  static bool probe_native_backend();
  static void require_native_backend();
  void require_native_for_solves();

  ILPResult solve(const Eigen::VectorXd& c,
                  const Eigen::SparseMatrix<double>& A,
                  const Eigen::VectorXd& b,
                  const std::vector<int>& integer_vars,
                  double time_limit_sec, double gap_target) override;
  std::string name() const override;

 private:
  bool native_required_{false};
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
