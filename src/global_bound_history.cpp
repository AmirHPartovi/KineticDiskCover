#include "kdc/global_bound_history.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>
#include <stdexcept>

namespace kdc {
namespace {
using Json = nlohmann::json;

bool conservative_arithmetic(
    TemporalBoundArithmeticV1 arithmetic) {
  return arithmetic ==
             TemporalBoundArithmeticV1::CONSERVATIVE_INTERVAL ||
         arithmetic ==
             TemporalBoundArithmeticV1::EXACT_RATIONAL;
}

double sum_down(double lhs, double rhs) {
  if (lhs == 0.0) {
    return rhs;
  }
  return std::nextafter(
      lhs + rhs,
      -std::numeric_limits<double>::infinity());
}

double sum_up(double lhs, double rhs) {
  if (lhs == 0.0) {
    return rhs;
  }
  return std::nextafter(
      lhs + rhs,
      std::numeric_limits<double>::infinity());
}

std::vector<std::string> collect_leaf_certificates(
    const TemporalPartition& partition,
    bool lb) {
  std::vector<std::string> certificates;
  for (const auto& entry :
       partition.partition()) {
    const auto& bound =
        lb ? entry.second.lb : entry.second.ub;
    if (!bound.certificate_id.empty()) {
      certificates.push_back(bound.certificate_id);
    }
  }
  std::sort(certificates.begin(), certificates.end());
  certificates.erase(
      std::unique(certificates.begin(),
                  certificates.end()),
      certificates.end());
  return certificates;
}

bool complete_snapshot(
    const TemporalPartition& partition) {
  for (const auto& entry :
       partition.partition()) {
    const IntervalRecordV1& leaf = entry.second;
    if ((leaf.ub.validity != TemporalBoundValidityV1::VALID &&
         leaf.ub.validity !=
             TemporalBoundValidityV1::NUMERICALLY_UNBOUNDED) ||
        !std::isfinite(leaf.ub.value) ||
        (!leaf.ub_piecewise_program.has_value() &&
         !leaf.ub_piecewise_program_v2.has_value())) {
      return false;
    }
  }
  return !partition.partition().empty();
}

GlobalBoundUbSnapshotV1 make_snapshot(
    const TemporalPartition& partition,
    std::uint64_t partition_version,
    double global_ub,
    const GlobalBoundIdentityV1& identity) {
  GlobalBoundUbSnapshotV1 snapshot;
  snapshot.partition_version = partition_version;
  snapshot.global_ub = global_ub;
  snapshot.identity = identity;
  snapshot.complete = true;
  for (const auto& entry :
       partition.partition()) {
    const IntervalRecordV1& leaf = entry.second;
    GlobalBoundUbSnapshotLeafV1 snapshot_leaf;
    snapshot_leaf.leaf_id = leaf.leaf_id;
    snapshot_leaf.start = leaf.start;
    snapshot_leaf.end = leaf.end;
    snapshot_leaf.ub_value = leaf.ub.value;
    if (!leaf.ub.certificate_id.empty()) {
      snapshot_leaf.certificate_ids.push_back(
          leaf.ub.certificate_id);
    }
    if (leaf.ub_piecewise_program_v2.has_value()) {
      const auto& program = *leaf.ub_piecewise_program_v2;
      snapshot_leaf.program_schema_version = 2;
      snapshot_leaf.program_provenance = program.provenance;
      snapshot_leaf.assignment = program.point_assignment;
      for (const auto& piece : program.pieces) {
        GlobalBoundUbSnapshotPieceV1 snapshot_piece;
        snapshot_piece.start = piece.start;
        snapshot_piece.end = piece.end;
        snapshot_piece.time_origin = piece.time_origin;
        snapshot_piece.station_q = piece.station_q;
        snapshot_leaf.pieces.push_back(std::move(snapshot_piece));
      }
    } else if (leaf.ub_piecewise_program.has_value()) {
      const auto& program =
          *leaf.ub_piecewise_program;
      snapshot_leaf.program_schema_version = 1;
      snapshot_leaf.program_provenance =
          "V1 constant-radius program";
      snapshot_leaf.assignment =
          program.point_assignment;
      for (const auto& piece :
           program.pieces) {
        GlobalBoundUbSnapshotPieceV1 snapshot_piece;
        snapshot_piece.start = piece.start;
        snapshot_piece.end = piece.end;
        snapshot_piece.station_radii =
            piece.station_radii;
        snapshot_leaf.pieces.push_back(
            snapshot_piece);
      }
    } else {
      snapshot.complete = false;
    }
    snapshot.leaves.push_back(snapshot_leaf);
  }
  return snapshot;
}

Json snapshot_to_json(
    const GlobalBoundUbSnapshotV1& snapshot) {
  Json leaves = Json::array();
  for (const auto& leaf :
       snapshot.leaves) {
    Json pieces = Json::array();
    for (const auto& piece :
         leaf.pieces) {
      pieces.push_back({
          {"start", piece.start},
          {"end", piece.end},
          {"station_radii", piece.station_radii},
          {"time_origin", piece.time_origin},
          {"station_q", [&piece]() {
             Json values = Json::array();
             for (const auto& q : piece.station_q) {
               values.push_back(
                   {{"a", q.a}, {"b", q.b}, {"c", q.c}});
             }
             return values;
           }()}});
    }
    leaves.push_back({
        {"leaf_id", leaf.leaf_id},
        {"start", leaf.start},
        {"end", leaf.end},
        {"ub", leaf.ub_value},
        {"program_schema_version", leaf.program_schema_version},
        {"program_provenance", leaf.program_provenance},
        {"assignment", leaf.assignment},
        {"pieces", pieces},
        {"certificate_ids", leaf.certificate_ids}});
  }
  Json certificate_ids =
      snapshot.identity.certificate_ids;
  return {
      {"schema_version", snapshot.schema_version},
      {"partition_version", snapshot.partition_version},
      {"global_ub", snapshot.global_ub},
      {"instance_hash", snapshot.identity.instance_hash},
      {"solver_identity", snapshot.identity.solver_identity},
      {"config_identity", snapshot.identity.config_identity},
      {"numeric_backend", snapshot.identity.numeric_backend},
      {"verifier_state", snapshot.identity.verifier_state},
      {"certificate_ids", certificate_ids},
      {"complete", snapshot.complete},
      {"leaves", leaves}};
}

GlobalBoundUbSnapshotV1 snapshot_from_json(
    const Json& json) {
  GlobalBoundUbSnapshotV1 snapshot;
  snapshot.schema_version =
      json.value("schema_version",
                 std::uint32_t{1});
  snapshot.partition_version =
      json.value("partition_version",
                 std::uint64_t{0});
  snapshot.global_ub =
      json.value("global_ub",
                 0.0);
  snapshot.identity.instance_hash =
      json.value("instance_hash", "");
  snapshot.identity.solver_identity =
      json.value("solver_identity", "");
  snapshot.identity.config_identity =
      json.value("config_identity", "");
  snapshot.identity.numeric_backend =
      json.value("numeric_backend", "");
  snapshot.identity.verifier_state =
      json.value("verifier_state", "");
  snapshot.identity.certificate_ids =
      json.value("certificate_ids",
                 std::vector<std::string>{});
  snapshot.complete =
      json.value("complete", false);
  if (json.contains("leaves")) {
    for (const auto& leaf_json :
         json.at("leaves")) {
      GlobalBoundUbSnapshotLeafV1 leaf;
      leaf.leaf_id =
          leaf_json.value("leaf_id",
                          std::uint64_t{0});
      leaf.start =
          leaf_json.value("start", 0.0);
      leaf.end =
          leaf_json.value("end", 0.0);
      leaf.ub_value =
          leaf_json.value("ub", 0.0);
      leaf.program_schema_version =
          leaf_json.value("program_schema_version", 1U);
      leaf.program_provenance =
          leaf_json.value("program_provenance",
                          "V1 constant-radius program");
      leaf.assignment =
          leaf_json.value("assignment",
                          std::vector<int>{});
      leaf.certificate_ids =
          leaf_json.value("certificate_ids",
                          std::vector<std::string>{});
      if (leaf_json.contains("pieces")) {
        for (const auto& piece_json :
             leaf_json.at("pieces")) {
          GlobalBoundUbSnapshotPieceV1 piece;
          piece.start =
              piece_json.value("start", 0.0);
          piece.end =
              piece_json.value("end", 0.0);
          piece.station_radii =
              piece_json.value("station_radii",
                               std::vector<double>{});
          piece.time_origin =
              piece_json.value("time_origin", 0.0);
          if (piece_json.contains("station_q")) {
            for (const auto& q_json : piece_json.at("station_q")) {
              piece.station_q.push_back({
                  q_json.value("a", 0.0),
                  q_json.value("b", 0.0),
                  q_json.value("c", 0.0)});
            }
          }
          leaf.pieces.push_back(piece);
        }
      }
      snapshot.leaves.push_back(leaf);
    }
  }
  return snapshot;
}
}

GlobalBoundHistory::GlobalBoundHistory(
    GlobalBoundIdentityV1 identity)
    : identity_(std::move(identity)) {}

GlobalBoundHistoryResultV1
GlobalBoundHistory::update(
    const TemporalPartition& partition,
    std::uint64_t partition_version) {
  GlobalBoundHistoryResultV1 result;
  result.schema_version = 1;

  GlobalBoundCurrentV1 lb_current;
  lb_current.partition_version = partition_version;
  lb_current.valid = true;
  lb_current.trivial = true;
  lb_current.value = 0.0;

  double lb_sum = 0.0;
  bool lb_all_valid = true;
  bool lb_any_nontrivial = false;
  std::vector<std::string> lb_certificates;
  for (const auto& entry :
       partition.partition()) {
    const IntervalRecordV1& leaf = entry.second;
    if (leaf.lb.validity ==
            TemporalBoundValidityV1::VALID &&
        conservative_arithmetic(leaf.lb.arithmetic) &&
        std::isfinite(leaf.lb.value)) {
      lb_sum =
          sum_down(lb_sum, leaf.lb.value);
      lb_any_nontrivial =
          lb_any_nontrivial ||
          leaf.lb.value > 0.0;
      if (!leaf.lb.certificate_id.empty()) {
        lb_certificates.push_back(
            leaf.lb.certificate_id);
      }
    } else {
      lb_all_valid = false;
    }
  }
  lb_current.value = lb_sum;
  lb_current.valid =
      lb_all_valid &&
      !partition.partition().empty();
  lb_current.trivial =
      !lb_any_nontrivial ||
      !lb_all_valid;
  lb_current.certificate_ids =
      lb_certificates;
  result.lb_current = lb_current;

  if (lb_current.valid) {
    ++sequence_;
    GlobalLbHistoryEntryV1 lb_entry;
    lb_entry.value = lb_current.value;
    lb_entry.partition_version = partition_version;
    lb_entry.certificate_ids =
        lb_certificates;
    lb_entry.provenance =
        lb_any_nontrivial
            ? "conservative leaf LB sum"
            : "trivial nonnegative";
    lb_entry.sequence = sequence_;
    lb_history_.push_back(lb_entry);
    recompute_lb_best();
  } else {
    recompute_lb_best();
  }

  GlobalBoundCurrentV1 ub_current;
  ub_current.partition_version = partition_version;
  ub_current.valid = true;
  ub_current.trivial = false;
  ub_current.value = 0.0;
  bool ub_all_valid = true;
  double ub_sum = 0.0;
  std::vector<std::string> ub_certificates;
  for (const auto& entry :
       partition.partition()) {
    const IntervalRecordV1& leaf = entry.second;
    const bool finite_program_ub =
        (leaf.ub.validity == TemporalBoundValidityV1::VALID ||
         leaf.ub.validity ==
             TemporalBoundValidityV1::NUMERICALLY_UNBOUNDED) &&
        std::isfinite(leaf.ub.value) &&
        (leaf.ub_piecewise_program.has_value() ||
         leaf.ub_piecewise_program_v2.has_value());
    if (finite_program_ub) {
      ub_sum =
          sum_up(ub_sum, leaf.ub.value);
      if (!leaf.ub.certificate_id.empty()) {
        ub_certificates.push_back(
            leaf.ub.certificate_id);
      }
    } else {
      ub_all_valid = false;
    }
  }
  if (ub_all_valid &&
      !partition.partition().empty()) {
    ub_current.value = ub_sum;
  } else {
    ub_current.value =
        std::numeric_limits<double>::infinity();
  }
  ub_current.valid =
      ub_all_valid &&
      !partition.partition().empty();
  ub_current.certificate_ids =
      ub_certificates;
  result.ub_current = ub_current;

  if (ub_current.valid &&
      complete_snapshot(partition)) {
    ++sequence_;
    GlobalBoundUbSnapshotV1 snapshot =
        make_snapshot(
            partition,
            partition_version,
            ub_current.value,
            identity_);
    if (snapshot.complete) {
      GlobalUbHistoryEntryV1 ub_entry;
      ub_entry.value = ub_current.value;
      ub_entry.partition_version =
          partition_version;
      ub_entry.certificate_ids =
          ub_certificates;
      ub_entry.provenance =
          "sum of complete program-backed leaf UBs";
      ub_entry.sequence = sequence_;
      ub_entry.snapshot = snapshot;
      ub_history_.push_back(ub_entry);
      recompute_ub_best();
    } else {
      recompute_ub_best();
    }
  } else {
    recompute_ub_best();
  }

  result.lb_best = lb_best_;
  result.ub_best = ub_best_;
  return result;
}

bool GlobalBoundHistory::lb_entry_valid(
    const GlobalLbHistoryEntryV1& entry,
    const std::vector<std::string>& invalid_certificates) const {
  for (const std::string& certificate :
       entry.certificate_ids) {
    if (std::find(invalid_certificates.begin(),
                  invalid_certificates.end(),
                  certificate) !=
        invalid_certificates.end()) {
      return false;
    }
  }
  return true;
}

bool GlobalBoundHistory::ub_entry_valid(
    const GlobalUbHistoryEntryV1& entry,
    const std::vector<std::string>& invalid_certificates) const {
  for (const std::string& certificate :
       entry.certificate_ids) {
    if (std::find(invalid_certificates.begin(),
                  invalid_certificates.end(),
                  certificate) !=
        invalid_certificates.end()) {
      return false;
    }
  }
  if (!entry.snapshot.complete) {
    return false;
  }
  return true;
}

void GlobalBoundHistory::recompute_lb_best() {
  lb_best_.reset();
  for (const auto& entry :
       lb_history_) {
    if (!lb_entry_valid(
            entry, invalid_certificates_)) {
      continue;
    }
    if (!lb_best_.has_value() ||
        entry.value > lb_best_->value) {
      lb_best_ = entry;
    }
  }
}

void GlobalBoundHistory::recompute_ub_best() {
  ub_best_.reset();
  for (const auto& entry :
       ub_history_) {
    if (!ub_entry_valid(
            entry, invalid_certificates_)) {
      continue;
    }
    if (!ub_best_.has_value() ||
        entry.value < ub_best_->value) {
      ub_best_ = entry;
    }
  }
}

bool GlobalBoundHistory::invalidate_certificate(
    const std::string& certificate_id) {
  if (certificate_id.empty()) {
    return false;
  }
  if (std::find(invalid_certificates_.begin(),
                invalid_certificates_.end(),
                certificate_id) ==
      invalid_certificates_.end()) {
    invalid_certificates_.push_back(
        certificate_id);
  }
  recompute_lb_best();
  recompute_ub_best();
  return true;
}

const std::optional<GlobalLbHistoryEntryV1>&
GlobalBoundHistory::lb_best() const {
  return lb_best_;
}

const std::optional<GlobalUbHistoryEntryV1>&
GlobalBoundHistory::ub_best() const {
  return ub_best_;
}

void GlobalBoundHistory::serialize(
    const std::string& path) const {
  Json document{
      {"schema_version", 1},
      {"identity",
       {{"instance_hash", identity_.instance_hash},
        {"solver_identity", identity_.solver_identity},
        {"config_identity", identity_.config_identity},
        {"numeric_backend", identity_.numeric_backend},
        {"verifier_state", identity_.verifier_state},
        {"certificate_ids", identity_.certificate_ids}}},
      {"sequence", sequence_},
      {"lb_history", Json::array()},
      {"ub_history", Json::array()},
      {"invalid_certificates", invalid_certificates_}};
  for (const auto& entry :
       lb_history_) {
    document["lb_history"].push_back({
        {"value", entry.value},
        {"partition_version", entry.partition_version},
        {"certificate_ids", entry.certificate_ids},
        {"provenance", entry.provenance},
        {"sequence", entry.sequence}});
  }
  for (const auto& entry :
       ub_history_) {
    document["ub_history"].push_back({
        {"value", entry.value},
        {"partition_version", entry.partition_version},
        {"certificate_ids", entry.certificate_ids},
        {"provenance", entry.provenance},
        {"sequence", entry.sequence},
        {"snapshot",
         snapshot_to_json(entry.snapshot)}});
  }
  std::ofstream output(path);
  if (!output) {
    throw std::runtime_error(
        "cannot open global bound history output");
  }
  output << document.dump(2) << "\n";
}

GlobalBoundHistory
GlobalBoundHistory::deserialize(
    const std::string& path) {
  std::ifstream input(path);
  if (!input) {
    throw std::runtime_error(
        "cannot open global bound history");
  }
  Json document;
  try {
    input >> document;
  } catch (const Json::exception& error) {
    throw std::runtime_error(
        std::string("invalid global bound history JSON: ") +
        error.what());
  }
  GlobalBoundIdentityV1 identity;
  if (document.contains("identity")) {
    const auto& value = document.at("identity");
    identity.instance_hash =
        value.value("instance_hash", "");
    identity.solver_identity =
        value.value("solver_identity", "");
    identity.config_identity =
        value.value("config_identity", "");
    identity.numeric_backend =
        value.value("numeric_backend", "");
    identity.verifier_state =
        value.value("verifier_state", "");
    identity.certificate_ids =
        value.value("certificate_ids",
                    std::vector<std::string>{});
  }
  GlobalBoundHistory history(identity);
  history.sequence_ =
      document.value("sequence", std::uint64_t{0});
  if (document.contains("lb_history")) {
    for (const auto& value :
         document.at("lb_history")) {
      GlobalLbHistoryEntryV1 entry;
      entry.value =
          value.value("value", 0.0);
      entry.partition_version =
          value.value("partition_version",
                      std::uint64_t{0});
      entry.certificate_ids =
          value.value("certificate_ids",
                      std::vector<std::string>{});
      entry.provenance =
          value.value("provenance", "");
      entry.sequence =
          value.value("sequence", std::uint64_t{0});
      history.lb_history_.push_back(entry);
    }
  }
  if (document.contains("ub_history")) {
    for (const auto& value :
         document.at("ub_history")) {
      GlobalUbHistoryEntryV1 entry;
      entry.value =
          value.value("value", 0.0);
      entry.partition_version =
          value.value("partition_version",
                      std::uint64_t{0});
      entry.certificate_ids =
          value.value("certificate_ids",
                      std::vector<std::string>{});
      entry.provenance =
          value.value("provenance", "");
      entry.sequence =
          value.value("sequence", std::uint64_t{0});
      if (value.contains("snapshot")) {
        entry.snapshot =
            snapshot_from_json(
                value.at("snapshot"));
      }
      history.ub_history_.push_back(entry);
    }
  }
  history.invalid_certificates_ =
      document.value("invalid_certificates",
                     std::vector<std::string>{});
  history.recompute_lb_best();
  history.recompute_ub_best();
  return history;
}

}  // namespace kdc
