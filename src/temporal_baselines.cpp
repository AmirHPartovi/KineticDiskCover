#include "kdc/temporal_baselines.hpp"

#include "kdc/weighted_geometry.hpp"
#include "kdc/weighted_static.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <utility>

namespace kdc {
namespace {
using Json = nlohmann::json;

Json finite_json(double value) {
  return std::isfinite(value) ? Json(value) : Json(nullptr);
}

Json optional_finite_json(const std::optional<double>& value) {
  return value.has_value() ? finite_json(*value) : Json(nullptr);
}

std::string exact_termination_name(
    WeightedOracleTerminationStatusV1 status) {
  switch (status) {
    case WeightedOracleTerminationStatusV1::NOT_IMPLEMENTED:
      return "NOT_IMPLEMENTED";
    case WeightedOracleTerminationStatusV1::OPTIMAL:
      return "OPTIMAL";
    case WeightedOracleTerminationStatusV1::FEASIBLE:
      return "FEASIBLE";
    case WeightedOracleTerminationStatusV1::TIME_LIMIT:
      return "TIME_LIMIT";
    case WeightedOracleTerminationStatusV1::INTERRUPTED:
      return "INTERRUPTED";
    case WeightedOracleTerminationStatusV1::INFEASIBLE:
      return "INFEASIBLE";
    case WeightedOracleTerminationStatusV1::UNBOUNDED:
      return "UNBOUNDED";
    case WeightedOracleTerminationStatusV1::NUMERIC_FAILURE:
      return "NUMERIC_FAILURE";
    case WeightedOracleTerminationStatusV1::ERROR:
      return "ERROR";
  }
  return "ERROR";
}

std::vector<int> station_ids(const Instance& instance) {
  std::vector<int> ids;
  ids.reserve(instance.stations.size());
  for (const auto& station : instance.stations) {
    ids.push_back(station.id);
  }
  return ids;
}

bool valid_assignment(
    const Instance& instance,
    const std::vector<int>& assignment) {
  if (assignment.size() != instance.trajectories.size()) {
    return false;
  }
  return std::all_of(
      assignment.begin(), assignment.end(),
      [&instance](int station) {
        return station >= 0 && station < instance.m;
      });
}

void validate_instance(const Instance& instance) {
  const WeightedGeometry geometry(instance);
  (void)geometry;
  if (instance.n > 0 && instance.m == 0) {
    throw std::invalid_argument(
        "temporal baseline cannot cover points without stations");
  }
  std::set<int> ids;
  for (const auto& station : instance.stations) {
    if (!ids.insert(station.id).second) {
      throw std::invalid_argument(
          "temporal baseline requires unique station IDs for tie-breaking");
    }
  }
}

TemporalBaselineStationProgramV1 make_station_program(
    const WeightedGeometry& geometry,
    int station,
    double start,
    double end,
    const std::vector<int>& assignment,
    const std::optional<Weight>& fixed_radius_squared) {
  TemporalBaselineStationProgramV1 result;
  result.station_index = station;
  result.fixed_radius_squared = fixed_radius_squared;
  if (fixed_radius_squared.has_value()) {
    return result;
  }
  for (WeightIndex point = 0U;
       point < assignment.size();
       ++point) {
    if (assignment[point] != station) {
      continue;
    }
    const auto coefficients =
        geometry.coefficients(point, static_cast<WeightIndex>(station),
                              start, end);
    result.envelope_terms.push_back(
        {static_cast<int>(point), coefficients.a,
         coefficients.b, coefficients.c});
  }
  return result;
}

TemporalBaselineProgramPieceV1 make_program_piece(
    const Instance& instance,
    const WeightedGeometry& geometry,
    double start,
    double end,
    const std::vector<int>& assignment,
    const std::optional<std::vector<Weight>>& fixed_radii = std::nullopt) {
  TemporalBaselineProgramPieceV1 piece;
  piece.start = start;
  piece.end = end;
  piece.assignment = assignment;
  piece.stations.reserve(instance.stations.size());
  for (int station = 0; station < instance.m; ++station) {
    const std::optional<Weight> fixed =
        fixed_radii.has_value()
            ? std::optional<Weight>(
                  fixed_radii->at(static_cast<WeightIndex>(station)))
            : std::nullopt;
    piece.stations.push_back(
        make_station_program(geometry, station,
                             start, end, assignment, fixed));
  }
  return piece;
}

TemporalBaselineBoundaryV1 make_boundary(
    const WeightedGeometry& geometry,
    double time,
    const std::vector<int>& assignment) {
  TemporalBaselineBoundaryV1 boundary;
  boundary.time = time;
  boundary.assignment = assignment;
  boundary.station_radius_squared.assign(
      geometry.station_ids().size(), 0.0);
  const auto distances = geometry.d_at(time).matrix;
  for (WeightIndex point = 0U;
       point < assignment.size();
       ++point) {
    const auto station =
        static_cast<WeightIndex>(assignment[point]);
    boundary.station_radius_squared[station] =
        std::max(boundary.station_radius_squared[station],
                 distances.weight(station, point));
  }
  return boundary;
}

bool program_valid(
    const Instance& instance,
    const TemporalBaselineProgramV1& program) {
  if (program.pieces.empty() ||
      program.boundaries.size() != program.pieces.size() + 1U ||
      program.pieces.front().start != 0.0 ||
      program.pieces.back().end != instance.T_end) {
    return false;
  }
  for (WeightIndex index = 0U;
       index < program.pieces.size();
       ++index) {
    const auto& piece = program.pieces[index];
    if (!std::isfinite(piece.start) ||
        !std::isfinite(piece.end) ||
        piece.end <= piece.start ||
        (index > 0U &&
         piece.start != program.pieces[index - 1U].end) ||
        !valid_assignment(instance, piece.assignment) ||
        piece.stations.size() != instance.stations.size()) {
      return false;
    }
    for (WeightIndex station = 0U;
         station < piece.stations.size();
         ++station) {
      const auto& station_program = piece.stations[station];
      if (station_program.station_index !=
          static_cast<int>(station)) {
        return false;
      }
      if (station_program.fixed_radius_squared.has_value()) {
        if (!station_program.envelope_terms.empty() ||
            !std::isfinite(*station_program.fixed_radius_squared) ||
            *station_program.fixed_radius_squared < 0.0) {
          return false;
        }
      } else {
        for (const auto& term : station_program.envelope_terms) {
          if (term.point_index < 0 ||
              static_cast<WeightIndex>(term.point_index) >=
                  piece.assignment.size() ||
              piece.assignment[static_cast<WeightIndex>(
                  term.point_index)] !=
                  static_cast<int>(station) ||
              !std::isfinite(term.a) ||
              !std::isfinite(term.b) ||
              !std::isfinite(term.c)) {
            return false;
          }
        }
      }
    }
  }
  for (WeightIndex index = 0U;
       index < program.boundaries.size();
       ++index) {
    const auto& boundary = program.boundaries[index];
    const double expected =
        index == 0U
            ? program.pieces.front().start
            : program.pieces[index - 1U].end;
    if (boundary.time != expected ||
        !valid_assignment(instance, boundary.assignment) ||
        boundary.station_radius_squared.size() !=
            instance.stations.size() ||
        !std::all_of(
            boundary.station_radius_squared.begin(),
            boundary.station_radius_squared.end(),
            [](Weight radius_squared) {
              return std::isfinite(radius_squared) &&
                     radius_squared >= 0.0;
            })) {
      return false;
    }
  }
  return true;
}

void append_envelope_cost(
    const Instance& instance,
    const std::vector<int>& assignment,
    double start,
    double end,
    SolverBudget& budget,
    Weight& total) {
  budget.checkpoint();
  const WeightedUpperEnvelope envelope(instance);
  const auto integrated = envelope.solve(
      start, end, assignment, UpperEnvelopeModeV1::FAST_DOUBLE);
  if (!std::isfinite(integrated.upper_bound) ||
      integrated.upper_bound < 0.0 ||
      integrated.status == UpperEnvelopeStatusV1::INVALID ||
      integrated.status == UpperEnvelopeStatusV1::INFEASIBLE) {
    throw std::runtime_error(
        integrated.failure_reason.empty()
            ? "upper-envelope integration failed"
            : integrated.failure_reason);
  }
  total += integrated.upper_bound;
  if (!std::isfinite(total)) {
    throw std::overflow_error(
        "temporal baseline cost is not finite");
  }
  budget.checkpoint();
}

std::vector<double> common_cuts(const WeightedGeometry& geometry) {
  return geometry.common_partition();
}

std::vector<int> nearest_assignment(
    const WeightedGeometry& geometry,
    const Instance& instance,
    double time,
    const std::map<int,
        std::vector<std::pair<WeightIndex, WeightIndex>>>* tied_pairs =
            nullptr) {
  const WeightMatrix distances = geometry.d_at(time).matrix;
  std::vector<int> assignment(
      static_cast<WeightIndex>(instance.n), -1);
  std::vector<Weight> values(
      static_cast<WeightIndex>(instance.m));
  for (int point = 0; point < instance.n; ++point) {
    for (int station = 0; station < instance.m; ++station) {
      values[static_cast<WeightIndex>(station)] =
          distances.weight(static_cast<WeightIndex>(station),
                           static_cast<WeightIndex>(point));
    }
    if (tied_pairs != nullptr) {
      const auto point_ties = tied_pairs->find(point);
      if (point_ties != tied_pairs->end()) {
        for (const auto& pair : point_ties->second) {
          const Weight common =
              std::min(values[pair.first], values[pair.second]);
          values[pair.first] = common;
          values[pair.second] = common;
        }
      }
    }
    int selected = -1;
    for (int station = 0; station < instance.m; ++station) {
      if (selected < 0 ||
          values[static_cast<WeightIndex>(station)] <
              values[static_cast<WeightIndex>(selected)] ||
          (values[static_cast<WeightIndex>(station)] ==
               values[static_cast<WeightIndex>(selected)] &&
           instance.stations[static_cast<WeightIndex>(station)].id <
               instance.stations[static_cast<WeightIndex>(selected)].id)) {
        selected = station;
      }
    }
    assignment[static_cast<WeightIndex>(point)] = selected;
  }
  return assignment;
}

void finish_program(
    const Instance& instance,
    TemporalBaselineProgramV1& program) {
  program.complete = program_valid(instance, program);
}

void validate_config(const TemporalBaselineConfigV1& config) {
  if (!std::isfinite(config.global_time_limit_seconds) ||
      config.global_time_limit_seconds <= 0.0 ||
      !std::isfinite(config.oracle_time_limit_seconds) ||
      config.oracle_time_limit_seconds <= 0.0 ||
      config.max_oracle_calls == 0U) {
    throw std::invalid_argument(
        "temporal baseline budgets must be positive");
  }
}

std::vector<Weight> fixed_radii_from_dplus(
    const WeightMatrix& dplus,
    const std::vector<int>& assignment) {
  std::vector<Weight> radii(dplus.station_count(), 0.0);
  for (WeightIndex point = 0U;
       point < assignment.size();
       ++point) {
    const auto station =
        static_cast<WeightIndex>(assignment[point]);
    radii[station] =
        std::max(radii[station], dplus.weight(station, point));
  }
  return radii;
}

}  // namespace

WeightedMinMaxOracleResultV1 ColumnMinimumWeightedMinMaxOracle::solve(
    const WeightMatrix& matrix,
    const std::vector<int>& ids,
    SolverBudget* budget) {
  if (ids.size() != matrix.station_count()) {
    throw std::invalid_argument(
        "MinMax oracle station ID count does not match matrix rows");
  }
  if (matrix.point_count() > 0U &&
      matrix.station_count() == 0U) {
    throw std::invalid_argument(
        "MinMax subproblem has points but no stations");
  }
  WeightedMinMaxOracleResultV1 result;
  std::vector<int> assignment(matrix.point_count(), -1);
  Weight objective = 0.0;
  for (WeightIndex point = 0U;
       point < matrix.point_count();
       ++point) {
    if (budget != nullptr) {
      budget->checkpoint();
    }
    Weight best = std::numeric_limits<Weight>::infinity();
    int selected = -1;
    for (WeightIndex station = 0U;
         station < matrix.station_count();
         ++station) {
      const Weight value = matrix.weight(station, point);
      if (value < best ||
          (value == best &&
           ids[station] < ids[static_cast<WeightIndex>(selected)])) {
        best = value;
        selected = static_cast<int>(station);
      }
    }
    if (selected < 0 || !std::isfinite(best)) {
      result.termination = WeightedMinMaxTerminationV1::ERROR;
      result.method = name();
      return result;
    }
    assignment[point] = selected;
    objective = std::max(objective, best);
  }
  if (budget != nullptr) {
    budget->checkpoint();
  }
  result.assignment = std::move(assignment);
  result.incumbent_feasible = true;
  result.objective = objective;
  result.optimality_proved = true;
  result.termination = WeightedMinMaxTerminationV1::OPTIMAL;
  result.method = name();
  return result;
}

TemporalBaselineSolver::TemporalBaselineSolver(
    const Instance& instance)
    : instance_(instance) {
  validate_instance(instance_);
}

TemporalBaselineResultV1 TemporalBaselineSolver::solve_nn(
    const TemporalBaselineConfigV1& config) const {
  validate_config(config);
  TemporalBaselineResultV1 result;
  result.baseline = TemporalBaselineKindV1::B_NN;
  result.engineering_maturity = "experimental_numerical_candidate";
  result.program.boundary_ownership =
      "explicit_boundary_assignment_by_nearest_station_and_station_id";
  SolverBudget budget(config.global_time_limit_seconds);
  const WeightedGeometry geometry(instance_);
  const auto base_cuts = common_cuts(geometry);
  if (base_cuts.size() < 2U) {
    result.failure_reason = "common trajectory partition is incomplete";
    return result;
  }

  std::vector<double> cuts{base_cuts.front()};
  std::map<double, std::map<int,
      std::vector<std::pair<WeightIndex, WeightIndex>>>> tied_pairs;
  try {
    for (WeightIndex segment = 0U;
         segment + 1U < base_cuts.size();
         ++segment) {
      const double segment_start = base_cuts[segment];
      const double segment_end = base_cuts[segment + 1U];
      std::map<double, std::map<int,
          std::vector<std::pair<WeightIndex, WeightIndex>>>> local_roots;
      std::map<double, long double> unrounded_roots;
      for (int point = 0; point < instance_.n; ++point) {
        const auto& trajectory =
            instance_.trajectories[static_cast<WeightIndex>(point)];
        const double midpoint =
            segment_start + (segment_end - segment_start) / 2.0;
        const int trajectory_segment = trajectory.segment_index(midpoint);
        const Point position =
            trajectory.waypoints[
                static_cast<WeightIndex>(trajectory_segment)];
        const double trajectory_start =
            trajectory.t_breaks[
                static_cast<WeightIndex>(trajectory_segment)];
        const double duration =
            trajectory.t_breaks[
                static_cast<WeightIndex>(trajectory_segment + 1)] -
            trajectory_start;
        const Point velocity =
            (trajectory.waypoints[
                 static_cast<WeightIndex>(trajectory_segment + 1)] -
             position) *
            (1.0 / duration);
        for (WeightIndex first = 0U;
             first < instance_.stations.size();
             ++first) {
          for (WeightIndex second = first + 1U;
               second < instance_.stations.size();
               ++second) {
            budget.checkpoint();
            const Point first_center =
                instance_.stations[first].pos;
            const Point second_center =
                instance_.stations[second].pos;
            const long double dx =
                static_cast<long double>(second_center.x) -
                static_cast<long double>(first_center.x);
            const long double dy =
                static_cast<long double>(second_center.y) -
                static_cast<long double>(first_center.y);
            const long double slope =
                2.0L *
                (static_cast<long double>(velocity.x) * dx +
                 static_cast<long double>(velocity.y) * dy);
            if (slope == 0.0L) {
              continue;
            }
            const long double intercept =
                2.0L *
                    (static_cast<long double>(position.x) * dx +
                     static_cast<long double>(position.y) * dy) +
                static_cast<long double>(first_center.x) *
                    static_cast<long double>(first_center.x) +
                static_cast<long double>(first_center.y) *
                    static_cast<long double>(first_center.y) -
                static_cast<long double>(second_center.x) *
                    static_cast<long double>(second_center.x) -
                static_cast<long double>(second_center.y) *
                    static_cast<long double>(second_center.y) -
                slope * static_cast<long double>(trajectory_start);
            const long double root = -intercept / slope;
            if (!std::isfinite(root) ||
                root <= static_cast<long double>(segment_start) ||
                root >= static_cast<long double>(segment_end)) {
              continue;
            }
            const double root_double = static_cast<double>(root);
            if (!std::isfinite(root_double) ||
                root_double <= segment_start ||
                root_double >= segment_end) {
              result.status =
                  TemporalBaselineStatusV1::INDETERMINATE;
              result.failure_reason =
                  "nearest-station switch root is not representable";
              return result;
            }
            const auto prior_root = unrounded_roots.find(root_double);
            if (prior_root != unrounded_roots.end() &&
                prior_root->second != root) {
              result.status =
                  TemporalBaselineStatusV1::INDETERMINATE;
              result.failure_reason =
                  "distinct station-switch roots collapse to one time";
              return result;
            }
            unrounded_roots.emplace(root_double, root);
            local_roots[root_double][point].emplace_back(first, second);
          }
        }
      }
      for (const auto& root_entry : local_roots) {
        cuts.push_back(root_entry.first);
        tied_pairs[root_entry.first] = root_entry.second;
      }
      cuts.push_back(segment_end);
    }
    std::sort(cuts.begin(), cuts.end());
    cuts.erase(std::unique(cuts.begin(), cuts.end()), cuts.end());
    Weight total_cost = 0.0;
    for (WeightIndex index = 0U; index + 1U < cuts.size(); ++index) {
      budget.checkpoint();
      const double start = cuts[index];
      const double end = cuts[index + 1U];
      if (end <= start) {
        result.status = TemporalBaselineStatusV1::INDETERMINATE;
        result.failure_reason =
            "nearest-station event ordering collapsed";
        return result;
      }
      const double midpoint = start + (end - start) / 2.0;
      if (midpoint <= start || midpoint >= end) {
        result.status = TemporalBaselineStatusV1::INDETERMINATE;
        result.failure_reason =
            "nearest-station interval midpoint is not representable";
        return result;
      }
      const auto assignment =
          nearest_assignment(geometry, instance_, midpoint);
      if (!valid_assignment(instance_, assignment)) {
        throw std::runtime_error(
            "nearest-station assignment is incomplete");
      }
      if (!result.program.pieces.empty()) {
        for (WeightIndex point = 0U;
             point < assignment.size();
             ++point) {
          if (assignment[point] !=
              result.program.pieces.back().assignment[point]) {
            ++result.assignment_switch_count;
          }
        }
      }
      result.program.pieces.push_back(
          make_program_piece(instance_, geometry, start, end,
                             assignment));
      append_envelope_cost(instance_, assignment, start, end,
                           budget, total_cost);
    }
    result.cost = total_cost;
    result.program.boundaries.reserve(cuts.size());
    for (const double time : cuts) {
      const std::map<int,
          std::vector<std::pair<WeightIndex, WeightIndex>>>* ties = nullptr;
      const auto root = tied_pairs.find(time);
      if (root != tied_pairs.end()) {
        ties = &root->second;
      }
      const auto assignment =
          nearest_assignment(geometry, instance_, time, ties);
      result.program.boundaries.push_back(
          make_boundary(geometry, time, assignment));
    }
    finish_program(instance_, result.program);
    if (!result.program.complete) {
      result.status = TemporalBaselineStatusV1::FAILED;
      result.failure_reason =
          "nearest-station program failed structural validation";
      result.cost.reset();
      return result;
    }
    result.program_complete = true;
    result.program_feasibility = "ENVELOPE_BY_CONSTRUCTION";
    result.status = TemporalBaselineStatusV1::COMPLETE;
    result.cost_role = "MIN_SUM_FEASIBLE_PROGRAM_COST";
    result.cost_validity = "FAST_FLOAT_ONLY";
    result.minsum_ub_valid = false;
    return result;
  } catch (const SolverBudgetExpired&) {
    result.status = TemporalBaselineStatusV1::TIME_LIMIT;
    result.failure_reason = "global time budget expired";
  } catch (const std::exception& error) {
    result.status = TemporalBaselineStatusV1::FAILED;
    result.failure_reason = error.what();
  }
  result.cost.reset();
  result.program.complete = false;
  return result;
}

TemporalBaselineResultV1 TemporalBaselineSolver::solve_static_hold(
    IWeightedExactOracle* exact_oracle,
    IWeightedHeuristicOracle* heuristic_oracle,
    const TemporalBaselineConfigV1& config) const {
  validate_config(config);
  TemporalBaselineResultV1 result;
  result.baseline = TemporalBaselineKindV1::B_STATIC_HOLD;
  result.program.boundary_ownership = "LOWER_INTERVAL";
  SolverBudget global_budget(config.global_time_limit_seconds);
  const WeightedGeometry geometry(instance_);
  WeightedHeuristicOracleResultV1 heuristic_result;
  std::vector<int> assignment;
  bool optimality_proved = false;

  try {
    const auto at_zero = geometry.d_at(0.0);
    if (exact_oracle != nullptr) {
      if (result.oracle_call_count >= config.max_oracle_calls) {
        result.status = TemporalBaselineStatusV1::ORACLE_CAP;
        result.failure_reason =
            "maximum Oracle call count reached";
        return result;
      }
      global_budget.checkpoint();
      SolverBudget oracle_budget(std::min(
          config.oracle_time_limit_seconds,
          global_budget.remaining_seconds()));
      ++result.oracle_call_count;
      const auto exact = exact_oracle->solve(
          at_zero.matrix, &oracle_budget);
      oracle_budget.checkpoint();
      global_budget.checkpoint();
      result.static_at_zero.termination =
          exact_termination_name(exact.termination);
      result.static_at_zero.oracle = exact.provenance.backend_name;
      if (exact.incumbent_feasible && exact.assignment.has_value() &&
          valid_assignment(instance_, *exact.assignment)) {
        assignment = *exact.assignment;
        optimality_proved = exact.optimality_certified;
      }
    }
    if (assignment.empty() && instance_.n > 0) {
      if (heuristic_oracle == nullptr) {
        auto fallback = WeightedOracleFactory::greedy_heuristic();
        heuristic_oracle = fallback.get();
        if (result.oracle_call_count >= config.max_oracle_calls) {
          result.status = TemporalBaselineStatusV1::ORACLE_CAP;
          result.failure_reason =
              "maximum Oracle call count reached";
          return result;
        }
        global_budget.checkpoint();
        SolverBudget oracle_budget(std::min(
            config.oracle_time_limit_seconds,
            global_budget.remaining_seconds()));
        ++result.oracle_call_count;
        heuristic_result =
            heuristic_oracle->solve(at_zero.matrix, &oracle_budget);
        oracle_budget.checkpoint();
        global_budget.checkpoint();
      } else {
        if (result.oracle_call_count >= config.max_oracle_calls) {
          result.status = TemporalBaselineStatusV1::ORACLE_CAP;
          result.failure_reason =
              "maximum Oracle call count reached";
          return result;
        }
        global_budget.checkpoint();
        SolverBudget oracle_budget(std::min(
            config.oracle_time_limit_seconds,
            global_budget.remaining_seconds()));
        ++result.oracle_call_count;
        heuristic_result =
            heuristic_oracle->solve(at_zero.matrix, &oracle_budget);
        oracle_budget.checkpoint();
        global_budget.checkpoint();
      }
      if (!heuristic_result.incumbent_feasible ||
          !heuristic_result.assignment.has_value() ||
          !valid_assignment(instance_, *heuristic_result.assignment)) {
        result.status = TemporalBaselineStatusV1::FAILED;
        result.failure_reason =
            "no feasible assignment at t=0";
        return result;
      }
      assignment = *heuristic_result.assignment;
      result.static_at_zero.termination =
          "HEURISTIC_FEASIBLE";
      result.static_at_zero.oracle =
          heuristic_result.provenance.backend_name;
    }
    if (!valid_assignment(instance_, assignment)) {
      result.status = TemporalBaselineStatusV1::FAILED;
      result.failure_reason =
          "static-at-zero assignment is incomplete";
      return result;
    }
    result.static_at_zero.assignment = assignment;
    result.static_at_zero.optimality_proved = optimality_proved;
    result.static_at_zero.objective_at_zero =
        WeightedStaticObjective::evaluate_assignment(
            at_zero.matrix, assignment);

    Weight total_cost = 0.0;
    const auto cuts = common_cuts(geometry);
    for (WeightIndex index = 0U; index + 1U < cuts.size(); ++index) {
      global_budget.checkpoint();
      const double start = cuts[index];
      const double end = cuts[index + 1U];
      result.program.pieces.push_back(
          make_program_piece(instance_, geometry, start, end,
                             assignment));
      append_envelope_cost(instance_, assignment, start, end,
                           global_budget, total_cost);
    }
    for (const double time : cuts) {
      result.program.boundaries.push_back(
          make_boundary(geometry, time, assignment));
    }
    finish_program(instance_, result.program);
    if (!result.program.complete || !std::isfinite(total_cost)) {
      result.status = TemporalBaselineStatusV1::FAILED;
      result.failure_reason =
          "static-hold program failed validation or cost integration";
      return result;
    }
    result.program_complete = true;
    result.program_feasibility = "ENVELOPE_BY_CONSTRUCTION";
    result.cost = total_cost;
    result.cost_role = "MIN_SUM_FEASIBLE_PROGRAM_COST";
    result.cost_validity = "FAST_FLOAT_ONLY";
    result.minsum_ub_valid = false;
    result.status = optimality_proved
                        ? TemporalBaselineStatusV1::OPTIMAL_AT_ZERO
                        : TemporalBaselineStatusV1::CANDIDATE;
  } catch (const SolverBudgetExpired&) {
    result.status = TemporalBaselineStatusV1::TIME_LIMIT;
    result.failure_reason = "global or per-Oracle time budget expired";
  } catch (const std::exception& error) {
    result.status = TemporalBaselineStatusV1::FAILED;
    result.failure_reason = error.what();
  }
  if (result.status != TemporalBaselineStatusV1::CANDIDATE &&
      result.status != TemporalBaselineStatusV1::OPTIMAL_AT_ZERO) {
    result.cost.reset();
    result.program.complete = false;
    result.program_complete = false;
  }
  return result;
}

TemporalBaselineResultV1 TemporalBaselineSolver::solve_minmax(
    IWeightedMinMaxOracle& minmax_oracle,
    const TemporalBaselineConfigV1& config) const {
  validate_config(config);
  TemporalBaselineResultV1 result;
  result.baseline = TemporalBaselineKindV1::B_MINMAX;
  result.program.boundary_ownership = "LOWER_INTERVAL";
  SolverBudget global_budget(config.global_time_limit_seconds);
  const WeightedGeometry geometry(instance_);
  const auto cuts = common_cuts(geometry);
  Weight total_cost = 0.0;
  bool all_optimal = true;
  try {
    for (WeightIndex index = 0U; index + 1U < cuts.size(); ++index) {
      global_budget.checkpoint();
      if (result.oracle_call_count >= config.max_oracle_calls) {
        result.status = TemporalBaselineStatusV1::ORACLE_CAP;
        result.failure_reason =
            "maximum Oracle call count reached";
        return result;
      }
      const double start = cuts[index];
      const double end = cuts[index + 1U];
      const auto bounds = geometry.bounds_on(start, end);
      SolverBudget oracle_budget(std::min(
          config.oracle_time_limit_seconds,
          global_budget.remaining_seconds()));
      ++result.oracle_call_count;
      const auto subproblem = minmax_oracle.solve(
          bounds.upper.matrix, geometry.station_ids(), &oracle_budget);
      oracle_budget.checkpoint();
      global_budget.checkpoint();

      TemporalBaselineMinMaxSubproblemV1 record;
      record.start = start;
      record.end = end;
      record.termination =
          weighted_minmax_termination_name(subproblem.termination);
      record.optimality_proved = subproblem.optimality_proved &&
          subproblem.termination == WeightedMinMaxTerminationV1::OPTIMAL;
      record.minmax_objective = subproblem.objective;
      record.oracle = minmax_oracle.name();
      if (!subproblem.incumbent_feasible ||
          !subproblem.assignment.has_value() ||
          !valid_assignment(instance_, *subproblem.assignment)) {
        record.assignment.clear();
        result.minmax_subproblems.push_back(std::move(record));
        result.status =
            subproblem.termination ==
                    WeightedMinMaxTerminationV1::TIME_LIMIT
                ? TemporalBaselineStatusV1::TIME_LIMIT
                : TemporalBaselineStatusV1::FAILED;
        result.failure_reason =
            "MinMax subproblem did not return a feasible assignment";
        return result;
      }

      const auto assignment = *subproblem.assignment;
      const auto radii =
          fixed_radii_from_dplus(bounds.upper.matrix, assignment);
      const double duration = end - start;
      Weight station_sum = 0.0;
      for (const Weight radius_squared : radii) {
        station_sum += radius_squared;
      }
      const Weight local_cost =
          std::acos(-1.0) * duration * station_sum;
      if (!std::isfinite(local_cost) || local_cost < 0.0) {
        result.status = TemporalBaselineStatusV1::FAILED;
        result.failure_reason =
            "fixed-radius MinMax program cost is invalid";
        return result;
      }
      record.minsum_program_cost = local_cost;
      record.assignment = assignment;
      result.minmax_subproblems.push_back(record);
      result.program.pieces.push_back(
          make_program_piece(instance_, geometry, start, end,
                             assignment, radii));
      total_cost += local_cost;
      all_optimal = all_optimal && record.optimality_proved;
    }

    for (WeightIndex index = 0U; index <= result.program.pieces.size(); ++index) {
      const double time =
          index == result.program.pieces.size()
              ? instance_.T_end
              : result.program.pieces[index].start;
      const WeightIndex owner =
          index == 0U ? 0U : index - 1U;
      const auto& piece = result.program.pieces[owner];
      TemporalBaselineBoundaryV1 boundary;
      boundary.time = time;
      boundary.assignment = piece.assignment;
      boundary.station_radius_squared.reserve(piece.stations.size());
      for (const auto& station : piece.stations) {
        boundary.station_radius_squared.push_back(
            station.fixed_radius_squared.value_or(0.0));
      }
      result.program.boundaries.push_back(std::move(boundary));
    }
    finish_program(instance_, result.program);
    if (!result.program.complete || !std::isfinite(total_cost)) {
      result.status = TemporalBaselineStatusV1::FAILED;
      result.failure_reason =
          "fixed-radius MinMax program failed validation";
      return result;
    }
    result.program_complete = true;
    result.program_feasibility = "DPLUS_FAST_FLOAT_ONLY";
    result.cost = total_cost;
    result.cost_role = "MIN_SUM_COST_OF_MINMAX_CONSTRUCTED_PROGRAM";
    result.cost_validity = "FAST_FLOAT_ONLY";
    result.minsum_ub_valid = false;
    result.status = all_optimal
                        ? TemporalBaselineStatusV1::COMPLETE
                        : TemporalBaselineStatusV1::CANDIDATE;
  } catch (const SolverBudgetExpired&) {
    result.status = TemporalBaselineStatusV1::TIME_LIMIT;
    result.failure_reason = "global or per-Oracle time budget expired";
  } catch (const std::exception& error) {
    result.status = TemporalBaselineStatusV1::FAILED;
    result.failure_reason = error.what();
  }
  if (result.status != TemporalBaselineStatusV1::COMPLETE &&
      result.status != TemporalBaselineStatusV1::CANDIDATE) {
    result.cost.reset();
    result.program.complete = false;
    result.program_complete = false;
  }
  return result;
}

std::string temporal_baseline_name(TemporalBaselineKindV1 baseline) {
  switch (baseline) {
    case TemporalBaselineKindV1::B_NN:
      return "B-nn";
    case TemporalBaselineKindV1::B_STATIC_HOLD:
      return "B-static-hold";
    case TemporalBaselineKindV1::B_MINMAX:
      return "B-minmax";
  }
  return "unknown";
}

std::string temporal_baseline_status_name(
    TemporalBaselineStatusV1 status) {
  switch (status) {
    case TemporalBaselineStatusV1::COMPLETE:
      return "COMPLETE";
    case TemporalBaselineStatusV1::CANDIDATE:
      return "CANDIDATE";
    case TemporalBaselineStatusV1::OPTIMAL_AT_ZERO:
      return "OPTIMAL_AT_ZERO";
    case TemporalBaselineStatusV1::TIME_LIMIT:
      return "TIME_LIMIT";
    case TemporalBaselineStatusV1::ORACLE_CAP:
      return "ORACLE_CAP";
    case TemporalBaselineStatusV1::INDETERMINATE:
      return "INDETERMINATE";
    case TemporalBaselineStatusV1::FAILED:
      return "FAILED";
  }
  return "FAILED";
}

std::string weighted_minmax_termination_name(
    WeightedMinMaxTerminationV1 termination) {
  switch (termination) {
    case WeightedMinMaxTerminationV1::OPTIMAL:
      return "OPTIMAL";
    case WeightedMinMaxTerminationV1::FEASIBLE:
      return "FEASIBLE";
    case WeightedMinMaxTerminationV1::TIME_LIMIT:
      return "TIME_LIMIT";
    case WeightedMinMaxTerminationV1::ERROR:
      return "ERROR";
  }
  return "ERROR";
}

Json temporal_baseline_result_to_json(
    const TemporalBaselineResultV1& result) {
  Json pieces = Json::array();
  for (const auto& piece : result.program.pieces) {
    Json stations = Json::array();
    for (const auto& station : piece.stations) {
      Json terms = Json::array();
      for (const auto& term : station.envelope_terms) {
        terms.push_back({{"point_index", term.point_index},
                         {"a", finite_json(term.a)},
                         {"b", finite_json(term.b)},
                         {"c", finite_json(term.c)}});
      }
      stations.push_back(
          {{"station_index", station.station_index},
           {"fixed_radius_squared",
            optional_finite_json(station.fixed_radius_squared)},
           {"envelope_terms", std::move(terms)}});
    }
    pieces.push_back(
        {{"start", finite_json(piece.start)},
         {"end", finite_json(piece.end)},
         {"assignment", piece.assignment},
         {"stations", std::move(stations)}});
  }
  Json boundaries = Json::array();
  for (const auto& boundary : result.program.boundaries) {
    Json radii = Json::array();
    for (const Weight radius : boundary.station_radius_squared) {
      radii.push_back(finite_json(radius));
    }
    boundaries.push_back(
        {{"time", finite_json(boundary.time)},
         {"assignment", boundary.assignment},
         {"station_radius_squared", std::move(radii)}});
  }
  Json minmax_subproblems = Json::array();
  for (const auto& subproblem : result.minmax_subproblems) {
    minmax_subproblems.push_back(
        {{"start", finite_json(subproblem.start)},
         {"end", finite_json(subproblem.end)},
         {"termination", subproblem.termination},
         {"optimality_proved", subproblem.optimality_proved},
         {"minmax_objective",
          optional_finite_json(subproblem.minmax_objective)},
         {"minsum_program_cost",
          optional_finite_json(subproblem.minsum_program_cost)},
         {"assignment", subproblem.assignment},
         {"oracle", subproblem.oracle},
         {"matrix_validity", subproblem.matrix_validity}});
  }
  return {
      {"schema_version", result.schema_version},
      {"baseline", temporal_baseline_name(result.baseline)},
      {"status", temporal_baseline_status_name(result.status)},
      {"engineering_maturity", result.engineering_maturity},
      {"cost_role", result.cost_role},
      {"cost_validity", result.cost_validity},
      {"cost", optional_finite_json(result.cost)},
      {"program_complete", result.program_complete},
      {"program_feasibility", result.program_feasibility},
      {"minsum_ub_valid", result.minsum_ub_valid},
      {"oracle_call_count", result.oracle_call_count},
      {"assignment_switch_count", result.assignment_switch_count},
      {"failure_reason", result.failure_reason},
      {"program",
       {{"schema_version", result.program.schema_version},
        {"complete", result.program.complete},
        {"boundary_ownership", result.program.boundary_ownership},
        {"pieces", std::move(pieces)},
        {"boundaries", std::move(boundaries)}}},
      {"static_at_zero",
       {{"termination", result.static_at_zero.termination},
        {"optimality_proved", result.static_at_zero.optimality_proved},
        {"oracle", result.static_at_zero.oracle},
        {"objective_at_zero",
         optional_finite_json(result.static_at_zero.objective_at_zero)},
        {"assignment", result.static_at_zero.assignment}}},
      {"minmax_subproblems", std::move(minmax_subproblems)}};
}

Json temporal_baseline_failure_to_json(
    TemporalBaselineKindV1 baseline,
    const std::string& reason) {
  TemporalBaselineResultV1 result;
  result.baseline = baseline;
  result.status = TemporalBaselineStatusV1::FAILED;
  result.failure_reason = reason;
  return temporal_baseline_result_to_json(result);
}

}  // namespace kdc
