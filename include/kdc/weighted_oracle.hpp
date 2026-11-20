#pragma once

#include "kdc/solver_budget.hpp"
#include "kdc/solver_interface.hpp"
#include "kdc/weighted_static.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace kdc {

enum class WeightedOracleTerminationStatusV1 {
  NOT_IMPLEMENTED,
  OPTIMAL,
  FEASIBLE,
  TIME_LIMIT,
  INTERRUPTED,
  INFEASIBLE,
  UNBOUNDED,
  NUMERIC_FAILURE,
  ERROR
};

enum class WeightedBoundValidityV1 {
  ABSENT,
  REPORTED_UNVALIDATED,
  VALID,
  INVALID
};

enum class WeightedArithmeticModeV1 {
  UNKNOWN,
  DOUBLE,
  CONSERVATIVE_INTERVAL,
  EXACT_RATIONAL
};

struct WeightedOracleProvenanceV1 {
  std::uint32_t schema_version{1};
  std::string backend_name{"unknown"};
  std::string backend_version{"unknown"};
  std::string method{"none"};
  std::string source{"none"};
  std::string cache_key;
  double elapsed_seconds{0.0};
  std::uint64_t iteration_count{0};
  std::uint64_t node_count{0};
};

struct WeightedRelaxationCertificateV1 {
  std::uint32_t schema_version{1};
  bool present{false};
  bool independently_validated{false};
  std::string method{"none"};
  std::string certificate_id;
  std::string digest;
  WeightedArithmeticModeV1 arithmetic{
      WeightedArithmeticModeV1::UNKNOWN};
};

struct WeightedExactOracleResultV1 {
  std::uint32_t schema_version{1};
  std::optional<std::vector<int>> assignment;
  bool incumbent_feasible{false};
  std::optional<Weight> feasible_primal_cost;
  std::optional<Weight> backend_primal_objective;
  std::optional<Weight> solver_gap;
  std::string solver_message;

  bool solver_reports_optimal{false};
  bool optimality_claim_possible{false};
  bool optimality_certified{false};
  std::optional<Weight> solver_best_bound;
  WeightedBoundValidityV1 solver_best_bound_validity{
      WeightedBoundValidityV1::ABSENT};

  std::optional<Weight> independently_validated_lower_bound;
  WeightedBoundValidityV1 independently_validated_lower_bound_validity{
      WeightedBoundValidityV1::ABSENT};

  WeightedOracleTerminationStatusV1 termination{
      WeightedOracleTerminationStatusV1::NOT_IMPLEMENTED};
  WeightedArithmeticModeV1 arithmetic{
      WeightedArithmeticModeV1::UNKNOWN};
  WeightedOracleProvenanceV1 provenance;
};

struct WeightedHeuristicOracleResultV1 {
  std::uint32_t schema_version{1};
  std::optional<std::vector<int>> assignment;
  bool incumbent_feasible{false};
  std::optional<Weight> feasible_primal_cost;
  WeightedOracleTerminationStatusV1 termination{
      WeightedOracleTerminationStatusV1::NOT_IMPLEMENTED};
  WeightedArithmeticModeV1 arithmetic{
      WeightedArithmeticModeV1::UNKNOWN};
  WeightedOracleProvenanceV1 provenance;
};

struct WeightedRelaxationOracleResultV1 {
  std::uint32_t schema_version{1};
  std::optional<Weight> independently_validated_lower_bound;
  WeightedBoundValidityV1 lower_bound_validity{
      WeightedBoundValidityV1::ABSENT};
  WeightedRelaxationCertificateV1 certificate;
  std::optional<Weight> diagnostic_primal_objective;
  WeightedOracleTerminationStatusV1 termination{
      WeightedOracleTerminationStatusV1::NOT_IMPLEMENTED};
  WeightedArithmeticModeV1 arithmetic{
      WeightedArithmeticModeV1::UNKNOWN};
  WeightedOracleProvenanceV1 provenance;
};

struct WeightedLPDualValidationV1 {
  std::uint32_t schema_version{1};
  // `valid` is retained for source compatibility and is always false until
  // dual feasibility can be checked with a validated arithmetic backend.
  bool valid{false};
  bool numerical_diagnostic_valid{false};
  std::optional<Weight> directed_lower_bound;
  std::optional<Weight> numerical_diagnostic_lower_bound;
  std::optional<Weight> max_constraint_residual;
  WeightedArithmeticModeV1 arithmetic{WeightedArithmeticModeV1::UNKNOWN};
  WeightedRelaxationCertificateV1 certificate;
  std::string reason;
};

class IWeightedExactOracle {
 public:
  virtual ~IWeightedExactOracle() = default;
  virtual WeightedExactOracleResultV1 solve(
      const WeightMatrix& matrix,
      SolverBudget* budget = nullptr) = 0;
  virtual std::string name() const = 0;
};

class IWeightedHeuristicOracle {
 public:
  virtual ~IWeightedHeuristicOracle() = default;
  virtual WeightedHeuristicOracleResultV1 solve(
      const WeightMatrix& matrix,
      SolverBudget* budget = nullptr) = 0;
  virtual std::string name() const = 0;
};

class IWeightedRelaxationOracle {
 public:
  virtual ~IWeightedRelaxationOracle() = default;
  virtual WeightedRelaxationOracleResultV1 solve(
      const WeightMatrix& matrix,
      SolverBudget* budget = nullptr) = 0;
  virtual std::string name() const = 0;
};

class GreedyWeightedHeuristicOracle final
    : public IWeightedHeuristicOracle {
 public:
  WeightedHeuristicOracleResultV1 solve(
      const WeightMatrix& matrix,
      SolverBudget* budget = nullptr) override;
  std::string name() const override {
    return "greedy-weighted";
  }
};

class WeightedLPRelaxationOracle final
    : public IWeightedRelaxationOracle {
 public:
  struct Config {
    double time_limit_seconds{600.0};
    double gap_target{0.0};
  };

  explicit WeightedLPRelaxationOracle(ILPSolver* ilp);
  WeightedLPRelaxationOracle(ILPSolver* ilp, Config config);

  WeightedRelaxationOracleResultV1 solve(
      const WeightMatrix& matrix,
      SolverBudget* budget = nullptr) override;
  std::string name() const override {
    return "lp-weighted-relaxation";
  }

 private:
  ILPSolver* ilp_{nullptr};
  Config config_;
};

class ILPWeightedExactOracle final : public IWeightedExactOracle {
 public:
  struct Config {
    double time_limit_seconds{600.0};
    double gap_target{0.0};
  };

  explicit ILPWeightedExactOracle(ILPSolver* ilp);
  ILPWeightedExactOracle(ILPSolver* ilp, Config config);

  WeightedExactOracleResultV1 solve(
      const WeightMatrix& matrix,
      SolverBudget* budget = nullptr) override;
  std::string name() const override {
    return "ilp-weighted-exact";
  }

 private:
  ILPSolver* ilp_{nullptr};
  Config config_;
};

class WeightedOracleFactory {
 public:
  static std::unique_ptr<IWeightedExactOracle> unavailable_exact();
  static std::unique_ptr<IWeightedExactOracle> ilp_exact(
      ILPSolver* ilp,
      double time_limit_seconds = 600.0,
      double gap_target = 0.0);
  static std::unique_ptr<IWeightedHeuristicOracle> unavailable_heuristic();
  static std::unique_ptr<IWeightedHeuristicOracle> greedy_heuristic();
  static std::unique_ptr<IWeightedRelaxationOracle> unavailable_relaxation();
  static std::unique_ptr<IWeightedRelaxationOracle> lp_relaxation(
      ILPSolver* ilp,
      double time_limit_seconds = 600.0,
      double gap_target = 0.0);
};

// Existing ILPResult has a solver-reported lower_bound field but no dual
// certificate field or independently checked certificate provenance.
// Therefore this adapter may expose a reported/suggested bound, but it cannot
// set independent_lower_bound_validity=VALID or optimality_certified=true.
WeightedExactOracleResultV1 adapt_weighted_exact_ilp_result(
    const WeightMatrix& matrix,
    const std::optional<std::vector<int>>& assignment,
    const ILPResult& ilp_result,
    WeightedArithmeticModeV1 arithmetic =
        WeightedArithmeticModeV1::UNKNOWN,
    const std::string& source = "ILPResult adapter");

WeightedHeuristicOracleResultV1 adapt_weighted_heuristic_assignment(
    const WeightMatrix& matrix,
    const std::optional<std::vector<int>>& assignment,
    WeightedOracleTerminationStatusV1 termination =
        WeightedOracleTerminationStatusV1::FEASIBLE,
    WeightedArithmeticModeV1 arithmetic =
        WeightedArithmeticModeV1::UNKNOWN,
    WeightedOracleProvenanceV1 provenance = {});

WeightedRelaxationOracleResultV1 adapt_weighted_relaxation_certificate(
    const std::optional<Weight>& reported_lower_bound,
    const WeightedRelaxationCertificateV1& certificate,
    WeightedArithmeticModeV1 arithmetic =
        WeightedArithmeticModeV1::UNKNOWN,
    WeightedOracleProvenanceV1 provenance = {});

// Validates the LP dual:
// max sum_j y_j
// s.t. y_j - w_ij z_ij <= 0 for all i,j
//      sum_j z_ij <= pi for all i
//      z_ij >= 0
// y_j is free.
//
// The current implementation returns only a numerical diagnostic. It never
// issues a lower-bound certificate until validated arithmetic is available.
WeightedLPDualValidationV1 validate_weighted_lp_dual(
    const WeightMatrix& matrix,
    const std::vector<Weight>& y,
    const std::vector<Weight>& z,
    WeightedArithmeticModeV1 arithmetic =
        WeightedArithmeticModeV1::UNKNOWN);

}  // namespace kdc
