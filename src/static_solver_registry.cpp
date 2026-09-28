#include "kdc/static_solver_registry.hpp"

#include "kdc/algorithms/ip_static_solver.hpp"
#include "kdc/algorithms/branch_and_bound_solver.hpp"
#include "kdc/algorithms/brute_force_solver.hpp"
#include "kdc/algorithms/genetic_solver.hpp"
#include "kdc/algorithms/greedy_set_cover_solver.hpp"
#include "kdc/algorithms/lp_rounding_solver.hpp"
#include "kdc/algorithms/local_search_solver.hpp"
#include "kdc/algorithms/nn_static_solver.hpp"
#include "kdc/algorithms/primal_dual_solver.hpp"
#include "kdc/algorithms/simulated_annealing_solver.hpp"
#include "kdc/algorithms/shifting_strategy_solver.hpp"
#include "kdc/logging.hpp"

#include <algorithm>
#include <mutex>
#include <stdexcept>
#include <unordered_map>
#include <utility>

namespace kdc {
namespace {
std::unordered_map<std::string, StaticSolverRegistry::Factory>& registry() {
  static std::unordered_map<std::string, StaticSolverRegistry::Factory> value;
  return value;
}

std::mutex& registry_mutex() {
  static std::mutex value;
  return value;
}

std::once_flag builtin_flag;
}

void StaticSolverRegistry::register_solver(const std::string& name,
                                          Factory factory) {
  if (name.empty() || !factory) {
    throw std::invalid_argument("static solver registration is invalid");
  }
  std::lock_guard<std::mutex> lock(registry_mutex());
  const auto existing = registry().find(name);
  if (existing != registry().end()) {
    LOG_WARN("StaticSolverRegistry: replacing solver '{}'", name);
    existing->second = std::move(factory);
  } else {
    registry().emplace(name, std::move(factory));
  }
}

std::unique_ptr<IStaticSolver> StaticSolverRegistry::create(
    const std::string& name, ILPSolver* ilp_solver) {
  std::call_once(builtin_flag, register_builtins);
  Factory factory;
  {
    std::lock_guard<std::mutex> lock(registry_mutex());
    const auto found = registry().find(name);
    if (found == registry().end()) {
      LOG_ERROR("StaticSolverRegistry: unknown solver '{}'", name);
      return nullptr;
    }
    factory = found->second;
  }
  return factory(ilp_solver);
}

std::vector<std::string> StaticSolverRegistry::list() {
  std::call_once(builtin_flag, register_builtins);
  std::lock_guard<std::mutex> lock(registry_mutex());
  std::vector<std::string> names;
  names.reserve(registry().size());
  for (const auto& entry : registry()) {
    names.push_back(entry.first);
  }
  std::sort(names.begin(), names.end());
  return names;
}

void StaticSolverRegistry::register_builtins() {
  register_solver("nn", [](ILPSolver*) {
    return std::make_unique<NNStaticSolver>();
  });
  register_solver("ip-kont", [](ILPSolver* solver) {
    return std::make_unique<IPStaticSolver>(solver);
  });
  register_solver("brute-force", [](ILPSolver*) {
    return std::make_unique<BruteForceSolver>();
  });
  register_solver("branch-and-bound", [](ILPSolver* solver) {
    return std::make_unique<BranchAndBoundSolver>(solver);
  });
  register_solver("greedy", [](ILPSolver*) {
    return std::make_unique<GreedySetCoverSolver>();
  });
  register_solver("lp-rounding", [](ILPSolver* solver) {
    return std::make_unique<LPRoundingSolver>(solver);
  });
  register_solver("primal-dual", [](ILPSolver*) {
    return std::make_unique<PrimalDualSolver>();
  });
  register_solver("local-search", [](ILPSolver*) {
    return std::make_unique<LocalSearchSolver>();
  });
  register_solver("sa", [](ILPSolver*) {
    return std::make_unique<SimulatedAnnealingSolver>();
  });
  register_solver("genetic", [](ILPSolver*) {
    return std::make_unique<GeneticSolver>();
  });
  register_solver("shifting", [](ILPSolver* solver) {
    return std::make_unique<ShiftingStrategySolver>(solver);
  });
}
}
