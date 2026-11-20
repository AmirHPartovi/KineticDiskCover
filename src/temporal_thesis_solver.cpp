#include "kdc/temporal_thesis_solver.hpp"

#include "kdc/global_bound_history.hpp"
#include "kdc/weighted_geometry.hpp"
#include "kdc/weighted_oracle.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace kdc {
namespace {
using SteadyClock = std::chrono::steady_clock;

class OracleCallCapExceeded final : public std::runtime_error {
 public:
  OracleCallCapExceeded()
      : std::runtime_error("maximum Oracle call count reached") {}
};

void record_oracle_call(
    std::uint64_t& count,
    std::uint64_t maximum,
    TemporalOracleStatisticsV2* statistics,
    std::uint64_t* role_count) {
  if (count >= maximum) {
    throw OracleCallCapExceeded();
  }
  ++count;
  if (statistics != nullptr) {
    ++statistics->total_calls;
    ++(*role_count);
  }
}

class CountingExactOracle final : public IWeightedExactOracle {
 public:
  CountingExactOracle(
      IWeightedExactOracle& inner,
      std::uint64_t& count,
      std::uint64_t maximum,
      double time_limit_seconds,
      TemporalOracleStatisticsV2* statistics)
      : inner_(inner),
        count_(count),
        maximum_(maximum),
        time_limit_seconds_(time_limit_seconds),
        statistics_(statistics) {}

  WeightedExactOracleResultV1 solve(
      const WeightMatrix& matrix,
      SolverBudget* budget) override {
    record_oracle_call(
        count_, maximum_, statistics_,
        statistics_ == nullptr ? nullptr : &statistics_->exact_calls);
    if (budget != nullptr) {
      budget->checkpoint();
    }
    SolverBudget local_budget(
        budget == nullptr
            ? time_limit_seconds_
            : std::min(time_limit_seconds_,
                       budget->remaining_seconds()));
    WeightedExactOracleResultV1 result;
    try {
      result = inner_.solve(matrix, &local_budget);
    } catch (...) {
      if (statistics_ != nullptr) {
        ++statistics_->failures;
      }
      throw;
    }
    local_budget.checkpoint();
    if (budget != nullptr) {
      budget->checkpoint();
    }
    if (statistics_ != nullptr &&
        !result.incumbent_feasible) {
      ++statistics_->failures;
    }
    return result;
  }

  std::string name() const override {
    return inner_.name();
  }

 private:
  IWeightedExactOracle& inner_;
  std::uint64_t& count_;
  std::uint64_t maximum_;
  double time_limit_seconds_;
  TemporalOracleStatisticsV2* statistics_;
};

class CountingHeuristicOracle final : public IWeightedHeuristicOracle {
 public:
  CountingHeuristicOracle(
      IWeightedHeuristicOracle& inner,
      std::uint64_t& count,
      std::uint64_t maximum,
      double time_limit_seconds,
      TemporalOracleStatisticsV2* statistics)
      : inner_(inner),
        count_(count),
        maximum_(maximum),
        time_limit_seconds_(time_limit_seconds),
        statistics_(statistics) {}

  WeightedHeuristicOracleResultV1 solve(
      const WeightMatrix& matrix,
      SolverBudget* budget) override {
    record_oracle_call(
        count_, maximum_, statistics_,
        statistics_ == nullptr ? nullptr : &statistics_->heuristic_calls);
    if (budget != nullptr) {
      budget->checkpoint();
    }
    SolverBudget local_budget(
        budget == nullptr
            ? time_limit_seconds_
            : std::min(time_limit_seconds_,
                       budget->remaining_seconds()));
    WeightedHeuristicOracleResultV1 result;
    try {
      result = inner_.solve(matrix, &local_budget);
    } catch (...) {
      if (statistics_ != nullptr) {
        ++statistics_->failures;
      }
      throw;
    }
    local_budget.checkpoint();
    if (budget != nullptr) {
      budget->checkpoint();
    }
    if (statistics_ != nullptr && !result.incumbent_feasible) {
      ++statistics_->failures;
    }
    return result;
  }

  std::string name() const override {
    return inner_.name();
  }

 private:
  IWeightedHeuristicOracle& inner_;
  std::uint64_t& count_;
  std::uint64_t maximum_;
  double time_limit_seconds_;
  TemporalOracleStatisticsV2* statistics_;
};

class CountingRelaxationOracle final : public IWeightedRelaxationOracle {
 public:
  CountingRelaxationOracle(
      IWeightedRelaxationOracle& inner,
      std::uint64_t& count,
      std::uint64_t maximum,
      double time_limit_seconds,
      TemporalOracleStatisticsV2* statistics)
      : inner_(inner),
        count_(count),
        maximum_(maximum),
        time_limit_seconds_(time_limit_seconds),
        statistics_(statistics) {}

  WeightedRelaxationOracleResultV1 solve(
      const WeightMatrix& matrix,
      SolverBudget* budget) override {
    record_oracle_call(
        count_, maximum_, statistics_,
        statistics_ == nullptr
            ? nullptr
            : &statistics_->relaxation_calls);
    if (budget != nullptr) {
      budget->checkpoint();
    }
    SolverBudget local_budget(
        budget == nullptr
            ? time_limit_seconds_
            : std::min(time_limit_seconds_,
                       budget->remaining_seconds()));
    WeightedRelaxationOracleResultV1 result;
    try {
      result = inner_.solve(matrix, &local_budget);
    } catch (...) {
      if (statistics_ != nullptr) {
        ++statistics_->failures;
      }
      throw;
    }
    local_budget.checkpoint();
    if (budget != nullptr) {
      budget->checkpoint();
    }
    if (statistics_ != nullptr &&
        result.termination !=
            WeightedOracleTerminationStatusV1::OPTIMAL &&
        result.termination !=
            WeightedOracleTerminationStatusV1::FEASIBLE) {
      ++statistics_->failures;
    }
    return result;
  }

  std::string name() const override {
    return inner_.name();
  }

 private:
  IWeightedRelaxationOracle& inner_;
  std::uint64_t& count_;
  std::uint64_t maximum_;
  double time_limit_seconds_;
  TemporalOracleStatisticsV2* statistics_;
};

std::string local_instance_hash(const Instance& instance) {
  std::ostringstream canonical;
  canonical << std::setprecision(
                   std::numeric_limits<double>::max_digits10)
            << instance.n << ';' << instance.m << ';'
            << instance.T_end << ';' << instance.name;
  for (const auto& station : instance.stations) {
    canonical << ';' << station.id << ';'
              << station.pos.x << ';' << station.pos.y;
  }
  for (const auto& trajectory : instance.trajectories) {
    canonical << ";trajectory";
    for (double breakpoint : trajectory.t_breaks) {
      canonical << ';' << breakpoint;
    }
    for (const auto& waypoint : trajectory.waypoints) {
      canonical << ';' << waypoint.x << ';' << waypoint.y;
    }
  }

  std::uint64_t hash = UINT64_C(14695981039346656037);
  for (const unsigned char byte : canonical.str()) {
    hash ^= byte;
    hash *= UINT64_C(1099511628211);
  }
  std::ostringstream encoded;
  encoded << "fnv1a64:" << std::hex << hash;
  return encoded.str();
}

TemporalBoundRecordV1 trivial_lower_bound() {
  TemporalBoundRecordV1 bound;
  bound.value = 0.0;
  bound.validity = TemporalBoundValidityV1::VALID;
  bound.method = "TRIVIAL_NONNEGATIVE";
  bound.certificate_id = "model-a-nonnegative-integral-v1";
  bound.provenance =
      "J is an integral of nonnegative disk areas";
  bound.arithmetic = TemporalBoundArithmeticV1::EXACT_RATIONAL;
  return bound;
}

TemporalBoundRecordV1 absent_upper_bound(
    const std::string& reason) {
  TemporalBoundRecordV1 bound;
  bound.value = std::numeric_limits<double>::infinity();
  bound.validity = TemporalBoundValidityV1::ABSENT;
  bound.method = "NONE";
  bound.provenance = reason;
  bound.arithmetic = TemporalBoundArithmeticV1::FAST_DOUBLE;
  return bound;
}

bool valid_assignment(
    const Instance& instance,
    const std::vector<int>& assignment) {
  if (assignment.size() !=
      static_cast<std::size_t>(instance.n)) {
    return false;
  }
  return std::all_of(
      assignment.begin(),
      assignment.end(),
      [&instance](int station) {
        return station >= 0 && station < instance.m;
      });
}

TemporalPiecewiseProgramV1 make_program(
    const Instance& instance,
    const WeightedGeometry& geometry,
    double start,
    double end,
    const IntervalEvaluatorResultV1& evaluated) {
  if (!evaluated.assignment.has_value() ||
      !valid_assignment(instance, *evaluated.assignment)) {
    throw std::invalid_argument(
        "cannot construct a program without a complete assignment");
  }

  TemporalPiecewiseProgramV1 program;
  program.point_assignment = *evaluated.assignment;
  if (evaluated.upper_method ==
      IntervalUpperMethodV1::DIRECT_UPPER_ENVELOPE) {
    for (const auto& piece : evaluated.envelope.pieces) {
      if (piece.station_radii.size() !=
          static_cast<std::size_t>(instance.m)) {
        throw std::invalid_argument(
            "direct envelope piece has an invalid station vector");
      }
      TemporalRadiusPieceV1 radius_piece;
      radius_piece.start = piece.start;
      radius_piece.end = piece.end;
      radius_piece.station_radii = piece.station_radii;
      program.pieces.push_back(std::move(radius_piece));
    }
  } else if (evaluated.upper_method ==
             IntervalUpperMethodV1::DPLUS_FALLBACK) {
    const WeightMatrix upper =
        geometry.bounds_on(start, end).upper.matrix;
    TemporalRadiusPieceV1 radius_piece;
    radius_piece.start = start;
    radius_piece.end = end;
    radius_piece.station_radii.assign(
        static_cast<std::size_t>(instance.m), 0.0);
    for (int station = 0; station < instance.m; ++station) {
      Weight maximum_squared_distance = 0.0;
      for (int point = 0; point < instance.n; ++point) {
        if (program.point_assignment[
                static_cast<std::size_t>(point)] == station) {
          maximum_squared_distance =
              std::max(
                  maximum_squared_distance,
                  upper.weight(
                      static_cast<WeightIndex>(station),
                      static_cast<WeightIndex>(point)));
        }
      }
      radius_piece.station_radii[
          static_cast<std::size_t>(station)] =
          std::sqrt(maximum_squared_distance);
    }
    program.pieces.push_back(std::move(radius_piece));
  } else {
    throw std::invalid_argument(
        "interval evaluation did not produce a program method");
  }

  if (program.pieces.empty()) {
    TemporalRadiusPieceV1 empty_program_piece;
    empty_program_piece.start = start;
    empty_program_piece.end = end;
    empty_program_piece.station_radii.assign(
        static_cast<std::size_t>(instance.m), 0.0);
    program.pieces.push_back(std::move(empty_program_piece));
  }
  return program;
}

bool program_covers_interval(
    const TemporalPiecewiseProgramV1& program,
    double start,
    double end,
    int station_count) {
  if (program.pieces.empty()) {
    return false;
  }
  std::vector<std::pair<double, double>> pieces;
  pieces.reserve(program.pieces.size());
  for (const auto& piece : program.pieces) {
    if (!std::isfinite(piece.start) ||
        !std::isfinite(piece.end) ||
        piece.end <= piece.start ||
        piece.start < start ||
        piece.end > end ||
        piece.station_radii.size() !=
            static_cast<std::size_t>(station_count)) {
      return false;
    }
    if (!std::all_of(
            piece.station_radii.begin(),
            piece.station_radii.end(),
            [](double radius) {
              return std::isfinite(radius) && radius >= 0.0;
            })) {
      return false;
    }
    pieces.emplace_back(piece.start, piece.end);
  }
  std::sort(pieces.begin(), pieces.end());
  double covered_until = start;
  for (const auto& piece : pieces) {
    if (piece.first > covered_until) {
      return false;
    }
    covered_until = std::max(covered_until, piece.second);
  }
  return covered_until == end;
}

std::uint64_t maximum_oracle_calls_for_evaluation(
    const IntervalEvaluatorConfigV1& config,
    bool inherited_assignment) {
  std::uint64_t calls = 0U;
  if (config.exact_dminus != nullptr) {
    ++calls;
  }
  if (config.relaxation_dminus != nullptr) {
    ++calls;
  }
  if (!inherited_assignment &&
      config.heuristic_dplus != nullptr) {
    ++calls;
  }
  return calls;
}

IntervalEvaluatorResultV1 evaluate_with_budget(
    const IntervalEvaluator& evaluator,
    double start,
    double end,
    const std::optional<std::vector<int>>& inherited_assignment,
    const IntervalEvaluatorConfigV1& config,
    SolverBudget& global_budget) {
  global_budget.checkpoint();
  auto evaluated = evaluator.evaluate(
      start,
      end,
      inherited_assignment,
      config,
      &global_budget);
  global_budget.checkpoint();
  return evaluated;
}

void store_incomplete_leaf(
    TemporalPartition& partition,
    TemporalLeafId id,
    const std::string& reason) {
  partition.set_leaf_bounds(
      id,
      trivial_lower_bound(),
      absent_upper_bound(reason),
      std::nullopt);
  partition.freeze_leaf(id, reason);
}

void store_evaluated_leaf(
    TemporalPartition& partition,
    TemporalLeafId id,
    double start,
    double end,
    const IntervalEvaluatorResultV1& evaluated,
    const Instance& instance,
    const WeightedGeometry& geometry) {
  TemporalBoundRecordV1 lb;
  if (evaluated.final_lb.valid &&
      evaluated.final_lb.trivial) {
    lb = trivial_lower_bound();
  } else if (evaluated.final_lb.valid &&
             std::isfinite(evaluated.final_lb.value)) {
    lb.value = evaluated.final_lb.value;
    lb.validity =
        TemporalBoundValidityV1::NUMERICALLY_UNBOUNDED;
    lb.method = "INTERVAL_EVALUATOR";
    lb.certificate_id =
        evaluated.final_lb.certificate_id;
    lb.provenance = evaluated.final_lb.provenance;
    lb.arithmetic = TemporalBoundArithmeticV1::FAST_DOUBLE;
  } else {
    lb = trivial_lower_bound();
  }

  TemporalBoundRecordV1 ub =
      absent_upper_bound("no complete feasible program");
  std::optional<TemporalPiecewiseProgramV1> program;
  if (evaluated.assignment.has_value() &&
      std::isfinite(evaluated.upper_bound) &&
      evaluated.upper_bound >= 0.0 &&
      evaluated.upper_method != IntervalUpperMethodV1::NONE) {
    auto candidate_program =
        make_program(instance, geometry, start, end, evaluated);
    if (!valid_assignment(instance,
                          candidate_program.point_assignment) ||
        !program_covers_interval(
            candidate_program, start, end, instance.m)) {
      throw std::invalid_argument(
          "candidate upper-bound program is incomplete or malformed");
    }
    ub.value = evaluated.upper_bound;
    ub.validity =
        TemporalBoundValidityV1::NUMERICALLY_UNBOUNDED;
    ub.method =
        evaluated.upper_method ==
                IntervalUpperMethodV1::DIRECT_UPPER_ENVELOPE
            ? "DIRECT_UPPER_ENVELOPE"
            : "DPLUS_FALLBACK";
    ub.provenance =
        "complete feasible program; FAST_DOUBLE, not certified";
    ub.arithmetic = TemporalBoundArithmeticV1::FAST_DOUBLE;
    program = std::move(candidate_program);
  }

  partition.set_leaf_bounds(id, lb, ub, program);
}

std::string freeze_summary(const TemporalPartition& partition) {
  std::ostringstream summary;
  bool first = true;
  for (const auto& entry : partition.partition()) {
    const auto& leaf = entry.second;
    if (leaf.status != TemporalLeafStatusV1::FROZEN ||
        leaf.freeze_reason == "split") {
      continue;
    }
    if (!first) {
      summary << "; ";
    }
    first = false;
    summary << "leaf " << leaf.leaf_id << ": "
            << leaf.freeze_reason;
  }
  return summary.str();
}

TemporalArithmeticAssuranceV2 temporal_assurance_for_backend(
    const std::string& backend) {
  if (backend == "FAST_DOUBLE") {
    return TemporalArithmeticAssuranceV2::FAST_FLOAT;
  }
  if (backend == "EXACT_RATIONAL") {
    return TemporalArithmeticAssuranceV2::EXACT;
  }
  if (backend == "DIRECTED_INTERVAL" ||
      backend == "VALIDATED_INTERVAL") {
    return TemporalArithmeticAssuranceV2::DIRECTED_INTERVAL;
  }
  return TemporalArithmeticAssuranceV2::UNAVAILABLE;
}

TemporalStopReasonV2 execution_stop_reason(
    const TemporalThesisResultV1& execution,
    TemporalFinalEvidenceV2& evidence) {
  for (const auto& leaf : execution.leaves) {
    if (leaf.status != TemporalLeafStatusV1::FROZEN) {
      continue;
    }
    evidence.has_oracle_failure =
        evidence.has_oracle_failure ||
        leaf.freeze_reason == "ORACLE_FAIL";
    evidence.has_verification_failure =
        evidence.has_verification_failure ||
        leaf.freeze_reason == "VERIFY_FAIL";
    evidence.has_inconsistent_bounds =
        evidence.has_inconsistent_bounds ||
        leaf.freeze_reason == "INCONSISTENT";
  }
  evidence.has_inconsistent_bounds =
      evidence.has_inconsistent_bounds ||
      execution.status ==
          TemporalThesisStatusV1::RECONCILIATION_FAILURE ||
      execution.status == TemporalThesisStatusV1::INCONSISTENT;

  switch (execution.status) {
    case TemporalThesisStatusV1::TIME_LIMIT:
      return TemporalStopReasonV2::TIMEOUT;
    case TemporalThesisStatusV1::ORACLE_TIME_LIMIT:
      return TemporalStopReasonV2::ORACLE_TIME_LIMIT;
    case TemporalThesisStatusV1::MAX_LEAVES:
      return TemporalStopReasonV2::INTERVAL_CAP;
    case TemporalThesisStatusV1::ORACLE_CAP:
      return TemporalStopReasonV2::ORACLE_CAP;
    case TemporalThesisStatusV1::RECONCILIATION_FAILURE:
    case TemporalThesisStatusV1::INCONSISTENT:
      return TemporalStopReasonV2::INCONSISTENT;
    case TemporalThesisStatusV1::INVALID:
      return TemporalStopReasonV2::INVALID_INPUT;
    case TemporalThesisStatusV1::FAILED:
      return TemporalStopReasonV2::ORACLE_FAIL;
    case TemporalThesisStatusV1::COMPLETE:
    case TemporalThesisStatusV1::INCOMPLETE:
      break;
  }

  if (evidence.has_inconsistent_bounds) {
    return TemporalStopReasonV2::INCONSISTENT;
  }
  if (evidence.has_verification_failure) {
    return TemporalStopReasonV2::VERIFY_FAIL;
  }
  if (evidence.has_oracle_failure) {
    return TemporalStopReasonV2::ORACLE_FAIL;
  }

  bool saw_frozen = false;
  bool all_resolution_limited = true;
  bool all_midpoint_collapsed = true;
  for (const auto& leaf : execution.leaves) {
    if (leaf.status != TemporalLeafStatusV1::FROZEN ||
        leaf.freeze_reason == "split") {
      continue;
    }
    saw_frozen = true;
    const bool min_interval =
        leaf.freeze_reason == "MIN_INTERVAL";
    const bool midpoint_collapse =
        leaf.freeze_reason == "MIDPOINT_COLLAPSE";
    all_resolution_limited =
        all_resolution_limited &&
        (min_interval || midpoint_collapse);
    all_midpoint_collapsed =
        all_midpoint_collapsed && midpoint_collapse;
  }
  evidence.unresolved_leaves_resolution_limited =
      saw_frozen && all_resolution_limited;
  evidence.all_unresolved_leaves_midpoint_collapsed =
      evidence.unresolved_leaves_resolution_limited &&
      all_midpoint_collapsed;
  if (evidence.unresolved_leaves_resolution_limited) {
    return evidence.all_unresolved_leaves_midpoint_collapsed
               ? TemporalStopReasonV2::MIDPOINT_COLLAPSE
               : TemporalStopReasonV2::MIN_INTERVAL;
  }
  return TemporalStopReasonV2::CERTIFICATE_UNAVAILABLE;
}

bool snapshot_structure_valid(
    const GlobalBoundUbSnapshotV1& snapshot,
    const Instance& instance) {
  if (!snapshot.complete || snapshot.leaves.empty()) {
    return false;
  }
  std::vector<const GlobalBoundUbSnapshotLeafV1*> leaves;
  leaves.reserve(snapshot.leaves.size());
  for (const auto& leaf : snapshot.leaves) {
    leaves.push_back(&leaf);
  }
  std::sort(
      leaves.begin(),
      leaves.end(),
      [](const auto* lhs, const auto* rhs) {
        return lhs->start < rhs->start;
      });
  if (leaves.front()->start != 0.0 ||
      leaves.back()->end != instance.T_end) {
    return false;
  }
  double previous_end = 0.0;
  for (const auto* leaf : leaves) {
    if (!std::isfinite(leaf->start) ||
        !std::isfinite(leaf->end) ||
        leaf->end <= leaf->start ||
        leaf->start != previous_end ||
        !std::isfinite(leaf->ub_value) ||
        !valid_assignment(instance, leaf->assignment)) {
      return false;
    }
    TemporalPiecewiseProgramV1 program;
    program.point_assignment = leaf->assignment;
    for (const auto& piece : leaf->pieces) {
      TemporalRadiusPieceV1 radius_piece;
      radius_piece.start = piece.start;
      radius_piece.end = piece.end;
      radius_piece.station_radii = piece.station_radii;
      program.pieces.push_back(std::move(radius_piece));
    }
    if (!program_covers_interval(
            program, leaf->start, leaf->end, instance.m)) {
      return false;
    }
    previous_end = leaf->end;
  }
  return true;
}
}  // namespace

TemporalThesisSolver::TemporalThesisSolver(
    const Instance& instance)
    : instance_(instance) {}

TemporalThesisResultV1 TemporalThesisSolver::solve(
    const IntervalEvaluatorConfigV1& evaluator_config,
    const TemporalThesisConfigV1& config) {
  return solve_impl(evaluator_config, config, nullptr);
}

TemporalThesisResultV2 TemporalThesisSolver::solve_v2(
    const IntervalEvaluatorConfigV1& evaluator_config,
    const TemporalThesisConfigV2& config) {
  TemporalThesisResultV2 result;
  result.schema_version = 2;
  if (!std::isfinite(config.epsilon_abs) ||
      !std::isfinite(config.epsilon_rel) ||
      config.epsilon_abs < 0.0 ||
      config.epsilon_rel < 0.0) {
    result.execution.status = TemporalThesisStatusV1::INVALID;
    result.execution.stop_reason =
        "invalid final-result gap tolerances";
    TemporalFinalEvidenceV2 evidence;
    evidence.epsilon_abs = config.epsilon_abs;
    evidence.epsilon_rel = config.epsilon_rel;
    evidence.requested_stop_reason =
        TemporalStopReasonV2::INVALID_INPUT;
    result.final = finalize_temporal_result_v2(evidence);
    return result;
  }

  TemporalOracleStatisticsV2 statistics;
  result.execution = solve_impl(
      evaluator_config, config.execution, &statistics);
  TemporalFinalEvidenceV2 evidence;
  evidence.epsilon_abs = config.epsilon_abs;
  evidence.epsilon_rel = config.epsilon_rel;
  evidence.oracle_statistics = statistics;
  evidence.solve_time_seconds =
      result.execution.elapsed_seconds;
  evidence.verify_time_seconds = 0.0;
  evidence.current_lb_contributions_valid =
      result.execution.global_bounds.lb_current.valid;
  evidence.historical_certificates_valid = true;
  evidence.lower_bound = 0.0;
  evidence.lower_bound_valid = true;
  evidence.lower_bound_method = "TRIVIAL_NONNEGATIVE";
  evidence.lower_bound_arithmetic =
      TemporalArithmeticAssuranceV2::EXACT;
  evidence.lower_bound_provenance.emplace_back(
      "nonnegativity of the Model A integral");
  evidence.provenance.emplace_back(
      "LB=0: nonnegativity of the Model A integral");

  if (result.execution.global_bounds.lb_best.has_value()) {
    const auto& best =
        *result.execution.global_bounds.lb_best;
    evidence.lower_bound = best.value;
    evidence.lower_bound_valid = true;
    evidence.lower_bound_method = best.provenance;
    evidence.lower_bound_provenance = {best.provenance};
    evidence.lower_bound_certificate_ids =
        best.certificate_ids;
    evidence.provenance.push_back(
        "LB history: " + best.provenance);
    for (const auto& certificate : best.certificate_ids) {
      evidence.provenance.push_back(
          "LB certificate: " + certificate);
    }
    if (result.execution.global_bounds.ub_best.has_value()) {
      evidence.lower_bound_arithmetic =
          temporal_assurance_for_backend(
              result.execution.global_bounds.ub_best
                  ->snapshot.identity.numeric_backend);
    } else {
      evidence.lower_bound_arithmetic =
          TemporalArithmeticAssuranceV2::UNAVAILABLE;
    }
  }
  if (result.execution.global_bounds.ub_best.has_value()) {
    const auto& best =
        *result.execution.global_bounds.ub_best;
    evidence.upper_bound = best.value;
    evidence.upper_bound_valid =
        std::isfinite(best.value);
    evidence.upper_bound_method = best.provenance;
    evidence.upper_bound_arithmetic =
        temporal_assurance_for_backend(
            best.snapshot.identity.numeric_backend);
    evidence.upper_bound_provenance = {best.provenance};
    evidence.upper_bound_certificate_ids =
        best.certificate_ids;
    evidence.complete_model_a_program =
        best.snapshot.complete;
    evidence.incumbent_ref =
        "partition-version-" +
        std::to_string(best.partition_version);
    evidence.provenance.push_back(
        "UB history: " + best.provenance);
    for (const auto& certificate : best.certificate_ids) {
      evidence.provenance.push_back(
          "UB certificate: " + certificate);
    }
    evidence.upper_cost_enclosure_consistent =
        best.snapshot.complete &&
        std::isfinite(best.snapshot.global_ub) &&
        best.snapshot.global_ub == best.value;
  }
  evidence.continuous_verification_passed =
      result.execution.verified;
  evidence.continuous_verification_failed =
      result.execution.verifier_status ==
          "SNAPSHOT_STRUCTURE_INVALID";
  evidence.requested_stop_reason =
      execution_stop_reason(result.execution, evidence);
  if (evidence.has_oracle_failure) {
    evidence.provenance.emplace_back(
        "one or more partition leaves were frozen after an Oracle failure");
  }
  if (evidence.has_verification_failure) {
    evidence.provenance.emplace_back(
        "one or more partition leaves were frozen after a verification failure");
  }
  if (evidence.has_inconsistent_bounds) {
    evidence.provenance.emplace_back(
        "the execution reported an unresolved bound inconsistency");
  }
  result.final = finalize_temporal_result_v2(evidence);
  return result;
}

TemporalThesisResultV1 TemporalThesisSolver::solve_impl(
    const IntervalEvaluatorConfigV1& evaluator_config,
    const TemporalThesisConfigV1& config,
    TemporalOracleStatisticsV2* oracle_statistics) {
  const auto solve_started = SteadyClock::now();
  TemporalThesisResultV1 result;
  result.status = TemporalThesisStatusV1::INCOMPLETE;
  result.stop_reason = "not stopped";

  if (instance_.n < 0 ||
      instance_.m < 0 ||
      static_cast<std::size_t>(instance_.n) !=
          instance_.trajectories.size() ||
      static_cast<std::size_t>(instance_.m) !=
          instance_.stations.size() ||
      !std::isfinite(instance_.T_end) ||
      instance_.T_end <= 0.0 ||
      config.max_leaves == 0U ||
      config.max_oracle_calls == 0U ||
      !std::isfinite(config.delta_min) ||
      config.delta_min < 0.0 ||
      !std::isfinite(config.global_time_limit_seconds) ||
      config.global_time_limit_seconds <= 0.0 ||
      !std::isfinite(config.oracle_time_limit_seconds) ||
      config.oracle_time_limit_seconds <= 0.0) {
    result.status = TemporalThesisStatusV1::INVALID;
    result.stop_reason = "invalid Model A instance or config";
    return result;
  }

  SolverBudget global_budget(config.global_time_limit_seconds);
  GreedyWeightedHeuristicOracle default_heuristic;
  IntervalEvaluatorConfigV1 active_evaluator_config =
      evaluator_config;
  if (active_evaluator_config.heuristic_dplus == nullptr &&
      active_evaluator_config.arithmetic ==
          IntervalEvaluatorArithmeticV1::FAST_DOUBLE) {
    active_evaluator_config.heuristic_dplus = &default_heuristic;
  }

  std::uint64_t oracle_calls = 0U;
  std::unique_ptr<CountingExactOracle> counting_exact;
  std::unique_ptr<CountingHeuristicOracle> counting_heuristic;
  std::unique_ptr<CountingRelaxationOracle> counting_relaxation;
  if (active_evaluator_config.exact_dminus != nullptr) {
    counting_exact = std::make_unique<CountingExactOracle>(
        *active_evaluator_config.exact_dminus,
        oracle_calls,
        config.max_oracle_calls,
        config.oracle_time_limit_seconds,
        oracle_statistics);
    active_evaluator_config.exact_dminus = counting_exact.get();
  }
  if (active_evaluator_config.heuristic_dplus != nullptr) {
    counting_heuristic =
        std::make_unique<CountingHeuristicOracle>(
            *active_evaluator_config.heuristic_dplus,
            oracle_calls,
            config.max_oracle_calls,
            config.oracle_time_limit_seconds,
            oracle_statistics);
    active_evaluator_config.heuristic_dplus =
        counting_heuristic.get();
  }
  if (active_evaluator_config.relaxation_dminus != nullptr) {
    counting_relaxation =
        std::make_unique<CountingRelaxationOracle>(
            *active_evaluator_config.relaxation_dminus,
            oracle_calls,
            config.max_oracle_calls,
            config.oracle_time_limit_seconds,
            oracle_statistics);
    active_evaluator_config.relaxation_dminus =
        counting_relaxation.get();
  }

  std::unique_ptr<WeightedGeometry> geometry;
  std::unique_ptr<IntervalEvaluator> evaluator;
  try {
    geometry = std::make_unique<WeightedGeometry>(instance_);
    evaluator = std::make_unique<IntervalEvaluator>(instance_);
  } catch (const std::exception& error) {
    result.status = TemporalThesisStatusV1::INVALID;
    result.stop_reason = error.what();
    return result;
  }

  TemporalPartition partition(0.0, instance_.T_end);
  std::uint64_t partition_version = 1U;
  const auto& common = geometry->common_partition();
  for (const double breakpoint : common) {
    if (breakpoint <= 0.0 ||
        breakpoint >= instance_.T_end) {
      continue;
    }
    std::vector<TemporalLeafId> open_ids(
        partition.queue().begin(),
        partition.queue().end());
    for (const TemporalLeafId id : open_ids) {
      const auto& leaf = partition.leaf(id);
      if (breakpoint > leaf.start &&
          breakpoint < leaf.end) {
        partition.split_leaf(id, breakpoint);
        ++partition_version;
      }
    }
  }

  const std::vector<TemporalLeafId> initial_ids(
      partition.queue().begin(),
      partition.queue().end());
  for (const TemporalLeafId id : initial_ids) {
    const IntervalRecordV1 leaf = partition.leaf(id);
    if (partition.partition().size() > config.max_leaves) {
      store_incomplete_leaf(partition, id, "MAX_LEAVES");
      result.status = TemporalThesisStatusV1::MAX_LEAVES;
      result.stop_reason =
          "initial common partition exceeds maximum leaf count";
      continue;
    }
    if (global_budget.expired()) {
      store_incomplete_leaf(partition, id, "TIME_LIMIT");
      result.status = TemporalThesisStatusV1::TIME_LIMIT;
      result.stop_reason =
          "global time budget expired before leaf evaluation";
      continue;
    }

    if (oracle_calls >= config.max_oracle_calls) {
      store_incomplete_leaf(partition, id, "ORACLE_CAP");
      result.status = TemporalThesisStatusV1::ORACLE_CAP;
      result.stop_reason =
          "maximum Oracle call count reached before leaf evaluation";
      continue;
    }

    try {
      const auto evaluated = evaluate_with_budget(
          *evaluator,
          leaf.start,
          leaf.end,
          std::nullopt,
          active_evaluator_config,
          global_budget);
      if (evaluated.status ==
          IntervalEvaluatorStatusV1::RECONCILIATION_FAILURE) {
        store_incomplete_leaf(partition, id, "VERIFY_FAIL");
        result.status =
            TemporalThesisStatusV1::RECONCILIATION_FAILURE;
        result.stop_reason =
            "local LB/UB contradiction; independent revalidation is unavailable";
        continue;
      }
      store_evaluated_leaf(
          partition,
          id,
          leaf.start,
          leaf.end,
          evaluated,
          instance_,
          *geometry);
      const auto& stored = partition.leaf(id);
      if (!stored.ub_piecewise_program.has_value() ||
          stored.ub.validity ==
              TemporalBoundValidityV1::ABSENT) {
        partition.freeze_leaf(id, "ORACLE_FAIL");
        if (result.status == TemporalThesisStatusV1::INCOMPLETE) {
          result.stop_reason =
              evaluated.failure_reason.empty()
                  ? "leaf has no complete UB program"
                  : evaluated.failure_reason;
        }
      }
    } catch (const OracleCallCapExceeded&) {
      store_incomplete_leaf(partition, id, "ORACLE_CAP");
      result.status = TemporalThesisStatusV1::ORACLE_CAP;
      result.stop_reason =
          "maximum Oracle call count reached during leaf evaluation";
    } catch (const SolverBudgetExpired&) {
      const bool global_expired = global_budget.expired();
      const std::string reason =
          global_expired ? "TIME_LIMIT" : "ORACLE_TIME_LIMIT";
      store_incomplete_leaf(partition, id, reason);
      result.status =
          global_expired
              ? TemporalThesisStatusV1::TIME_LIMIT
              : TemporalThesisStatusV1::ORACLE_TIME_LIMIT;
      result.stop_reason =
          global_expired
              ? "global budget expired during leaf evaluation"
              : "per-Oracle time limit expired during leaf evaluation";
    } catch (const std::exception& error) {
      store_incomplete_leaf(partition, id, "ORACLE_FAIL");
      if (result.status == TemporalThesisStatusV1::INCOMPLETE) {
        result.stop_reason = error.what();
      }
    }
  }

  TemporalSplitConfigV1 split_config;
  split_config.delta_min = config.delta_min;
  split_config.evaluator = active_evaluator_config;
  split_config.selection = config.split_selection;
  TemporalSplittingPolicy split_policy(instance_, split_config);

  GlobalBoundIdentityV1 identity;
  identity.instance_hash = local_instance_hash(instance_);
  identity.solver_identity = "temporal-thesis-local-loop-v1";
  identity.config_identity =
      "delta_min=" + std::to_string(config.delta_min) +
      ";max_leaves=" + std::to_string(config.max_leaves) +
      ";max_oracle_calls=" +
      std::to_string(config.max_oracle_calls);
  identity.numeric_backend =
      active_evaluator_config.arithmetic ==
              IntervalEvaluatorArithmeticV1::FAST_DOUBLE
          ? "FAST_DOUBLE"
          : "VALIDATED_UNAVAILABLE";
  identity.verifier_state =
      "independent validated verifier unavailable";
  GlobalBoundHistory history(identity);

  auto update_history = [&]() {
    result.global_bounds =
        history.update(partition, partition_version);
    if (result.global_bounds.lb_current.valid &&
        result.global_bounds.ub_current.valid &&
        result.global_bounds.lb_current.value >
            result.global_bounds.ub_current.value) {
      result.status =
          TemporalThesisStatusV1::RECONCILIATION_FAILURE;
      result.stop_reason =
          "global LB exceeds UB; independent revalidation is unavailable";
      return false;
    }
    return true;
  };

  bool keep_running = update_history() &&
                      (result.status ==
                           TemporalThesisStatusV1::INCOMPLETE ||
                       result.status ==
                           TemporalThesisStatusV1::ORACLE_TIME_LIMIT);
  while (keep_running) {
    if (global_budget.expired()) {
      result.status = TemporalThesisStatusV1::TIME_LIMIT;
      result.stop_reason = "global wall-clock budget expired";
      break;
    }
    if (partition.partition().size() + 1U >
        config.max_leaves) {
      result.status = TemporalThesisStatusV1::MAX_LEAVES;
      result.stop_reason =
          "maximum leaf count would be exceeded by the next split";
      break;
    }

    const TemporalLeafId next =
        split_policy.next_leaf(partition);
    if (next == 0U) {
      const std::string reasons = freeze_summary(partition);
      if (result.status !=
          TemporalThesisStatusV1::ORACLE_TIME_LIMIT) {
        result.status = TemporalThesisStatusV1::INCOMPLETE;
      }
      result.stop_reason =
          reasons.empty()
              ? "Q is empty; no eligible leaves remain"
              : "Q is empty; " + reasons;
      break;
    }

    const IntervalRecordV1 parent = partition.leaf(next);
    const bool inherited =
        parent.ub_piecewise_program.has_value();
    const std::uint64_t calls_per_child =
        maximum_oracle_calls_for_evaluation(
            active_evaluator_config, inherited);
    const std::uint64_t required_calls =
        calls_per_child > 0U
            ? calls_per_child * 2U
            : 0U;
    const std::uint64_t remaining_calls =
        config.max_oracle_calls -
        std::min(config.max_oracle_calls, oracle_calls);
    if (required_calls > remaining_calls) {
      result.status = TemporalThesisStatusV1::ORACLE_CAP;
      result.stop_reason =
          "maximum Oracle call count reached before split evaluation";
      break;
    }

    TemporalSplitAttemptV1 attempt;
    try {
      attempt = split_policy.attempt_split(
          partition, next, &global_budget);
    } catch (const SolverBudgetExpired&) {
      const bool global_expired = global_budget.expired();
      partition.freeze_leaf(
          next,
          global_expired ? "TIME_LIMIT" : "ORACLE_TIME_LIMIT");
      result.status =
          global_expired
              ? TemporalThesisStatusV1::TIME_LIMIT
              : TemporalThesisStatusV1::ORACLE_TIME_LIMIT;
      result.stop_reason =
          global_expired
              ? "global budget expired during split evaluation"
              : "per-Oracle time limit expired during split evaluation";
      ++partition_version;
      if (!update_history()) {
        break;
      }
      break;
    } catch (const OracleCallCapExceeded&) {
      partition.freeze_leaf(next, "ORACLE_CAP");
      result.status = TemporalThesisStatusV1::ORACLE_CAP;
      result.stop_reason =
          "maximum Oracle call count reached during split evaluation";
      ++partition_version;
      if (!update_history()) {
        break;
      }
      break;
    } catch (const std::exception& error) {
      partition.freeze_leaf(next, "ORACLE_FAIL");
      result.status = TemporalThesisStatusV1::INCOMPLETE;
      result.stop_reason =
          std::string("split transaction failed: ") + error.what();
      ++partition_version;
      if (!update_history()) {
        break;
      }
      continue;
    }
    if (attempt.outcome == TemporalSplitOutcomeV1::SPLIT) {
      ++partition_version;
      for (const TemporalLeafId child_id :
           {attempt.left_id, attempt.right_id}) {
        const IntervalRecordV1 child = partition.leaf(child_id);
        TemporalBoundRecordV1 lb = child.lb;
        TemporalBoundRecordV1 ub = child.ub;
        if (lb.method == "TRIVIAL_NONNEGATIVE") {
          lb = trivial_lower_bound();
        } else if (lb.validity ==
                   TemporalBoundValidityV1::VALID) {
          lb.validity =
              TemporalBoundValidityV1::NUMERICALLY_UNBOUNDED;
          lb.arithmetic =
              TemporalBoundArithmeticV1::FAST_DOUBLE;
        }
        if (ub.validity == TemporalBoundValidityV1::VALID) {
          ub.validity =
              TemporalBoundValidityV1::NUMERICALLY_UNBOUNDED;
          ub.arithmetic =
              TemporalBoundArithmeticV1::FAST_DOUBLE;
          ub.provenance =
              "complete feasible program; FAST_DOUBLE, not certified";
        }
        partition.set_leaf_bounds(
            child_id,
            lb,
            ub,
            child.ub_piecewise_program);
      }
      keep_running = update_history();
      continue;
    }

    ++partition_version;
    if (attempt.outcome ==
            TemporalSplitOutcomeV1::ORACLE_FAIL ||
        attempt.outcome ==
            TemporalSplitOutcomeV1::VERIFY_FAIL ||
        attempt.outcome ==
            TemporalSplitOutcomeV1::PARTITION_INVALID) {
      if (result.status == TemporalThesisStatusV1::INCOMPLETE) {
        result.stop_reason = attempt.reason;
      }
    }
    keep_running = update_history();
  }

  if (keep_running &&
      result.status == TemporalThesisStatusV1::INCOMPLETE &&
      result.stop_reason == "not stopped") {
    result.stop_reason = "algorithm stopped without a certified gap";
  }
  result.global_bounds =
      history.update(partition, partition_version);
  result.leaf_count = partition.partition().size();
  result.oracle_call_count = oracle_calls;
  result.partition_version = partition_version;
  result.leaves.reserve(partition.partition().size());
  for (const auto& entry : partition.partition()) {
    result.leaves.push_back(entry.second);
  }

  if (result.global_bounds.ub_best.has_value()) {
    const bool structure_valid =
        snapshot_structure_valid(
            result.global_bounds.ub_best->snapshot,
            instance_);
    result.verifier_status =
        structure_valid
            ? "STRUCTURAL_ONLY; validated arithmetic is unavailable"
            : "SNAPSHOT_STRUCTURE_INVALID";
  } else {
    result.verifier_status =
        "NO_CERTIFIED_SNAPSHOT; validated arithmetic is unavailable";
  }
  result.verified = false;
  result.elapsed_seconds =
      std::chrono::duration<double>(
          SteadyClock::now() - solve_started)
          .count();
  return result;
}

}  // namespace kdc
