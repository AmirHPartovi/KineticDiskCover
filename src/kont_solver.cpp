#include "kdc/kont_solver.hpp"

#include "kdc/kont_build_config.hpp"
#include "kdc/logging.hpp"

#ifdef KDC_HAS_KONT_NATIVE_PLUGIN
#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif
#endif

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <limits>
#include <mutex>
#include <numeric>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace kdc {
using Index = std::size_t;

namespace {
constexpr double kEpsilon = 1e-9;

struct NativeRuntimeState {
  std::atomic<bool> initialization_unavailable{false};
  std::mutex mutex;
  std::string failure_reason;
};

NativeRuntimeState& native_runtime_state() {
  static NativeRuntimeState state;
  return state;
}

const char* status_name(ILPResult::Status status) {
  switch (status) {
    case ILPResult::Status::OPTIMAL:
      return "OPTIMAL";
    case ILPResult::Status::FEASIBLE:
      return "FEASIBLE";
    case ILPResult::Status::INFEASIBLE:
      return "INFEASIBLE";
    case ILPResult::Status::UNBOUNDED:
      return "UNBOUNDED";
    case ILPResult::Status::TIME_LIMIT:
      return "TIME_LIMIT";
    case ILPResult::Status::ERROR:
      return "ERROR";
  }
  return "ERROR";
}

#ifdef KDC_HAS_KONT_NATIVE_PLUGIN
using NativeSolveFunction = bool (*)(
    const Eigen::VectorXd*, const Eigen::SparseMatrix<double>*,
    const Eigen::VectorXd*, const std::vector<int>*, double, double,
    ILPResult*) noexcept;

struct NativePlugin {
  NativeSolveFunction solve{nullptr};
  std::string load_error;
};

const NativePlugin& native_plugin() {
  static const NativePlugin plugin = [] {
    NativePlugin loaded;
#ifdef _WIN32
    const HMODULE handle = LoadLibraryA(KDC_KONT_NATIVE_PLUGIN_PATH);
    if (handle == nullptr) {
      loaded.load_error = "LoadLibrary failed with error " +
                          std::to_string(GetLastError());
      return loaded;
    }
    loaded.solve = reinterpret_cast<NativeSolveFunction>(
        GetProcAddress(handle, "kdc_kont_native_solve"));
#else
    void* handle =
        dlopen(KDC_KONT_NATIVE_PLUGIN_PATH, RTLD_NOW | RTLD_LOCAL);
    if (handle == nullptr) {
      const char* error = dlerror();
      loaded.load_error = error == nullptr ? "dlopen failed" : error;
      return loaded;
    }
    dlerror();
    loaded.solve = reinterpret_cast<NativeSolveFunction>(
        dlsym(handle, "kdc_kont_native_solve"));
    const char* error = dlerror();
    if (error != nullptr) {
      loaded.load_error = error;
      return loaded;
    }
#endif
    if (loaded.solve == nullptr) {
      loaded.load_error = "native adapter does not export its solve entry point";
    }
    return loaded;
  }();
  return plugin;
}

ILPResult solve_with_native_plugin(
    const Eigen::VectorXd& costs,
    const Eigen::SparseMatrix<double>& matrix, const Eigen::VectorXd& rhs,
    const std::vector<int>& integer_vars, double time_limit_sec,
    double gap_target) {
  ILPResult result;
  result.actual_backend = "KONT/COPT";
  result.solver_version = kont_build_config::version;
  const NativePlugin& plugin = native_plugin();
  if (plugin.solve == nullptr) {
    result.status = ILPResult::Status::ERROR;
    result.solver_message =
        "native KONT/COPT adapter could not be loaded: " + plugin.load_error;
    return result;
  }
  if (!plugin.solve(&costs, &matrix, &rhs, &integer_vars, time_limit_sec,
                    gap_target, &result)) {
    result.status = ILPResult::Status::ERROR;
    result.solver_message =
        "native KONT/COPT adapter rejected the solve request";
  }
  return result;
}
#endif

enum class LPStatus { OPTIMAL, INFEASIBLE, UNBOUNDED };

class Simplex {
 public:
  Simplex(const std::vector<std::vector<double>>& matrix,
          const std::vector<double>& rhs, const std::vector<double>& objective)
      : rows_(static_cast<int>(rhs.size())),
        columns_(static_cast<int>(objective.size())),
        basic_(static_cast<Index>(rows_)),
        nonbasic_(static_cast<Index>(columns_) + 1U),
        tableau_(static_cast<Index>(rows_) + 2U,
                 std::vector<double>(static_cast<Index>(columns_) + 2U, 0.0)) {
    for (int row = 0; row < rows_; ++row) {
      for (int column = 0; column < columns_; ++column) {
        tableau_[static_cast<Index>(row)][static_cast<Index>(column)] =
            matrix[static_cast<Index>(row)][static_cast<Index>(column)];
      }
      basic_[static_cast<Index>(row)] = columns_ + row;
      tableau_[static_cast<Index>(row)][static_cast<Index>(columns_)] = -1.0;
      tableau_[static_cast<Index>(row)][static_cast<Index>(columns_ + 1)] =
          rhs[static_cast<Index>(row)];
    }
    for (int column = 0; column < columns_; ++column) {
      nonbasic_[static_cast<Index>(column)] = column;
      tableau_[static_cast<Index>(rows_)][static_cast<Index>(column)] =
          -objective[static_cast<Index>(column)];
    }
    nonbasic_[static_cast<Index>(columns_)] = -1;
    tableau_[static_cast<Index>(rows_ + 1)][static_cast<Index>(columns_)] = 1.0;
  }

  LPStatus solve(std::vector<double>& solution, double& maximum) {
    if (columns_ == 0) {
      for (int row = 0; row < rows_; ++row) {
        if (tableau_[static_cast<Index>(row)]
                    [static_cast<Index>(columns_ + 1)] < -kEpsilon) {
          return LPStatus::INFEASIBLE;
        }
      }
      solution.clear();
      maximum = 0.0;
      return LPStatus::OPTIMAL;
    }

    int lowest_row = 0;
    for (int row = 1; row < rows_; ++row) {
      if (tableau_[static_cast<Index>(row)]
                  [static_cast<Index>(columns_ + 1)] <
          tableau_[static_cast<Index>(lowest_row)]
                  [static_cast<Index>(columns_ + 1)]) {
        lowest_row = row;
      }
    }
    if (rows_ > 0 &&
        tableau_[static_cast<Index>(lowest_row)]
                [static_cast<Index>(columns_ + 1)] < -kEpsilon) {
      pivot(lowest_row, columns_);
      if (!simplex(1) ||
          tableau_[static_cast<Index>(rows_ + 1)]
                  [static_cast<Index>(columns_ + 1)] < -kEpsilon) {
        return LPStatus::INFEASIBLE;
      }
      for (int row = 0; row < rows_; ++row) {
        if (basic_[static_cast<Index>(row)] == -1) {
          int entering = 0;
          for (int column = 1; column <= columns_; ++column) {
            if (tableau_[static_cast<Index>(row)]
                        [static_cast<Index>(column)] <
                tableau_[static_cast<Index>(row)]
                        [static_cast<Index>(entering)] - kEpsilon) {
              entering = column;
            }
          }
          pivot(row, entering);
        }
      }
    }
    if (!simplex(2)) {
      return LPStatus::UNBOUNDED;
    }

    solution.assign(static_cast<Index>(columns_), 0.0);
    for (int row = 0; row < rows_; ++row) {
      const int variable = basic_[static_cast<Index>(row)];
      if (variable >= 0 && variable < columns_) {
        solution[static_cast<Index>(variable)] =
            tableau_[static_cast<Index>(row)]
                    [static_cast<Index>(columns_ + 1)];
      }
    }
    maximum = tableau_[static_cast<Index>(rows_)]
                      [static_cast<Index>(columns_ + 1)];
    return LPStatus::OPTIMAL;
  }

 private:
  void pivot(int row, int column) {
    const double inverse =
        1.0 / tableau_[static_cast<Index>(row)][static_cast<Index>(column)];
    for (int other_row = 0; other_row < rows_ + 2; ++other_row) {
      if (other_row == row) {
        continue;
      }
      for (int other_column = 0; other_column < columns_ + 2;
           ++other_column) {
        if (other_column == column) {
          continue;
        }
        tableau_[static_cast<Index>(other_row)]
                [static_cast<Index>(other_column)] -=
            tableau_[static_cast<Index>(row)]
                    [static_cast<Index>(other_column)] *
            tableau_[static_cast<Index>(other_row)]
                    [static_cast<Index>(column)] * inverse;
      }
    }
    for (int other_column = 0; other_column < columns_ + 2; ++other_column) {
      if (other_column != column) {
        tableau_[static_cast<Index>(row)]
                [static_cast<Index>(other_column)] *= inverse;
      }
    }
    for (int other_row = 0; other_row < rows_ + 2; ++other_row) {
      if (other_row != row) {
        tableau_[static_cast<Index>(other_row)][static_cast<Index>(column)] *=
            -inverse;
      }
    }
    tableau_[static_cast<Index>(row)][static_cast<Index>(column)] = inverse;
    std::swap(basic_[static_cast<Index>(row)],
              nonbasic_[static_cast<Index>(column)]);
  }

  bool simplex(int phase) {
    const int objective_row = phase == 1 ? rows_ + 1 : rows_;
    for (;;) {
      int entering = -1;
      for (int column = 0; column <= columns_; ++column) {
        if (phase == 2 && nonbasic_[static_cast<Index>(column)] == -1) {
          continue;
        }
        if (entering == -1 ||
            tableau_[static_cast<Index>(objective_row)]
                    [static_cast<Index>(column)] <
                tableau_[static_cast<Index>(objective_row)]
                        [static_cast<Index>(entering)] - kEpsilon ||
            (std::abs(tableau_[static_cast<Index>(objective_row)]
                              [static_cast<Index>(column)] -
                      tableau_[static_cast<Index>(objective_row)]
                              [static_cast<Index>(entering)]) <= kEpsilon &&
             nonbasic_[static_cast<Index>(column)] <
                 nonbasic_[static_cast<Index>(entering)])) {
          entering = column;
        }
      }
      if (entering == -1 ||
          tableau_[static_cast<Index>(objective_row)]
                  [static_cast<Index>(entering)] >= -kEpsilon) {
        return true;
      }

      int leaving = -1;
      for (int row = 0; row < rows_; ++row) {
        const double coefficient =
            tableau_[static_cast<Index>(row)][static_cast<Index>(entering)];
        if (coefficient <= kEpsilon) {
          continue;
        }
        if (leaving == -1) {
          leaving = row;
          continue;
        }
        const double ratio =
            tableau_[static_cast<Index>(row)]
                    [static_cast<Index>(columns_ + 1)] /
            coefficient;
        const double best_ratio =
            tableau_[static_cast<Index>(leaving)]
                    [static_cast<Index>(columns_ + 1)] /
            tableau_[static_cast<Index>(leaving)]
                    [static_cast<Index>(entering)];
        if (ratio < best_ratio - kEpsilon ||
            (std::abs(ratio - best_ratio) <= kEpsilon &&
             basic_[static_cast<Index>(row)] <
                 basic_[static_cast<Index>(leaving)])) {
          leaving = row;
        }
      }
      if (leaving == -1) {
        return false;
      }
      pivot(leaving, entering);
    }
  }

  int rows_;
  int columns_;
  std::vector<int> basic_;
  std::vector<int> nonbasic_;
  std::vector<std::vector<double>> tableau_;
};

struct LPAnswer {
  LPStatus status{LPStatus::INFEASIBLE};
  std::vector<double> values;
  double objective{0.0};
};

LPAnswer solve_relaxation(const Eigen::VectorXd& costs,
                          const Eigen::SparseMatrix<double>& matrix,
                          const Eigen::VectorXd& rhs,
                          const std::vector<int>& fixed_vars = {},
                          const std::vector<int>& fixed_values = {}) {
  const int variables = static_cast<int>(costs.size());
  const int constraints = static_cast<int>(matrix.rows());
  std::vector<std::vector<double>> dense;
  std::vector<double> bounds;
  dense.reserve(static_cast<Index>(constraints) +
                static_cast<Index>(variables) +
                2U * fixed_vars.size());
  bounds.reserve(dense.capacity());

  for (int row = 0; row < constraints; ++row) {
    std::vector<double> coefficients(static_cast<Index>(variables), 0.0);
    for (int column = 0; column < variables; ++column) {
      coefficients[static_cast<Index>(column)] = -matrix.coeff(row, column);
    }
    dense.push_back(std::move(coefficients));
    bounds.push_back(-rhs[row]);
  }
  for (int variable = 0; variable < variables; ++variable) {
    std::vector<double> upper(static_cast<Index>(variables), 0.0);
    upper[static_cast<Index>(variable)] = 1.0;
    dense.push_back(std::move(upper));
    bounds.push_back(1.0);
  }
  for (Index index = 0; index < fixed_vars.size(); ++index) {
    std::vector<double> equality(static_cast<Index>(variables), 0.0);
    equality[static_cast<Index>(fixed_vars[index])] = 1.0;
    if (fixed_values[index] == 1) {
      for (double& coefficient : equality) {
        coefficient = -coefficient;
      }
      dense.push_back(std::move(equality));
      bounds.push_back(-1.0);
    } else {
      dense.push_back(std::move(equality));
      bounds.push_back(0.0);
    }
  }

  std::vector<double> maximize(static_cast<Index>(variables));
  for (int variable = 0; variable < variables; ++variable) {
    maximize[static_cast<Index>(variable)] = -costs[variable];
  }
  Simplex simplex(dense, bounds, maximize);
  LPAnswer answer;
  double max_value = 0.0;
  answer.status = simplex.solve(answer.values, max_value);
  if (answer.status == LPStatus::OPTIMAL) {
    answer.objective = -max_value;
  }
  return answer;
}

void validate_problem(const Eigen::VectorXd& costs,
                      const Eigen::SparseMatrix<double>& matrix,
                      const Eigen::VectorXd& rhs,
                      const std::vector<int>& integer_vars,
                      double time_limit_sec, double gap_target) {
  if (matrix.cols() != costs.size() || matrix.rows() != rhs.size()) {
    throw std::invalid_argument("ILP dimensions do not match");
  }
  if (!std::isfinite(time_limit_sec) || time_limit_sec <= 0.0 ||
      !std::isfinite(gap_target) || gap_target < 0.0) {
    throw std::invalid_argument("invalid ILP time limit or gap target");
  }
  if (!costs.allFinite() || !rhs.allFinite()) {
    throw std::invalid_argument("ILP objective and rhs must be finite");
  }
  for (Eigen::Index index = 0; index < matrix.nonZeros(); ++index) {
    if (!std::isfinite(matrix.valuePtr()[index])) {
      throw std::invalid_argument("ILP constraint coefficients must be finite");
    }
  }
  std::vector<bool> seen(static_cast<Index>(costs.size()), false);
  for (const int variable : integer_vars) {
    if (variable < 0 || variable >= costs.size() ||
        seen[static_cast<Index>(variable)]) {
      throw std::invalid_argument("integer variable index is invalid or repeated");
    }
    seen[static_cast<Index>(variable)] = true;
  }
}

ILPResult solve_fallback(const Eigen::VectorXd& costs,
                         const Eigen::SparseMatrix<double>& matrix,
                         const Eigen::VectorXd& rhs,
                         const std::vector<int>& integer_vars,
                         double time_limit_sec, double gap_target,
                         const std::string& backend) {
  validate_problem(costs, matrix, rhs, integer_vars, time_limit_sec, gap_target);
  const auto started = std::chrono::steady_clock::now();
  const double lower_bound = costs.cwiseMin(0.0).sum();
  ILPResult result;
  result.lower_bound = lower_bound;
  bool has_solution = false;
  bool timed_out = false;
  bool unbounded = false;
  const auto deadline = started +
                        std::chrono::duration_cast<
                            std::chrono::steady_clock::duration>(
                            std::chrono::duration<double>(time_limit_sec));

  std::vector<int> fixed_values(integer_vars.size(), -1);
  std::function<void()> search = [&]() {
    if (timed_out) {
      return;
    }
    if (std::chrono::steady_clock::now() >= deadline) {
      timed_out = true;
      return;
    }
    std::vector<int> node_vars;
    std::vector<int> node_values;
    node_vars.reserve(integer_vars.size());
    node_values.reserve(integer_vars.size());
    for (Index index = 0; index < integer_vars.size(); ++index) {
      if (fixed_values[index] != -1) {
        node_vars.push_back(integer_vars[index]);
        node_values.push_back(fixed_values[index]);
      }
    }
    const LPAnswer relaxation =
        solve_relaxation(costs, matrix, rhs, node_vars, node_values);
    if (relaxation.status == LPStatus::INFEASIBLE) {
      return;
    }
    if (relaxation.status == LPStatus::UNBOUNDED) {
      unbounded = true;
      timed_out = true;
      return;
    }
    if (has_solution &&
        relaxation.objective >= result.objective - kEpsilon) {
      return;
    }

    Index branch = integer_vars.size();
    double best_fractionality = -1.0;
    for (Index index = 0; index < integer_vars.size(); ++index) {
      if (fixed_values[index] != -1) {
        continue;
      }
      const double value =
          relaxation.values[static_cast<Index>(integer_vars[index])];
      const double fractionality = std::min(value, 1.0 - value);
      if (fractionality > kEpsilon &&
          fractionality > best_fractionality) {
        branch = index;
        best_fractionality = fractionality;
      }
    }
    if (branch == integer_vars.size()) {
      has_solution = true;
      result.objective = relaxation.objective;
      result.x = relaxation.values;
      for (const int variable : integer_vars) {
        double& value = result.x[static_cast<Index>(variable)];
        value = value >= 0.5 ? 1.0 : 0.0;
      }
      return;
    }

    fixed_values[branch] = 0;
    search();
    fixed_values[branch] = 1;
    search();
    fixed_values[branch] = -1;
  };

  search();
  result.solve_time_sec =
      std::chrono::duration<double>(std::chrono::steady_clock::now() - started)
          .count();
  if (unbounded) {
    result.status = ILPResult::Status::UNBOUNDED;
    result.solver_message = backend + ": relaxation is unbounded";
  } else if (timed_out) {
    result.status = ILPResult::Status::TIME_LIMIT;
    result.solver_message = backend + ": time limit reached";
  } else if (has_solution) {
    result.status = ILPResult::Status::OPTIMAL;
    result.lower_bound = result.objective;
    result.gap = std::abs(result.objective - result.lower_bound) /
                 std::max(1.0, std::abs(result.objective));
    result.solver_message = backend + ": optimal solution found";
  } else {
    result.status = ILPResult::Status::INFEASIBLE;
    result.solver_message = backend + ": model is infeasible";
  }
  return result;
}

#ifdef KDC_HAS_COPT_CPP_API
ILPResult solve_with_kont(const Eigen::VectorXd& costs,
                          const Eigen::SparseMatrix<double>& matrix,
                          const Eigen::VectorXd& rhs,
                          const std::vector<int>& integer_vars,
                          double time_limit_sec, double gap_target) {
  static copt::Env environment;
  ILPResult result;
  result.actual_backend = "KONT/COPT";
  result.solver_version = kont_build_config::version;
  result.native_backend_used = true;
  result.license_runtime_initialization_passed = true;
  copt::Model model = environment.CreateModel("kdc_ip");
  std::vector<copt::Var> variables;
  variables.reserve(static_cast<Index>(costs.size()));
  for (Eigen::Index column = 0; column < costs.size(); ++column) {
    const bool is_integer =
        std::find(integer_vars.begin(), integer_vars.end(),
                  static_cast<int>(column)) != integer_vars.end();
    variables.push_back(model.AddVar(
        0.0, 1.0, costs[column],
        is_integer ? COPT_BINARY : COPT_CONTINUOUS,
        "x" + std::to_string(column)));
  }
  for (Eigen::Index row = 0; row < matrix.rows(); ++row) {
    copt::Expr expression;
    for (Eigen::Index column = 0; column < matrix.cols(); ++column) {
      const double coefficient = matrix.coeff(row, column);
      if (coefficient != 0.0) {
        expression += coefficient * variables[static_cast<Index>(column)];
      }
    }
    model.AddConstr(expression, COPT_GREATER_EQUAL, rhs[row],
                    "c" + std::to_string(row));
  }
  model.SetDblParam(COPT_DBLPARAM_TIMELIMIT, time_limit_sec);
  model.SetDblParam(COPT_DBLPARAM_RELGAP, gap_target);
  model.SetIntParam(COPT_INTPARAM_LOGGING, 1);
  model.SetIntParam(COPT_INTPARAM_THREADS, 1);

  const int return_code = model.Solve();
  if (return_code != 0) {
    result.status = ILPResult::Status::ERROR;
    result.solver_message = model.GetMessage();
    LOG_ERROR("KONT: solve failed with code {}", return_code);
    model.clear();
    return result;
  }

  result.objective = model.GetDblAttr(COPT_DBLATTR_LPOBJVAL);
  result.lower_bound = model.GetDblAttr(COPT_DBLATTR_BESTBND);
  result.solve_time_sec = model.GetDblAttr(COPT_DBLATTR_SOLVINGTIME);
  result.gap = model.GetDblAttr(COPT_DBLATTR_MIPGAP);
  const int status = model.GetIntAttr(COPT_INTATTR_MIPSTATUS);
  switch (status) {
    case COPT_MIP_STATUS_OPTIMAL:
      result.status = ILPResult::Status::OPTIMAL;
      break;
    case COPT_MIP_STATUS_INFEASIBLE:
      result.status = ILPResult::Status::INFEASIBLE;
      break;
    case COPT_MIP_STATUS_UNBOUNDED:
      result.status = ILPResult::Status::UNBOUNDED;
      break;
    case COPT_MIP_STATUS_TIMELIMIT:
      result.status = ILPResult::Status::TIME_LIMIT;
      break;
    case COPT_MIP_STATUS_GAPLIMIT:
      result.status = ILPResult::Status::FEASIBLE;
      break;
    default:
      result.status = ILPResult::Status::ERROR;
      break;
  }
  if (result.status == ILPResult::Status::OPTIMAL ||
      result.status == ILPResult::Status::FEASIBLE ||
      result.status == ILPResult::Status::TIME_LIMIT) {
    result.x.resize(static_cast<Index>(costs.size()));
    for (Index column = 0; column < result.x.size(); ++column) {
      result.x[column] = variables[column].Get(COPT_DBLINFO_VALUE);
    }
  }
  result.solver_message = model.GetMessage();
  model.clear();
  return result;
}
#elif defined(KDC_HAS_KONT_CPP_API)
ILPResult solve_with_kont(const Eigen::VectorXd& costs,
                          const Eigen::SparseMatrix<double>& matrix,
                          const Eigen::VectorXd& rhs,
                          const std::vector<int>& integer_vars,
                          double time_limit_sec, double gap_target) {
  const auto started = std::chrono::steady_clock::now();
  ILPResult result;
  result.actual_backend = "KONT/COPT";
  result.solver_version = kont_build_config::version;
  try {
    static Envr environment = [] {
      if (const char* license_directory =
              std::getenv("KONT_LICENSE_DIR")) {
        if (license_directory[0] != '\0') {
          return Envr(license_directory);
        }
      }
      const std::filesystem::path root(kont_build_config::root);
      const std::filesystem::path installed_license_directory =
          root / "bin";
      if (std::filesystem::exists(installed_license_directory /
                                  "license.dat")) {
        return Envr(installed_license_directory.string().c_str());
      }
      return Envr();
    }();
    result.license_runtime_initialization_passed = true;
    Model model = environment.CreateModel("kdc_ip");
    std::vector<Var> variables;
    variables.reserve(static_cast<Index>(costs.size()));
    for (Eigen::Index column = 0; column < costs.size(); ++column) {
      const bool is_integer =
          std::find(integer_vars.begin(), integer_vars.end(),
                    static_cast<int>(column)) != integer_vars.end();
      variables.push_back(model.AddVar(
          0.0, 1.0, costs[column],
          is_integer ? KONT_BINARY : KONT_CONTINUOUS,
          ("x" + std::to_string(column)).c_str()));
    }
    for (Eigen::Index row = 0; row < matrix.rows(); ++row) {
      Expr expression;
      for (Eigen::Index column = 0; column < matrix.cols(); ++column) {
        const double coefficient = matrix.coeff(row, column);
        if (coefficient != 0.0) {
          expression.AddTerm(variables[static_cast<Index>(column)],
                             coefficient);
        }
      }
      model.AddConstr(expression, KONT_GREATER_EQUAL, rhs[row],
                      ("c" + std::to_string(row)).c_str());
    }
    model.SetDblParam(KONT_DBLPARAM_TIMELIMIT, time_limit_sec);
    model.SetDblParam(KONT_DBLPARAM_RELGAP, gap_target);
    model.SetIntParam(KONT_INTPARAM_LOGGING, 0);
    model.SetIntParam(KONT_INTPARAM_LOGTOCONSOLE, 0);
    model.SetIntParam(KONT_INTPARAM_THREADS, 1);
    result.native_backend_used = true;
    model.Solve();
    const int status = model.GetIntAttr(KONT_INTATTR_MIPSTATUS);
    const bool has_solution =
        model.GetIntAttr(KONT_INTATTR_HASMIPSOL) != 0;
    switch (status) {
      case KONT_MIPSTATUS_OPTIMAL:
        result.status = ILPResult::Status::OPTIMAL;
        break;
      case KONT_MIPSTATUS_INFEASIBLE:
        result.status = ILPResult::Status::INFEASIBLE;
        break;
      case KONT_MIPSTATUS_UNBOUNDED:
        result.status = ILPResult::Status::UNBOUNDED;
        break;
      case KONT_MIPSTATUS_TIMEOUT:
        result.status = ILPResult::Status::TIME_LIMIT;
        break;
      case KONT_MIPSTATUS_NODELIMIT:
      case KONT_MIPSTATUS_UNFINISHED:
      case KONT_MIPSTATUS_INTERRUPTED:
        result.status = has_solution ? ILPResult::Status::FEASIBLE
                                     : ILPResult::Status::TIME_LIMIT;
        break;
      case KONT_MIPSTATUS_UNSTARTED:
      case KONT_MIPSTATUS_INF_OR_UNB:
      default:
        result.status = ILPResult::Status::ERROR;
        break;
    }
    if (has_solution) {
      result.objective = model.GetDblAttr(KONT_DBLATTR_BESTOBJ);
      result.x.resize(static_cast<Index>(costs.size()));
      for (Index column = 0; column < result.x.size(); ++column) {
        result.x[column] = variables[column].Get(KONT_DBLINFO_VALUE);
      }
    }
    result.lower_bound = model.GetDblAttr(KONT_DBLATTR_BESTBND);
    result.gap = model.GetDblAttr(KONT_DBLATTR_BESTGAP);
    result.solve_time_sec = model.GetDblAttr(KONT_DBLATTR_SOLVINGTIME);
    if (!std::isfinite(result.lower_bound) ||
        std::abs(result.lower_bound) >= KONT_UNDEFINED) {
      result.lower_bound = 0.0;
    }
    if (!std::isfinite(result.gap) || std::abs(result.gap) >= KONT_UNDEFINED) {
      result.gap = 0.0;
    }
    if (result.status == ILPResult::Status::OPTIMAL) {
      result.lower_bound = result.objective;
    }
    result.solver_message =
        "KONT/COPT " + result.solver_version +
        ": MIP status " + std::to_string(status);
  } catch (const KontException& error) {
    result.status = ILPResult::Status::ERROR;
    result.solver_message =
        "KONT/COPT initialization/model error (code " +
        std::to_string(error.GetCode()) + "): " + error.what();
  } catch (const std::exception& error) {
    result.status = ILPResult::Status::ERROR;
    result.solver_message =
        "KONT/COPT initialization/model error: " + std::string(error.what());
  }
  if (result.solve_time_sec <= 0.0 ||
      !std::isfinite(result.solve_time_sec)) {
    result.solve_time_sec =
        std::chrono::duration<double>(std::chrono::steady_clock::now() -
                                      started)
            .count();
  }
  return result;
}
#endif
}

KontSolver::KontSolver() = default;
KontSolver::~KontSolver() = default;

std::string KontSolver::name() const {
  return probe_native_backend()
             ? "KONT/COPT"
             : "built-in-branch-and-bound-fallback";
}

bool KontSolver::native_backend_compiled() {
#ifdef KDC_HAS_KONT_NATIVE_PLUGIN
  return true;
#else
  return false;
#endif
}

KontBackendDiagnostics KontSolver::backend_diagnostics() {
  static const KontBackendDiagnostics diagnostics = [] {
    KontBackendDiagnostics info;
    info.root = kont_build_config::root;
    info.root_source = kont_build_config::root_source;
    info.copt_header = kont_build_config::copt_header;
    info.copt_library = kont_build_config::copt_library;
    info.kont_header = kont_build_config::kont_header;
    info.kont_cpp_header = kont_build_config::kont_cpp_header;
    info.kont_cpp_library = kont_build_config::kont_cpp_library;
    info.include_directory = kont_build_config::include_directory;
    info.library = kont_build_config::library;
    info.architecture = kont_build_config::architecture;
    info.native_api = kont_build_config::native_api;
    info.compile_check = kont_build_config::compile_check;
    info.compile_failure_reason = kont_build_config::compile_failure_reason;
    info.version = kont_build_config::version;
    if (!KontSolver::native_backend_compiled()) {
      info.runtime_probe_failure_reason =
          info.compile_failure_reason.empty()
              ? "native KONT/COPT C++ API is not compiled into this build"
              : info.compile_failure_reason;
      return info;
    }

    Eigen::VectorXd costs(1);
    costs[0] = 1.0;
    Eigen::SparseMatrix<double> constraints(1, 1);
    constraints.insert(0, 0) = 1.0;
    Eigen::VectorXd rhs(1);
    rhs[0] = 1.0;
    KontSolver solver;
    ILPResult result;
    try {
      result = solver.solve(costs, constraints, rhs, {0}, 5.0, 0.0);
    } catch (const std::exception& error) {
      info.runtime_probe_failure_reason = error.what();
      return info;
    }
    info.runtime_probe_passed =
        result.status == ILPResult::Status::OPTIMAL &&
        result.native_backend_used && !result.fallback_used;
    info.license_runtime_initialization_passed =
        result.license_runtime_initialization_passed;
    info.native_backend_available = info.runtime_probe_passed;
    if (!info.runtime_probe_passed) {
      info.runtime_probe_failure_reason =
          result.solver_message.empty()
              ? "native runtime probe did not produce a proven-optimal result"
              : result.solver_message;
    }
    return info;
  }();
  return diagnostics;
}

bool KontSolver::probe_native_backend() {
  return backend_diagnostics().native_backend_available;
}

void KontSolver::require_native_backend() {
  const KontBackendDiagnostics info = backend_diagnostics();
  if (info.native_backend_available) {
    return;
  }
  std::ostringstream message;
  message << "explicit ip-kont request cannot run: native KONT/COPT is "
             "unavailable; KONT_ROOT="
          << (info.root.empty() ? "<not supplied>" : info.root)
          << "; detected API=" << info.native_api
          << "; C++ API compile check=" << info.compile_check
          << "; include directory="
          << (info.include_directory.empty() ? "<not found>"
                                             : info.include_directory)
          << "; library="
          << (info.library.empty() ? "<not found>" : info.library)
          << "; architecture=" << info.architecture
          << "; reason="
          << (!info.runtime_probe_failure_reason.empty()
                  ? info.runtime_probe_failure_reason
                  : info.compile_failure_reason);
  throw std::runtime_error(message.str());
}

void KontSolver::require_native_for_solves() {
  require_native_backend();
  native_required_ = true;
}

ILPResult KontSolver::solve(const Eigen::VectorXd& costs,
                            const Eigen::SparseMatrix<double>& matrix,
                            const Eigen::VectorXd& rhs,
                            const std::vector<int>& integer_vars,
                            double time_limit_sec, double gap_target) {
  LOG_DEBUG("KONT: building model with {} vars, {} constrs", costs.size(),
            matrix.rows());
  validate_problem(costs, matrix, rhs, integer_vars, time_limit_sec, gap_target);
  auto run_fallback = [&](const std::string& reason) {
    ILPResult fallback = solve_fallback(
        costs, matrix, rhs, integer_vars, time_limit_sec, gap_target,
        "built-in branch-and-bound fallback");
    fallback.actual_backend = "built-in-branch-and-bound-fallback";
    fallback.fallback_used = true;
    fallback.solver_version = "not-applicable";
    fallback.solver_message =
        "Native KONT/COPT initialization failed (" + reason +
        "); generic KontSolver used the built-in branch-and-bound fallback";
    return fallback;
  };
#ifdef KDC_HAS_KONT_NATIVE_PLUGIN
  NativeRuntimeState& runtime_state = native_runtime_state();
  if (runtime_state.initialization_unavailable.load()) {
    if (native_required_) {
      require_native_backend();
    }
    std::string reason;
    {
      std::lock_guard<std::mutex> lock(runtime_state.mutex);
      reason = runtime_state.failure_reason;
    }
    ILPResult result = run_fallback(
        reason.empty()
            ? "a previous native initialization/license probe failed"
            : reason);
    LOG_DEBUG("KONT: using built-in fallback after prior native initialization "
              "failure");
    return result;
  }
  ILPResult result;
  try {
    result = solve_with_native_plugin(costs, matrix, rhs, integer_vars,
                                      time_limit_sec, gap_target);
  } catch (const std::exception& error) {
    result.status = ILPResult::Status::ERROR;
    result.actual_backend = "KONT/COPT";
    result.solver_version = kont_build_config::version;
    result.solver_message =
        "KONT/COPT initialization/model error: " + std::string(error.what());
  }
  if (result.status == ILPResult::Status::ERROR &&
      !result.license_runtime_initialization_passed) {
    const std::string reason = result.solver_message;
    {
      std::lock_guard<std::mutex> lock(runtime_state.mutex);
      runtime_state.failure_reason = reason;
    }
    runtime_state.initialization_unavailable.store(true);
    if (native_required_) {
      throw std::runtime_error(
          "explicit ip-kont solve failed during native initialization: " +
          reason);
    }
    result = run_fallback(reason);
    LOG_WARN("KONT: native initialization failed; generic KontSolver used "
             "the built-in fallback");
  }
#else
  ILPResult result = solve_fallback(
      costs, matrix, rhs, integer_vars, time_limit_sec, gap_target,
      "built-in branch-and-bound fallback");
  result.actual_backend = "built-in-branch-and-bound-fallback";
  result.fallback_used = true;
#endif
  LOG_INFO("KONT: status={}, obj={:.6f}, lb={:.6f}, gap={:.4f}, t={:.3f}s",
           status_name(result.status), result.objective,
           result.lower_bound, result.gap, result.solve_time_sec);
  return result;
}

ILPResult DummyLPAdapter::solve(const Eigen::VectorXd& costs,
                                const Eigen::SparseMatrix<double>& matrix,
                                const Eigen::VectorXd& rhs,
                                const std::vector<int>& integer_vars,
                                double time_limit_sec, double gap_target) {
  static_cast<void>(integer_vars);
  validate_problem(costs, matrix, rhs, {}, time_limit_sec, gap_target);
  const auto started = std::chrono::steady_clock::now();
  const LPAnswer answer = solve_relaxation(costs, matrix, rhs);
  ILPResult result;
  result.solve_time_sec =
      std::chrono::duration<double>(std::chrono::steady_clock::now() - started)
          .count();
  if (answer.status == LPStatus::OPTIMAL) {
    result.status = ILPResult::Status::OPTIMAL;
    result.x = answer.values;
    result.objective = answer.objective;
    result.lower_bound = answer.objective;
    result.gap = 0.0;
    result.solver_message = "LP relaxation solved to optimality";
  } else if (answer.status == LPStatus::UNBOUNDED) {
    result.status = ILPResult::Status::UNBOUNDED;
    result.solver_message = "LP relaxation is unbounded";
  } else {
    result.status = ILPResult::Status::INFEASIBLE;
    result.solver_message = "LP relaxation is infeasible";
  }
  LOG_INFO("DummyLP: status={}, obj={:.6f}",
           status_name(result.status), result.objective);
  return result;
}
}
