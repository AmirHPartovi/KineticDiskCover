#include "kdc/weighted_oracle.hpp"

#include <Eigen/Core>
#include <Eigen/SparseCore>

#include <algorithm>
#include <cmath>
#include <cctype>
#include <limits>
#include <stdexcept>

namespace kdc {
namespace {
WeightedOracleTerminationStatusV1 map_ilp_status(
    ILPResult::Status status) {
  switch (status) {
    case ILPResult::Status::OPTIMAL:
      return WeightedOracleTerminationStatusV1::OPTIMAL;
    case ILPResult::Status::FEASIBLE:
      return WeightedOracleTerminationStatusV1::FEASIBLE;
    case ILPResult::Status::INFEASIBLE:
      return WeightedOracleTerminationStatusV1::INFEASIBLE;
    case ILPResult::Status::UNBOUNDED:
      return WeightedOracleTerminationStatusV1::UNBOUNDED;
    case ILPResult::Status::TIME_LIMIT:
      return WeightedOracleTerminationStatusV1::TIME_LIMIT;
    case ILPResult::Status::ERROR:
      return WeightedOracleTerminationStatusV1::ERROR;
  }
  throw std::invalid_argument("unknown ILPResult status");
}

class UnavailableWeightedExactOracle final
    : public IWeightedExactOracle {
 public:
  WeightedExactOracleResultV1 solve(
      const WeightMatrix& matrix,
      SolverBudget* budget) override {
    if (budget != nullptr) {
      budget->checkpoint();
    }
    (void)matrix;
    WeightedExactOracleResultV1 result;
    result.termination =
        WeightedOracleTerminationStatusV1::NOT_IMPLEMENTED;
    result.provenance.backend_name = name();
    result.provenance.method = "unavailable";
    result.provenance.source = "WeightedOracleFactory";
    return result;
  }
  std::string name() const override { return "unavailable-exact"; }
};

class UnavailableWeightedHeuristicOracle final
    : public IWeightedHeuristicOracle {
 public:
  WeightedHeuristicOracleResultV1 solve(
      const WeightMatrix& matrix,
      SolverBudget* budget) override {
    if (budget != nullptr) {
      budget->checkpoint();
    }
    (void)matrix;
    WeightedHeuristicOracleResultV1 result;
    result.termination =
        WeightedOracleTerminationStatusV1::NOT_IMPLEMENTED;
    result.provenance.backend_name = name();
    result.provenance.method = "unavailable";
    result.provenance.source = "WeightedOracleFactory";
    return result;
  }
  std::string name() const override { return "unavailable-heuristic"; }
};

class UnavailableWeightedRelaxationOracle final
    : public IWeightedRelaxationOracle {
 public:
  WeightedRelaxationOracleResultV1 solve(
      const WeightMatrix& matrix,
      SolverBudget* budget) override {
    if (budget != nullptr) {
      budget->checkpoint();
    }
    (void)matrix;
    WeightedRelaxationOracleResultV1 result;
    result.termination =
        WeightedOracleTerminationStatusV1::NOT_IMPLEMENTED;
    result.provenance.backend_name = name();
    result.provenance.method = "unavailable";
    result.provenance.source = "WeightedOracleFactory";
    return result;
  }
  std::string name() const override { return "unavailable-relaxation"; }
};

WeightedOracleTerminationStatusV1 classify_ilp_termination(
    const ILPResult& result) {
  std::string message = result.solver_message;
  std::transform(message.begin(), message.end(), message.begin(),
                 [](unsigned char character) {
                   return static_cast<char>(
                       std::tolower(character));
                 });
  if (result.status == ILPResult::Status::TIME_LIMIT &&
      (message.find("interrupted") != std::string::npos ||
       message.find("unfinished") != std::string::npos ||
       message.find("node limit") != std::string::npos ||
       message.find("nodelimit") != std::string::npos)) {
    return WeightedOracleTerminationStatusV1::INTERRUPTED;
  }
  if (result.status == ILPResult::Status::ERROR &&
      (message.find("numeric") != std::string::npos ||
       message.find("nan") != std::string::npos ||
       message.find("inf") != std::string::npos ||
       message.find("license") != std::string::npos ||
       message.find("invalid") != std::string::npos)) {
    return WeightedOracleTerminationStatusV1::NUMERIC_FAILURE;
  }
  return map_ilp_status(result.status);
}

std::optional<std::vector<int>> extract_weighted_assignment(
    const WeightMatrix& matrix,
    const ILPResult& result) {
  if (result.status != ILPResult::Status::OPTIMAL &&
      result.status != ILPResult::Status::FEASIBLE &&
      result.status != ILPResult::Status::TIME_LIMIT) {
    return std::nullopt;
  }
  const std::size_t x_count =
      matrix.station_count() * matrix.point_count();
  if (result.x.size() != x_count + matrix.station_count()) {
    return std::nullopt;
  }
  std::vector<int> assignment(matrix.point_count());
  constexpr double tolerance = 1e-7;
  for (WeightIndex point = 0U; point < matrix.point_count(); ++point) {
    int owner = -1;
    int assigned_count = 0;
    for (WeightIndex station = 0U;
         station < matrix.station_count();
         ++station) {
      const WeightIndex column = station * matrix.point_count() + point;
      const double value = result.x[column];
      if (!std::isfinite(value)) {
        return std::nullopt;
      }
      if (std::abs(value) <= tolerance) {
        continue;
      }
      if (std::abs(value - 1.0) <= tolerance) {
        ++assigned_count;
        owner = static_cast<int>(station);
      } else {
        return std::nullopt;
      }
    }
    if (assigned_count != 1) {
      return std::nullopt;
    }
    assignment[point] = owner;
  }
  return assignment;
}

WeightedExactOracleResultV1 direct_weighted_result(
    const WeightMatrix& matrix,
    WeightedOracleTerminationStatusV1 termination,
    bool incumbent_feasible,
    std::optional<std::vector<int>> assignment,
    std::optional<Weight> primal_cost,
    std::optional<Weight> lower_bound,
    const std::string& method,
    const std::string& source) {
  WeightedExactOracleResultV1 result;
  result.termination = termination;
  result.assignment = std::move(assignment);
  result.incumbent_feasible = incumbent_feasible;
  result.feasible_primal_cost = primal_cost;
  result.solver_best_bound = lower_bound;
  result.solver_best_bound_validity =
      lower_bound.has_value()
          ? WeightedBoundValidityV1::REPORTED_UNVALIDATED
          : WeightedBoundValidityV1::ABSENT;
  result.independently_validated_lower_bound.reset();
  result.independently_validated_lower_bound_validity =
      WeightedBoundValidityV1::ABSENT;
  result.solver_reports_optimal =
      termination == WeightedOracleTerminationStatusV1::OPTIMAL;
  result.optimality_claim_possible =
      termination == WeightedOracleTerminationStatusV1::OPTIMAL &&
      incumbent_feasible;
  result.optimality_certified = false;
  result.arithmetic = WeightedArithmeticModeV1::DOUBLE;
  result.provenance.method = method;
  result.provenance.source = source;
  result.provenance.backend_name = "weighted-direct";
  return result;
}
}

ILPWeightedExactOracle::ILPWeightedExactOracle(ILPSolver* ilp)
    : ILPWeightedExactOracle(ilp, Config{}) {}

ILPWeightedExactOracle::ILPWeightedExactOracle(ILPSolver* ilp,
                                               Config config)
    : ilp_(ilp), config_(config) {
  if (ilp_ == nullptr) {
    throw std::invalid_argument(
        "ILPWeightedExactOracle requires a non-null ILP solver");
  }
  if (!std::isfinite(config_.time_limit_seconds) ||
      config_.time_limit_seconds <= 0.0 ||
      !std::isfinite(config_.gap_target) ||
      config_.gap_target < 0.0) {
    throw std::invalid_argument(
        "ILP weighted exact oracle configuration is invalid");
  }
}

WeightedExactOracleResultV1 ILPWeightedExactOracle::solve(
    const WeightMatrix& matrix,
    SolverBudget* budget) {
  if (budget != nullptr) {
    budget->checkpoint();
  }

  const WeightIndex station_count = matrix.station_count();
  const WeightIndex point_count = matrix.point_count();

  if (station_count == 0U && point_count > 0U) {
    WeightedExactOracleResultV1 result;
    result.termination =
        WeightedOracleTerminationStatusV1::INFEASIBLE;
    result.arithmetic = WeightedArithmeticModeV1::DOUBLE;
    result.incumbent_feasible = false;
    result.assignment.reset();
    result.feasible_primal_cost.reset();
    result.solver_reports_optimal = false;
    result.optimality_claim_possible = false;
    result.optimality_certified = false;
    result.provenance.backend_name = ilp_->name();
    result.provenance.method = "weighted-direct-infeasible";
    result.provenance.source = "WeightMatrix m=0,n>0";
    return result;
  }

  if (point_count == 0U) {
    std::vector<int> empty_assignment;
    return direct_weighted_result(
        matrix,
        WeightedOracleTerminationStatusV1::OPTIMAL,
        true,
        empty_assignment,
        0.0,
        std::nullopt,
        "weighted-direct-empty-point",
        "WeightMatrix n=0");
  }

  Weight maximum_weight = 0.0;
  bool all_zero = true;
  for (WeightIndex station = 0U;
       station < station_count;
       ++station) {
    for (WeightIndex point = 0U;
         point < point_count;
         ++point) {
      const Weight weight = matrix.weight(station, point);
      if (weight != 0.0) {
        all_zero = false;
      }
      maximum_weight = std::max(maximum_weight, weight);
    }
  }
  if (all_zero) {
    std::vector<int> assignment(point_count, 0);
    return direct_weighted_result(
        matrix,
        WeightedOracleTerminationStatusV1::OPTIMAL,
        true,
        assignment,
        0.0,
        std::nullopt,
        "weighted-direct-zero",
        "WeightMatrix all weights are zero");
  }

  const WeightIndex x_count = station_count * point_count;
  const WeightIndex q_offset = x_count;
  const WeightIndex variable_count = x_count + station_count;
  if (variable_count < x_count ||
      variable_count > static_cast<WeightIndex>(
                           std::numeric_limits<Eigen::Index>::max())) {
    throw WeightedMatrixError(
        "weighted MILP variable count exceeds supported range");
  }
  Eigen::VectorXd costs =
      Eigen::VectorXd::Zero(static_cast<Eigen::Index>(variable_count));
  const Weight pi = std::acos(-1.0);
  for (WeightIndex station = 0U;
       station < station_count;
       ++station) {
    costs(static_cast<Eigen::Index>(q_offset + station)) =
        pi * maximum_weight;
  }

  const WeightIndex row_count =
      2U * point_count + station_count * point_count;
  Eigen::SparseMatrix<double> constraints(
      static_cast<Eigen::Index>(row_count),
      static_cast<Eigen::Index>(variable_count));
  Eigen::VectorXd rhs =
      Eigen::VectorXd::Zero(static_cast<Eigen::Index>(row_count));
  std::vector<Eigen::Triplet<double>> entries;
  entries.reserve(static_cast<WeightIndex>(
      2U * station_count * point_count +
      station_count * point_count));

  WeightIndex row = 0U;
  for (WeightIndex point = 0U; point < point_count; ++point) {
    for (WeightIndex station = 0U;
         station < station_count;
         ++station) {
      const WeightIndex column =
          station * point_count + point;
      entries.emplace_back(static_cast<Eigen::Index>(row),
                           static_cast<Eigen::Index>(column),
                           1.0);
    }
    rhs(static_cast<Eigen::Index>(row)) = 1.0;
    ++row;
  }
  for (WeightIndex point = 0U; point < point_count; ++point) {
    for (WeightIndex station = 0U;
         station < station_count;
         ++station) {
      const WeightIndex column =
          station * point_count + point;
      entries.emplace_back(static_cast<Eigen::Index>(row),
                           static_cast<Eigen::Index>(column),
                           -1.0);
    }
    rhs(static_cast<Eigen::Index>(row)) = -1.0;
    ++row;
  }
  for (WeightIndex station = 0U;
       station < station_count;
       ++station) {
    for (WeightIndex point = 0U;
         point < point_count;
         ++point) {
      const WeightIndex x_column =
          station * point_count + point;
      const WeightIndex q_column = q_offset + station;
      entries.emplace_back(static_cast<Eigen::Index>(row),
                           static_cast<Eigen::Index>(q_column),
                           1.0);
      entries.emplace_back(
          static_cast<Eigen::Index>(row),
          static_cast<Eigen::Index>(x_column),
          -(matrix.weight(station, point) / maximum_weight));
      rhs(static_cast<Eigen::Index>(row)) = 0.0;
      ++row;
    }
  }
  constraints.setFromTriplets(entries.begin(), entries.end());

  std::vector<int> integer_vars;
  integer_vars.reserve(static_cast<WeightIndex>(x_count));
  for (WeightIndex column = 0U; column < x_count; ++column) {
    integer_vars.push_back(static_cast<int>(column));
  }

  const double effective_limit =
      budget == nullptr
          ? config_.time_limit_seconds
          : budget->limit_seconds(config_.time_limit_seconds);
  if (effective_limit <= 0.0) {
    throw SolverBudgetExpired();
  }

  ILPResult ilp_result = ilp_->solve(
      costs,
      constraints,
      rhs,
      integer_vars,
      effective_limit,
      config_.gap_target);

  std::optional<std::vector<int>> assignment =
      extract_weighted_assignment(matrix, ilp_result);
  WeightedExactOracleResultV1 result =
      adapt_weighted_exact_ilp_result(
          matrix,
          assignment,
          ilp_result,
          WeightedArithmeticModeV1::DOUBLE,
          "ILPWeightedExactOracle");
  result.termination = classify_ilp_termination(ilp_result);
  if (budget != nullptr && budget->expired() &&
      ilp_result.status != ILPResult::Status::OPTIMAL &&
      ilp_result.status != ILPResult::Status::INFEASIBLE &&
      ilp_result.status != ILPResult::Status::UNBOUNDED &&
      ilp_result.status != ILPResult::Status::ERROR) {
    result.termination = WeightedOracleTerminationStatusV1::TIME_LIMIT;
  }
  result.solver_message = ilp_result.solver_message;
  if (std::isfinite(ilp_result.objective)) {
    result.backend_primal_objective = ilp_result.objective;
  }
  if (std::isfinite(ilp_result.gap)) {
    result.solver_gap = ilp_result.gap;
  }
  result.provenance.backend_name =
      ilp_result.actual_backend.empty()
          ? ilp_->name()
          : ilp_result.actual_backend;
  result.provenance.backend_version =
      ilp_result.solver_version.empty()
          ? "unknown"
          : ilp_result.solver_version;
  result.provenance.method = "weighted-direct-MILP";
  result.provenance.source = "ILP exact model";
  result.provenance.elapsed_seconds = ilp_result.solve_time_sec;
  return result;
}

WeightedHeuristicOracleResultV1
GreedyWeightedHeuristicOracle::solve(
    const WeightMatrix& matrix,
    SolverBudget* budget) {
  if (budget != nullptr) {
    budget->checkpoint();
  }
  WeightedHeuristicOracleResultV1 result;
  result.arithmetic = WeightedArithmeticModeV1::DOUBLE;
  result.provenance.backend_name = name();
  result.provenance.method = "greedy-min-weight";
  result.provenance.source = "WeightMatrix";

  if (matrix.station_count() == 0U &&
      matrix.point_count() > 0U) {
    result.termination =
        WeightedOracleTerminationStatusV1::INFEASIBLE;
    result.incumbent_feasible = false;
    return result;
  }
  if (matrix.point_count() == 0U) {
    result.termination =
        WeightedOracleTerminationStatusV1::OPTIMAL;
    result.assignment = std::vector<int>{};
    result.incumbent_feasible = true;
    result.feasible_primal_cost = 0.0;
    return result;
  }

  std::vector<int> assignment(matrix.point_count());
  for (WeightIndex point = 0U;
       point < matrix.point_count();
       ++point) {
    WeightIndex selected = 0U;
    Weight selected_weight =
        matrix.weight(selected, point);
    for (WeightIndex station = 1U;
         station < matrix.station_count();
         ++station) {
      const Weight candidate =
          matrix.weight(station, point);
      if (candidate < selected_weight) {
        selected = station;
        selected_weight = candidate;
      }
    }
    assignment[point] = static_cast<int>(selected);
  }
  result.assignment = assignment;
  result.incumbent_feasible = true;
  result.feasible_primal_cost =
      WeightedStaticObjective::evaluate_assignment(
          matrix, assignment);
  result.termination =
      WeightedOracleTerminationStatusV1::FEASIBLE;
  return result;
}

WeightedLPRelaxationOracle::WeightedLPRelaxationOracle(ILPSolver* ilp)
    : WeightedLPRelaxationOracle(ilp, Config{}) {}

WeightedLPRelaxationOracle::WeightedLPRelaxationOracle(
    ILPSolver* ilp,
    Config config)
    : ilp_(ilp), config_(config) {
  if (ilp_ == nullptr) {
    throw std::invalid_argument(
        "WeightedLPRelaxationOracle requires a non-null ILP solver");
  }
  if (!std::isfinite(config_.time_limit_seconds) ||
      config_.time_limit_seconds <= 0.0 ||
      !std::isfinite(config_.gap_target) ||
      config_.gap_target < 0.0) {
    throw std::invalid_argument(
        "weighted LP relaxation configuration is invalid");
  }
}

WeightedRelaxationOracleResultV1
WeightedLPRelaxationOracle::solve(
    const WeightMatrix& matrix,
    SolverBudget* budget) {
  if (budget != nullptr) {
    budget->checkpoint();
  }
  WeightedRelaxationOracleResultV1 result;
  result.arithmetic = WeightedArithmeticModeV1::DOUBLE;
  result.provenance.backend_name = ilp_->name();
  result.provenance.method = "lp-primal-diagnostic";
  result.provenance.source = "WeightMatrix MILP relaxation";
  result.certificate.present = false;
  result.certificate.independently_validated = false;
  result.lower_bound_validity =
      WeightedBoundValidityV1::INVALID;

  const WeightIndex m = matrix.station_count();
  const WeightIndex n = matrix.point_count();
  if (m == 0U && n > 0U) {
    result.termination =
        WeightedOracleTerminationStatusV1::INFEASIBLE;
    return result;
  }
  if (n == 0U) {
    result.termination =
        WeightedOracleTerminationStatusV1::OPTIMAL;
    result.diagnostic_primal_objective = 0.0;
    return result;
  }

  Weight maximum_weight = 0.0;
  bool all_zero = true;
  for (WeightIndex i = 0U; i < m; ++i) {
    for (WeightIndex j = 0U; j < n; ++j) {
      const Weight weight = matrix.weight(i, j);
      all_zero = all_zero && weight == 0.0;
      maximum_weight = std::max(maximum_weight, weight);
    }
  }
  if (all_zero) {
    result.termination =
        WeightedOracleTerminationStatusV1::OPTIMAL;
    result.diagnostic_primal_objective = 0.0;
    return result;
  }

  const WeightIndex x_count = m * n;
  const WeightIndex q_offset = x_count;
  const WeightIndex variable_count = x_count + m;
  Eigen::VectorXd costs =
      Eigen::VectorXd::Zero(
          static_cast<Eigen::Index>(variable_count));
  const Weight pi = std::acos(-1.0);
  for (WeightIndex i = 0U; i < m; ++i) {
    costs(static_cast<Eigen::Index>(q_offset + i)) =
        pi * maximum_weight;
  }

  const WeightIndex row_count = 2U * n + m * n;
  Eigen::SparseMatrix<double> constraints(
      static_cast<Eigen::Index>(row_count),
      static_cast<Eigen::Index>(variable_count));
  Eigen::VectorXd rhs =
      Eigen::VectorXd::Zero(
          static_cast<Eigen::Index>(row_count));
  std::vector<Eigen::Triplet<double>> entries;
  entries.reserve(static_cast<WeightIndex>(
      2U * m * n + m * n));

  WeightIndex row = 0U;
  for (WeightIndex j = 0U; j < n; ++j) {
    for (WeightIndex i = 0U; i < m; ++i) {
      entries.emplace_back(
          static_cast<Eigen::Index>(row),
          static_cast<Eigen::Index>(i * n + j),
          1.0);
    }
    rhs(static_cast<Eigen::Index>(row)) = 1.0;
    ++row;
  }
  for (WeightIndex j = 0U; j < n; ++j) {
    for (WeightIndex i = 0U; i < m; ++i) {
      entries.emplace_back(
          static_cast<Eigen::Index>(row),
          static_cast<Eigen::Index>(i * n + j),
          -1.0);
    }
    rhs(static_cast<Eigen::Index>(row)) = -1.0;
    ++row;
  }
  for (WeightIndex i = 0U; i < m; ++i) {
    for (WeightIndex j = 0U; j < n; ++j) {
      entries.emplace_back(
          static_cast<Eigen::Index>(row),
          static_cast<Eigen::Index>(q_offset + i),
          1.0);
      entries.emplace_back(
          static_cast<Eigen::Index>(row),
          static_cast<Eigen::Index>(i * n + j),
          -(matrix.weight(i, j) / maximum_weight));
      rhs(static_cast<Eigen::Index>(row)) = 0.0;
      ++row;
    }
  }
  constraints.setFromTriplets(entries.begin(), entries.end());

  const double effective_limit =
      budget == nullptr
          ? config_.time_limit_seconds
          : budget->limit_seconds(config_.time_limit_seconds);
  if (effective_limit <= 0.0) {
    throw SolverBudgetExpired();
  }
  ILPResult ilp_result = ilp_->solve(
      costs,
      constraints,
      rhs,
      {},
      effective_limit,
      config_.gap_target);

  result.termination = map_ilp_status(ilp_result.status);
  result.provenance.elapsed_seconds = ilp_result.solve_time_sec;
  result.provenance.backend_version =
      ilp_result.solver_version.empty()
          ? "unknown"
          : ilp_result.solver_version;
  if (std::isfinite(ilp_result.objective)) {
    result.diagnostic_primal_objective = ilp_result.objective;
  }
  result.lower_bound_validity =
      WeightedBoundValidityV1::INVALID;
  result.independently_validated_lower_bound.reset();
  result.certificate.present = false;
  result.certificate.independently_validated = false;
  result.certificate.method = "none";
  return result;
}

WeightedLPDualValidationV1 validate_weighted_lp_dual(
    const WeightMatrix& matrix,
    const std::vector<Weight>& y,
    const std::vector<Weight>& z,
    WeightedArithmeticModeV1 arithmetic) {
  (void)arithmetic;
  WeightedLPDualValidationV1 validation;
  validation.arithmetic = WeightedArithmeticModeV1::DOUBLE;
  validation.certificate.arithmetic =
      WeightedArithmeticModeV1::DOUBLE;
  validation.certificate.method =
      "numerical-lp-dual-diagnostic";

  const WeightIndex m = matrix.station_count();
  const WeightIndex n = matrix.point_count();
  if (y.size() != n || z.size() != m * n) {
    validation.valid = false;
    validation.reason =
        "dual vector dimensions do not match W";
    return validation;
  }

  Weight max_residual = 0.0;
  for (const Weight value : y) {
    if (!std::isfinite(value)) {
      validation.valid = false;
      validation.reason = "dual y contains NaN or infinity";
      return validation;
    }
  }
  for (const Weight value : z) {
    if (!std::isfinite(value) || value < 0.0) {
      validation.valid = false;
      validation.reason =
          "dual z must be finite and nonnegative";
      return validation;
    }
  }

  bool constraints_numerically_satisfied = true;
  for (WeightIndex i = 0U; i < m; ++i) {
    long double z_station_sum = 0.0L;
    for (WeightIndex j = 0U; j < n; ++j) {
      const Weight z_value =
          z[i * n + j];
      z_station_sum += static_cast<long double>(z_value);

      const Weight y_value = y[j];
      const long double residual =
          static_cast<long double>(y_value) -
          static_cast<long double>(matrix.weight(i, j)) *
          static_cast<long double>(z_value);
      const double residual_double =
          residual > 0.0L
              ? static_cast<double>(residual)
              : 0.0;
      max_residual =
          std::max(max_residual,
                   residual_double);
      if (residual > 0.0L) {
        constraints_numerically_satisfied = false;
      }
    }
    const long double pi =
        static_cast<long double>(std::acos(-1.0));
    const long double station_residual =
        z_station_sum - pi;
    const double station_residual_double =
        station_residual > 0.0L
            ? static_cast<double>(station_residual)
            : 0.0;
    max_residual =
        std::max(max_residual,
                 station_residual_double);
    if (station_residual > 0.0L) {
      constraints_numerically_satisfied = false;
    }
  }

  long double lower_objective_long = 0.0L;
  for (const Weight y_value : y) {
    lower_objective_long +=
        static_cast<long double>(y_value);
  }
  validation.max_constraint_residual = max_residual;
  validation.valid = false;
  validation.certificate.present = false;
  validation.certificate.independently_validated = false;
  if (!constraints_numerically_satisfied) {
    validation.numerical_diagnostic_valid = false;
    validation.reason =
        "one or more dual constraints fail the floating-point diagnostic";
  } else {
    const double diagnostic_objective =
        static_cast<double>(lower_objective_long);
    if (!std::isfinite(diagnostic_objective) ||
        diagnostic_objective < 0.0) {
      validation.numerical_diagnostic_valid = false;
      validation.reason =
        "numerical dual objective is not finite/nonnegative";
    } else {
      validation.numerical_diagnostic_valid = true;
      validation.numerical_diagnostic_lower_bound =
        diagnostic_objective;
      validation.directed_lower_bound.reset();
      validation.reason =
        "numerically feasible dual diagnostic only; no lower-bound "
        "certificate issued because validated arithmetic is unavailable";
    }
  }
  return validation;
}

std::unique_ptr<IWeightedExactOracle>
WeightedOracleFactory::unavailable_exact() {
  return std::make_unique<UnavailableWeightedExactOracle>();
}

std::unique_ptr<IWeightedExactOracle>
WeightedOracleFactory::ilp_exact(ILPSolver* ilp,
                                 double time_limit_seconds,
                                 double gap_target) {
  ILPWeightedExactOracle::Config config;
  config.time_limit_seconds = time_limit_seconds;
  config.gap_target = gap_target;
  return std::make_unique<ILPWeightedExactOracle>(ilp, config);
}

std::unique_ptr<IWeightedHeuristicOracle>
WeightedOracleFactory::unavailable_heuristic() {
  return std::make_unique<UnavailableWeightedHeuristicOracle>();
}

std::unique_ptr<IWeightedHeuristicOracle>
WeightedOracleFactory::greedy_heuristic() {
  return std::make_unique<GreedyWeightedHeuristicOracle>();
}

std::unique_ptr<IWeightedRelaxationOracle>
WeightedOracleFactory::unavailable_relaxation() {
  return std::make_unique<UnavailableWeightedRelaxationOracle>();
}

std::unique_ptr<IWeightedRelaxationOracle>
WeightedOracleFactory::lp_relaxation(ILPSolver* ilp,
                                     double time_limit_seconds,
                                     double gap_target) {
  WeightedLPRelaxationOracle::Config config;
  config.time_limit_seconds = time_limit_seconds;
  config.gap_target = gap_target;
  return std::make_unique<WeightedLPRelaxationOracle>(ilp, config);
}

WeightedExactOracleResultV1 adapt_weighted_exact_ilp_result(
    const WeightMatrix& matrix,
    const std::optional<std::vector<int>>& assignment,
    const ILPResult& ilp_result,
    WeightedArithmeticModeV1 arithmetic,
    const std::string& source) {
  WeightedExactOracleResultV1 result;
  result.schema_version = 1;
  result.arithmetic = arithmetic;
  result.termination = map_ilp_status(ilp_result.status);
  result.solver_reports_optimal =
      ilp_result.status == ILPResult::Status::OPTIMAL;
  result.provenance.backend_name =
      ilp_result.actual_backend.empty()
          ? "ILPResult"
          : ilp_result.actual_backend;
  result.provenance.backend_version =
      ilp_result.solver_version.empty()
          ? "unknown"
          : ilp_result.solver_version;
  result.provenance.method = "adapt_ilp_result";
  result.provenance.source = source;
  result.provenance.elapsed_seconds = ilp_result.solve_time_sec;

  if (std::isfinite(ilp_result.lower_bound) &&
      ilp_result.lower_bound >= 0.0 &&
      ilp_result.status != ILPResult::Status::INFEASIBLE &&
      ilp_result.status != ILPResult::Status::UNBOUNDED &&
      ilp_result.status != ILPResult::Status::ERROR) {
    result.solver_best_bound = ilp_result.lower_bound;
    result.solver_best_bound_validity =
        WeightedBoundValidityV1::REPORTED_UNVALIDATED;
  } else {
    result.solver_best_bound.reset();
    result.solver_best_bound_validity =
        WeightedBoundValidityV1::ABSENT;
  }

  if (assignment.has_value()) {
    if (assignment->size() != matrix.point_count()) {
      throw WeightedAssignmentError(
          "exact ILP assignment size must equal point count");
    }
    WeightedStaticObjective::validate_assignment(matrix, *assignment);
    result.assignment = assignment;
    result.incumbent_feasible = true;
    result.feasible_primal_cost =
        WeightedStaticObjective::evaluate_assignment(matrix, *assignment);
  } else if (matrix.point_count() == 0U) {
    result.assignment = std::vector<int>{};
    result.incumbent_feasible = true;
    result.feasible_primal_cost = 0.0;
  } else {
    result.assignment.reset();
    result.incumbent_feasible = false;
    result.feasible_primal_cost.reset();
  }

  result.optimality_certified = false;
  result.optimality_claim_possible =
      result.solver_reports_optimal &&
      result.incumbent_feasible &&
      result.feasible_primal_cost.has_value() &&
      result.solver_best_bound.has_value();
  if (result.optimality_claim_possible) {
    const double difference =
        std::abs(*result.feasible_primal_cost -
                 *result.solver_best_bound);
    result.optimality_claim_possible =
        difference <= 1e-9 *
                         std::max(1.0,
                                  std::abs(*result.feasible_primal_cost));
  }

  // ILPResult does not carry an independently checked dual/relaxation
  // certificate, so no independent LB can be certified here.
  result.independently_validated_lower_bound.reset();
  result.independently_validated_lower_bound_validity =
      WeightedBoundValidityV1::ABSENT;
  return result;
}

WeightedHeuristicOracleResultV1 adapt_weighted_heuristic_assignment(
    const WeightMatrix& matrix,
    const std::optional<std::vector<int>>& assignment,
    WeightedOracleTerminationStatusV1 termination,
    WeightedArithmeticModeV1 arithmetic,
    WeightedOracleProvenanceV1 provenance) {
  WeightedHeuristicOracleResultV1 result;
  result.schema_version = 1;
  result.termination = termination;
  result.arithmetic = arithmetic;
  result.provenance = provenance;

  if (assignment.has_value()) {
    if (assignment->size() != matrix.point_count()) {
      throw WeightedAssignmentError(
          "heuristic assignment size must equal point count");
    }
    WeightedStaticObjective::validate_assignment(matrix, *assignment);
    result.assignment = assignment;
    result.incumbent_feasible = true;
    result.feasible_primal_cost =
        WeightedStaticObjective::evaluate_assignment(matrix, *assignment);
  } else if (matrix.point_count() == 0U) {
    result.assignment = std::vector<int>{};
    result.incumbent_feasible = true;
    result.feasible_primal_cost = 0.0;
  } else {
    result.assignment.reset();
    result.incumbent_feasible = false;
    result.feasible_primal_cost.reset();
  }
  return result;
}

WeightedRelaxationOracleResultV1 adapt_weighted_relaxation_certificate(
    const std::optional<Weight>& reported_lower_bound,
    const WeightedRelaxationCertificateV1& certificate,
    WeightedArithmeticModeV1 arithmetic,
    WeightedOracleProvenanceV1 provenance) {
  WeightedRelaxationOracleResultV1 result;
  result.schema_version = 1;
  result.arithmetic = arithmetic;
  result.certificate = certificate;
  result.termination =
      WeightedOracleTerminationStatusV1::NOT_IMPLEMENTED;
  result.provenance = provenance;

  if (certificate.present &&
      certificate.independently_validated &&
      !certificate.method.empty() &&
      reported_lower_bound.has_value() &&
      std::isfinite(*reported_lower_bound) &&
      *reported_lower_bound >= 0.0) {
    result.independently_validated_lower_bound = reported_lower_bound;
    result.lower_bound_validity = WeightedBoundValidityV1::VALID;
  } else {
    result.independently_validated_lower_bound.reset();
    result.lower_bound_validity = WeightedBoundValidityV1::INVALID;
  }
  return result;
}

}  // namespace kdc
