#include "kdc/mock_ilp_solver.hpp"

#include "kdc/logging.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace kdc {
MockILPSolver::MockILPSolver() : MockILPSolver(Config{}) {}

MockILPSolver::MockILPSolver(Config config) : config_(config) {}

ILPResult MockILPSolver::solve(
    const Eigen::VectorXd& costs, const Eigen::SparseMatrix<double>& matrix,
    const Eigen::VectorXd& rhs, const std::vector<int>& integer_vars,
    double time_limit_sec, double gap_target) {
  LOG_DEBUG("Mock::solve vars={} constrs={} integer={}", costs.size(),
            matrix.rows(), integer_vars.size());
  const auto start = std::chrono::steady_clock::now();
  if (matrix.cols() != costs.size() || matrix.rows() != rhs.size() ||
      !std::isfinite(time_limit_sec) || time_limit_sec <= 0.0 ||
      !std::isfinite(gap_target) || gap_target < 0.0) {
    throw std::invalid_argument("MockILPSolver received invalid dimensions or parameters");
  }

  for (Eigen::Index column = 0; column < costs.size(); ++column) {
    if (!std::isfinite(costs[column]) || costs[column] < 0.0) {
      throw std::invalid_argument(
          "MockILPSolver requires finite non-negative objective costs");
    }
  }
  for (Eigen::Index row = 0; row < rhs.size(); ++row) {
    if (!std::isfinite(rhs[row])) {
      throw std::invalid_argument("MockILPSolver requires finite rhs values");
    }
  }
  for (const int variable : integer_vars) {
    if (variable < 0 || variable >= costs.size()) {
      throw std::invalid_argument(
          "MockILPSolver integer variable index is out of range");
    }
  }

  std::vector<std::vector<std::pair<int, double>>> row_entries(
      static_cast<std::size_t>(matrix.rows()));
  std::vector<std::vector<std::pair<int, double>>> column_entries(
      static_cast<std::size_t>(matrix.cols()));
  for (int outer = 0; outer < matrix.outerSize(); ++outer) {
    for (Eigen::SparseMatrix<double>::InnerIterator item(matrix, outer); item;
         ++item) {
      const double value = item.value();
      if (!std::isfinite(value)) {
        throw std::invalid_argument("MockILPSolver matrix values must be finite");
      }
      if (value < 0.0) {
        throw std::invalid_argument(
            "MockILPSolver supports non-negative set-cover matrices only");
      }
      if (value > 0.0) {
        row_entries[static_cast<std::size_t>(item.row())].emplace_back(
            static_cast<int>(item.col()), value);
        column_entries[static_cast<std::size_t>(item.col())].emplace_back(
            static_cast<int>(item.row()), value);
      }
    }
  }

  ILPResult result;
  result.x.assign(static_cast<std::size_t>(costs.size()), 0.0);
  std::vector<double> row_activity(static_cast<std::size_t>(matrix.rows()), 0.0);
  constexpr double tolerance = 1e-9;
  for (int row = 0; row < matrix.rows(); ++row) {
    const std::size_t row_index = static_cast<std::size_t>(row);
    while (row_activity[row_index] + tolerance < rhs[row]) {
      int best_column = -1;
      double best_cost_per_unit = std::numeric_limits<double>::infinity();
      for (const auto& entry : row_entries[row_index]) {
        const int column = entry.first;
        if (result.x[static_cast<std::size_t>(column)] > 0.5) {
          continue;
        }
        const double score = costs[column] / entry.second;
        if (score < best_cost_per_unit) {
          best_cost_per_unit = score;
          best_column = column;
        }
      }
      if (best_column < 0) {
        result.status = ILPResult::Status::INFEASIBLE;
        result.solver_message =
            "Mock: no variable can satisfy row " + std::to_string(row);
        result.solve_time_sec =
            std::chrono::duration<double>(std::chrono::steady_clock::now() -
                                          start)
                .count();
        return result;
      }
      result.x[static_cast<std::size_t>(best_column)] = 1.0;
      for (const auto& entry :
           column_entries[static_cast<std::size_t>(best_column)]) {
        row_activity[static_cast<std::size_t>(entry.first)] += entry.second;
      }
    }
  }

  for (int row = 0; row < matrix.rows(); ++row) {
    if (row_activity[static_cast<std::size_t>(row)] + tolerance < rhs[row]) {
      result.status = ILPResult::Status::INFEASIBLE;
      result.solver_message =
          "Mock: greedy assignment does not satisfy row " +
          std::to_string(row);
      result.solve_time_sec =
          std::chrono::duration<double>(std::chrono::steady_clock::now() -
                                        start)
              .count();
      return result;
    }
  }

  for (Eigen::Index column = 0; column < costs.size(); ++column) {
    result.objective +=
        costs[column] * result.x[static_cast<std::size_t>(column)];
  }
  result.lower_bound = 0.0;
  result.gap = result.objective > 0.0
                   ? result.objective / std::max(1.0, result.objective)
                   : 0.0;
  result.status =
      config_.allow_optimal_claim && result.objective <= tolerance
          ? ILPResult::Status::OPTIMAL
          : ILPResult::Status::FEASIBLE;
  result.solve_time_sec =
      std::chrono::duration<double>(std::chrono::steady_clock::now() - start)
          .count();
  LOG_INFO("Mock: status={} obj={:.6f} lb={:.6f} time={:.6f}s",
           static_cast<int>(result.status), result.objective,
           result.lower_bound, result.solve_time_sec);
  return result;
}
}  // namespace kdc
