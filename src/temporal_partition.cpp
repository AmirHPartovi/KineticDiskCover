#include "kdc/temporal_partition.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace kdc {
namespace {
bool finite_leaf(const IntervalRecordV1& leaf) {
  return std::isfinite(leaf.start) &&
         std::isfinite(leaf.end) &&
         leaf.end > leaf.start &&
         std::isfinite(leaf.midpoint) &&
         std::isfinite(leaf.delta) &&
         leaf.delta == leaf.end - leaf.start &&
         leaf.midpoint >= leaf.start &&
         leaf.midpoint <= leaf.end;
}

bool valid_bound(const TemporalBoundRecordV1& bound) {
  return bound.validity != TemporalBoundValidityV1::ABSENT &&
         bound.validity != TemporalBoundValidityV1::INVALID &&
         std::isfinite(bound.value);
}

bool finite_equal_bound(const IntervalRecordV1& leaf) {
  return valid_bound(leaf.lb) &&
         valid_bound(leaf.ub) &&
         leaf.lb.value == leaf.ub.value &&
         std::isfinite(leaf.lb.value);
}
}

TemporalPartition::TemporalPartition(
    double domain_start,
    double domain_end,
    IntervalRecordV1 initial_leaf)
    : domain_start_(domain_start),
      domain_end_(domain_end),
      next_leaf_id_(2) {
  if (!std::isfinite(domain_start_) ||
      !std::isfinite(domain_end_) ||
      domain_end_ <= domain_start_) {
    throw std::invalid_argument(
        "temporal partition domain is invalid");
  }

  if (initial_leaf.leaf_id == 0) {
    initial_leaf.leaf_id = 1;
  }
  initial_leaf.start = domain_start_;
  initial_leaf.end = domain_end_;
  initial_leaf.midpoint =
      domain_start_ +
      (domain_end_ - domain_start_) / 2.0;
  initial_leaf.delta =
      domain_end_ - domain_start_;
  initial_leaf.depth = 0;
  if (!finite_leaf(initial_leaf)) {
    throw std::invalid_argument(
        "initial temporal leaf is invalid");
  }
  initial_leaf.status =
      TemporalLeafStatusV1::OPEN;
  initial_leaf.freeze_reason.clear();
  partition_.emplace(initial_leaf.leaf_id, initial_leaf);
  queue_.insert(initial_leaf.leaf_id);
  if (!validate().valid) {
    throw std::invalid_argument(
        "initial temporal partition failed validation");
  }
}

const IntervalRecordV1& TemporalPartition::leaf(
    TemporalLeafId id) const {
  const auto found = partition_.find(id);
  if (found == partition_.end()) {
    throw std::out_of_range(
        "temporal leaf id is not in P");
  }
  return found->second;
}

const std::map<TemporalLeafId, IntervalRecordV1>&
TemporalPartition::partition() const {
  return partition_;
}

const std::set<TemporalLeafId>&
TemporalPartition::queue() const {
  return queue_;
}

const std::map<TemporalLeafId, IntervalRecordV1>&
TemporalPartition::history() const {
  return history_;
}

TemporalLeafId TemporalPartition::allocate_leaf_id() {
  while (partition_.find(next_leaf_id_) != partition_.end() ||
         history_.find(next_leaf_id_) != history_.end()) {
    if (next_leaf_id_ ==
        std::numeric_limits<TemporalLeafId>::max()) {
      throw std::overflow_error(
          "temporal leaf id exhausted");
    }
    ++next_leaf_id_;
  }
  return next_leaf_id_++;
}

IntervalRecordV1 TemporalPartition::make_child(
    TemporalLeafId parent_id,
    double start,
    double end,
    unsigned depth) const {
  IntervalRecordV1 child;
  child.parent_id = parent_id;
  child.start = start;
  child.end = end;
  child.midpoint =
      start + (end - start) / 2.0;
  child.delta = end - start;
  child.depth = depth;
  child.status =
      TemporalLeafStatusV1::OPEN;
  child.freeze_reason.clear();
  if (!finite_leaf(child)) {
    throw std::invalid_argument(
        "temporal child leaf is invalid");
  }
  return child;
}

std::pair<TemporalLeafId, TemporalLeafId>
TemporalPartition::split_leaf(
    TemporalLeafId id,
    double split_point) {
  const auto found = partition_.find(id);
  if (found == partition_.end()) {
    throw std::out_of_range(
        "cannot split leaf outside P");
  }
  IntervalRecordV1& parent = found->second;
  if (parent.status != TemporalLeafStatusV1::OPEN) {
    throw std::invalid_argument(
        "only OPEN leaves may be split");
  }
  if (!std::isfinite(split_point) ||
      split_point <= parent.start ||
      split_point >= parent.end) {
    throw std::invalid_argument(
        "split point must be strictly inside the leaf");
  }

  const TemporalLeafId left_id =
      allocate_leaf_id();
  const TemporalLeafId right_id =
      allocate_leaf_id();
  IntervalRecordV1 left =
      make_child(id, parent.start,
                 split_point, parent.depth + 1U);
  left.leaf_id = left_id;
  IntervalRecordV1 right =
      make_child(id, split_point,
                 parent.end, parent.depth + 1U);
  right.leaf_id = right_id;

  parent.left_id = left_id;
  parent.right_id = right_id;
  parent.status =
      TemporalLeafStatusV1::FROZEN;
  parent.freeze_reason = "split";
  history_.emplace(id, parent);

  queue_.erase(id);
  partition_.erase(id);
  partition_.emplace(left_id, left);
  partition_.emplace(right_id, right);
  queue_.insert(left_id);
  queue_.insert(right_id);

  const auto validation = validate();
  if (!validation.valid) {
    throw std::invalid_argument(
        "temporal split produced invalid P/Q state");
  }
  return {left_id, right_id};
}

void TemporalPartition::freeze_leaf(
    TemporalLeafId id,
    const std::string& reason) {
  const auto found = partition_.find(id);
  if (found == partition_.end()) {
    throw std::out_of_range(
        "cannot freeze leaf outside P");
  }
  if (reason.empty()) {
    throw std::invalid_argument(
        "FROZEN leaf requires freeze reason");
  }
  found->second.status =
      TemporalLeafStatusV1::FROZEN;
  found->second.freeze_reason = reason;
  queue_.erase(id);
  const auto validation = validate();
  if (!validation.valid) {
    throw std::invalid_argument(
        "temporal freeze produced invalid P/Q state");
  }
}

void TemporalPartition::fathom_leaf(
    TemporalLeafId id) {
  const auto found = partition_.find(id);
  if (found == partition_.end()) {
    throw std::out_of_range(
        "cannot fathom leaf outside P");
  }
  IntervalRecordV1& leaf = found->second;
  if (!valid_bound(leaf.lb) ||
      !valid_bound(leaf.ub) ||
      !std::isfinite(leaf.lb.value) ||
      !std::isfinite(leaf.ub.value) ||
      leaf.lb.value != leaf.ub.value) {
    throw std::invalid_argument(
        "FATHOMED requires finite valid LB=UB");
  }
  leaf.status =
      TemporalLeafStatusV1::FATHOMED;
  leaf.freeze_reason = "LB=UB";
  queue_.erase(id);
  const auto validation = validate();
  if (!validation.valid) {
    throw std::invalid_argument(
        "temporal fathom produced invalid P/Q state");
  }
}

void TemporalPartition::rollback_split(
    TemporalLeafId parent_id,
    TemporalLeafId left_id,
    TemporalLeafId right_id,
    const IntervalRecordV1& parent_record,
    const std::string& freeze_reason) {
  partition_.erase(left_id);
  partition_.erase(right_id);
  queue_.erase(left_id);
  queue_.erase(right_id);
  IntervalRecordV1 restored = parent_record;
  restored.status =
      TemporalLeafStatusV1::FROZEN;
  restored.freeze_reason =
      freeze_reason.empty()
          ? "rollback"
          : freeze_reason;
  restored.left_id = 0;
  restored.right_id = 0;
  partition_[parent_id] = restored;
  queue_.erase(parent_id);
  history_.erase(parent_id);
  const auto validation = validate();
  if (!validation.valid) {
    throw std::invalid_argument(
        "temporal rollback could not restore valid P/Q");
  }
}

void TemporalPartition::set_leaf_bounds(
    TemporalLeafId id,
    const TemporalBoundRecordV1& lb,
    const TemporalBoundRecordV1& ub,
    const std::optional<TemporalPiecewiseProgramV1>& program) {
  const auto found = partition_.find(id);
  if (found == partition_.end()) {
    throw std::out_of_range(
        "cannot set bounds for leaf outside P");
  }
  found->second.lb = lb;
  found->second.ub = ub;
  found->second.ub_piecewise_program = program;
  const auto validation = validate();
  if (!validation.valid) {
    throw std::invalid_argument(
        "temporal leaf bound update produced invalid P/Q state");
  }
}

TemporalPartitionValidationV1
TemporalPartition::validate() const {
  return validate_partition(
      domain_start_,
      domain_end_,
      partition_,
      queue_);
}

TemporalPartitionValidationV1
TemporalPartition::validate_partition(
    double domain_start,
    double domain_end,
    const std::map<TemporalLeafId, IntervalRecordV1>& partition,
    const std::set<TemporalLeafId>& queue) {
  TemporalPartitionValidationV1 result;
  if (!std::isfinite(domain_start) ||
      !std::isfinite(domain_end) ||
      domain_end <= domain_start) {
    result.reason = "invalid temporal domain";
    return result;
  }
  if (partition.empty()) {
    result.reason = "P is empty";
    return result;
  }

  std::vector<const IntervalRecordV1*> sorted;
  sorted.reserve(partition.size());
  for (const auto& entry : partition) {
    sorted.push_back(&entry.second);
  }
  std::sort(
      sorted.begin(),
      sorted.end(),
      [](const IntervalRecordV1* lhs,
         const IntervalRecordV1* rhs) {
        if (lhs->start != rhs->start) {
          return lhs->start < rhs->start;
        }
        return lhs->end < rhs->end;
      });

  if (sorted.front()->start != domain_start ||
      sorted.back()->end != domain_end) {
    result.reason =
        "P does not cover [domain_start,domain_end]";
    return result;
  }

  double previous_end = sorted.front()->start;
  bool first = true;
  for (const IntervalRecordV1* leaf : sorted) {
    if (!finite_leaf(*leaf)) {
      result.reason =
          "P contains an invalid-length leaf";
      return result;
    }
    if (leaf->lb.validity == TemporalBoundValidityV1::ABSENT ||
        leaf->lb.validity == TemporalBoundValidityV1::INVALID) {
      if (leaf->status == TemporalLeafStatusV1::FATHOMED) {
        result.reason =
            "FATHOMED leaf lacks valid LB";
        return result;
      }
    }
    if (leaf->ub.validity == TemporalBoundValidityV1::ABSENT ||
        leaf->ub.validity == TemporalBoundValidityV1::INVALID) {
      if (leaf->status == TemporalLeafStatusV1::FATHOMED) {
        result.reason =
            "FATHOMED leaf lacks valid UB";
        return result;
      }
    }
    if (!first &&
        leaf->start != previous_end) {
      result.reason =
          "P has a gap or inconsistent boundary";
      return result;
    }
    if (leaf->start < previous_end &&
        !first) {
      result.reason =
          "P leaf interiors overlap";
      return result;
    }
    previous_end = leaf->end;
    first = false;

    if (leaf->status ==
        TemporalLeafStatusV1::FATHOMED &&
        !finite_equal_bound(*leaf)) {
      result.reason =
          "FATHOMED leaf must have finite valid LB=UB";
      return result;
    }
  }

  for (const TemporalLeafId id : queue) {
    const auto found = partition.find(id);
    if (found == partition.end()) {
      result.reason =
          "Q contains a leaf not in P";
      return result;
    }
    if (found->second.status !=
        TemporalLeafStatusV1::OPEN) {
      result.reason =
          "Q contains a non-OPEN leaf";
      return result;
    }
  }
  for (const auto& entry : partition) {
    if (entry.second.status ==
        TemporalLeafStatusV1::FROZEN &&
        queue.find(entry.first) != queue.end()) {
      result.reason =
          "FROZEN leaf remains in Q";
      return result;
    }
    if (entry.second.status ==
        TemporalLeafStatusV1::FATHOMED &&
        queue.find(entry.first) != queue.end()) {
      result.reason =
          "FATHOMED leaf remains in Q";
      return result;
    }
  }

  result.valid = true;
  result.reason.clear();
  return result;
}

}  // namespace kdc
