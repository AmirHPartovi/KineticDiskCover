#pragma once

#include <stdexcept>
#include <string>

namespace kdc {
enum class BoundStatus {
  NONE,
  HEURISTIC,
  CERTIFIED
};

enum class OptimalityStatus {
  OPTIMAL,
  FEASIBLE,
  TIME_LIMIT,
  INFEASIBLE,
  FAILED
};

enum class VerificationKind {
  NONE,
  EMPIRICAL,
  CERTIFIED_CONTINUOUS
};

inline std::string verification_kind_to_string(VerificationKind kind) {
  switch (kind) {
    case VerificationKind::NONE:
      return "none";
    case VerificationKind::EMPIRICAL:
      return "empirical";
    case VerificationKind::CERTIFIED_CONTINUOUS:
      return "certified_continuous";
  }
  throw std::invalid_argument("unknown verification kind");
}

inline VerificationKind verification_kind_from_string(
    const std::string& value) {
  if (value == "none") {
    return VerificationKind::NONE;
  }
  if (value == "empirical") {
    return VerificationKind::EMPIRICAL;
  }
  if (value == "certified_continuous") {
    return VerificationKind::CERTIFIED_CONTINUOUS;
  }
  throw std::invalid_argument("unknown verification kind: " + value);
}

inline std::string bound_status_to_string(BoundStatus status) {
  switch (status) {
    case BoundStatus::NONE:
      return "NONE";
    case BoundStatus::HEURISTIC:
      return "HEURISTIC";
    case BoundStatus::CERTIFIED:
      return "CERTIFIED";
  }
  throw std::invalid_argument("unknown bound status");
}

inline BoundStatus bound_status_from_string(const std::string& value) {
  if (value == "NONE") {
    return BoundStatus::NONE;
  }
  if (value == "HEURISTIC") {
    return BoundStatus::HEURISTIC;
  }
  if (value == "CERTIFIED") {
    return BoundStatus::CERTIFIED;
  }
  throw std::invalid_argument("unknown bound status: " + value);
}

inline std::string optimality_status_to_string(OptimalityStatus status) {
  switch (status) {
    case OptimalityStatus::OPTIMAL:
      return "OPTIMAL";
    case OptimalityStatus::FEASIBLE:
      return "FEASIBLE";
    case OptimalityStatus::TIME_LIMIT:
      return "TIME_LIMIT";
    case OptimalityStatus::INFEASIBLE:
      return "INFEASIBLE";
    case OptimalityStatus::FAILED:
      return "FAILED";
  }
  throw std::invalid_argument("unknown optimality status");
}

inline OptimalityStatus optimality_status_from_string(
    const std::string& value) {
  if (value == "OPTIMAL") {
    return OptimalityStatus::OPTIMAL;
  }
  if (value == "FEASIBLE") {
    return OptimalityStatus::FEASIBLE;
  }
  if (value == "TIME_LIMIT") {
    return OptimalityStatus::TIME_LIMIT;
  }
  if (value == "INFEASIBLE") {
    return OptimalityStatus::INFEASIBLE;
  }
  if (value == "FAILED") {
    return OptimalityStatus::FAILED;
  }
  throw std::invalid_argument("unknown optimality status: " + value);
}
}  // namespace kdc
