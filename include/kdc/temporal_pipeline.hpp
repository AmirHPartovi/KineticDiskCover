#pragma once

#include "kdc/temporal_thesis_solver.hpp"

#include <nlohmann/json.hpp>

namespace kdc {

nlohmann::json temporal_thesis_result_to_json(
    const TemporalThesisResultV2& result);
nlohmann::json temporal_thesis_failure_to_json(const std::string& reason);
bool temporal_thesis_result_failed(
    const TemporalThesisResultV2& result) noexcept;
bool temporal_thesis_result_timed_out(
    const TemporalThesisResultV2& result) noexcept;

}  // namespace kdc
