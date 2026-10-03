#include "kdc/kont_solver.hpp"

#include "kdc/logging.hpp"

#ifdef KDC_HAS_COPT_CPP_API
#include <copt.h>
#endif

#include <algorithm>
#include <chrono>
#include <cmath>
#include <functional>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <utility>

namespace kdc {
using Index = std::size_t;

namespace {
constexpr double kEpsilon = 1e-9;

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

  ILPResult result;
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
#endif
}

KontSolver::KontSolver() = default;
KontSolver::~KontSolver() = default;

bool KontSolver::probe_native_backend() {
  Eigen::VectorXd costs(1);
  costs[0] = 1.0;
  Eigen::SparseMatrix<double> constraints(1, 1);
  constraints.insert(0, 0) = 1.0;
  Eigen::VectorXd rhs(1);
  rhs[0] = 1.0;
  KontSolver solver;
  try {
    const ILPResult result =
        solver.solve(costs, constraints, rhs, {0}, 5.0, 0.0);
    return result.status == ILPResult::Status::OPTIMAL &&
           result.solver_message.find("Exact fallback") == std::string::npos;
  } catch (const std::exception& error) {
    LOG_WARN("KONT/COPT runtime probe failed: {}", error.what());
    return false;
  }
}

ILPResult KontSolver::solve(const Eigen::VectorXd& costs,
                            const Eigen::SparseMatrix<double>& matrix,
                            const Eigen::VectorXd& rhs,
                            const std::vector<int>& integer_vars,
                            double time_limit_sec, double gap_target) {
  LOG_DEBUG("KONT: building model with {} vars, {} constrs", costs.size(),
            matrix.rows());
  validate_problem(costs, matrix, rhs, integer_vars, time_limit_sec, gap_target);
#ifdef KDC_HAS_COPT_CPP_API
  ILPResult result = solve_with_kont(costs, matrix, rhs, integer_vars,
                                     time_limit_sec, gap_target);
#else
  ILPResult result = solve_fallback(
      costs, matrix, rhs, integer_vars, time_limit_sec, gap_target,
      "Exact fallback (KONT C++ API unavailable)");
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
