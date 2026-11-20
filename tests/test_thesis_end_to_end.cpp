#include "kdc/global_bound_history.hpp"
#include "kdc/interval_evaluator.hpp"
#include "kdc/io.hpp"
#include "kdc/model_a_snapshot_verifier.hpp"
#include "kdc/temporal_partition.hpp"
#include "kdc/weighted_oracle.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

kdc::Instance one_point_instance() {
  kdc::Instance instance;
  instance.n = 1;
  instance.m = 1;
  instance.T_end = 1.0;
  instance.stations = {{10, kdc::Point(0.0, 0.0)}};
  instance.trajectories = {
      kdc::Trajectory(
          {0.0, 1.0},
          {kdc::Point(0.0, 0.0),
           kdc::Point(1.0, 0.0)})};
  return instance;
}

kdc::Weight brute_force_minimum(
    const kdc::WeightMatrix& matrix) {
  const auto station_count = matrix.station_count();
  const auto point_count = matrix.point_count();
  if (station_count == 0U) {
    if (point_count == 0U) {
      return 0.0;
    }
    throw kdc::WeightedProblemInfeasible(
        "reference has points but no stations");
  }

  kdc::Weight best = std::numeric_limits<kdc::Weight>::infinity();
  std::vector<int> assignment(point_count, 0);
  const auto enumerate = [&](const auto& self,
                             kdc::WeightIndex point) -> void {
    if (point == point_count) {
      best = (std::min)(
          best,
          kdc::WeightedStaticObjective::evaluate_assignment(
              matrix, assignment));
      return;
    }
    for (kdc::WeightIndex station = 0U;
         station < station_count;
         ++station) {
      assignment[point] = static_cast<int>(station);
      self(self, point + 1U);
    }
  };
  enumerate(enumerate, 0U);
  return best;
}

class NonnegativeLowerBoundOracle final
    : public kdc::IWeightedExactOracle {
 public:
  kdc::WeightedExactOracleResultV1 solve(
      const kdc::WeightMatrix&,
      kdc::SolverBudget*) override {
    kdc::WeightedExactOracleResultV1 result;
    result.independently_validated_lower_bound = 0.0;
    result.independently_validated_lower_bound_validity =
        kdc::WeightedBoundValidityV1::VALID;
    result.arithmetic =
        kdc::WeightedArithmeticModeV1::EXACT_RATIONAL;
    result.provenance.method = "nonnegative-objective-certificate";
    result.provenance.source = "objective is a sum of nonnegative terms";
    return result;
  }

  std::string name() const override {
    return "nonnegative-objective-lower-bound";
  }
};

std::string instance_hash(const kdc::Instance& instance) {
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
    for (const double breakpoint : trajectory.t_breaks) {
      canonical << ';' << breakpoint;
    }
    for (const auto& waypoint : trajectory.waypoints) {
      canonical << ';' << waypoint.x << ';' << waypoint.y;
    }
  }

  std::uint64_t hash = UINT64_C(14695981039346656037);
  for (const char byte : canonical.str()) {
    hash ^= static_cast<unsigned char>(byte);
    hash *= UINT64_C(1099511628211);
  }
  std::ostringstream encoded;
  encoded << "fnv1a64:" << std::hex << hash;
  return encoded.str();
}

struct SnapshotFiles {
  std::string instance_path;
  std::string history_path;

  SnapshotFiles(const SnapshotFiles&) = delete;
  SnapshotFiles& operator=(const SnapshotFiles&) = delete;
  SnapshotFiles(SnapshotFiles&& other) noexcept
      : instance_path(std::move(other.instance_path)),
        history_path(std::move(other.history_path)) {
    other.instance_path.clear();
    other.history_path.clear();
  }
  SnapshotFiles& operator=(SnapshotFiles&&) = delete;

  SnapshotFiles() = default;
  ~SnapshotFiles() {
    if (!instance_path.empty()) {
      std::remove(instance_path.c_str());
    }
    if (!history_path.empty()) {
      std::remove(history_path.c_str());
    }
  }
};

SnapshotFiles write_snapshot(
    const kdc::Instance& instance,
    double stored_ub,
    double radius) {
  static std::atomic<unsigned long long> sequence{0U};
  const auto id = sequence.fetch_add(1U);
  SnapshotFiles files;
  files.instance_path =
      "/tmp/kdc-thesis-e2e-instance-" + std::to_string(id) + ".json";
  files.history_path =
      "/tmp/kdc-thesis-e2e-history-" + std::to_string(id) + ".json";
  kdc::DatasetReader::write_json(instance, files.instance_path);

  kdc::TemporalPiecewiseProgramV1 program;
  program.point_assignment = {0};
  kdc::TemporalRadiusPieceV1 piece;
  piece.start = 0.0;
  piece.end = 1.0;
  piece.station_radii = {radius};
  program.pieces.push_back(piece);

  kdc::TemporalBoundRecordV1 lb;
  lb.value = 0.0;
  lb.validity = kdc::TemporalBoundValidityV1::VALID;
  lb.method = "nonnegative-objective";
  lb.certificate_id = "e2e-lb";
  lb.provenance = "nonnegative objective";
  lb.arithmetic =
      kdc::TemporalBoundArithmeticV1::EXACT_RATIONAL;

  kdc::TemporalBoundRecordV1 ub;
  ub.value = stored_ub;
  ub.validity = kdc::TemporalBoundValidityV1::VALID;
  ub.method = "complete-program";
  ub.certificate_id = "e2e-ub";
  ub.provenance = "complete Model A program";
  ub.arithmetic =
      kdc::TemporalBoundArithmeticV1::CONSERVATIVE_INTERVAL;

  kdc::TemporalPartition partition(0.0, 1.0);
  partition.set_leaf_bounds(1U, lb, ub, program);
  kdc::GlobalBoundIdentityV1 identity;
  identity.instance_hash = instance_hash(instance);
  identity.solver_identity = "synthetic-e2e";
  identity.config_identity = "small-fixture";
  identity.numeric_backend = "FAST_DOUBLE";
  identity.verifier_state = "not-verified";
  kdc::GlobalBoundHistory history(identity);
  const auto result = history.update(partition, 1U);
  if (!result.ub_best.has_value()) {
    throw std::runtime_error(
        "complete synthetic snapshot was not retained");
  }
  history.serialize(files.history_path);
  return files;
}

}  // namespace

TEST_CASE("Model A evaluation composes distance bounds, valid zero LB, and integral UB") {
  const auto instance = one_point_instance();
  kdc::WeightedGeometry geometry(instance);
  const auto distance_bounds = geometry.bounds_on(0.0, 1.0);

  for (const double time : {0.0, 0.125, 0.5, 0.875, 1.0}) {
    const auto at_time = geometry.d_at(time);
    REQUIRE(distance_bounds.lower.matrix.weight(0U, 0U) <=
            at_time.matrix.weight(0U, 0U));
    REQUIRE(at_time.matrix.weight(0U, 0U) <=
            distance_bounds.upper.matrix.weight(0U, 0U));
  }

  NonnegativeLowerBoundOracle lower_oracle;
  kdc::IntervalEvaluatorConfigV1 config;
  config.exact_dminus = &lower_oracle;
  config.arithmetic =
      kdc::IntervalEvaluatorArithmeticV1::FAST_DOUBLE;
  kdc::IntervalEvaluator evaluator(instance);
  const auto result =
      evaluator.evaluate(0.0, 1.0, std::vector<int>{0}, config);
  const double reference_integral =
      std::acos(-1.0) / 3.0;

  REQUIRE(result.status ==
          kdc::IntervalEvaluatorStatusV1::COMPLETE);
  REQUIRE(result.dminus_lb.valid);
  REQUIRE(result.dminus_lb.value == 0.0);
  REQUIRE(result.final_lb.value <= reference_integral);
  REQUIRE(result.upper_bound >= reference_integral - 1e-12);
  REQUIRE(result.upper_method ==
          kdc::IntervalUpperMethodV1::DIRECT_UPPER_ENVELOPE);
  REQUIRE(result.assignment == std::optional<std::vector<int>>({0}));
}

TEST_CASE("A D-plus heuristic remains UB-only and invalid inherited assignment is rejected") {
  const auto instance = one_point_instance();
  kdc::GreedyWeightedHeuristicOracle heuristic;
  kdc::IntervalEvaluatorConfigV1 config;
  config.heuristic_dplus = &heuristic;
  kdc::IntervalEvaluator evaluator(instance);

  const auto heuristic_result =
      evaluator.evaluate(0.0, 1.0, std::nullopt, config);
  REQUIRE(heuristic_result.status ==
          kdc::IntervalEvaluatorStatusV1::COMPLETE);
  REQUIRE(heuristic_result.assignment.has_value());
  REQUIRE_FALSE(heuristic_result.dminus_lb.valid);
  REQUIRE(heuristic_result.final_lb.valid);
  REQUIRE(heuristic_result.final_lb.trivial);
  REQUIRE(heuristic_result.final_lb.value == 0.0);

  const auto incompatible =
      evaluator.evaluate(0.0, 1.0, std::vector<int>{1}, config);
  REQUIRE_FALSE(incompatible.assignment.has_value());
  REQUIRE(std::isinf(incompatible.upper_bound));
  REQUIRE(incompatible.status ==
          kdc::IntervalEvaluatorStatusV1::INCOMPLETE);
}

TEST_CASE("LP dual diagnostic is below reference but is not an official LB") {
  const auto matrix = kdc::WeightMatrix::from_rows(
      {{2.0, 1.0}, {1.0, 2.0}});
  const double pi = std::acos(-1.0);
  const auto dual = kdc::validate_weighted_lp_dual(
      matrix,
      {0.5 * pi, 0.5 * pi},
      {0.26 * pi, 0.5 * pi, 0.5 * pi, 0.26 * pi});

  REQUIRE_FALSE(dual.valid);
  REQUIRE(dual.numerical_diagnostic_valid);
  REQUIRE_FALSE(dual.directed_lower_bound.has_value());
  REQUIRE(dual.numerical_diagnostic_lower_bound.has_value());
  REQUIRE(*dual.numerical_diagnostic_lower_bound > 0.0);
  REQUIRE(*dual.numerical_diagnostic_lower_bound <=
          brute_force_minimum(matrix));
}

TEST_CASE("Partition validation catches an omitted leaf before aggregation") {
  kdc::TemporalPartition complete(0.0, 1.0);
  auto truncated = complete.partition();
  truncated.at(1U).end = 0.5;
  const auto validation =
      kdc::TemporalPartition::validate_partition(
          0.0, 1.0, truncated, complete.queue());
  REQUIRE_FALSE(validation.valid);
}

TEST_CASE("Serialized complete snapshot reloads and passes independent numerical verification") {
  kdc::Instance instance;
  instance.name = "stationary-snapshot";
  instance.n = 1;
  instance.m = 1;
  instance.T_end = 1.0;
  instance.stations = {{10, kdc::Point(0.0, 0.0)}};
  instance.trajectories = {
      kdc::Trajectory(
          {0.0, 1.0},
          {kdc::Point(0.0, 0.0),
           kdc::Point(0.0, 0.0)})};

  auto files = write_snapshot(instance, 1e-12, 0.0);
  const auto restored =
      kdc::GlobalBoundHistory::deserialize(files.history_path);
  REQUIRE(restored.ub_best().has_value());
  REQUIRE(restored.ub_best()->snapshot.complete);
  REQUIRE(restored.ub_best()->snapshot.leaves.size() == 1U);

  const auto verified =
      kdc::ModelASnapshotVerifier::verify_files(
          files.instance_path,
          files.history_path,
          kdc::ModelASnapshotVerificationModeV1::FAST_FLOAT);
  REQUIRE(verified.status ==
          kdc::ModelASnapshotVerificationStatusV1::NUMERIC_PASS);
  REQUIRE(verified.instance_hash_matches);
  REQUIRE(verified.partition_valid);
  REQUIRE(verified.assignments_valid);
  REQUIRE(verified.continuous_coverage_valid);
  REQUIRE(verified.stored_ub >= verified.cost_upper_endpoint);

  const auto validated =
      kdc::ModelASnapshotVerifier::verify_files(
          files.instance_path,
          files.history_path,
          kdc::ModelASnapshotVerificationModeV1::VALIDATED);
  REQUIRE(validated.status ==
          kdc::ModelASnapshotVerificationStatusV1::NOT_CERTIFIED);
}

TEST_CASE("Independent verifier rejects an upper bound rounded below program cost") {
  const auto instance = one_point_instance();
  const double pi = std::acos(-1.0);
  auto files = write_snapshot(
      instance,
      std::nextafter(pi, -std::numeric_limits<double>::infinity()),
      1.0);

  const auto result =
      kdc::ModelASnapshotVerifier::verify_files(
          files.instance_path,
          files.history_path,
          kdc::ModelASnapshotVerificationModeV1::FAST_FLOAT);
  REQUIRE(result.status ==
          kdc::ModelASnapshotVerificationStatusV1::FAIL);
  REQUIRE(result.first_violation.has_value());
  REQUIRE(result.first_violation->code ==
          "LEAF_UB_BELOW_COST_UPPER_ENDPOINT");
}
