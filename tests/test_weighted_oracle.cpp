#include "kdc/solver_interface.hpp"
#include "kdc/solver_budget.hpp"
#include "kdc/weighted_oracle.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace {
kdc::WeightMatrix oracle_matrix() {
  return kdc::WeightMatrix::from_rows(
      {{2.0, 1.0}, {1.0, 2.0}});
}

class ConfiguredWeightedMockILP final : public kdc::ILPSolver {
 public:
  kdc::WeightMatrix matrix;
  kdc::ILPResult::Status status{
      kdc::ILPResult::Status::OPTIMAL};
  std::optional<std::vector<int>> incumbent;
  kdc::Weight lower_bound{0.0};
  kdc::Weight gap{0.0};
  std::string message;
  kdc::Weight solve_time{0.01};
  kdc::Weight diagnostic_objective{0.0};
  int call_count{0};

  explicit ConfiguredWeightedMockILP(kdc::WeightMatrix matrix_value)
      : matrix(std::move(matrix_value)) {}

  kdc::ILPResult solve(
      const Eigen::VectorXd& costs,
      const Eigen::SparseMatrix<double>& model,
      const Eigen::VectorXd& rhs,
      const std::vector<int>& integer_vars,
      double time_limit_sec,
      double gap_target) override {
    ++call_count;
    if (integer_vars.empty()) {
      kdc::ILPResult diagnostic;
      diagnostic.status = status;
      diagnostic.actual_backend = "configured-weighted-mock";
      diagnostic.solver_version = "test";
      diagnostic.objective = diagnostic_objective;
      diagnostic.lower_bound = 0.0;
      diagnostic.gap = gap;
      diagnostic.solve_time_sec = solve_time;
      diagnostic.solver_message =
          message.empty() ? "LP primal diagnostic" : message;
      diagnostic.x.clear();
      return diagnostic;
    }
    if (!std::isfinite(time_limit_sec) ||
        time_limit_sec <= 0.0 ||
        !std::isfinite(gap_target) ||
        gap_target < 0.0 ||
        rhs.size() != model.rows() ||
        costs.size() != model.cols()) {
      kdc::ILPResult invalid;
      invalid.status = kdc::ILPResult::Status::ERROR;
      invalid.solver_message = "invalid weighted MILP dimensions";
      return invalid;
    }
    const kdc::WeightIndex m = matrix.station_count();
    const kdc::WeightIndex n = matrix.point_count();
    const kdc::WeightIndex x_count = m * n;
    if (integer_vars.size() != x_count ||
        costs.size() !=
            static_cast<Eigen::Index>(x_count + m) ||
        rhs.size() !=
            static_cast<Eigen::Index>(2U * n + m * n) ||
        model.rows() != rhs.size() ||
        model.cols() != costs.size()) {
      kdc::ILPResult invalid;
      invalid.status = kdc::ILPResult::Status::ERROR;
      invalid.solver_message = "weighted MILP has wrong variable layout";
      return invalid;
    }
    for (kdc::WeightIndex index = 0U; index < x_count; ++index) {
      if (integer_vars[index] !=
          static_cast<int>(index)) {
        kdc::ILPResult invalid;
        invalid.status = kdc::ILPResult::Status::ERROR;
        invalid.solver_message =
            "weighted MILP integer variable layout is invalid";
        return invalid;
      }
    }

    kdc::ILPResult result;
    result.status = status;
    result.actual_backend = "configured-weighted-mock";
    result.solver_version = "test";
    result.lower_bound = lower_bound;
    result.gap = gap;
    result.solve_time_sec = solve_time;
    result.solver_message = message;

    if (incumbent.has_value() &&
        (status == kdc::ILPResult::Status::OPTIMAL ||
         status == kdc::ILPResult::Status::FEASIBLE ||
         status == kdc::ILPResult::Status::TIME_LIMIT)) {
      const kdc::Weight primal =
          kdc::WeightedStaticObjective::evaluate_assignment(
              matrix, *incumbent);
      result.objective = primal;
      result.x.assign(static_cast<std::size_t>(costs.size()), 0.0);
      for (kdc::WeightIndex point = 0U;
           point < n;
           ++point) {
        const kdc::WeightIndex station =
            static_cast<kdc::WeightIndex>(
                (*incumbent)[point]);
        result.x[station * n + point] = 1.0;
      }
    } else {
      result.objective = 0.0;
      result.x.clear();
    }
    return result;
  }

  std::string name() const override {
    return "configured-weighted-mock";
  }
};

kdc::Weight brute_force_weighted_minimum(
    const kdc::WeightMatrix& matrix) {
  const kdc::WeightIndex m = matrix.station_count();
  const kdc::WeightIndex n = matrix.point_count();
  kdc::Weight optimum =
      std::numeric_limits<kdc::Weight>::infinity();
  std::vector<int> assignment(n);
  const auto enumerate =
      [&](const auto& self,
          kdc::WeightIndex point) -> void {
        if (point == n) {
          optimum = std::min(
              optimum,
              kdc::WeightedStaticObjective::evaluate_assignment(
                  matrix, assignment));
          return;
        }
        for (kdc::WeightIndex station = 0U;
             station < m;
             ++station) {
          assignment[point] = static_cast<int>(station);
          self(self, point + 1U);
        }
      };
  if (m == 0U && n == 0U) {
    return 0.0;
  }
  if (m == 0U) {
    throw kdc::WeightedProblemInfeasible(
        "no brute-force assignment with zero stations");
  }
  enumerate(enumerate, 0U);
  return optimum;
}

bool weights_close(kdc::Weight lhs, kdc::Weight rhs,
                   kdc::Weight tolerance = 1e-12) {
  return std::abs(lhs - rhs) <=
         tolerance * std::max(kdc::Weight{1.0},
                              std::abs(rhs));
}
}

TEST_CASE("Exact Oracle adapter separates reported and certified bounds") {
  const kdc::WeightMatrix matrix = oracle_matrix();
  kdc::ILPResult ilp;
  ilp.status = kdc::ILPResult::Status::OPTIMAL;
  ilp.objective = std::acos(-1.0) * 2.0;
  ilp.lower_bound = std::acos(-1.0) * 2.0;
  ilp.solve_time_sec = 0.125;
  ilp.actual_backend = "test-ilp";
  ilp.solver_version = "test-1";

  const auto result = kdc::adapt_weighted_exact_ilp_result(
      matrix, std::vector<int>({0, 0}), ilp,
      kdc::WeightedArithmeticModeV1::DOUBLE,
      "unit-test exact adapter");

  REQUIRE(result.assignment.has_value());
  REQUIRE(result.assignment == std::optional<std::vector<int>>({0, 0}));
  REQUIRE(result.incumbent_feasible);
  REQUIRE(result.feasible_primal_cost.has_value());
  REQUIRE(*result.feasible_primal_cost ==
          std::acos(-1.0) * 2.0);
  REQUIRE(result.solver_reports_optimal);
  REQUIRE_FALSE(result.optimality_certified);
  REQUIRE(result.solver_best_bound.has_value());
  REQUIRE(result.solver_best_bound_validity ==
          kdc::WeightedBoundValidityV1::REPORTED_UNVALIDATED);
  REQUIRE_FALSE(result.independently_validated_lower_bound.has_value());
  REQUIRE(result.independently_validated_lower_bound_validity ==
          kdc::WeightedBoundValidityV1::ABSENT);
  REQUIRE(result.optimality_claim_possible);
  REQUIRE(result.provenance.elapsed_seconds == 0.125);
}

TEST_CASE("Exact Oracle adapter recomputes primal cost from assignment") {
  const kdc::WeightMatrix matrix = oracle_matrix();
  kdc::ILPResult ilp;
  ilp.status = kdc::ILPResult::Status::FEASIBLE;
  ilp.objective = 999.0;
  ilp.lower_bound = 0.0;
  ilp.solve_time_sec = 0.25;
  ilp.actual_backend = "test-ilp";

  const auto result = kdc::adapt_weighted_exact_ilp_result(
      matrix, std::vector<int>({1, 0}), ilp);

  REQUIRE(result.incumbent_feasible);
  REQUIRE(result.feasible_primal_cost.has_value());
  REQUIRE(*result.feasible_primal_cost ==
          std::acos(-1.0) * 2.0);
  REQUIRE_FALSE(result.solver_reports_optimal);
  REQUIRE_FALSE(result.optimality_certified);
  REQUIRE(result.solver_best_bound_validity ==
          kdc::WeightedBoundValidityV1::REPORTED_UNVALIDATED);
  REQUIRE(result.optimality_claim_possible == false);
}

TEST_CASE("Exact Oracle adapter rejects invalid assignments") {
  const kdc::WeightMatrix matrix = oracle_matrix();
  kdc::ILPResult ilp;
  ilp.status = kdc::ILPResult::Status::OPTIMAL;
  ilp.lower_bound = 0.0;

  REQUIRE_THROWS_AS(
      kdc::adapt_weighted_exact_ilp_result(
          matrix, std::vector<int>({0, 2}), ilp),
      kdc::WeightedAssignmentError);
}

TEST_CASE("Heuristic Oracle result has only incumbent and recomputed cost") {
  const kdc::WeightMatrix matrix = oracle_matrix();
  kdc::WeightedOracleProvenanceV1 provenance;
  provenance.backend_name = "test-heuristic";
  provenance.method = "assignment-adapter";
  provenance.elapsed_seconds = 0.01;

  const auto result = kdc::adapt_weighted_heuristic_assignment(
      matrix, std::vector<int>({0, 1}),
      kdc::WeightedOracleTerminationStatusV1::FEASIBLE,
      kdc::WeightedArithmeticModeV1::DOUBLE,
      provenance);

  REQUIRE(result.incumbent_feasible);
  REQUIRE(result.assignment.has_value());
  REQUIRE(result.feasible_primal_cost.has_value());
  REQUIRE(*result.feasible_primal_cost ==
          std::acos(-1.0) * 4.0);
  REQUIRE(result.termination ==
          kdc::WeightedOracleTerminationStatusV1::FEASIBLE);
}

TEST_CASE("Relaxation Oracle rejects an unverified or absent certificate") {
  kdc::WeightedOracleProvenanceV1 provenance;
  provenance.backend_name = "test-relaxation";
  provenance.method = "unknown";

  kdc::WeightedRelaxationCertificateV1 absent;
  const auto absent_result =
      kdc::adapt_weighted_relaxation_certificate(
          4.0, absent,
          kdc::WeightedArithmeticModeV1::DOUBLE,
          provenance);
  REQUIRE_FALSE(
      absent_result.independently_validated_lower_bound.has_value());
  REQUIRE(absent_result.lower_bound_validity ==
          kdc::WeightedBoundValidityV1::INVALID);

  kdc::WeightedRelaxationCertificateV1 unverified;
  unverified.present = true;
  unverified.independently_validated = false;
  unverified.method = "LP primal";
  const auto unverified_result =
      kdc::adapt_weighted_relaxation_certificate(
          4.0, unverified,
          kdc::WeightedArithmeticModeV1::DOUBLE,
          provenance);
  REQUIRE_FALSE(
      unverified_result.independently_validated_lower_bound.has_value());
  REQUIRE(unverified_result.lower_bound_validity ==
          kdc::WeightedBoundValidityV1::INVALID);
}

TEST_CASE("Relaxation Oracle accepts only a finite independently validated LB") {
  kdc::WeightedRelaxationCertificateV1 certificate;
  certificate.present = true;
  certificate.independently_validated = true;
  certificate.method = "checked-dual";
  certificate.certificate_id = "dual-1";
  certificate.digest = "test-digest";
  certificate.arithmetic =
      kdc::WeightedArithmeticModeV1::CONSERVATIVE_INTERVAL;

  kdc::WeightedOracleProvenanceV1 provenance;
  provenance.backend_name = "test-relaxation";
  provenance.method = "checked-dual";

  const auto result =
      kdc::adapt_weighted_relaxation_certificate(
          3.5, certificate,
          kdc::WeightedArithmeticModeV1::CONSERVATIVE_INTERVAL,
          provenance);
  REQUIRE(result.independently_validated_lower_bound.has_value());
  REQUIRE(*result.independently_validated_lower_bound == 3.5);
  REQUIRE(result.lower_bound_validity ==
          kdc::WeightedBoundValidityV1::VALID);

  const auto inf_result =
      kdc::adapt_weighted_relaxation_certificate(
          std::numeric_limits<kdc::Weight>::infinity(),
          certificate,
          kdc::WeightedArithmeticModeV1::CONSERVATIVE_INTERVAL,
          provenance);
  REQUIRE_FALSE(
      inf_result.independently_validated_lower_bound.has_value());
  REQUIRE(inf_result.lower_bound_validity ==
          kdc::WeightedBoundValidityV1::INVALID);
}

TEST_CASE("Unavailable Oracle factory respects cancellable budgets") {
  auto exact = kdc::WeightedOracleFactory::unavailable_exact();
  auto heuristic = kdc::WeightedOracleFactory::unavailable_heuristic();
  auto relaxation = kdc::WeightedOracleFactory::unavailable_relaxation();
  const kdc::WeightMatrix matrix = oracle_matrix();

  kdc::SolverBudget live(1.0);
  REQUIRE(exact->solve(matrix, &live).termination ==
          kdc::WeightedOracleTerminationStatusV1::NOT_IMPLEMENTED);
  REQUIRE(heuristic->solve(matrix, &live).termination ==
          kdc::WeightedOracleTerminationStatusV1::NOT_IMPLEMENTED);
  REQUIRE(relaxation->solve(matrix, &live).termination ==
          kdc::WeightedOracleTerminationStatusV1::NOT_IMPLEMENTED);

  kdc::SolverBudget expired(0.0);
  REQUIRE_THROWS_AS(exact->solve(matrix, &expired),
                    kdc::SolverBudgetExpired);
  REQUIRE_THROWS_AS(heuristic->solve(matrix, &expired),
                    kdc::SolverBudgetExpired);
  REQUIRE_THROWS_AS(relaxation->solve(matrix, &expired),
                    kdc::SolverBudgetExpired);
}

TEST_CASE("ILP Exact Weighted Oracle matches brute force on small matrices") {
  kdc::WeightMatrix one(1U, 1U, {4.0});
  ConfiguredWeightedMockILP one_mock(one);
  one_mock.status = kdc::ILPResult::Status::OPTIMAL;
  one_mock.incumbent = std::vector<int>({0});
  one_mock.lower_bound = std::acos(-1.0) * 4.0;
  one_mock.gap = 0.0;
  one_mock.message = "optimal";
  auto one_oracle =
      kdc::WeightedOracleFactory::ilp_exact(&one_mock, 1.0, 0.0);
  const auto one_result = one_oracle->solve(one);

  REQUIRE(one_result.termination ==
          kdc::WeightedOracleTerminationStatusV1::OPTIMAL);
  REQUIRE(one_result.solver_reports_optimal);
  REQUIRE_FALSE(one_result.optimality_certified);
  REQUIRE(one_result.incumbent_feasible);
  REQUIRE(one_result.feasible_primal_cost.has_value());
  REQUIRE(*one_result.feasible_primal_cost ==
          std::acos(-1.0) * 4.0);
  REQUIRE(brute_force_weighted_minimum(one) ==
          *one_result.feasible_primal_cost);
  REQUIRE(one_result.backend_primal_objective.has_value());
  REQUIRE(*one_result.backend_primal_objective ==
          *one_result.feasible_primal_cost);
  REQUIRE(one_result.solver_gap.has_value());
  REQUIRE(*one_result.solver_gap == 0.0);
  REQUIRE(one_mock.call_count == 1);

  kdc::WeightMatrix two =
      kdc::WeightMatrix::from_rows(
          {{2.0, 1.0}, {1.0, 2.0}});
  ConfiguredWeightedMockILP two_mock(two);
  two_mock.status = kdc::ILPResult::Status::OPTIMAL;
  two_mock.incumbent = std::vector<int>({0, 0});
  two_mock.lower_bound = std::acos(-1.0) * 2.0;
  two_mock.gap = 0.0;
  two_mock.message = "optimal";
  auto two_oracle =
      kdc::WeightedOracleFactory::ilp_exact(&two_mock, 1.0, 0.0);
  const auto two_result = two_oracle->solve(two);

  REQUIRE(two_result.termination ==
          kdc::WeightedOracleTerminationStatusV1::OPTIMAL);
  REQUIRE(two_result.incumbent_feasible);
  REQUIRE(two_result.feasible_primal_cost.has_value());
  REQUIRE(brute_force_weighted_minimum(two) ==
          *two_result.feasible_primal_cost);
  REQUIRE(*two_result.feasible_primal_cost ==
          std::acos(-1.0) * 2.0);
  REQUIRE(two_result.backend_primal_objective.has_value());
  REQUIRE(*two_result.backend_primal_objective ==
          *two_result.feasible_primal_cost);
}

TEST_CASE("ILP Exact Weighted Oracle handles all-zero and empty dimensions") {
  kdc::WeightMatrix zero =
      kdc::WeightMatrix::from_rows(
          {{0.0, 0.0}, {0.0, 0.0}});
  ConfiguredWeightedMockILP zero_mock(zero);
  auto zero_oracle =
      kdc::WeightedOracleFactory::ilp_exact(&zero_mock, 1.0, 0.0);
  const auto zero_result = zero_oracle->solve(zero);

  REQUIRE(zero_result.termination ==
          kdc::WeightedOracleTerminationStatusV1::OPTIMAL);
  REQUIRE(zero_result.incumbent_feasible);
  REQUIRE(zero_result.assignment.has_value());
  REQUIRE(*zero_result.assignment ==
          std::vector<int>({0, 0}));
  REQUIRE(zero_result.feasible_primal_cost.has_value());
  REQUIRE(*zero_result.feasible_primal_cost == 0.0);
  REQUIRE_FALSE(zero_result.optimality_certified);
  REQUIRE(zero_mock.call_count == 0);

  kdc::WeightMatrix empty_points(3U, 0U);
  ConfiguredWeightedMockILP empty_mock(empty_points);
  auto empty_oracle =
      kdc::WeightedOracleFactory::ilp_exact(&empty_mock, 1.0, 0.0);
  const auto empty_result = empty_oracle->solve(empty_points);
  REQUIRE(empty_result.termination ==
          kdc::WeightedOracleTerminationStatusV1::OPTIMAL);
  REQUIRE(empty_result.incumbent_feasible);
  REQUIRE(empty_result.assignment.has_value());
  REQUIRE(empty_result.assignment->empty());
  REQUIRE(*empty_result.feasible_primal_cost == 0.0);
  REQUIRE(empty_mock.call_count == 0);

  kdc::WeightMatrix no_stations(0U, 2U);
  ConfiguredWeightedMockILP no_station_mock(no_stations);
  auto no_station_oracle =
      kdc::WeightedOracleFactory::ilp_exact(
          &no_station_mock, 1.0, 0.0);
  const auto no_station_result =
      no_station_oracle->solve(no_stations);
  REQUIRE(no_station_result.termination ==
          kdc::WeightedOracleTerminationStatusV1::INFEASIBLE);
  REQUIRE_FALSE(no_station_result.incumbent_feasible);
  REQUIRE_FALSE(no_station_result.assignment.has_value());
  REQUIRE(no_station_mock.call_count == 0);
}

TEST_CASE("ILP Exact Weighted Oracle distinguishes termination cases") {
  kdc::WeightMatrix matrix =
      kdc::WeightMatrix::from_rows(
          {{3.0, 1.0}, {1.0, 3.0}});

  ConfiguredWeightedMockILP timed(matrix);
  timed.status = kdc::ILPResult::Status::TIME_LIMIT;
  timed.incumbent = std::vector<int>({1, 0});
  timed.lower_bound = std::acos(-1.0) * 2.0;
  timed.gap = 0.25;
  timed.message = "time limit";
  auto timed_oracle =
      kdc::WeightedOracleFactory::ilp_exact(&timed, 1.0, 0.0);
  const auto timed_result = timed_oracle->solve(matrix);
  REQUIRE(timed_result.termination ==
          kdc::WeightedOracleTerminationStatusV1::TIME_LIMIT);
  REQUIRE(timed_result.incumbent_feasible);
  REQUIRE(timed_result.assignment.has_value());
  REQUIRE(timed_result.solver_gap.has_value());
  REQUIRE(*timed_result.solver_gap == 0.25);
  REQUIRE_FALSE(timed_result.optimality_certified);

  ConfiguredWeightedMockILP timed_empty(matrix);
  timed_empty.status = kdc::ILPResult::Status::TIME_LIMIT;
  timed_empty.lower_bound = 0.0;
  timed_empty.gap = 1.0;
  timed_empty.message = "time limit";
  auto timed_empty_oracle =
      kdc::WeightedOracleFactory::ilp_exact(
          &timed_empty, 1.0, 0.0);
  const auto timed_empty_result =
      timed_empty_oracle->solve(matrix);
  REQUIRE(timed_empty_result.termination ==
          kdc::WeightedOracleTerminationStatusV1::TIME_LIMIT);
  REQUIRE_FALSE(timed_empty_result.incumbent_feasible);
  REQUIRE_FALSE(timed_empty_result.assignment.has_value());

  ConfiguredWeightedMockILP interrupted(matrix);
  interrupted.status = kdc::ILPResult::Status::TIME_LIMIT;
  interrupted.incumbent = std::vector<int>({0, 0});
  interrupted.message = "solver interrupted";
  auto interrupted_oracle =
      kdc::WeightedOracleFactory::ilp_exact(
          &interrupted, 1.0, 0.0);
  const auto interrupted_result =
      interrupted_oracle->solve(matrix);
  REQUIRE(interrupted_result.termination ==
          kdc::WeightedOracleTerminationStatusV1::INTERRUPTED);

  ConfiguredWeightedMockILP infeasible(matrix);
  infeasible.status = kdc::ILPResult::Status::INFEASIBLE;
  infeasible.message = "infeasible";
  auto infeasible_oracle =
      kdc::WeightedOracleFactory::ilp_exact(
          &infeasible, 1.0, 0.0);
  const auto infeasible_result =
      infeasible_oracle->solve(matrix);
  REQUIRE(infeasible_result.termination ==
          kdc::WeightedOracleTerminationStatusV1::INFEASIBLE);
  REQUIRE_FALSE(infeasible_result.incumbent_feasible);

  ConfiguredWeightedMockILP numeric(matrix);
  numeric.status = kdc::ILPResult::Status::ERROR;
  numeric.message = "numeric failure: NAN/INF";
  auto numeric_oracle =
      kdc::WeightedOracleFactory::ilp_exact(
          &numeric, 1.0, 0.0);
  const auto numeric_result =
      numeric_oracle->solve(matrix);
  REQUIRE(numeric_result.termination ==
          kdc::WeightedOracleTerminationStatusV1::NUMERIC_FAILURE);
  REQUIRE_FALSE(numeric_result.incumbent_feasible);

  ConfiguredWeightedMockILP generic_error(matrix);
  generic_error.status = kdc::ILPResult::Status::ERROR;
  generic_error.message = "backend unavailable";
  auto generic_error_oracle =
      kdc::WeightedOracleFactory::ilp_exact(
          &generic_error, 1.0, 0.0);
  const auto generic_error_result =
      generic_error_oracle->solve(matrix);
  REQUIRE(generic_error_result.termination ==
          kdc::WeightedOracleTerminationStatusV1::ERROR);
}

TEST_CASE("Greedy Weighted Heuristic is feasible and UB-only") {
  kdc::WeightMatrix matrix =
      kdc::WeightMatrix::from_rows(
          {{2.0, 1.0}, {1.0, 2.0}});
  auto oracle =
      kdc::WeightedOracleFactory::greedy_heuristic();
  const auto result = oracle->solve(matrix);

  REQUIRE(result.incumbent_feasible);
  REQUIRE(result.assignment.has_value());
  REQUIRE(result.assignment->size() == 2U);
  for (const int station : *result.assignment) {
    REQUIRE(station >= 0);
    REQUIRE(station < 2);
  }
  REQUIRE(result.feasible_primal_cost.has_value());
  REQUIRE(brute_force_weighted_minimum(matrix) <=
          *result.feasible_primal_cost);
  REQUIRE(result.provenance.backend_name == "greedy-weighted");

  kdc::WeightMatrix zero =
      kdc::WeightMatrix::from_rows(
          {{0.0, 0.0}, {0.0, 0.0}});
  const auto zero_result = oracle->solve(zero);
  REQUIRE(zero_result.incumbent_feasible);
  REQUIRE(*zero_result.feasible_primal_cost == 0.0);
}

TEST_CASE("Weighted LP dual validation is numerical-only without certification") {
  kdc::WeightMatrix one(1U, 1U, {1.0});
  std::vector<kdc::Weight> y_one{std::acos(-1.0)};
  std::vector<kdc::Weight> z_one{std::acos(-1.0)};
  const auto dual_one =
      kdc::validate_weighted_lp_dual(one, y_one, z_one);

  REQUIRE_FALSE(dual_one.valid);
  REQUIRE(dual_one.numerical_diagnostic_valid);
  REQUIRE_FALSE(dual_one.certificate.present);
  REQUIRE_FALSE(dual_one.certificate.independently_validated);
  REQUIRE_FALSE(dual_one.directed_lower_bound.has_value());
  REQUIRE(dual_one.numerical_diagnostic_lower_bound.has_value());
  REQUIRE(weights_close(*dual_one.numerical_diagnostic_lower_bound,
                        std::acos(-1.0)));
  REQUIRE(dual_one.arithmetic ==
          kdc::WeightedArithmeticModeV1::DOUBLE);
  const auto requested_interval_mode =
      kdc::validate_weighted_lp_dual(
          one, y_one, z_one,
          kdc::WeightedArithmeticModeV1::CONSERVATIVE_INTERVAL);
  REQUIRE(requested_interval_mode.numerical_diagnostic_valid);
  REQUIRE_FALSE(requested_interval_mode.valid);
  REQUIRE_FALSE(requested_interval_mode.certificate.present);
  REQUIRE_FALSE(
      requested_interval_mode.certificate.independently_validated);

  kdc::WeightMatrix equal_two =
      kdc::WeightMatrix::from_rows({{1.0}, {1.0}});
  std::vector<kdc::Weight> y_two{std::acos(-1.0)};
  std::vector<kdc::Weight> z_two{
      std::acos(-1.0),
      std::acos(-1.0)};
  const auto dual_two =
      kdc::validate_weighted_lp_dual(
          equal_two, y_two, z_two);
  REQUIRE_FALSE(dual_two.valid);
  REQUIRE(dual_two.numerical_diagnostic_valid);
  REQUIRE_FALSE(dual_two.directed_lower_bound.has_value());
  REQUIRE(dual_two.numerical_diagnostic_lower_bound.has_value());
  REQUIRE(weights_close(*dual_two.numerical_diagnostic_lower_bound,
                        std::acos(-1.0)));
}

TEST_CASE("Weighted LP dual validation rejects unfeasible and degenerate duals") {
  kdc::WeightMatrix matrix =
      kdc::WeightMatrix::from_rows(
          {{2.0, 1.0}, {1.0, 2.0}});

  std::vector<kdc::Weight> negative_z(
      4U, -1.0);
  std::vector<kdc::Weight> y{
      std::acos(-1.0),
      std::acos(-1.0)};
  const auto negative =
      kdc::validate_weighted_lp_dual(
          matrix, y, negative_z);
  REQUIRE_FALSE(negative.numerical_diagnostic_valid);
  REQUIRE_FALSE(negative.certificate.independently_validated);
  REQUIRE_FALSE(negative.directed_lower_bound.has_value());

  std::vector<kdc::Weight> wrong_size_z{1.0};
  const auto wrong_size =
      kdc::validate_weighted_lp_dual(
          matrix, y, wrong_size_z);
  REQUIRE_FALSE(wrong_size.valid);
  REQUIRE_FALSE(wrong_size.directed_lower_bound.has_value());

  kdc::WeightMatrix zero =
      kdc::WeightMatrix::from_rows(
          {{0.0, 0.0}, {0.0, 0.0}});
  std::vector<kdc::Weight> zero_y{0.0, 0.0};
  std::vector<kdc::Weight> zero_z{0.0, 0.0, 0.0, 0.0};
  const auto zero_dual =
      kdc::validate_weighted_lp_dual(
          zero, zero_y, zero_z);
  REQUIRE_FALSE(zero_dual.valid);
  REQUIRE(zero_dual.numerical_diagnostic_valid);
  REQUIRE_FALSE(zero_dual.directed_lower_bound.has_value());
  REQUIRE(zero_dual.numerical_diagnostic_lower_bound.has_value());
  REQUIRE(*zero_dual.numerical_diagnostic_lower_bound == 0.0);
}

TEST_CASE("Numerical LP dual diagnostic cannot become an evaluator LB") {
  const kdc::WeightMatrix matrix =
      kdc::WeightMatrix::from_rows({{1.0}});
  const auto diagnostic = kdc::validate_weighted_lp_dual(
      matrix, {std::acos(-1.0)}, {std::acos(-1.0)});
  const auto relaxation = kdc::adapt_weighted_relaxation_certificate(
      diagnostic.directed_lower_bound,
      diagnostic.certificate,
      diagnostic.arithmetic);

  REQUIRE(diagnostic.numerical_diagnostic_valid);
  REQUIRE_FALSE(relaxation.independently_validated_lower_bound.has_value());
  REQUIRE(relaxation.lower_bound_validity ==
          kdc::WeightedBoundValidityV1::INVALID);
  REQUIRE_FALSE(relaxation.certificate.independently_validated);
}

TEST_CASE("LP Relaxation runs diagnostically without an official LB") {
  kdc::WeightMatrix matrix =
      kdc::WeightMatrix::from_rows(
          {{2.0, 1.0}, {1.0, 2.0}});
  ConfiguredWeightedMockILP mock(matrix);
  mock.status = kdc::ILPResult::Status::OPTIMAL;
  mock.diagnostic_objective =
      std::acos(-1.0) * 1.5;

  auto relaxation =
      kdc::WeightedOracleFactory::lp_relaxation(
          &mock, 1.0, 0.0);
  const auto result = relaxation->solve(matrix);

  REQUIRE(result.diagnostic_primal_objective.has_value());
  REQUIRE_FALSE(
      result.independently_validated_lower_bound.has_value());
  REQUIRE(result.lower_bound_validity ==
          kdc::WeightedBoundValidityV1::INVALID);
  REQUIRE_FALSE(result.certificate.independently_validated);
  REQUIRE(mock.call_count == 1);
  REQUIRE(result.provenance.method == "lp-primal-diagnostic");
}
