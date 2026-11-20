#include "kdc/io.hpp"
#include "kdc/model_a_snapshot_verifier.hpp"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {
using namespace kdc;
using Json = nlohmann::json;

struct TestArtifacts {
  std::string instance_path;
  std::string history_path;

  TestArtifacts() = default;
  TestArtifacts(const TestArtifacts&) = delete;
  TestArtifacts& operator=(const TestArtifacts&) = delete;
  TestArtifacts(TestArtifacts&& other) noexcept
      : instance_path(std::move(other.instance_path)),
        history_path(std::move(other.history_path)) {
    other.instance_path.clear();
    other.history_path.clear();
  }
  TestArtifacts& operator=(TestArtifacts&&) = delete;

  ~TestArtifacts() {
    if (!instance_path.empty()) {
      std::remove(instance_path.c_str());
    }
    if (!history_path.empty()) {
      std::remove(history_path.c_str());
    }
  }
};

std::string fixture_hash(const Instance& instance) {
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

Instance make_instance(
    std::vector<Trajectory> trajectories,
    std::vector<Station> stations = {{17, Point(0.0, 0.0)}}) {
  Instance instance;
  instance.id = 1;
  instance.name = "snapshot-verifier-fixture";
  instance.T_end = 1.0;
  instance.n = static_cast<int>(trajectories.size());
  instance.m = static_cast<int>(stations.size());
  instance.trajectories = std::move(trajectories);
  instance.stations = std::move(stations);
  return instance;
}

TemporalPiecewiseProgramV1 make_program(
    const std::vector<int>& assignment,
    const std::vector<TemporalRadiusPieceV1>& pieces) {
  TemporalPiecewiseProgramV1 program;
  program.point_assignment = assignment;
  program.pieces = pieces;
  return program;
}

TemporalRadiusPieceV1 radius_piece(
    double start,
    double end,
    std::vector<double> radii) {
  TemporalRadiusPieceV1 piece;
  piece.start = start;
  piece.end = end;
  piece.station_radii = std::move(radii);
  return piece;
}

TestArtifacts write_artifacts(
    const Instance& instance,
    const TemporalPiecewiseProgramV1& program,
    double leaf_ub,
    const std::string& instance_hash = {}) {
  static std::atomic<unsigned long long> sequence{0U};
  const auto id = sequence.fetch_add(1U);
  TestArtifacts artifacts;
  artifacts.instance_path =
      "/tmp/kdc-model-a-verifier-instance-" +
      std::to_string(id) + ".json";
  artifacts.history_path =
      "/tmp/kdc-model-a-verifier-history-" +
      std::to_string(id) + ".json";
  DatasetReader::write_json(instance, artifacts.instance_path);

  GlobalBoundIdentityV1 identity;
  identity.instance_hash =
      instance_hash.empty() ? fixture_hash(instance) : instance_hash;
  identity.solver_identity = "verifier-test-solver";
  identity.config_identity = "verifier-test-config";
  identity.numeric_backend = "FAST_DOUBLE";
  identity.verifier_state = "not-verified";
  GlobalBoundHistory history(identity);

  TemporalBoundRecordV1 lb;
  lb.value = 0.0;
  lb.validity = TemporalBoundValidityV1::VALID;
  lb.method = "TEST_ZERO_LB";
  lb.certificate_id = "test-lb-certificate";
  lb.provenance = "nonnegative objective";
  lb.arithmetic = TemporalBoundArithmeticV1::EXACT_RATIONAL;

  TemporalBoundRecordV1 ub;
  ub.value = leaf_ub;
  ub.validity = TemporalBoundValidityV1::VALID;
  ub.method = "TEST_PROGRAM_UB";
  ub.certificate_id = "test-ub-certificate";
  ub.provenance = "test snapshot program";
  ub.arithmetic =
      TemporalBoundArithmeticV1::CONSERVATIVE_INTERVAL;

  TemporalPartition partition(0.0, instance.T_end);
  partition.set_leaf_bounds(1U, lb, ub, program);
  const auto updated = history.update(partition, 1U);
  if (!updated.ub_best.has_value()) {
    throw std::runtime_error(
        "test fixture did not produce a serializable UB snapshot");
  }
  history.serialize(artifacts.history_path);
  return artifacts;
}

Trajectory trajectory(
    std::vector<double> times,
    std::vector<Point> points) {
  return Trajectory(std::move(times), std::move(points));
}

ModelASnapshotVerificationResultV1 verify(
    const Instance& instance,
    const TemporalPiecewiseProgramV1& program,
    double ub = 100.0,
    ModelASnapshotVerificationModeV1 mode =
        ModelASnapshotVerificationModeV1::FAST_FLOAT) {
  auto artifacts = write_artifacts(instance, program, ub);
  return ModelASnapshotVerifier::verify_files(
      artifacts.instance_path, artifacts.history_path, mode);
}
}  // namespace

TEST_CASE("Independent snapshot verifier reloads artifacts and validates constant program") {
  const Instance instance = make_instance({
      trajectory({0.0, 1.0},
                 {Point(0.0, 0.0), Point(0.5, 0.0)})});
  const auto program = make_program(
      {0}, {radius_piece(0.0, 1.0, {1.0})});
  const auto result = verify(instance, program);

  REQUIRE(result.input_reloaded);
  REQUIRE(result.instance_hash_matches);
  REQUIRE(result.provenance_matches);
  REQUIRE(result.partition_valid);
  REQUIRE(result.boundary_ownership_valid);
  REQUIRE(result.assignments_valid);
  REQUIRE(result.radii_valid);
  REQUIRE(result.continuous_coverage_valid);
  REQUIRE(result.cost_upper_check_valid);
  REQUIRE_FALSE(result.cost_enclosure_validated);
  REQUIRE(result.status ==
          ModelASnapshotVerificationStatusV1::NUMERIC_PASS);
  REQUIRE(result.stored_ub >= result.cost_upper_endpoint);
  REQUIRE_FALSE(result.first_violation.has_value());
}

TEST_CASE("Verifier marks a near-boundary coverage sign indeterminate") {
  const Instance instance = make_instance({
      trajectory({0.0, 1.0},
                 {Point(0.0, 0.0), Point(1.0, 0.0)})});
  const auto program = make_program(
      {0},
      {radius_piece(
          0.0, 1.0,
          {std::nextafter(1.0, 0.0)})});
  const auto result = verify(instance, program);

  REQUIRE(result.first_violation.has_value());
  REQUIRE(result.first_violation->code ==
          "COVERAGE_SIGN_INDETERMINATE");
  REQUIRE(result.status ==
          ModelASnapshotVerificationStatusV1::INDETERMINATE);
  REQUIRE(result.first_violation->interval_start == 0.0);
  REQUIRE(result.first_violation->interval_end == 1.0);
  REQUIRE(result.first_violation->station_index == 0);
  REQUIRE(result.first_violation->point_index == 0);
}

TEST_CASE("Verifier catches a repeated tangency root with negative coverage") {
  const Instance instance = make_instance({
      trajectory({0.0, 1.0},
                 {Point(-0.5, 0.0), Point(0.5, 0.0)})});
  const auto program = make_program(
      {0}, {radius_piece(0.0, 1.0, {0.0})});
  const auto result = verify(instance, program);

  REQUIRE(result.status == ModelASnapshotVerificationStatusV1::FAIL);
  REQUIRE(result.first_violation->code ==
          "CONTINUOUS_COVERAGE_VIOLATION");
}

TEST_CASE("Verifier handles equal functions and duplicate trajectories") {
  const auto same = trajectory(
      {0.0, 1.0}, {Point(0.0, 0.0), Point(1.0, 0.0)});
  const Instance instance = make_instance({same, same});
  const auto program = make_program(
      {0, 0}, {radius_piece(0.0, 1.0, {1.0})});
  const auto result = verify(instance, program);

  REQUIRE(result.status ==
          ModelASnapshotVerificationStatusV1::NUMERIC_PASS);
  REQUIRE(result.envelope_equality_points_checked == 0U);
  REQUIRE(result.continuous_coverage_valid);
}

TEST_CASE("Verifier splits at active-envelope equality near an endpoint") {
  constexpr double epsilon = 1e-8;
  const Instance instance = make_instance({
      trajectory({0.0, 1.0},
                 {Point(0.0, 0.0), Point(1.0, 0.0)}),
      trajectory({0.0, 1.0},
                 {Point(1.0 - epsilon, 0.0),
                  Point(1.0 - epsilon, 0.0)})});
  const auto program = make_program(
      {0, 0}, {radius_piece(0.0, 1.0, {1.0})});
  const auto result = verify(instance, program);

  REQUIRE(result.status ==
          ModelASnapshotVerificationStatusV1::NUMERIC_PASS);
  REQUIRE(result.envelope_equality_points_checked >= 1U);
}

TEST_CASE("Verifier handles zero distance and zero velocity") {
  const Instance instance = make_instance({
      trajectory({0.0, 1.0},
                 {Point(0.0, 0.0), Point(0.0, 0.0)})});
  const auto program = make_program(
      {0}, {radius_piece(0.0, 1.0, {0.0})});
  const auto result = verify(instance, program, 1.0);

  REQUIRE(result.status ==
          ModelASnapshotVerificationStatusV1::NUMERIC_PASS);
  REQUIRE(result.cost_upper_endpoint == 0.0);
  REQUIRE(result.continuous_coverage_valid);
}

TEST_CASE("Verifier handles simultaneous trajectory breakpoints") {
  const Instance instance = make_instance({
      trajectory(
          {0.0, 0.5, 1.0},
          {Point(0.0, 0.0), Point(0.5, 0.0), Point(1.0, 0.0)}),
      trajectory(
          {0.0, 0.5, 1.0},
          {Point(0.0, 0.0), Point(0.5, 0.0), Point(1.0, 0.0)})});
  const auto program = make_program(
      {0, 0}, {radius_piece(0.0, 1.0, {1.0})});
  const auto result = verify(instance, program);

  REQUIRE(result.status ==
          ModelASnapshotVerificationStatusV1::NUMERIC_PASS);
  REQUIRE(result.continuous_coverage_valid);
  REQUIRE(result.boundary_ownership == "LOWER_INTERVAL");
}

TEST_CASE("Verifier checks coverage at a shared boundary using lower ownership") {
  const Instance instance = make_instance({
      trajectory(
          {0.0, 0.5, 1.0},
          {Point(0.0, 0.0), Point(0.5, 0.0), Point(1.0, 0.0)})});
  const auto program = make_program(
      {0}, {radius_piece(0.0, 1.0, {1.0})});
  auto artifacts = write_artifacts(instance, program, 100.0);

  Json history;
  {
    std::ifstream input(artifacts.history_path);
    REQUIRE(input.good());
    input >> history;
  }
  auto& entry = history["ub_history"][0];
  auto& snapshot = entry["snapshot"];
  Json left = snapshot["leaves"][0];
  Json right = left;
  left["leaf_id"] = 1U;
  left["start"] = 0.0;
  left["end"] = 0.5;
  left["ub"] = 1000.0;
  left["pieces"] = Json::array({
      {{"start", 0.0}, {"end", 0.5}, {"station_radii", {0.4}}}});
  right["leaf_id"] = 2U;
  right["start"] = 0.5;
  right["end"] = 1.0;
  right["ub"] = 1000.0;
  right["pieces"] = Json::array({
      {{"start", 0.5}, {"end", 1.0}, {"station_radii", {1.0}}}});
  snapshot["leaves"] = Json::array({left, right});
  snapshot["global_ub"] = 3000.0;
  entry["value"] = 3000.0;
  {
    std::ofstream output(artifacts.history_path);
    REQUIRE(output.good());
    output << history.dump(2) << '\n';
  }

  const auto result = ModelASnapshotVerifier::verify_files(
      artifacts.instance_path, artifacts.history_path,
      ModelASnapshotVerificationModeV1::FAST_FLOAT);
  REQUIRE(result.partition_valid);
  REQUIRE(result.boundary_ownership_valid);
  REQUIRE(result.boundary_ownership == "LOWER_INTERVAL");
  REQUIRE(result.status == ModelASnapshotVerificationStatusV1::FAIL);
  REQUIRE(result.first_violation->code ==
          "CONTINUOUS_COVERAGE_VIOLATION");
  REQUIRE(result.first_violation->interval_start == 0.0);
  REQUIRE(result.first_violation->interval_end == 0.5);
}

TEST_CASE("Verifier checks station-empty radius and stored UB upper endpoint") {
  const Instance instance = make_instance(
      {trajectory({0.0, 1.0},
                  {Point(0.0, 0.0), Point(0.0, 0.0)})},
      {{17, Point(0.0, 0.0)}, {23, Point(10.0, 0.0)}});
  const auto nonzero_empty = make_program(
      {0}, {radius_piece(0.0, 1.0, {0.0, 0.25})});
  const auto empty_result = verify(instance, nonzero_empty);
  REQUIRE(empty_result.status ==
          ModelASnapshotVerificationStatusV1::FAIL);
  REQUIRE(empty_result.first_violation->code ==
          "EMPTY_STATION_NONZERO_RADIUS");
  REQUIRE(empty_result.first_violation->station_index == 1);
  REQUIRE(empty_result.first_violation->station_id == 23);

  const Instance covered = make_instance({
      trajectory({0.0, 1.0},
                 {Point(0.0, 0.0), Point(0.0, 0.0)})});
  const auto large_program = make_program(
      {0}, {radius_piece(0.0, 1.0, {1.0})});
  const auto ub_result = verify(covered, large_program, 1.0);
  REQUIRE(ub_result.status == ModelASnapshotVerificationStatusV1::FAIL);
  REQUIRE(ub_result.first_violation->code ==
          "LEAF_UB_BELOW_COST_UPPER_ENDPOINT");
}

TEST_CASE("Verifier refuses validated success without validated backend") {
  const Instance instance = make_instance({
      trajectory({0.0, 1.0},
                 {Point(0.0, 0.0), Point(0.0, 0.0)})});
  const auto program = make_program(
      {0}, {radius_piece(0.0, 1.0, {0.0})});
  const auto result = verify(
      instance, program, 1.0,
      ModelASnapshotVerificationModeV1::VALIDATED);

  REQUIRE(result.status ==
          ModelASnapshotVerificationStatusV1::NOT_CERTIFIED);
  REQUIRE(result.stored_ub >= 1.0);
  REQUIRE(result.first_violation->code ==
          "VALIDATED_BACKEND_UNAVAILABLE");
  REQUIRE_FALSE(result.cost_enclosure_validated);
}

TEST_CASE("Verifier detects independently reloaded instance hash mismatch") {
  const Instance original = make_instance({
      trajectory({0.0, 1.0},
                 {Point(0.0, 0.0), Point(0.0, 0.0)})});
  const auto program = make_program(
      {0}, {radius_piece(0.0, 1.0, {0.0})});
  auto artifacts = write_artifacts(original, program, 1.0);
  Instance changed = original;
  changed.stations[0].pos.x = 0.5;
  DatasetReader::write_json(changed, artifacts.instance_path);

  const auto result = ModelASnapshotVerifier::verify_files(
      artifacts.instance_path, artifacts.history_path,
      ModelASnapshotVerificationModeV1::FAST_FLOAT);
  REQUIRE(result.status == ModelASnapshotVerificationStatusV1::FAIL);
  REQUIRE(result.first_violation->code == "INSTANCE_HASH_MISMATCH");
}
