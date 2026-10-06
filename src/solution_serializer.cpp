#include "kdc/solution_serializer.hpp"

#include "kdc/logging.hpp"
#include "kdc/objective.hpp"
#include "kdc/profiling.hpp"
#include "kdc/verify.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <stdexcept>
#include <string>
#include <utility>

namespace kdc {
namespace {
using Json = nlohmann::json;

void validate_instance_dimensions(const Instance& instance) {
  if (instance.n < 0 || instance.m < 0 ||
      static_cast<Index>(instance.n) != instance.trajectories.size() ||
      static_cast<Index>(instance.m) != instance.stations.size() ||
      !std::isfinite(instance.T_end) || instance.T_end <= 0.0) {
    throw std::runtime_error("instance dimensions or T_end are invalid");
  }
}

[[noreturn]] void invalid_solution(const std::string& message) {
  throw std::runtime_error(message);
}
}  // namespace

bool SolutionSerializer::validate(const Instance& instance,
                                  const KineticSolution& solution,
                                  std::string* out_error) {
  const auto fail = [out_error](const char* message) {
    if (out_error != nullptr) {
      *out_error = message;
    }
    return false;
  };
  if (instance.n < 0 || instance.m < 0 ||
      static_cast<Index>(instance.n) != instance.trajectories.size() ||
      static_cast<Index>(instance.m) != instance.stations.size()) {
    return fail("instance dimensions mismatch");
  }
  if (!solution.is_well_formed()) {
    return fail("solution is not well formed");
  }
  for (const auto& interval : solution.intervals) {
    if (interval.supporting_point.size() != static_cast<Index>(instance.m)) {
      return fail("supporting_point size mismatch");
    }
    if (interval.assigned_points.size() != static_cast<Index>(instance.n)) {
      return fail("assigned_points size mismatch");
    }
    for (const int point : interval.supporting_point) {
      if (point < -1 || point >= instance.n) {
        return fail("supporting_point out of range");
      }
    }
    for (const int station : interval.assigned_points) {
      if (station < 0 || station >= instance.m) {
        return fail("assigned_points out of range");
      }
    }
  }
  if (out_error != nullptr) {
    out_error->clear();
  }
  return true;
}

void SolutionSerializer::save_json(const Instance& instance,
                                   const KineticSolution& solution,
                                   const std::string& path) {
  KDC_PROFILE_PHASE(ProfilePhase::SERIALIZATION);
  LOG_DEBUG("SolutionSerializer::save_json path={} intervals={}", path,
            solution.intervals.size());
  validate_instance_dimensions(instance);
  if (!solution.is_well_formed()) {
    invalid_solution("solution is not well formed");
  }
  std::string error;
  if (!validate(instance, solution, &error)) {
    invalid_solution(error);
  }
  const double peak_cost = solution.peak_cost();
  const double peak_time = solution.peak_time();
  const bool peak_consistent =
      solution.objective != ObjectiveType::MIN_MAX ||
      Verifier::check_peak_consistency(solution, peak_cost, peak_time);
  if (!peak_consistent) {
    invalid_solution("MinMax peak is inconsistent with the solution");
  }

  Json intervals = Json::array();
  for (const auto& interval : solution.intervals) {
    intervals.push_back({{"t_start", interval.t_start},
                         {"t_end", interval.t_end},
                         {"supporting_point", interval.supporting_point},
                         {"assigned_points", interval.assigned_points},
                         {"a", interval.a},
                         {"b", interval.b},
                         {"c", interval.c}});
  }
  const Json document{
      {"instance",
       {{"name", instance.name},
        {"n", instance.n},
        {"m", instance.m},
        {"T_end", instance.T_end}}},
      {"objective", to_string(solution.objective)},
      {"summary",
       {{"peak_cost", peak_cost},
        {"peak_time", peak_time},
        {"peak_consistent", peak_consistent},
        {"total_integral", solution.total_integral()},
        {"num_intervals", solution.intervals.size()}}},
      {"intervals", std::move(intervals)}};
  const std::string contents = document.dump(4) + "\n";
  std::ofstream output(path);
  if (!output) {
    throw std::runtime_error("cannot open solution output: " + path);
  }
  output << contents;
  if (!output) {
    throw std::runtime_error("failed writing solution output: " + path);
  }
  LOG_INFO("SolutionSerializer: wrote {} bytes to {}", contents.size(), path);
}

KineticSolution SolutionSerializer::load_json(const Instance& instance,
                                              const std::string& path) {
  LOG_DEBUG("SolutionSerializer::load_json path={}", path);
  validate_instance_dimensions(instance);
  std::ifstream input(path);
  if (!input) {
    throw std::runtime_error("cannot open solution file: " + path);
  }

  Json document;
  try {
    input >> document;
  } catch (const Json::exception& error) {
    throw std::runtime_error(std::string("invalid solution JSON: ") +
                             error.what());
  }
  try {
    const Json& metadata = document.at("instance");
    if (metadata.at("n").get<int>() != instance.n) {
      throw std::runtime_error("solution instance n does not match");
    }
    if (metadata.at("m").get<int>() != instance.m) {
      throw std::runtime_error("solution instance m does not match");
    }
    const double stored_end = metadata.at("T_end").get<double>();
    if (!std::isfinite(stored_end) ||
        std::abs(stored_end - instance.T_end) >= 1e-9) {
      throw std::runtime_error("solution instance T_end does not match");
    }

    KineticSolution solution;
    solution.objective =
        objective_from_string(document.at("objective").get<std::string>());
    for (const auto& value : document.at("intervals")) {
      SolutionInterval interval;
      interval.t_start = value.at("t_start").get<double>();
      interval.t_end = value.at("t_end").get<double>();
      interval.supporting_point =
          value.at("supporting_point").get<std::vector<int>>();
      interval.assigned_points =
          value.at("assigned_points").get<std::vector<int>>();
      interval.a = value.at("a").get<double>();
      interval.b = value.at("b").get<double>();
      interval.c = value.at("c").get<double>();
      solution.intervals.push_back(std::move(interval));
    }

    for (Index index = 1; index < solution.intervals.size(); ++index) {
      auto& previous = solution.intervals[index - 1U];
      auto& current = solution.intervals[index];
      if (current.t_start < previous.t_start) {
        invalid_solution("solution intervals are not sorted");
      }
      if (std::abs(previous.t_end - current.t_start) > 1e-9) {
        invalid_solution("solution intervals are not contiguous");
      }
      current.t_start = previous.t_end;
    }

    std::string validation_error;
    if (!validate(instance, solution, &validation_error)) {
      invalid_solution(validation_error);
    }
    const Json& summary = document.at("summary");
    const double stored_peak = summary.at("peak_cost").get<double>();
    const double stored_peak_time = summary.at("peak_time").get<double>();
    const double stored_integral = summary.at("total_integral").get<double>();
    const Index stored_intervals = summary.at("num_intervals").get<Index>();
    const auto summary_matches = [](double stored, double computed) {
      return std::isfinite(stored) && std::isfinite(computed) &&
             std::abs(stored - computed) <
                 1e-6 * std::max(1.0, std::abs(computed));
    };
    const bool peak_consistent =
        solution.objective != ObjectiveType::MIN_MAX ||
        Verifier::check_peak_consistency(solution, stored_peak,
                                         stored_peak_time);
    if (stored_intervals != solution.intervals.size() ||
        (summary.contains("peak_consistent") &&
         !summary.at("peak_consistent").get<bool>()) ||
        !peak_consistent ||
        !summary_matches(stored_peak, solution.peak_cost()) ||
        !summary_matches(stored_peak_time, solution.peak_time()) ||
        !summary_matches(stored_integral, solution.total_integral())) {
      invalid_solution("summary mismatch");
    }
    LOG_INFO("SolutionSerializer: loaded {} intervals from {}",
             solution.intervals.size(), path);
    return solution;
  } catch (const Json::exception& error) {
    throw std::runtime_error(std::string("invalid solution structure: ") +
                             error.what());
  }
}
}  // namespace kdc
