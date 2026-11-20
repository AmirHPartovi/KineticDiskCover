#pragma once

#include "kdc/temporal_partition.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace kdc {

struct GlobalBoundIdentityV1 {
  std::string instance_hash;
  std::string solver_identity;
  std::string config_identity;
  std::string numeric_backend;
  std::string verifier_state;
  std::vector<std::string> certificate_ids;
};

struct GlobalBoundCurrentV1 {
  double value{0.0};
  bool valid{false};
  bool trivial{false};
  std::uint64_t partition_version{0};
  std::vector<std::string> certificate_ids;
};

struct GlobalBoundUbSnapshotPieceV1 {
  double start{0.0};
  double end{0.0};
  std::vector<double> station_radii;
};

struct GlobalBoundUbSnapshotLeafV1 {
  TemporalLeafId leaf_id{0};
  double start{0.0};
  double end{0.0};
  double ub_value{0.0};
  std::vector<int> assignment;
  std::vector<GlobalBoundUbSnapshotPieceV1> pieces;
  std::vector<std::string> certificate_ids;
};

struct GlobalBoundUbSnapshotV1 {
  std::uint32_t schema_version{1};
  std::uint64_t partition_version{0};
  double global_ub{0.0};
  GlobalBoundIdentityV1 identity;
  std::vector<GlobalBoundUbSnapshotLeafV1> leaves;
  bool complete{false};
};

struct GlobalLbHistoryEntryV1 {
  double value{0.0};
  std::uint64_t partition_version{0};
  std::vector<std::string> certificate_ids;
  std::string provenance;
  std::uint64_t sequence{0};
};

struct GlobalUbHistoryEntryV1 {
  double value{0.0};
  std::uint64_t partition_version{0};
  std::vector<std::string> certificate_ids;
  std::string provenance;
  std::uint64_t sequence{0};
  GlobalBoundUbSnapshotV1 snapshot;
};

struct GlobalBoundHistoryResultV1 {
  std::uint32_t schema_version{1};
  GlobalBoundCurrentV1 lb_current;
  GlobalBoundCurrentV1 ub_current;
  std::optional<GlobalLbHistoryEntryV1> lb_best;
  std::optional<GlobalUbHistoryEntryV1> ub_best;
};

class GlobalBoundHistory {
 public:
  explicit GlobalBoundHistory(
      GlobalBoundIdentityV1 identity = {});

  GlobalBoundHistoryResultV1 update(
      const TemporalPartition& partition,
      std::uint64_t partition_version);

  bool invalidate_certificate(
      const std::string& certificate_id);

  const std::optional<GlobalLbHistoryEntryV1>& lb_best() const;
  const std::optional<GlobalUbHistoryEntryV1>& ub_best() const;

  void serialize(const std::string& path) const;
  static GlobalBoundHistory deserialize(
      const std::string& path);

 private:
  GlobalBoundIdentityV1 identity_;
  std::uint64_t sequence_{0};
  std::optional<GlobalLbHistoryEntryV1> lb_best_;
  std::optional<GlobalUbHistoryEntryV1> ub_best_;

  bool lb_entry_valid(
      const GlobalLbHistoryEntryV1& entry,
      const std::vector<std::string>& invalid_certificates) const;
  bool ub_entry_valid(
      const GlobalUbHistoryEntryV1& entry,
      const std::vector<std::string>& invalid_certificates) const;

  void recompute_lb_best();
  void recompute_ub_best();

  std::vector<std::string> invalid_certificates_;
  std::vector<GlobalLbHistoryEntryV1> lb_history_;
  std::vector<GlobalUbHistoryEntryV1> ub_history_;
};

}  // namespace kdc
