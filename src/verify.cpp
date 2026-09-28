#include "kdc/verify.hpp"

#include "kdc/logging.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <sstream>

namespace kdc {
namespace {
const SolutionInterval* find_interval(const KineticSolution& solution,
                                      double time) {
  if (solution.intervals.empty() || !std::isfinite(time) ||
      time < solution.intervals.front().t_start ||
      time > solution.intervals.back().t_end) {
    return nullptr;
  }
  auto upper = std::upper_bound(
      solution.intervals.begin(), solution.intervals.end(), time,
      [](double value, const SolutionInterval& interval) {
        return value < interval.t_start;
      });
  if (upper == solution.intervals.begin()) {
    upper = solution.intervals.begin();
  } else {
    --upper;
  }
  return time <= upper->t_end ? &*upper : nullptr;
}

bool valid_instance(const Instance& instance) {
  return instance.n >= 0 && instance.m >= 0 &&
         static_cast<Index>(instance.n) == instance.trajectories.size() &&
         static_cast<Index>(instance.m) == instance.stations.size();
}

bool valid_sample(const Instance& instance, const KineticSolution& solution,
                  double time, const SolutionInterval*& interval) {
  if (!valid_instance(instance) || !solution.is_well_formed() ||
      !std::isfinite(time)) {
    return false;
  }
  interval = find_interval(solution, time);
  if (interval == nullptr ||
      interval->supporting_point.size() != static_cast<Index>(instance.m) ||
      interval->assigned_points.size() != static_cast<Index>(instance.n)) {
    return false;
  }
  for (const auto& trajectory : instance.trajectories) {
    if (time < trajectory.t_breaks.front() ||
        time > trajectory.t_breaks.back()) {
      return false;
    }
  }
  return true;
}

double direct_cost(const Instance& instance, const SolutionInterval& interval,
                   double time) {
  double cost = 0.0;
  const double pi = std::acos(-1.0);
  for (int station_id = 0; station_id < instance.m; ++station_id) {
    const int support =
        interval.supporting_point[static_cast<Index>(station_id)];
    if (support < 0 || support >= instance.n) {
      continue;
    }
    const Point displacement =
        instance.trajectories[static_cast<Index>(support)].position(time) -
        instance.stations[static_cast<Index>(station_id)].pos;
    cost += pi * displacement.norm2();
  }
  return cost;
}

double sampled_integral(const Instance& instance,
                        const KineticSolution& solution, int num_samples) {
  double result = 0.0;
  for (const auto& interval : solution.intervals) {
    std::vector<double> cuts{interval.t_start, interval.t_end};
    for (const int support : interval.supporting_point) {
      if (support < 0 || support >= instance.n) {
        continue;
      }
      for (const double breakpoint :
           instance.trajectories[static_cast<Index>(support)].t_breaks) {
        if (breakpoint > interval.t_start &&
            breakpoint < interval.t_end) {
          cuts.push_back(breakpoint);
        }
      }
    }
    std::sort(cuts.begin(), cuts.end());
    cuts.erase(std::unique(cuts.begin(), cuts.end()), cuts.end());
    for (Index part = 0; part + 1U < cuts.size(); ++part) {
      const double lower = cuts[part];
      const double upper = cuts[part + 1U];
      if (upper <= lower) {
        continue;
      }
      int steps = std::max(2, num_samples /
                                  static_cast<int>(cuts.size() - 1U));
      if (steps % 2 != 0) {
        ++steps;
      }
      const double step = (upper - lower) / static_cast<double>(steps);
      double sum = direct_cost(instance, interval, lower) +
                   direct_cost(instance, interval, upper);
      for (int index = 1; index < steps; ++index) {
        const double time = lower + static_cast<double>(index) * step;
        sum += (index % 2 == 0 ? 2.0 : 4.0) *
               direct_cost(instance, interval, time);
      }
      result += sum * step / 3.0;
    }
  }
  return result;
}
}

bool Verifier::check_coverage(const Instance& instance,
                              const KineticSolution& solution, double time,
                              double tolerance,
                              double* out_max_violation) {
  if (out_max_violation != nullptr) {
    *out_max_violation = std::numeric_limits<double>::infinity();
  }
  const SolutionInterval* interval = nullptr;
  if (!valid_sample(instance, solution, time, interval) ||
      !std::isfinite(tolerance) || tolerance < 0.0) {
    return false;
  }
  double maximum_violation = 0.0;
  for (int point_id = 0; point_id < instance.n; ++point_id) {
    const Point point =
        instance.trajectories[static_cast<Index>(point_id)].position(time);
    double minimum_violation = std::numeric_limits<double>::infinity();
    for (int station_id = 0; station_id < instance.m; ++station_id) {
      const int support =
          interval->supporting_point[static_cast<Index>(station_id)];
      if (support < -1 || support >= instance.n) {
        return false;
      }
      if (support < 0) {
        continue;
      }
      const Point station = instance.stations[static_cast<Index>(station_id)].pos;
      const double radius =
          (station - instance.trajectories[static_cast<Index>(support)]
                         .position(time))
              .norm();
      const double violation = (point - station).norm() - radius;
      minimum_violation = std::min(minimum_violation, violation);
      if (violation <= tolerance) {
        minimum_violation = -1.0;
        break;
      }
    }
    if (minimum_violation > 0.0) {
      maximum_violation =
          std::max(maximum_violation, minimum_violation);
    }
  }
  if (out_max_violation != nullptr) {
    *out_max_violation = maximum_violation;
  }
  return maximum_violation <= tolerance;
}

bool Verifier::check_supporting_points(const Instance& instance,
                                       const KineticSolution& solution,
                                       double time, double tolerance) {
  const SolutionInterval* interval = nullptr;
  if (!valid_sample(instance, solution, time, interval) ||
      !std::isfinite(tolerance) || tolerance < 0.0) {
    return false;
  }
  for (int station_id = 0; station_id < instance.m; ++station_id) {
    const Index station_index = static_cast<Index>(station_id);
    const int support = interval->supporting_point[station_index];
    if (support == -1) {
      for (int point_id = 0; point_id < instance.n; ++point_id) {
        if (interval->assigned_points[static_cast<Index>(point_id)] ==
            station_id) {
          return false;
        }
      }
      continue;
    }
    if (support < 0 || support >= instance.n ||
        interval->assigned_points[static_cast<Index>(support)] != station_id) {
      return false;
    }
    const Point station = instance.stations[station_index].pos;
    const double support_radius =
        (station - instance.trajectories[static_cast<Index>(support)]
                       .position(time))
            .norm();
    for (int point_id = 0; point_id < instance.n; ++point_id) {
      if (interval->assigned_points[static_cast<Index>(point_id)] !=
          station_id) {
        continue;
      }
      const double radius =
          (station - instance.trajectories[static_cast<Index>(point_id)]
                         .position(time))
              .norm();
      if (radius > support_radius + tolerance) {
        return false;
      }
    }
  }
  return true;
}

bool Verifier::check_cost(const Instance& instance,
                          const KineticSolution& solution, double time,
                          double tolerance) {
  const SolutionInterval* interval = nullptr;
  if (!valid_sample(instance, solution, time, interval) ||
      !std::isfinite(tolerance) || tolerance < 0.0) {
    return false;
  }
  const double cost = direct_cost(instance, *interval, time);
  const double polynomial =
      (interval->a * time + interval->b) * time + interval->c;
  const double scale = std::max({1.0, std::abs(cost), std::abs(polynomial)});
  return std::isfinite(cost) && std::isfinite(polynomial) &&
         std::abs(polynomial - cost) <= tolerance * scale;
}

bool Verifier::check_integral(const Instance& instance,
                              const KineticSolution& solution,
                              int num_samples, double tolerance) {
  if (!valid_instance(instance) || !solution.is_well_formed() ||
      num_samples <= 0 || !std::isfinite(tolerance) || tolerance < 0.0) {
    return false;
  }
  for (const auto& trajectory : instance.trajectories) {
    if (trajectory.t_breaks.empty() ||
        solution.intervals.front().t_start < trajectory.t_breaks.front() ||
        solution.intervals.back().t_end > trajectory.t_breaks.back()) {
      return false;
    }
  }
  const double analytic = solution.total_integral();
  const double numeric = sampled_integral(instance, solution, num_samples);
  const double scale = std::max({1.0, std::abs(analytic), std::abs(numeric)});
  return std::isfinite(analytic) && std::isfinite(numeric) &&
         std::abs(analytic - numeric) <= tolerance * scale;
}

VerificationReport Verifier::verify(const Instance& instance,
                                    const KineticSolution& solution,
                                    int samples_per_interval,
                                    double tolerance) {
  LOG_INFO("Verifier: starting on {} intervals", solution.intervals.size());
  VerificationReport report;
  if (!valid_instance(instance) || !solution.is_well_formed() ||
      samples_per_interval <= 0 || !std::isfinite(tolerance) ||
      tolerance < 0.0 ||
      solution.intervals.front().supporting_point.size() !=
          static_cast<Index>(instance.m) ||
      solution.intervals.front().assigned_points.size() !=
          static_cast<Index>(instance.n)) {
    report.errors.emplace_back(
        "invalid instance, solution, or verification parameters");
    return report;
  }

  report.coverage_ok = true;
  report.supporting_points_ok = true;
  report.cost_consistent_ok = true;
  report.assignment_consistent_ok = true;
  for (const auto& interval : solution.intervals) {
    for (int sample = 0; sample <= samples_per_interval; ++sample) {
      const double fraction =
          static_cast<double>(sample) /
          static_cast<double>(samples_per_interval);
      const double time =
          interval.t_start + fraction * (interval.t_end - interval.t_start);
      double violation = 0.0;
      if (!check_coverage(instance, solution, time, tolerance, &violation)) {
        report.coverage_ok = false;
        report.max_coverage_violation =
            std::max(report.max_coverage_violation, violation);
      }
      if (!check_supporting_points(instance, solution, time, tolerance)) {
        report.supporting_points_ok = false;
      }
      if (!check_cost(instance, solution, time, tolerance)) {
        report.cost_consistent_ok = false;
      }

      const SolutionInterval* active = nullptr;
      if (!valid_sample(instance, solution, time, active)) {
        report.assignment_consistent_ok = false;
        continue;
      }
      for (int point_id = 0; point_id < instance.n; ++point_id) {
        const int owner =
            active->assigned_points[static_cast<Index>(point_id)];
        if (owner < 0 || owner >= instance.m ||
            active->supporting_point[static_cast<Index>(owner)] < 0) {
          report.assignment_consistent_ok = false;
          continue;
        }
        const Point station = instance.stations[static_cast<Index>(owner)].pos;
        const Point point =
            instance.trajectories[static_cast<Index>(point_id)].position(time);
        const int support =
            active->supporting_point[static_cast<Index>(owner)];
        const Point support_position =
            instance.trajectories[static_cast<Index>(support)].position(time);
        if ((point - station).norm() >
            (support_position - station).norm() + tolerance) {
          report.assignment_consistent_ok = false;
        }
      }
    }
  }

  const auto record_failure = [&report](bool okay, const char* label) {
    if (!okay) {
      report.errors.emplace_back(std::string(label) + " verification failed");
    }
  };
  record_failure(report.coverage_ok, "coverage");
  record_failure(report.supporting_points_ok, "supporting-points");
  record_failure(report.cost_consistent_ok, "cost");
  record_failure(report.assignment_consistent_ok, "assignment");
  report.integral_consistent_ok =
      check_integral(instance, solution, 10000, tolerance);
  record_failure(report.integral_consistent_ok, "integral");

  LOG_INFO("Verifier: coverage_ok={}, supporting_points_ok={}, "
           "cost_consistent_ok={}, integral_consistent_ok={}, "
           "assignment_consistent_ok={}",
           report.coverage_ok, report.supporting_points_ok,
           report.cost_consistent_ok, report.integral_consistent_ok,
           report.assignment_consistent_ok);
  return report;
}

bool verify_solution(const KineticSolution& solution) {
  return solution.is_well_formed();
}
}
