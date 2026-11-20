#pragma once

#include "kdc/weighted_geometry.hpp"
#include "kdc/weighted_static.hpp"

#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace kdc {

using TemporalLeafId = std::uint64_t;

enum class TemporalLeafStatusV1 {
  OPEN,
  FROZEN,
  FATHOMED
};

enum class TemporalBoundArithmeticV1 {
  UNKNOWN,
  FAST_DOUBLE,
  CONSERVATIVE_INTERVAL,
  EXACT_RATIONAL
};

enum class TemporalBoundValidityV1 {
  ABSENT,
  INVALID,
  NUMERICALLY_UNBOUNDED,
  VALID
};

enum class TemporalBoundaryOwnershipV1 {
  LOWER_INTERVAL
};

struct TemporalBoundRecordV1 {
  std::uint32_t schema_version{1};
  double value{0.0};
  TemporalBoundValidityV1 validity{
      TemporalBoundValidityV1::ABSENT};
  std::string method{"none"};
  std::string certificate_id;
  std::string provenance{"none"};
  TemporalBoundArithmeticV1 arithmetic{
      TemporalBoundArithmeticV1::UNKNOWN};
};

struct TemporalRadiusPieceV1 {
  double start{0.0};
  double end{0.0};
  std::vector<double> station_radii;
};

struct TemporalPiecewiseProgramV1 {
  std::uint32_t schema_version{1};
  std::vector<int> point_assignment;
  std::vector<TemporalRadiusPieceV1> pieces;
};

struct TemporalOracleCallSummaryV1 {
  std::uint32_t schema_version{1};
  std::uint64_t exact_calls{0};
  std::uint64_t heuristic_calls{0};
  std::uint64_t relaxation_calls{0};
  std::uint64_t failures{0};
  std::vector<std::string> certificate_ids;
  std::string provenance{"none"};
};

struct TemporalOracleTimingV1 {
  std::uint64_t precompute_nanoseconds{0};
  std::uint64_t bound_nanoseconds{0};
  std::uint64_t oracle_nanoseconds{0};
};

struct IntervalRecordV1 {
  std::uint32_t schema_version{1};
  TemporalLeafId leaf_id{0};
  TemporalLeafId parent_id{0};
  TemporalLeafId left_id{0};
  TemporalLeafId right_id{0};
  double start{0.0};
  double end{0.0};
  double midpoint{0.0};
  double delta{0.0};
  unsigned depth{0};

  TemporalBoundRecordV1 lb;
  TemporalBoundRecordV1 ub;
  std::optional<TemporalPiecewiseProgramV1>
      ub_piecewise_program;

  double lambda_upper{0.0};
  std::optional<WeightMatrix> dminus;
  std::optional<WeightMatrix> dplus;

  TemporalLeafStatusV1 status{
      TemporalLeafStatusV1::OPEN};
  std::string freeze_reason;

  TemporalOracleCallSummaryV1 oracle_summary;
  TemporalOracleTimingV1 timings;
};

struct TemporalPartitionValidationV1 {
  bool valid{false};
  std::string reason;
};

class TemporalPartition {
 public:
  explicit TemporalPartition(
      double domain_start,
      double domain_end,
      IntervalRecordV1 initial_leaf = {});

  const IntervalRecordV1& leaf(TemporalLeafId id) const;
  const std::map<TemporalLeafId, IntervalRecordV1>& partition() const;
  const std::set<TemporalLeafId>& queue() const;
  const std::map<TemporalLeafId, IntervalRecordV1>& history() const;

  double domain_start() const noexcept { return domain_start_; }
  double domain_end() const noexcept { return domain_end_; }

  std::pair<TemporalLeafId, TemporalLeafId> split_leaf(
      TemporalLeafId id,
      double split_point);
  void freeze_leaf(
      TemporalLeafId id,
      const std::string& reason);
  void fathom_leaf(TemporalLeafId id);

  void rollback_split(
      TemporalLeafId parent_id,
      TemporalLeafId left_id,
      TemporalLeafId right_id,
      const IntervalRecordV1& parent_record,
      const std::string& freeze_reason);

  void set_leaf_bounds(
      TemporalLeafId id,
      const TemporalBoundRecordV1& lb,
      const TemporalBoundRecordV1& ub,
      const std::optional<TemporalPiecewiseProgramV1>& program);

  TemporalPartitionValidationV1 validate() const;

  static TemporalPartitionValidationV1 validate_partition(
      double domain_start,
      double domain_end,
      const std::map<TemporalLeafId, IntervalRecordV1>& partition,
      const std::set<TemporalLeafId>& queue);

  static TemporalBoundaryOwnershipV1 boundary_ownership() {
    return TemporalBoundaryOwnershipV1::LOWER_INTERVAL;
  }

 private:
  double domain_start_{0.0};
  double domain_end_{1.0};
  TemporalLeafId next_leaf_id_{1};
  std::map<TemporalLeafId, IntervalRecordV1> partition_;
  std::set<TemporalLeafId> queue_;
  std::map<TemporalLeafId, IntervalRecordV1> history_;

  TemporalLeafId allocate_leaf_id();
  IntervalRecordV1 make_child(
      TemporalLeafId parent_id,
      double start,
      double end,
      unsigned depth) const;
};

}  // namespace kdc
