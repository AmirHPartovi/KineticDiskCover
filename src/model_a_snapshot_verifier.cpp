#include "kdc/model_a_snapshot_verifier.hpp"

#include "kdc/io.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <limits>
#include <set>
#include <sstream>
#include <stdexcept>
#include <utility>
#include <vector>

namespace kdc {
namespace {
using Wide = long double;

struct Quadratic {
  Wide a{0.0L};
  Wide b{0.0L};
  Wide c{0.0L};
};

struct OrderedLeaf {
  const GlobalBoundUbSnapshotLeafV1* leaf{nullptr};
};

void set_violation(
    ModelASnapshotVerificationResultV1& result,
    const std::string& code,
    const std::string& message,
    ModelASnapshotVerificationStatusV1 status,
    std::optional<double> start = std::nullopt,
    std::optional<double> end = std::nullopt,
    std::optional<int> station_index = std::nullopt,
    std::optional<int> station_id = std::nullopt,
    std::optional<int> point_index = std::nullopt) {
  if (!result.first_violation.has_value()) {
    result.first_violation = ModelASnapshotViolationV1{
        code,
        message,
        start,
        end,
        station_index,
        station_id,
        point_index};
  }
  result.status = status;
}

void set_violation(
    ModelASnapshotVerificationResultV1& result,
    const std::string& code,
    const std::string& message,
    double start,
    double end,
    std::optional<int> station_index = std::nullopt,
    std::optional<int> station_id = std::nullopt,
    std::optional<int> point_index = std::nullopt) {
  set_violation(
      result, code, message,
      ModelASnapshotVerificationStatusV1::FAIL,
      start, end, station_index, station_id, point_index);
}

std::string instance_hash_v1(const Instance& instance) {
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
  const std::string bytes = canonical.str();
  for (const char byte : bytes) {
    hash ^= static_cast<unsigned char>(byte);
    hash *= UINT64_C(1099511628211);
  }
  std::ostringstream encoded;
  encoded << "fnv1a64:" << std::hex << hash;
  return encoded.str();
}

bool validate_instance_geometry(
    const Instance& instance,
    ModelASnapshotVerificationResultV1& result) {
  if (instance.n < 0 || instance.m < 0 ||
      static_cast<std::size_t>(instance.n) !=
          instance.trajectories.size() ||
      static_cast<std::size_t>(instance.m) !=
          instance.stations.size() ||
      !std::isfinite(instance.T_end) ||
      instance.T_end <= 0.0) {
    set_violation(
        result, "INVALID_INSTANCE_DIMENSIONS",
        "input dimensions or time horizon are invalid",
        ModelASnapshotVerificationStatusV1::FAIL);
    return false;
  }
  for (std::size_t station = 0;
       station < instance.stations.size();
       ++station) {
    const auto& position = instance.stations[station].pos;
    if (!std::isfinite(position.x) ||
        !std::isfinite(position.y)) {
      set_violation(
          result, "NONFINITE_STATION",
          "station coordinates must be finite",
          ModelASnapshotVerificationStatusV1::FAIL,
          std::nullopt, std::nullopt,
          static_cast<int>(station),
          instance.stations[station].id);
      return false;
    }
  }
  for (std::size_t point = 0;
       point < instance.trajectories.size();
       ++point) {
    const auto& trajectory = instance.trajectories[point];
    if (trajectory.t_breaks.size() < 2U ||
        trajectory.t_breaks.size() !=
            trajectory.waypoints.size() ||
        trajectory.t_breaks.front() != 0.0 ||
        trajectory.t_breaks.back() != instance.T_end) {
      set_violation(
          result, "INVALID_TRAJECTORY_SHAPE",
          "trajectory must define matching waypoints and breakpoints "
          "over exactly [0,T]",
          ModelASnapshotVerificationStatusV1::FAIL,
          0.0, instance.T_end, std::nullopt, std::nullopt,
          static_cast<int>(point));
      return false;
    }
    for (std::size_t index = 0;
         index < trajectory.t_breaks.size();
         ++index) {
      const auto& waypoint = trajectory.waypoints[index];
      if (!std::isfinite(trajectory.t_breaks[index]) ||
          !std::isfinite(waypoint.x) ||
          !std::isfinite(waypoint.y) ||
          (index > 0U &&
           trajectory.t_breaks[index] <=
               trajectory.t_breaks[index - 1U])) {
        set_violation(
            result, "INVALID_TRAJECTORY_DATA",
            "trajectory contains a nonfinite value or unordered breakpoint",
            ModelASnapshotVerificationStatusV1::FAIL,
            std::nullopt, std::nullopt, std::nullopt, std::nullopt,
            static_cast<int>(point));
        return false;
      }
    }
  }
  return true;
}

Quadratic distance_polynomial(
    const Instance& instance,
    std::size_t point,
    std::size_t station,
    std::size_t segment,
    Wide origin) {
  const auto& trajectory = instance.trajectories[point];
  const Wide start = trajectory.t_breaks[segment];
  const Wide duration =
      static_cast<Wide>(trajectory.t_breaks[segment + 1U]) - start;
  const Wide dx =
      static_cast<Wide>(trajectory.waypoints[segment + 1U].x) -
      trajectory.waypoints[segment].x;
  const Wide dy =
      static_cast<Wide>(trajectory.waypoints[segment + 1U].y) -
      trajectory.waypoints[segment].y;
  const Wide vx = dx / duration;
  const Wide vy = dy / duration;
  const Wide rx =
      static_cast<Wide>(trajectory.waypoints[segment].x) +
      vx * (origin - start) -
      instance.stations[station].pos.x;
  const Wide ry =
      static_cast<Wide>(trajectory.waypoints[segment].y) +
      vy * (origin - start) -
      instance.stations[station].pos.y;
  return {
      vx * vx + vy * vy,
      2.0L * (rx * vx + ry * vy),
      rx * rx + ry * ry};
}

Wide evaluate(const Quadratic& polynomial, Wide x) {
  return (polynomial.a * x + polynomial.b) * x +
         polynomial.c;
}

std::size_t segment_for_midpoint(
    const Trajectory& trajectory,
    Wide midpoint) {
  for (std::size_t segment = 0;
       segment + 1U < trajectory.t_breaks.size();
       ++segment) {
    if (midpoint >= trajectory.t_breaks[segment] &&
        midpoint <= trajectory.t_breaks[segment + 1U]) {
      return segment;
    }
  }
  throw std::runtime_error(
      "midpoint does not belong to an input trajectory segment");
}

void append_roots(
    const Quadratic& difference,
    Wide segment_start,
    Wide lower,
    Wide upper,
    std::vector<Wide>& cuts,
    bool& indeterminate) {
  const Wide a = difference.a;
  const Wide b = difference.b;
  const Wide c = difference.c;
  std::vector<Wide> roots;
  if (a == 0.0L) {
    if (b == 0.0L) {
      return;
    }
    roots.push_back(-c / b);
  } else {
    const Wide discriminant = b * b - 4.0L * a * c;
    if (!std::isfinite(discriminant)) {
      indeterminate = true;
      return;
    }
    if (discriminant < 0.0L) {
      return;
    }
    if (discriminant == 0.0L) {
      roots.push_back(-b / (2.0L * a));
    } else {
      const Wide square_root = std::sqrt(discriminant);
      const Wide q =
          -0.5L * (b + std::copysign(square_root, b));
      if (q == 0.0L) {
        roots.push_back(-b / (2.0L * a));
      } else {
        roots.push_back(q / a);
        roots.push_back(c / q);
      }
    }
  }

  const Wide scale = std::max(
      {1.0L, std::abs(lower), std::abs(upper)});
  const Wide ambiguity =
      16.0L * std::numeric_limits<Wide>::epsilon() * scale;
  for (const Wide local_root : roots) {
    const Wide root = segment_start + local_root;
    if (!std::isfinite(root)) {
      indeterminate = true;
      return;
    }
    if (root < lower || root > upper) {
      continue;
    }
    if ((root > lower && root - lower <= ambiguity) ||
        (root < upper && upper - root <= ambiguity)) {
      indeterminate = true;
      return;
    }
    if (root > lower && root < upper) {
      cuts.push_back(root);
    }
  }
  if (roots.size() == 2U) {
    const Wide first = segment_start + roots[0];
    const Wide second = segment_start + roots[1];
    if (first != second &&
        std::abs(first - second) <= ambiguity) {
      indeterminate = true;
    }
  }
}

double round_down_nonnegative(Wide value) {
  double result = static_cast<double>(value);
  if (static_cast<Wide>(result) > value) {
    result = std::nextafter(
        result, -std::numeric_limits<double>::infinity());
  }
  return std::max(0.0, result);
}

double round_up_nonnegative(Wide value) {
  double result = static_cast<double>(value);
  if (static_cast<Wide>(result) < value) {
    result = std::nextafter(
        result, std::numeric_limits<double>::infinity());
  }
  if (value > 0.0L) {
    result = std::nextafter(
        result, std::numeric_limits<double>::infinity());
  }
  return result;
}

bool report_numerical_fail(
    ModelASnapshotVerificationResultV1& result,
    const std::string& code,
    const std::string& message,
    double start,
    double end,
    std::optional<int> station = std::nullopt,
    std::optional<int> station_id = std::nullopt,
    std::optional<int> point = std::nullopt) {
  set_violation(
      result, code, message,
      ModelASnapshotVerificationStatusV1::FAIL,
      start, end, station, station_id, point);
  return false;
}

bool validate_snapshot_structure(
    const Instance& instance,
    const GlobalBoundUbSnapshotV1& snapshot,
    ModelASnapshotVerificationResultV1& result) {
  if (TemporalPartition::boundary_ownership() !=
      TemporalBoundaryOwnershipV1::LOWER_INTERVAL) {
    set_violation(
        result, "BOUNDARY_POLICY_MISMATCH",
        "snapshot verifier requires lower-interval ownership",
        ModelASnapshotVerificationStatusV1::FAIL);
    return false;
  }
  if (snapshot.leaves.empty()) {
    set_violation(
        result, "EMPTY_PARTITION",
        "snapshot partition must contain at least one leaf",
        ModelASnapshotVerificationStatusV1::FAIL);
    return false;
  }
  std::vector<const GlobalBoundUbSnapshotLeafV1*> leaves;
  leaves.reserve(snapshot.leaves.size());
  std::set<TemporalLeafId> leaf_ids;
  for (const auto& leaf : snapshot.leaves) {
    if (leaf.leaf_id == 0U ||
        !leaf_ids.insert(leaf.leaf_id).second) {
      return report_numerical_fail(
          result, "DUPLICATE_OR_INVALID_LEAF_ID",
          "snapshot leaf identifiers must be unique and nonzero",
          leaf.start, leaf.end);
    }
    if (!std::isfinite(leaf.start) ||
        !std::isfinite(leaf.end) ||
        leaf.end <= leaf.start ||
        !std::isfinite(leaf.ub_value) ||
        leaf.ub_value < 0.0) {
      return report_numerical_fail(
          result, "INVALID_LEAF_RECORD",
          "leaf interval and UB must be finite and nonnegative",
          leaf.start, leaf.end);
    }
    leaves.push_back(&leaf);
  }
  std::sort(
      leaves.begin(), leaves.end(),
      [](const auto* lhs, const auto* rhs) {
        return lhs->start < rhs->start;
      });
  if (leaves.front()->start != 0.0 ||
      leaves.back()->end != instance.T_end) {
    set_violation(
        result, "PARTITION_DOMAIN_GAP",
        "snapshot leaves do not cover the full [0,T] domain",
        ModelASnapshotVerificationStatusV1::FAIL);
    return false;
  }
  for (std::size_t index = 0; index < leaves.size(); ++index) {
    const auto& leaf = *leaves[index];
    if (index > 0U && leaf.start != leaves[index - 1U]->end) {
      const auto& previous = *leaves[index - 1U];
      return report_numerical_fail(
          result,
          leaf.start < previous.end
              ? "PARTITION_INTERIOR_OVERLAP"
              : "PARTITION_DOMAIN_GAP",
          leaf.start < previous.end
              ? "leaf interiors overlap"
              : "adjacent leaves do not share the same boundary",
          previous.start, leaf.end);
    }
    if (leaf.assignment.size() !=
        static_cast<std::size_t>(instance.n)) {
      return report_numerical_fail(
          result, "ASSIGNMENT_DIMENSION_MISMATCH",
          "leaf assignment must contain exactly one station index per point",
          leaf.start, leaf.end);
    }
    if (leaf.program_schema_version == 2U &&
        leaf.program_provenance.empty()) {
      return report_numerical_fail(
          result, "MISSING_PROGRAM_PROVENANCE",
          "V2 program must retain its construction provenance",
          leaf.start, leaf.end);
    }
    for (std::size_t point = 0;
         point < leaf.assignment.size();
         ++point) {
      const int station = leaf.assignment[point];
      if (station < 0 || station >= instance.m) {
        return report_numerical_fail(
            result, "INVALID_STATION_ASSIGNMENT",
            "point does not have exactly one valid assigned station",
            leaf.start, leaf.end, station, std::nullopt,
            static_cast<int>(point));
      }
    }
    if (leaf.pieces.empty()) {
      return report_numerical_fail(
          result, "MISSING_PROGRAM_PIECES",
          "complete snapshot leaf has no radius program pieces",
          leaf.start, leaf.end);
    }
    std::vector<std::pair<double, double>> piece_intervals;
    for (const auto& piece : leaf.pieces) {
      if (!std::isfinite(piece.start) ||
          !std::isfinite(piece.end) ||
          piece.start < leaf.start ||
          piece.end > leaf.end ||
          piece.end <= piece.start) {
        return report_numerical_fail(
            result, "INVALID_PROGRAM_PIECE",
            "program piece interval is malformed",
            piece.start, piece.end);
      }
      if (leaf.program_schema_version == 1U) {
        if (piece.station_radii.size() !=
            static_cast<std::size_t>(instance.m)) {
          return report_numerical_fail(
              result, "INVALID_V1_RADIUS_VECTOR",
              "V1 constant-radius pieces require one radius per station",
              piece.start, piece.end);
        }
        for (std::size_t station = 0;
             station < piece.station_radii.size();
             ++station) {
          const double radius = piece.station_radii[station];
          if (!std::isfinite(radius) || radius < 0.0) {
            return report_numerical_fail(
                result, "INVALID_STATION_RADIUS",
                "V1 radius must be finite and nonnegative",
                piece.start, piece.end,
                static_cast<int>(station),
                instance.stations[station].id);
          }
        }
      } else if (leaf.program_schema_version == 2U) {
        if (piece.station_q.size() !=
                static_cast<std::size_t>(instance.m) ||
            !std::isfinite(piece.time_origin)) {
          return report_numerical_fail(
              result, "INVALID_V2_Q_VECTOR",
              "V2 piece requires a finite time origin and one q_i per station",
              piece.start, piece.end);
        }
        for (std::size_t station = 0;
             station < piece.station_q.size();
             ++station) {
          const auto& q = piece.station_q[station];
          if (!std::isfinite(q.a) ||
              !std::isfinite(q.b) ||
              !std::isfinite(q.c)) {
            return report_numerical_fail(
                result, "NONFINITE_Q_COEFFICIENT",
                "V2 squared-radius coefficients must be finite",
                piece.start, piece.end,
                static_cast<int>(station),
                instance.stations[station].id);
          }
        }
      } else {
        return report_numerical_fail(
            result, "UNSUPPORTED_PROGRAM_SCHEMA",
            "snapshot leaf program schema is not supported",
            piece.start, piece.end);
      }
      piece_intervals.emplace_back(piece.start, piece.end);
    }
    std::sort(piece_intervals.begin(), piece_intervals.end());
    double covered_until = leaf.start;
    for (const auto& interval : piece_intervals) {
      if (interval.first > covered_until) {
        return report_numerical_fail(
            result, "PROGRAM_TIME_GAP",
            "radius program pieces do not cover the leaf interval",
            covered_until, interval.first);
      }
      covered_until = std::max(covered_until, interval.second);
    }
    if (covered_until != leaf.end) {
      return report_numerical_fail(
          result, "PROGRAM_TIME_GAP",
          "radius program pieces do not reach the leaf endpoint",
          covered_until, leaf.end);
    }
  }
  result.partition_valid = true;
  result.boundary_ownership_valid = true;
  result.assignments_valid = true;
  result.radii_valid = true;
  return true;
}

bool run_numerical_checks(
    const Instance& instance,
    const GlobalUbHistoryEntryV1& history_entry,
    ModelASnapshotVerificationResultV1& result) {
  const auto& snapshot = history_entry.snapshot;
  std::vector<OrderedLeaf> leaves;
  leaves.reserve(snapshot.leaves.size());
  for (const auto& leaf : snapshot.leaves) {
    leaves.push_back({&leaf});
  }
  std::sort(
      leaves.begin(), leaves.end(),
      [](const OrderedLeaf& lhs, const OrderedLeaf& rhs) {
        return lhs.leaf->start < rhs.leaf->start;
      });

  std::vector<double> common_breakpoints{0.0, instance.T_end};
  for (const auto& trajectory : instance.trajectories) {
    common_breakpoints.insert(
        common_breakpoints.end(),
        trajectory.t_breaks.begin(),
        trajectory.t_breaks.end());
  }
  std::sort(common_breakpoints.begin(), common_breakpoints.end());
  common_breakpoints.erase(
      std::unique(common_breakpoints.begin(),
                  common_breakpoints.end()),
      common_breakpoints.end());

  Wide total_cost = 0.0L;
  Wide leaf_ub_sum = 0.0L;
  bool all_empty_stations_zero = true;
  for (const auto& ordered : leaves) {
    const auto& leaf = *ordered.leaf;

    std::vector<Wide> cuts{
        static_cast<Wide>(leaf.start),
        static_cast<Wide>(leaf.end)};
    for (const auto& piece : leaf.pieces) {
      cuts.push_back(static_cast<Wide>(piece.start));
      cuts.push_back(static_cast<Wide>(piece.end));
    }
    for (const double breakpoint : common_breakpoints) {
      if (breakpoint > leaf.start && breakpoint < leaf.end) {
        cuts.push_back(static_cast<Wide>(breakpoint));
      }
    }

    bool roots_indeterminate = false;
    std::vector<Wide> envelope_roots;
    for (std::size_t common_index = 0;
         common_index + 1U < common_breakpoints.size();
         ++common_index) {
      const Wide common_start =
          common_breakpoints[common_index];
      const Wide common_end =
          common_breakpoints[common_index + 1U];
      const Wide lower =
          std::max(common_start, static_cast<Wide>(leaf.start));
      const Wide upper =
          std::min(common_end, static_cast<Wide>(leaf.end));
      if (upper <= lower) {
        continue;
      }
      const Wide midpoint = lower + (upper - lower) / 2.0L;
      for (std::size_t station = 0;
           station < static_cast<std::size_t>(instance.m);
           ++station) {
        std::vector<std::size_t> assigned_points;
        for (std::size_t point = 0;
             point < leaf.assignment.size();
             ++point) {
          if (leaf.assignment[point] ==
              static_cast<int>(station)) {
            assigned_points.push_back(point);
          }
        }
        for (std::size_t first = 0;
             first < assigned_points.size();
             ++first) {
          const std::size_t first_point =
              assigned_points[first];
          const std::size_t first_segment =
              segment_for_midpoint(
                  instance.trajectories[first_point], midpoint);
          const Quadratic first_poly =
              distance_polynomial(
                  instance, first_point, station,
                  first_segment, lower);
          for (std::size_t second = first + 1U;
               second < assigned_points.size();
               ++second) {
            const std::size_t second_point =
                assigned_points[second];
            const std::size_t second_segment =
                segment_for_midpoint(
                    instance.trajectories[second_point], midpoint);
            const Quadratic second_poly =
                distance_polynomial(
                    instance, second_point, station,
                    second_segment, lower);
            append_roots(
                {first_poly.a - second_poly.a,
                 first_poly.b - second_poly.b,
                 first_poly.c - second_poly.c},
                lower, lower, upper, envelope_roots,
                roots_indeterminate);
            if (roots_indeterminate) {
              set_violation(
                  result, "ENVELOPE_ROOT_INDETERMINATE",
                  "distance-polynomial equality roots cannot be "
                  "ordered reliably in FAST_FLOAT mode",
                  static_cast<double>(lower),
                  static_cast<double>(upper),
                  static_cast<int>(station),
                  instance.stations[station].id,
                  static_cast<int>(first_point));
              result.status =
                  ModelASnapshotVerificationStatusV1::INDETERMINATE;
              return false;
            }
          }
        }
      }
    }
    std::sort(envelope_roots.begin(), envelope_roots.end());
    std::vector<Wide> unique_envelope_roots;
    for (const Wide root : envelope_roots) {
      if (unique_envelope_roots.empty() ||
          unique_envelope_roots.back() != root) {
        if (!unique_envelope_roots.empty()) {
          const Wide scale =
              std::max({1.0L, std::abs(root),
                        std::abs(unique_envelope_roots.back())});
          if (root - unique_envelope_roots.back() <=
              16.0L *
                  std::numeric_limits<Wide>::epsilon() * scale) {
            set_violation(
                result, "ENVELOPE_ROOT_ORDER_INDETERMINATE",
                "near-coincident envelope equality points cannot be "
                "ordered reliably",
                static_cast<double>(unique_envelope_roots.back()),
                static_cast<double>(root));
            result.status =
                ModelASnapshotVerificationStatusV1::INDETERMINATE;
            return false;
          }
        }
        unique_envelope_roots.push_back(root);
      }
    }
    result.envelope_equality_points_checked +=
        unique_envelope_roots.size();
    cuts.insert(
        cuts.end(),
        unique_envelope_roots.begin(),
        unique_envelope_roots.end());
    std::sort(cuts.begin(), cuts.end());
    std::vector<Wide> unique_cuts;
    unique_cuts.reserve(cuts.size());
    for (const Wide cut : cuts) {
      if (unique_cuts.empty() || unique_cuts.back() != cut) {
        if (!unique_cuts.empty()) {
          const Wide scale =
              std::max({1.0L, std::abs(cut),
                        std::abs(unique_cuts.back())});
          if (cut - unique_cuts.back() <=
              16.0L *
                  std::numeric_limits<Wide>::epsilon() *
                  scale) {
            set_violation(
                result, "CUT_ORDER_INDETERMINATE",
                "program or envelope cut points are too close "
                "to order reliably",
                static_cast<double>(unique_cuts.back()),
                static_cast<double>(cut));
            result.status =
                ModelASnapshotVerificationStatusV1::INDETERMINATE;
            return false;
          }
        }
        unique_cuts.push_back(cut);
      }
    }
    cuts = std::move(unique_cuts);

    Wide leaf_cost = 0.0L;
    for (std::size_t cut_index = 0;
         cut_index + 1U < cuts.size();
         ++cut_index) {
      const Wide start = cuts[cut_index];
      const Wide end = cuts[cut_index + 1U];
      if (end <= start) {
        continue;
      }
      const Wide midpoint = start + (end - start) / 2.0L;
      const GlobalBoundUbSnapshotPieceV1* v2_piece = nullptr;
      std::vector<Wide> v1_radii(
          static_cast<std::size_t>(instance.m), 0.0L);
      bool program_piece_covers = false;
      for (const auto& piece : leaf.pieces) {
        if (piece.start > start || piece.end < end) {
          continue;
        }
        program_piece_covers = true;
        if (leaf.program_schema_version == 2U) {
          if (v2_piece != nullptr) {
            return report_numerical_fail(
                result, "OVERLAPPING_V2_PROGRAM_PIECES",
                "V2 program pieces overlap on an open subinterval",
                static_cast<double>(start),
                static_cast<double>(end));
          }
          v2_piece = &piece;
        } else {
          for (std::size_t station = 0;
               station < v1_radii.size();
               ++station) {
            v1_radii[station] =
                std::max(v1_radii[station],
                         static_cast<Wide>(
                             piece.station_radii[station]));
          }
        }
      }
      if (!program_piece_covers) {
        return report_numerical_fail(
            result, "PROGRAM_TIME_GAP",
            "radius program pieces do not cover this open interval",
            static_cast<double>(start),
            static_cast<double>(end));
      }

      std::vector<bool> station_is_empty(
          static_cast<std::size_t>(instance.m), true);
      for (const int station : leaf.assignment) {
        station_is_empty[static_cast<std::size_t>(station)] = false;
      }
      for (std::size_t station = 0;
           station < static_cast<std::size_t>(instance.m);
           ++station) {
        Quadratic q_local;
        if (leaf.program_schema_version == 2U) {
          const auto& stored = v2_piece->station_q[station];
          const Wide shift =
              start - static_cast<Wide>(v2_piece->time_origin);
          q_local = {
              static_cast<Wide>(stored.a),
              2.0L * static_cast<Wide>(stored.a) * shift +
                  static_cast<Wide>(stored.b),
              evaluate(
                  {static_cast<Wide>(stored.a),
                   static_cast<Wide>(stored.b),
                   static_cast<Wide>(stored.c)},
                  shift)};
        } else {
          const Wide q = v1_radii[station] * v1_radii[station];
          q_local = {0.0L, 0.0L, q};
        }
        const Wide width = end - start;
        std::vector<Wide> q_candidates{
            0.0L, width};
        if (q_local.a != 0.0L) {
          const Wide vertex =
              -q_local.b / (2.0L * q_local.a);
          if (vertex > 0.0L && vertex < width) {
            q_candidates.push_back(vertex);
          }
        }
        Wide q_min = std::numeric_limits<Wide>::infinity();
        for (const Wide x : q_candidates) {
          q_min = std::min(q_min, evaluate(q_local, x));
        }
        const Wide q_scale = std::max(
            {1.0L,
             std::abs(evaluate(q_local, 0.0L)),
             std::abs(evaluate(q_local, width))});
        const Wide q_tolerance =
            32.0L *
            std::numeric_limits<Wide>::epsilon() * q_scale;
        if (!std::isfinite(q_min)) {
          return report_numerical_fail(
              result, "NONFINITE_Q_FUNCTION",
              "q_i(t) is not finite on a program subinterval",
              static_cast<double>(start),
              static_cast<double>(end),
              static_cast<int>(station),
              instance.stations[station].id);
        }
        if (q_min < -q_tolerance) {
          return report_numerical_fail(
              result, "NEGATIVE_Q_FUNCTION",
              "q_i(t) is negative inside the program subinterval",
              static_cast<double>(start),
              static_cast<double>(end),
              static_cast<int>(station),
              instance.stations[station].id);
        }
        if (q_min < 0.0L) {
          set_violation(
              result, "Q_SIGN_INDETERMINATE",
              "q_i(t) is within floating-point ambiguity of zero",
              ModelASnapshotVerificationStatusV1::INDETERMINATE,
              static_cast<double>(start),
              static_cast<double>(end),
              static_cast<int>(station),
              instance.stations[station].id);
          return false;
        }
        if (station_is_empty[station] &&
            (q_local.a != 0.0L || q_local.b != 0.0L ||
             q_local.c != 0.0L)) {
          all_empty_stations_zero = false;
          return report_numerical_fail(
              result, "EMPTY_STATION_NONZERO_RADIUS",
              "empty station must have q_i(t)=0",
              static_cast<double>(start),
              static_cast<double>(end),
              static_cast<int>(station),
              instance.stations[station].id);
        }
        leaf_cost +=
            q_local.a * width * width * width / 3.0L +
            q_local.b * width * width / 2.0L +
            q_local.c * width;

        for (std::size_t point = 0;
             point < leaf.assignment.size();
             ++point) {
          if (leaf.assignment[point] !=
              static_cast<int>(station)) {
            continue;
          }
          const Wide point_midpoint = midpoint;
          const std::size_t segment =
              segment_for_midpoint(
                  instance.trajectories[point],
                  point_midpoint);
          const auto& trajectory =
              instance.trajectories[point];
          const Wide segment_start =
              trajectory.t_breaks[segment];
          const Quadratic distance =
              distance_polynomial(
                  instance, point, station, segment,
                  segment_start);
          const Wide distance_shift = start - segment_start;
          const Quadratic distance_local{
              distance.a,
              2.0L * distance.a * distance_shift +
                  distance.b,
              evaluate(distance, distance_shift)};
          const Quadratic difference{
              q_local.a - distance_local.a,
              q_local.b - distance_local.b,
              q_local.c - distance_local.c};
          std::vector<Wide> candidates{0.0L, width};
          if (difference.a != 0.0L) {
            const Wide vertex =
                -difference.b / (2.0L * difference.a);
            if (vertex > 0.0L && vertex < width) {
              candidates.push_back(vertex);
            }
          }
          Wide minimum =
              std::numeric_limits<Wide>::infinity();
          for (const Wide candidate : candidates) {
            minimum = std::min(
                minimum, evaluate(difference, candidate));
          }
          const Wide scale = std::max(
              {1.0L,
               std::abs(evaluate(q_local, 0.0L)),
               std::abs(evaluate(q_local, width)),
               std::abs(evaluate(distance_local, 0.0L)),
               std::abs(evaluate(distance_local, width))});
          const Wide tolerance =
              32.0L *
              std::numeric_limits<Wide>::epsilon() * scale;
          if (!std::isfinite(minimum)) {
            set_violation(
                result, "COVERAGE_INDETERMINATE",
                "q_i(t)-d_ij(t) could not be evaluated finitely",
                static_cast<double>(start),
                static_cast<double>(end),
                static_cast<int>(station),
                instance.stations[station].id,
                static_cast<int>(point));
            result.status =
                ModelASnapshotVerificationStatusV1::INDETERMINATE;
            return false;
          }
          if (minimum < -tolerance) {
            return report_numerical_fail(
                result, "CONTINUOUS_COVERAGE_VIOLATION",
                "q_i(t)-d_ij(t) is negative inside the piece",
                static_cast<double>(start),
                static_cast<double>(end),
                static_cast<int>(station),
                instance.stations[station].id,
                static_cast<int>(point));
          }
          if (minimum < 0.0L) {
            set_violation(
                result, "COVERAGE_SIGN_INDETERMINATE",
                "q_i(t)-d_ij(t) is within floating-point ambiguity "
                "of zero",
                static_cast<double>(start),
                static_cast<double>(end),
                static_cast<int>(station),
                instance.stations[station].id,
                static_cast<int>(point));
            result.status =
                ModelASnapshotVerificationStatusV1::INDETERMINATE;
            return false;
          }
        }
      }
    }

    if (!std::isfinite(leaf_cost) || leaf_cost < 0.0L) {
      set_violation(
          result, "COST_INDETERMINATE",
          "piecewise program cost is not finite",
          leaf.start, leaf.end);
      result.status =
          ModelASnapshotVerificationStatusV1::INDETERMINATE;
      return false;
    }
    const Wide pi = std::acos(-1.0L);
    const Wide leaf_integral = pi * leaf_cost;
    const double leaf_cost_upper =
        round_up_nonnegative(leaf_integral);
    if (!std::isfinite(leaf_cost_upper)) {
      set_violation(
          result, "COST_INDETERMINATE",
          "piecewise program cost exceeds the numeric range",
          leaf.start, leaf.end);
      result.status =
          ModelASnapshotVerificationStatusV1::INDETERMINATE;
      return false;
    }
    if (leaf.ub_value < leaf_cost_upper) {
      return report_numerical_fail(
          result, "LEAF_UB_BELOW_COST_UPPER_ENDPOINT",
          "leaf UB is below the upper endpoint of the independently "
          "recomputed program cost",
          leaf.start, leaf.end);
    }
    total_cost += leaf_cost;
    leaf_ub_sum += static_cast<Wide>(leaf.ub_value);
  }

  if (!std::isfinite(total_cost) || total_cost < 0.0L) {
    set_violation(
        result, "COST_INDETERMINATE",
        "global program cost is not finite",
        ModelASnapshotVerificationStatusV1::INDETERMINATE);
    return false;
  }
  const Wide global_integral =
      std::acos(-1.0L) * total_cost;
  result.cost_lower_endpoint =
      round_down_nonnegative(global_integral);
  result.cost_upper_endpoint =
      round_up_nonnegative(global_integral);
  result.cost_enclosure_validated = false;
  if (static_cast<Wide>(snapshot.global_ub) < leaf_ub_sum) {
    set_violation(
        result, "GLOBAL_UB_BELOW_LEAF_UB_SUM",
        "snapshot global UB does not dominate the independently "
        "summed leaf UB contributions",
        0.0, instance.T_end);
    return false;
  }
  if (snapshot.global_ub < result.cost_upper_endpoint) {
    set_violation(
        result, "UB_BELOW_COST_UPPER_ENDPOINT",
        "stored global UB is below the upper endpoint of the "
        "independently recomputed cost; comparison with the lower "
        "endpoint is insufficient",
        0.0, instance.T_end);
    return false;
  }
  result.cost_upper_check_valid = true;
  result.assignments_valid = true;
  result.radii_valid = true;
  result.continuous_coverage_valid = true;
  result.empty_stations_zero = all_empty_stations_zero;
  result.arithmetic_note =
      "FAST_FLOAT numerical check using long double intermediates; "
      "cost endpoints are not validated enclosures";
  result.status =
      ModelASnapshotVerificationStatusV1::NUMERIC_PASS;
  return true;
}

}  // namespace

ModelASnapshotVerificationResultV1
ModelASnapshotVerifier::verify_files(
    const std::string& instance_json_path,
    const std::string& bound_history_json_path,
    ModelASnapshotVerificationModeV1 mode) {
  ModelASnapshotVerificationResultV1 result;
  result.mode = mode;
  Instance instance;
  GlobalBoundHistory history;
  try {
    instance = DatasetReader::read_json(instance_json_path);
    result.input_reloaded = true;
    history = GlobalBoundHistory::deserialize(
        bound_history_json_path);
  } catch (const std::exception& error) {
    set_violation(
        result, "ARTIFACT_LOAD_FAILED", error.what(),
        ModelASnapshotVerificationStatusV1::FAIL);
    return result;
  }
  if (!validate_instance_geometry(instance, result)) {
    return result;
  }
  if (!history.ub_best().has_value()) {
    set_violation(
        result, "NO_VALID_UB_SNAPSHOT",
        "serialized bound history contains no valid complete UB snapshot",
        ModelASnapshotVerificationStatusV1::FAIL);
    return result;
  }
  const auto& entry = *history.ub_best();
  const auto& snapshot = entry.snapshot;
  result.stored_ub = snapshot.global_ub;

  const std::string recomputed_hash =
      instance_hash_v1(instance);
  result.instance_hash_matches =
      snapshot.identity.instance_hash == recomputed_hash;
  if (!result.instance_hash_matches) {
    set_violation(
        result, "INSTANCE_HASH_MISMATCH",
        "snapshot instance hash does not match the independently "
        "reloaded input data",
        ModelASnapshotVerificationStatusV1::FAIL);
    return result;
  }
  if ((snapshot.schema_version != 1U &&
       snapshot.schema_version != 2U) ||
      !snapshot.complete ||
      snapshot.partition_version != entry.partition_version ||
      snapshot.global_ub != entry.value ||
      !std::isfinite(snapshot.global_ub) ||
      snapshot.global_ub < 0.0) {
    set_violation(
        result, "SNAPSHOT_PROVENANCE_MISMATCH",
        "snapshot schema, completeness, partition version, or stored "
        "global UB does not match its history record",
        ModelASnapshotVerificationStatusV1::FAIL);
    return result;
  }
  if (snapshot.identity.solver_identity.empty() ||
      snapshot.identity.config_identity.empty() ||
      snapshot.identity.numeric_backend.empty() ||
      snapshot.identity.verifier_state.empty()) {
    set_violation(
        result, "INCOMPLETE_SNAPSHOT_PROVENANCE",
        "snapshot solver/config/backend/verifier provenance is incomplete",
        ModelASnapshotVerificationStatusV1::FAIL);
    return result;
  }
  std::set<std::string> leaf_certificates;
  for (const auto& leaf : snapshot.leaves) {
    if (leaf.certificate_ids.empty() &&
        leaf.program_schema_version == 1U) {
      set_violation(
          result, "MISSING_LEAF_CERTIFICATE",
          "legacy V1 snapshot leaf has no UB certificate reference",
          ModelASnapshotVerificationStatusV1::FAIL,
          leaf.start, leaf.end);
      return result;
    }
    leaf_certificates.insert(
        leaf.certificate_ids.begin(),
        leaf.certificate_ids.end());
  }
  const std::set<std::string> record_certificates(
      entry.certificate_ids.begin(),
      entry.certificate_ids.end());
  if (leaf_certificates != record_certificates ||
      entry.provenance.empty()) {
    set_violation(
        result, "SNAPSHOT_CERTIFICATE_PROVENANCE_MISMATCH",
        "leaf certificate references do not match the selected UB "
        "history record",
        ModelASnapshotVerificationStatusV1::FAIL);
    return result;
  }
  result.provenance_matches = true;
  if (!validate_snapshot_structure(instance, snapshot, result)) {
    return result;
  }

  if (mode == ModelASnapshotVerificationModeV1::VALIDATED) {
    set_violation(
        result, "VALIDATED_BACKEND_UNAVAILABLE",
        "no validated arithmetic backend is available; structural and "
        "provenance checks passed, but this snapshot is NOT_CERTIFIED",
        ModelASnapshotVerificationStatusV1::NOT_CERTIFIED);
    result.arithmetic_note =
        "VALIDATED mode unavailable; no interval/root/integration "
        "certificate was issued";
    return result;
  }

  (void)run_numerical_checks(instance, entry, result);
  return result;
}

const char* model_a_snapshot_verification_status_name_v1(
    ModelASnapshotVerificationStatusV1 status) noexcept {
  switch (status) {
    case ModelASnapshotVerificationStatusV1::NUMERIC_PASS:
      return "NUMERIC_PASS";
    case ModelASnapshotVerificationStatusV1::VALIDATED_PASS:
      return "VALIDATED_PASS";
    case ModelASnapshotVerificationStatusV1::FAIL:
      return "FAIL";
    case ModelASnapshotVerificationStatusV1::INDETERMINATE:
      return "INDETERMINATE";
    case ModelASnapshotVerificationStatusV1::NOT_CERTIFIED:
      return "NOT_CERTIFIED";
  }
  return "INDETERMINATE";
}

}  // namespace kdc
