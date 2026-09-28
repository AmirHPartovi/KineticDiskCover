#pragma once

#include "kdc/istatic_solver.hpp"
#include "kdc/solver_interface.hpp"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace kdc {
class StaticSolverRegistry {
 public:
  using Factory = std::function<std::unique_ptr<IStaticSolver>(ILPSolver*)>;

  static void register_solver(const std::string& name, Factory factory);
  static std::unique_ptr<IStaticSolver> create(const std::string& name,
                                               ILPSolver* ilp_solver);
  static std::vector<std::string> list();
  static void register_builtins();
};
}
