#include "kdc/kont_build_config.hpp"
#include "kdc/solver_interface.hpp"

#if defined(KDC_NATIVE_API_COPT)
#include <copt.h>
#elif defined(KDC_NATIVE_API_KONT)
#include <kontcpp_pch.h>
#endif

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <string>
#include <vector>

namespace {
using Index = std::size_t;

void set_error(kdc::ILPResult& result, const std::string& message) {
  result.status = kdc::ILPResult::Status::ERROR;
  result.actual_backend = "KONT/COPT";
  result.solver_version = kdc::kont_build_config::version;
  result.solver_message = message;
}
}

extern "C" bool kdc_kont_native_solve(
    const Eigen::VectorXd* costs,
    const Eigen::SparseMatrix<double>* matrix, const Eigen::VectorXd* rhs,
    const std::vector<int>* integer_vars, double time_limit_sec,
    double gap_target, kdc::ILPResult* result) noexcept {
  if (costs == nullptr || matrix == nullptr || rhs == nullptr ||
      integer_vars == nullptr || result == nullptr) {
    return false;
  }
  result->actual_backend = "KONT/COPT";
  result->solver_version = kdc::kont_build_config::version;
#if defined(KDC_NATIVE_API_COPT)
  try {
    static copt::Env environment;
    result->license_runtime_initialization_passed = true;
    copt::Model model = environment.CreateModel("kdc_ip");
    std::vector<copt::Var> variables;
    variables.reserve(static_cast<Index>(costs->size()));
    for (Eigen::Index column = 0; column < costs->size(); ++column) {
      const bool is_integer =
          std::find(integer_vars->begin(), integer_vars->end(),
                    static_cast<int>(column)) != integer_vars->end();
      variables.push_back(model.AddVar(
          0.0, 1.0, (*costs)[column],
          is_integer ? COPT_BINARY : COPT_CONTINUOUS,
          ("x" + std::to_string(column)).c_str()));
    }
    for (Eigen::Index row = 0; row < matrix->rows(); ++row) {
      copt::Expr expression;
      for (Eigen::Index column = 0; column < matrix->cols(); ++column) {
        const double coefficient = matrix->coeff(row, column);
        if (coefficient != 0.0) {
          expression += coefficient * variables[static_cast<Index>(column)];
        }
      }
      model.AddConstr(expression, COPT_GREATER_EQUAL, (*rhs)[row],
                      ("c" + std::to_string(row)).c_str());
    }
    model.SetDblParam(COPT_DBLPARAM_TIMELIMIT, time_limit_sec);
    model.SetDblParam(COPT_DBLPARAM_RELGAP, gap_target);
    model.SetIntParam(COPT_INTPARAM_LOGGING, 1);
    model.SetIntParam(COPT_INTPARAM_THREADS, 1);
    result->native_backend_used = true;
    const int return_code = model.Solve();
    if (return_code != 0) {
      set_error(*result, model.GetMessage());
      model.clear();
      return true;
    }
    result->objective = model.GetDblAttr(COPT_DBLATTR_LPOBJVAL);
    result->lower_bound = model.GetDblAttr(COPT_DBLATTR_BESTBND);
    result->solve_time_sec = model.GetDblAttr(COPT_DBLATTR_SOLVINGTIME);
    result->gap = model.GetDblAttr(COPT_DBLATTR_MIPGAP);
    const int status = model.GetIntAttr(COPT_INTATTR_MIPSTATUS);
    switch (status) {
      case COPT_MIP_STATUS_OPTIMAL:
        result->status = kdc::ILPResult::Status::OPTIMAL;
        break;
      case COPT_MIP_STATUS_INFEASIBLE:
        result->status = kdc::ILPResult::Status::INFEASIBLE;
        break;
      case COPT_MIP_STATUS_UNBOUNDED:
        result->status = kdc::ILPResult::Status::UNBOUNDED;
        break;
      case COPT_MIP_STATUS_TIMELIMIT:
        result->status = kdc::ILPResult::Status::TIME_LIMIT;
        break;
      case COPT_MIP_STATUS_GAPLIMIT:
        result->status = kdc::ILPResult::Status::FEASIBLE;
        break;
      default:
        result->status = kdc::ILPResult::Status::ERROR;
        break;
    }
    if (result->status == kdc::ILPResult::Status::OPTIMAL ||
        result->status == kdc::ILPResult::Status::FEASIBLE ||
        result->status == kdc::ILPResult::Status::TIME_LIMIT) {
      result->x.resize(static_cast<Index>(costs->size()));
      for (Index column = 0; column < result->x.size(); ++column) {
        result->x[column] = variables[column].Get(COPT_DBLINFO_VALUE);
      }
    }
    result->solver_message = model.GetMessage();
    model.clear();
    return true;
  } catch (const std::exception& error) {
    set_error(*result, "KONT/COPT initialization/model error: " +
                           std::string(error.what()));
    return true;
  }
#elif defined(KDC_NATIVE_API_KONT)
  try {
    static Envr environment = [] {
      if (const char* license_directory =
              std::getenv("KONT_LICENSE_DIR")) {
        if (license_directory[0] != '\0') {
          return Envr(license_directory);
        }
      }
      const std::filesystem::path root(kdc::kont_build_config::root);
      const std::filesystem::path installed_license_directory = root / "bin";
      if (std::filesystem::exists(installed_license_directory /
                                  "license.dat")) {
        return Envr(installed_license_directory.string().c_str());
      }
      return Envr();
    }();
    result->license_runtime_initialization_passed = true;
    Model model = environment.CreateModel("kdc_ip");
    std::vector<Var> variables;
    variables.reserve(static_cast<Index>(costs->size()));
    for (Eigen::Index column = 0; column < costs->size(); ++column) {
      const bool is_integer =
          std::find(integer_vars->begin(), integer_vars->end(),
                    static_cast<int>(column)) != integer_vars->end();
      variables.push_back(model.AddVar(
          0.0, 1.0, (*costs)[column],
          is_integer ? KONT_BINARY : KONT_CONTINUOUS,
          ("x" + std::to_string(column)).c_str()));
    }
    for (Eigen::Index row = 0; row < matrix->rows(); ++row) {
      Expr expression;
      for (Eigen::Index column = 0; column < matrix->cols(); ++column) {
        const double coefficient = matrix->coeff(row, column);
        if (coefficient != 0.0) {
          expression.AddTerm(variables[static_cast<Index>(column)],
                             coefficient);
        }
      }
      model.AddConstr(expression, KONT_GREATER_EQUAL, (*rhs)[row],
                      ("c" + std::to_string(row)).c_str());
    }
    model.SetDblParam(KONT_DBLPARAM_TIMELIMIT, time_limit_sec);
    model.SetDblParam(KONT_DBLPARAM_RELGAP, gap_target);
    model.SetIntParam(KONT_INTPARAM_LOGGING, 0);
    model.SetIntParam(KONT_INTPARAM_LOGTOCONSOLE, 0);
    model.SetIntParam(KONT_INTPARAM_THREADS, 1);
    result->native_backend_used = true;
    model.Solve();
    const int status = model.GetIntAttr(KONT_INTATTR_MIPSTATUS);
    const bool has_solution =
        model.GetIntAttr(KONT_INTATTR_HASMIPSOL) != 0;
    switch (status) {
      case KONT_MIPSTATUS_OPTIMAL:
        result->status = kdc::ILPResult::Status::OPTIMAL;
        break;
      case KONT_MIPSTATUS_INFEASIBLE:
        result->status = kdc::ILPResult::Status::INFEASIBLE;
        break;
      case KONT_MIPSTATUS_UNBOUNDED:
        result->status = kdc::ILPResult::Status::UNBOUNDED;
        break;
      case KONT_MIPSTATUS_TIMEOUT:
        result->status = kdc::ILPResult::Status::TIME_LIMIT;
        break;
      case KONT_MIPSTATUS_NODELIMIT:
      case KONT_MIPSTATUS_UNFINISHED:
      case KONT_MIPSTATUS_INTERRUPTED:
        result->status = has_solution ? kdc::ILPResult::Status::FEASIBLE
                                      : kdc::ILPResult::Status::TIME_LIMIT;
        break;
      case KONT_MIPSTATUS_UNSTARTED:
      case KONT_MIPSTATUS_INF_OR_UNB:
      default:
        result->status = kdc::ILPResult::Status::ERROR;
        break;
    }
    if (has_solution) {
      result->objective = model.GetDblAttr(KONT_DBLATTR_BESTOBJ);
      result->x.resize(static_cast<Index>(costs->size()));
      for (Index column = 0; column < result->x.size(); ++column) {
        result->x[column] = variables[column].Get(KONT_DBLINFO_VALUE);
      }
    }
    result->lower_bound = model.GetDblAttr(KONT_DBLATTR_BESTBND);
    result->gap = model.GetDblAttr(KONT_DBLATTR_BESTGAP);
    result->solve_time_sec = model.GetDblAttr(KONT_DBLATTR_SOLVINGTIME);
    if (!std::isfinite(result->lower_bound) ||
        std::abs(result->lower_bound) >= KONT_UNDEFINED) {
      result->lower_bound = 0.0;
    }
    if (!std::isfinite(result->gap) || std::abs(result->gap) >= KONT_UNDEFINED) {
      result->gap = 0.0;
    }
    if (result->status == kdc::ILPResult::Status::OPTIMAL) {
      result->lower_bound = result->objective;
    }
    result->solver_message =
        "KONT/COPT " + result->solver_version +
        ": MIP status " + std::to_string(status);
    return true;
  } catch (const KontException& error) {
    set_error(*result,
              "KONT/COPT initialization/model error (code " +
                  std::to_string(error.GetCode()) + "): " + error.what());
    return true;
  } catch (const std::exception& error) {
    set_error(*result, "KONT/COPT initialization/model error: " +
                           std::string(error.what()));
    return true;
  }
#else
  static_cast<void>(time_limit_sec);
  static_cast<void>(gap_target);
  return false;
#endif
}
