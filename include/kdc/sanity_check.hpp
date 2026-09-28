#pragma once

#include "kdc/istatic_solver.hpp"
#include "kdc/solver_interface.hpp"

#include <string>
#include <vector>

namespace kdc {

struct SanityCheckItem {
  std::string name;
  bool passed{false};
  std::string detail;
  double duration_sec{0.0};
};

struct SanityCheckReport {
  std::vector<SanityCheckItem> items;
  int num_passed{0};
  int num_failed{0};
  bool all_passed() const { return num_failed == 0; }
  void write_markdown(const std::string& path) const;
};

class SanityChecker {
 public:
  static SanityCheckReport run(ILPSolver* ilp);

 private:
  static SanityCheckItem check_registry_populated();
  static SanityCheckItem check_algorithm_callable(const std::string& name,
                                                  ILPSolver* ilp);
  static SanityCheckItem check_all_algorithms_on_dummy(ILPSolver* ilp);
  static SanityCheckItem check_verifier_accepts_all(ILPSolver* ilp);
  static SanityCheckItem check_minmax_pipeline(ILPSolver* ilp);
  static SanityCheckItem check_minsum_pipeline(ILPSolver* ilp);
  static SanityCheckItem check_serializer_roundtrip();
  static SanityCheckItem check_data_instances_directory(const std::string& dir);
};

}  // namespace kdc
