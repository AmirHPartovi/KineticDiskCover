#pragma once

#include <stdexcept>
#include <string>

namespace kdc {

enum class PipelineSolverMethod {
  LEGACY,
  MINSUM_ADAPTIVE,
  TEMPORAL_INTERVAL_BNB,
  B_GRID,
  B_NN,
  B_STATIC_HOLD,
  B_MINMAX
};

inline const char* pipeline_solver_method_name(
    PipelineSolverMethod method) noexcept {
  switch (method) {
    case PipelineSolverMethod::LEGACY:
      return "legacy";
    case PipelineSolverMethod::MINSUM_ADAPTIVE:
      return "minsum-adaptive";
    case PipelineSolverMethod::TEMPORAL_INTERVAL_BNB:
      return "temporal-interval-bnb";
    case PipelineSolverMethod::B_GRID:
      return "b-grid";
    case PipelineSolverMethod::B_NN:
      return "b-nn";
    case PipelineSolverMethod::B_STATIC_HOLD:
      return "b-static-hold";
    case PipelineSolverMethod::B_MINMAX:
      return "b-minmax";
  }
  return "unknown";
}

inline bool is_temporal_solver_method(
    PipelineSolverMethod method) noexcept {
  return method == PipelineSolverMethod::TEMPORAL_INTERVAL_BNB ||
         method == PipelineSolverMethod::B_GRID ||
         method == PipelineSolverMethod::B_NN ||
         method == PipelineSolverMethod::B_STATIC_HOLD ||
         method == PipelineSolverMethod::B_MINMAX;
}

inline bool is_temporal_bound_solver_method(
    PipelineSolverMethod method) noexcept {
  return method == PipelineSolverMethod::TEMPORAL_INTERVAL_BNB ||
         method == PipelineSolverMethod::B_GRID;
}

inline PipelineSolverMethod pipeline_solver_method_from_string(
    const std::string& value) {
  if (value == "legacy") {
    return PipelineSolverMethod::LEGACY;
  }
  if (value == "minsum-adaptive") {
    return PipelineSolverMethod::MINSUM_ADAPTIVE;
  }
  if (value == "temporal-interval-bnb") {
    return PipelineSolverMethod::TEMPORAL_INTERVAL_BNB;
  }
  if (value == "b-grid") {
    return PipelineSolverMethod::B_GRID;
  }
  if (value == "b-nn") {
    return PipelineSolverMethod::B_NN;
  }
  if (value == "b-static-hold") {
    return PipelineSolverMethod::B_STATIC_HOLD;
  }
  if (value == "b-minmax") {
    return PipelineSolverMethod::B_MINMAX;
  }
  throw std::invalid_argument("unknown solver method: " + value);
}

}  // namespace kdc
