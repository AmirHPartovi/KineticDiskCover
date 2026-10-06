#include "kdc/verify.hpp"

#include "kdc/logging.hpp"
#include "kdc/profiling.hpp"

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
  if (instance.n < 0 || instance.m < 0 ||
      static_cast<Index>(instance.n) != instance.trajectories.size() ||
      static_cast<Index>(instance.m) != instance.stations.size() ||
      !std::isfinite(instance.T_end)) {
    return false;
  }
  for (const auto& station : instance.stations) {
    if (!std::isfinite(station.pos.x) || !std::isfinite(station.pos.y)) {
      return false;
    }
  }
  for (const auto& trajectory : instance.trajectories) {
    if (trajectory.t_breaks.size() < 2U ||
        trajectory.t_breaks.size() != trajectory.waypoints.size()) {
      return false;
    }
    for (Index index = 0; index < trajectory.t_breaks.size(); ++index) {
      if (!std::isfinite(trajectory.t_breaks[index]) ||
          !std::isfinite(trajectory.waypoints[index].x) ||
          !std::isfinite(trajectory.waypoints[index].y) ||
          (index > 0U &&
           trajectory.t_breaks[index] <= trajectory.t_breaks[index - 1U])) {
        return false;
      }
    }
  }
  return true;
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
                        const KineticSolution& solution, int num_samples,
                        SolverBudget* budget) {
  double result = 0.0;
  for (const auto& interval : solution.intervals) {
    if (budget != nullptr) {
      budget->checkpoint();
    }
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
      if (budget != nullptr) {
        budget->checkpoint();
      }
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

using Polynomial = std::vector<double>;

void trim_polynomial(Polynomial& polynomial) {
  while (polynomial.size() > 1U &&
         polynomial.back() == 0.0) {
    polynomial.pop_back();
  }
}

double evaluate_polynomial(const Polynomial& polynomial, double x) {
  double result = 0.0;
  for (auto coefficient = polynomial.rbegin();
       coefficient != polynomial.rend(); ++coefficient) {
    result = result * x + *coefficient;
  }
  return result;
}

double polynomial_scale(const Polynomial& polynomial, double x) {
  const double magnitude = std::max(1.0, std::abs(x));
  double scale = 0.0;
  double power = 1.0;
  for (const double coefficient : polynomial) {
    scale += std::abs(coefficient) * power;
    power *= magnitude;
  }
  return std::max(1.0, scale);
}

Polynomial derivative(const Polynomial& polynomial) {
  if (polynomial.size() <= 1U) {
    return {0.0};
  }
  Polynomial result(polynomial.size() - 1U, 0.0);
  for (Index power = 1; power < polynomial.size(); ++power) {
    result[power - 1U] =
        static_cast<double>(power) * polynomial[power];
  }
  trim_polynomial(result);
  return result;
}

Polynomial add_polynomials(const Polynomial& lhs, const Polynomial& rhs,
                           double rhs_scale = 1.0) {
  Polynomial result(std::max(lhs.size(), rhs.size()), 0.0);
  for (Index index = 0; index < lhs.size(); ++index) {
    result[index] += lhs[index];
  }
  for (Index index = 0; index < rhs.size(); ++index) {
    result[index] += rhs_scale * rhs[index];
  }
  trim_polynomial(result);
  return result;
}

Polynomial multiply_polynomials(const Polynomial& lhs,
                                const Polynomial& rhs) {
  Polynomial result(lhs.size() + rhs.size() - 1U, 0.0);
  for (Index i = 0; i < lhs.size(); ++i) {
    for (Index j = 0; j < rhs.size(); ++j) {
      result[i + j] += lhs[i] * rhs[j];
    }
  }
  trim_polynomial(result);
  return result;
}

std::vector<double> polynomial_roots_in_interval(
    Polynomial polynomial, double lower, double upper, SolverBudget* budget,
    int recursion = 0) {
  trim_polynomial(polynomial);
  if (std::any_of(polynomial.begin(), polynomial.end(),
                  [](double value) { return !std::isfinite(value); })) {
    throw std::overflow_error(
        "continuous verification polynomial is not finite");
  }
  std::vector<double> roots;
  if (upper <= lower || recursion > 8) {
    return roots;
  }
  if (polynomial.size() == 1U) {
    return roots;
  }
  if (polynomial.size() == 2U) {
    const double root = -polynomial[0] / polynomial[1];
    if (std::isfinite(root) && root >= lower && root <= upper) {
      roots.push_back(root);
    }
    return roots;
  }

  auto critical = polynomial_roots_in_interval(
      derivative(polynomial), lower, upper, budget, recursion + 1);
  std::sort(critical.begin(), critical.end());
  critical.erase(std::unique(critical.begin(), critical.end()), critical.end());
  std::vector<double> cuts;
  cuts.reserve(critical.size() + 2U);
  cuts.push_back(lower);
  cuts.insert(cuts.end(), critical.begin(), critical.end());
  cuts.push_back(upper);
  constexpr int kBisectionIterations = 100;
  constexpr double kRootEpsilon = 1e-12;

  for (const double point : cuts) {
    if (budget != nullptr) {
      budget->checkpoint();
    }
    const double value = evaluate_polynomial(polynomial, point);
    if (!std::isfinite(value)) {
      throw std::overflow_error(
          "continuous verification polynomial evaluation overflowed");
    }
    const double scale = polynomial_scale(polynomial, point);
    if (!std::isfinite(scale)) {
      throw std::overflow_error(
          "continuous verification polynomial scale overflowed");
    }
    if (std::abs(value) <= kRootEpsilon * scale) {
      roots.push_back(point);
    }
  }
  for (Index index = 0; index + 1U < cuts.size(); ++index) {
    if (budget != nullptr) {
      budget->checkpoint();
    }
    double left = cuts[index];
    double right = cuts[index + 1U];
    double left_value = evaluate_polynomial(polynomial, left);
    double right_value = evaluate_polynomial(polynomial, right);
    if (!std::isfinite(left_value) || !std::isfinite(right_value)) {
      throw std::overflow_error(
          "continuous verification polynomial evaluation overflowed");
    }
    if (left_value == 0.0 || right_value == 0.0 ||
        std::signbit(left_value) == std::signbit(right_value)) {
      continue;
    }
    for (int iteration = 0; iteration < kBisectionIterations; ++iteration) {
      if (budget != nullptr) {
        budget->checkpoint();
      }
      const double middle = left + (right - left) / 2.0;
      if (middle == left || middle == right) {
        break;
      }
      const double middle_value = evaluate_polynomial(polynomial, middle);
      if (!std::isfinite(middle_value)) {
        throw std::overflow_error(
            "continuous verification polynomial evaluation overflowed");
      }
      if (middle_value == 0.0) {
        left = middle;
        right = middle;
        break;
      }
      if (std::signbit(left_value) != std::signbit(middle_value)) {
        right = middle;
        right_value = middle_value;
      } else {
        left = middle;
        left_value = middle_value;
      }
    }
    roots.push_back(left + (right - left) / 2.0);
  }
  std::sort(roots.begin(), roots.end());
  roots.erase(std::unique(roots.begin(), roots.end()), roots.end());
  return roots;
}

Polynomial squared_distance_polynomial(const Trajectory& trajectory,
                                       const Point& station, double midpoint,
                                       double local_origin) {
  const Index segment =
      static_cast<Index>(trajectory.segment_index(midpoint));
  const double duration = trajectory.t_breaks[segment + 1U] -
                           trajectory.t_breaks[segment];
  const Point velocity =
      (trajectory.waypoints[segment + 1U] - trajectory.waypoints[segment]) *
      (1.0 / duration);
  const Point origin =
      trajectory.position(midpoint) +
      velocity * (local_origin - midpoint) - station;
  return {origin.norm2(), 2.0 * origin.dot(velocity), velocity.norm2()};
}

Polynomial comparison_boundary_polynomial(const Trajectory& point,
                                          const Trajectory& support,
                                          const Point& station,
                                          double midpoint,
                                          double local_origin,
                                          double tolerance) {
  const Polynomial point_distance =
      squared_distance_polynomial(point, station, midpoint, local_origin);
  const Polynomial support_distance =
      squared_distance_polynomial(support, station, midpoint, local_origin);
  Polynomial difference =
      add_polynomials(point_distance, support_distance, -1.0);
  difference[0] -= tolerance * tolerance;
  return add_polynomials(
      multiply_polynomials(difference, difference),
      support_distance, -4.0 * tolerance * tolerance);
}

void append_unique_cut(std::vector<double>& cuts, double value) {
  if (std::isfinite(value)) {
    cuts.push_back(value);
  }
}

void append_comparison_roots(std::vector<double>& cuts,
                             const Instance& instance, int point_id,
                             int support_id, int station_id, double lower,
                             double upper, double tolerance,
                             SolverBudget* budget) {
  const double midpoint = lower + (upper - lower) / 2.0;
  const auto polynomial = comparison_boundary_polynomial(
      instance.trajectories[static_cast<Index>(point_id)],
      instance.trajectories[static_cast<Index>(support_id)],
      instance.stations[static_cast<Index>(station_id)].pos, midpoint,
      lower, tolerance);
  const auto roots = polynomial_roots_in_interval(
      polynomial, 0.0, upper - lower, budget);
  for (const double root : roots) {
    append_unique_cut(cuts, lower + root);
  }
}

bool covered_at(const Instance& instance, const SolutionInterval& interval,
                int point_id, double time, double tolerance,
                SolverBudget* budget) {
  const Point point =
      instance.trajectories[static_cast<Index>(point_id)].position(time);
  for (int station_id = 0; station_id < instance.m; ++station_id) {
    if (budget != nullptr) {
      budget->checkpoint();
    }
    const int support =
        interval.supporting_point[static_cast<Index>(station_id)];
    if (support < 0) {
      continue;
    }
    const Point station = instance.stations[static_cast<Index>(station_id)].pos;
    const double point_distance =
        (point - station).norm();
    const double support_distance =
        (instance.trajectories[static_cast<Index>(support)].position(time) -
         station)
            .norm();
    if (!std::isfinite(point_distance) || !std::isfinite(support_distance)) {
      return false;
    }
    if (point_distance <= support_distance + tolerance) {
      return true;
    }
  }
  return false;
}

bool comparison_holds_at(const Instance& instance, int point_id,
                         int support_id, int station_id, double time,
                         double tolerance) {
  const Point station = instance.stations[static_cast<Index>(station_id)].pos;
  const double point_distance =
      (instance.trajectories[static_cast<Index>(point_id)].position(time) -
       station)
          .norm();
  const double support_distance =
      (instance.trajectories[static_cast<Index>(support_id)].position(time) -
       station)
          .norm();
  if (!std::isfinite(point_distance) || !std::isfinite(support_distance)) {
    return false;
  }
  return point_distance <= support_distance + tolerance;
}

std::vector<double> interval_cuts(const Instance& instance,
                                  const SolutionInterval& interval,
                                  SolverBudget* budget) {
  std::vector<double> cuts{interval.t_start, interval.t_end};
  for (const auto& trajectory : instance.trajectories) {
    if (budget != nullptr) {
      budget->checkpoint();
    }
    for (const double breakpoint : trajectory.t_breaks) {
      if (budget != nullptr) {
        budget->checkpoint();
      }
      if (breakpoint > interval.t_start && breakpoint < interval.t_end) {
        cuts.push_back(breakpoint);
      }
    }
  }
  std::sort(cuts.begin(), cuts.end());
  cuts.erase(std::unique(cuts.begin(), cuts.end()), cuts.end());
  return cuts;
}

bool verify_continuous_coverage(const Instance& instance,
                               const KineticSolution& solution,
                               double tolerance, double& max_violation,
                               SolverBudget* budget) {
  max_violation = 0.0;
  for (const auto& interval : solution.intervals) {
    if (budget != nullptr) {
      budget->checkpoint();
    }
    const auto base_cuts = interval_cuts(instance, interval, budget);
    for (Index piece = 0; piece + 1U < base_cuts.size(); ++piece) {
      const double lower = base_cuts[piece];
      const double upper = base_cuts[piece + 1U];
      if (upper <= lower) {
        continue;
      }
      for (int point_id = 0; point_id < instance.n; ++point_id) {
        if (budget != nullptr) {
          budget->checkpoint();
        }
        std::vector<double> cuts{lower, upper};
        for (int station_id = 0; station_id < instance.m; ++station_id) {
          if (budget != nullptr) {
            budget->checkpoint();
          }
          const int support =
              interval.supporting_point[static_cast<Index>(station_id)];
          if (support >= 0) {
            append_comparison_roots(cuts, instance, point_id, support,
                                    station_id, lower, upper, tolerance,
                                    budget);
          }
        }
        std::sort(cuts.begin(), cuts.end());
        cuts.erase(std::unique(cuts.begin(), cuts.end()), cuts.end());
        for (Index index = 0; index < cuts.size(); ++index) {
          if (budget != nullptr) {
            budget->checkpoint();
          }
          const double time = cuts[index];
          if (!covered_at(instance, interval, point_id, time, tolerance,
                          budget)) {
            double best_violation =
                std::numeric_limits<double>::infinity();
            const Point point =
                instance.trajectories[static_cast<Index>(point_id)]
                    .position(time);
            for (int station_id = 0; station_id < instance.m; ++station_id) {
              const int support = interval.supporting_point[
                  static_cast<Index>(station_id)];
              if (support < 0) {
                continue;
              }
              const Point station =
                  instance.stations[static_cast<Index>(station_id)].pos;
              best_violation = std::min(
                  best_violation,
                  (point - station).norm() -
                      (instance.trajectories[static_cast<Index>(support)]
                           .position(time) -
                       station)
                          .norm());
            }
            max_violation = std::max(max_violation, best_violation);
            return false;
          }
          if (index + 1U < cuts.size()) {
            const double midpoint = time + (cuts[index + 1U] - time) / 2.0;
            if (!covered_at(instance, interval, point_id, midpoint, tolerance,
                            budget)) {
              max_violation = std::max(max_violation, tolerance);
              return false;
            }
          }
        }
      }
    }
  }
  return true;
}

bool verify_continuous_assignment(const Instance& instance,
                                  const KineticSolution& solution,
                                  double tolerance, SolverBudget* budget) {
  for (const auto& interval : solution.intervals) {
    if (budget != nullptr) {
      budget->checkpoint();
    }
    const auto base_cuts = interval_cuts(instance, interval, budget);
    for (int station_id = 0; station_id < instance.m; ++station_id) {
      if (budget != nullptr) {
        budget->checkpoint();
      }
      const int support =
          interval.supporting_point[static_cast<Index>(station_id)];
      if (support < 0) {
        for (const int owner : interval.assigned_points) {
          if (budget != nullptr) {
            budget->checkpoint();
          }
          if (owner == station_id) {
            return false;
          }
        }
        continue;
      }
      if (support >= instance.n ||
          interval.assigned_points[static_cast<Index>(support)] != station_id) {
        return false;
      }
      for (Index piece = 0; piece + 1U < base_cuts.size(); ++piece) {
        const double lower = base_cuts[piece];
        const double upper = base_cuts[piece + 1U];
        if (upper <= lower) {
          continue;
        }
        for (int point_id = 0; point_id < instance.n; ++point_id) {
          if (budget != nullptr) {
            budget->checkpoint();
          }
          if (interval.assigned_points[static_cast<Index>(point_id)] !=
              station_id) {
            continue;
          }
          std::vector<double> cuts{lower, upper};
          append_comparison_roots(cuts, instance, point_id, support,
                                  station_id, lower, upper, tolerance, budget);
          std::sort(cuts.begin(), cuts.end());
          cuts.erase(std::unique(cuts.begin(), cuts.end()), cuts.end());
          for (Index index = 0; index < cuts.size(); ++index) {
            if (budget != nullptr) {
              budget->checkpoint();
            }
            if (!comparison_holds_at(instance, point_id, support, station_id,
                                     cuts[index], tolerance)) {
              return false;
            }
            if (index + 1U < cuts.size()) {
              const double midpoint =
                  cuts[index] + (cuts[index + 1U] - cuts[index]) / 2.0;
              if (!comparison_holds_at(instance, point_id, support, station_id,
                                       midpoint, tolerance)) {
                return false;
              }
            }
          }
        }
      }
    }
  }
  return true;
}

bool verify_continuous_cost(const Instance& instance,
                            const KineticSolution& solution,
                            double tolerance, SolverBudget* budget) {
  const double pi = std::acos(-1.0);
  for (const auto& interval : solution.intervals) {
    if (budget != nullptr) {
      budget->checkpoint();
    }
    const auto cuts = interval_cuts(instance, interval, budget);
    for (Index piece = 0; piece + 1U < cuts.size(); ++piece) {
      const double lower = cuts[piece];
      const double upper = cuts[piece + 1U];
      if (upper <= lower) {
        continue;
      }
      const double midpoint = lower + (upper - lower) / 2.0;
      double a = 0.0;
      double b = 0.0;
      double c = 0.0;
      for (Index station = 0; station < interval.supporting_point.size();
           ++station) {
        if (budget != nullptr) {
          budget->checkpoint();
        }
        const int support = interval.supporting_point[station];
        if (support < 0) {
          continue;
        }
        const auto& trajectory =
            instance.trajectories[static_cast<Index>(support)];
        const Index segment =
            static_cast<Index>(trajectory.segment_index(midpoint));
        const double segment_start = trajectory.t_breaks[segment];
        const double duration =
            trajectory.t_breaks[segment + 1U] - segment_start;
        const Point velocity =
            (trajectory.waypoints[segment + 1U] -
             trajectory.waypoints[segment]) *
            (1.0 / duration);
        const Point origin =
            trajectory.waypoints[segment] - velocity * segment_start -
            instance.stations[station].pos;
        a += pi * velocity.norm2();
        b += 2.0 * pi * origin.dot(velocity);
        c += pi * origin.norm2();
      }
      const double da = a - interval.a;
      const double db = b - interval.b;
      const double dc = c - interval.c;
      double maximum_error =
          std::max(std::abs((da * lower + db) * lower + dc),
                   std::abs((da * upper + db) * upper + dc));
      if (std::abs(da) > std::numeric_limits<double>::epsilon()) {
        const double vertex = -db / (2.0 * da);
        if (vertex > lower && vertex < upper) {
          maximum_error =
              std::max(maximum_error,
                       std::abs((da * vertex + db) * vertex + dc));
        }
      }
      if (!std::isfinite(maximum_error) || maximum_error > tolerance) {
        return false;
      }
    }
  }
  return true;
}

double exact_integral(const Instance& instance,
                      const KineticSolution& solution,
                      SolverBudget* budget) {
  const double pi = std::acos(-1.0);
  double result = 0.0;
  for (const auto& interval : solution.intervals) {
    if (budget != nullptr) {
      budget->checkpoint();
    }
    const auto cuts = interval_cuts(instance, interval, budget);
    for (Index piece = 0; piece + 1U < cuts.size(); ++piece) {
      if (budget != nullptr) {
        budget->checkpoint();
      }
      const double lower = cuts[piece];
      const double upper = cuts[piece + 1U];
      const double midpoint = lower + (upper - lower) / 2.0;
      double a = 0.0;
      double b = 0.0;
      double c = 0.0;
      for (Index station = 0; station < interval.supporting_point.size();
           ++station) {
        if (budget != nullptr) {
          budget->checkpoint();
        }
        const int support = interval.supporting_point[station];
        if (support < 0) {
          continue;
        }
        const auto& trajectory =
            instance.trajectories[static_cast<Index>(support)];
        const Index segment =
            static_cast<Index>(trajectory.segment_index(midpoint));
        const double segment_start = trajectory.t_breaks[segment];
        const double duration =
            trajectory.t_breaks[segment + 1U] - segment_start;
        const Point velocity =
            (trajectory.waypoints[segment + 1U] -
             trajectory.waypoints[segment]) *
            (1.0 / duration);
        const Point origin =
            trajectory.waypoints[segment] - velocity * segment_start -
            instance.stations[station].pos;
        a += pi * velocity.norm2();
        b += 2.0 * pi * origin.dot(velocity);
        c += pi * origin.norm2();
      }
      result += (a / 3.0) * (upper * upper * upper - lower * lower * lower) +
                (b / 2.0) * (upper * upper - lower * lower) +
                c * (upper - lower);
    }
  }
  return result;
}
}

bool Verifier::check_coverage(const Instance& instance,
                              const KineticSolution& solution, double time,
                              double tolerance,
                              double* out_max_violation,
                              SolverBudget* budget) {
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
    if (budget != nullptr) {
      budget->checkpoint();
    }
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
                                       double time, double tolerance,
                                       SolverBudget* budget) {
  const SolutionInterval* interval = nullptr;
  if (!valid_sample(instance, solution, time, interval) ||
      !std::isfinite(tolerance) || tolerance < 0.0) {
    return false;
  }
  for (int station_id = 0; station_id < instance.m; ++station_id) {
    if (budget != nullptr) {
      budget->checkpoint();
    }
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
                          double tolerance, SolverBudget* budget) {
  if (budget != nullptr) {
    budget->checkpoint();
  }
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
                              int num_samples, double tolerance,
                              SolverBudget* budget) {
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
  const double numeric =
      sampled_integral(instance, solution, num_samples, budget);
  const double scale = std::max({1.0, std::abs(analytic), std::abs(numeric)});
  return std::isfinite(analytic) && std::isfinite(numeric) &&
         std::abs(analytic - numeric) <= tolerance * scale;
}

bool Verifier::check_peak_consistency(const KineticSolution& solution,
                                     double reported_peak,
                                     double reported_peak_time,
                                     double tolerance) {
  if (!solution.is_well_formed() || !std::isfinite(reported_peak) ||
      !std::isfinite(reported_peak_time) || !std::isfinite(tolerance) ||
      tolerance < 0.0 || reported_peak_time <
                            solution.intervals.front().t_start ||
      reported_peak_time > solution.intervals.back().t_end) {
    return false;
  }
  const auto value_at = [](const SolutionInterval& interval, double time) {
    return (interval.a * time + interval.b) * time + interval.c;
  };
  double reconstructed_peak = -std::numeric_limits<double>::infinity();
  for (const auto& interval : solution.intervals) {
    reconstructed_peak =
        std::max(reconstructed_peak, value_at(interval, interval.t_start));
    reconstructed_peak =
        std::max(reconstructed_peak, value_at(interval, interval.t_end));
    if (interval.a < 0.0) {
      const double vertex = -interval.b / (2.0 * interval.a);
      if (vertex > interval.t_start && vertex < interval.t_end) {
        reconstructed_peak =
            std::max(reconstructed_peak, value_at(interval, vertex));
      }
    }
  }
  const double reported_at_time =
      value_at(*find_interval(solution, reported_peak_time),
               reported_peak_time);
  const double peak_scale =
      std::max({1.0, std::abs(reconstructed_peak), std::abs(reported_peak)});
  const double time_scale =
      std::max({1.0, std::abs(solution.intervals.front().t_start),
                std::abs(solution.intervals.back().t_end)});
  const double cost_tolerance = tolerance * peak_scale;
  return std::abs(reconstructed_peak - reported_peak) <= cost_tolerance &&
         std::abs(reported_at_time - reconstructed_peak) <= cost_tolerance &&
         reported_peak_time >= solution.intervals.front().t_start -
                                   tolerance * time_scale &&
         reported_peak_time <= solution.intervals.back().t_end +
                                   tolerance * time_scale;
}

VerificationReport Verifier::verify(const Instance& instance,
                                    const KineticSolution& solution,
                                    int samples_per_interval,
                                    double tolerance, SolverBudget* budget) {
  (void)samples_per_interval;
  return verify_continuous(instance, solution, tolerance, budget);
}

VerificationReport Verifier::verify_empirical(
    const Instance& instance, const KineticSolution& solution,
    int samples_per_interval, double tolerance, SolverBudget* budget) {
  KDC_PROFILE_PHASE(ProfilePhase::VERIFICATION);
  LOG_INFO("Verifier: starting on {} intervals", solution.intervals.size());
  VerificationReport report;
  report.kind = VerificationKind::EMPIRICAL;
  report.empirical_verification = true;
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
    if (budget != nullptr) {
      budget->checkpoint();
    }
    for (int sample = 0; sample <= samples_per_interval; ++sample) {
      if (budget != nullptr) {
        budget->checkpoint();
      }
      const double fraction =
          static_cast<double>(sample) /
          static_cast<double>(samples_per_interval);
      const double time =
          interval.t_start + fraction * (interval.t_end - interval.t_start);
      double violation = 0.0;
      if (!check_coverage(instance, solution, time, tolerance, &violation,
                          budget)) {
        report.coverage_ok = false;
        report.max_coverage_violation =
            std::max(report.max_coverage_violation, violation);
      }
      if (!check_supporting_points(instance, solution, time, tolerance,
                                   budget)) {
        report.supporting_points_ok = false;
      }
      if (!check_cost(instance, solution, time, tolerance, budget)) {
        report.cost_consistent_ok = false;
      }

      const SolutionInterval* active = nullptr;
      if (!valid_sample(instance, solution, time, active)) {
        report.assignment_consistent_ok = false;
        continue;
      }
      for (int point_id = 0; point_id < instance.n; ++point_id) {
        if (budget != nullptr) {
          budget->checkpoint();
        }
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
      check_integral(instance, solution, 10000, tolerance, budget);
  record_failure(report.integral_consistent_ok, "integral");

  LOG_INFO("Verifier: kind={}, coverage_ok={}, supporting_points_ok={}, "
           "cost_consistent_ok={}, integral_consistent_ok={}, "
           "assignment_consistent_ok={}",
           verification_kind_to_string(report.kind), report.coverage_ok,
           report.supporting_points_ok, report.cost_consistent_ok,
           report.integral_consistent_ok, report.assignment_consistent_ok);
  return report;
}

VerificationReport Verifier::verify_continuous(
    const Instance& instance, const KineticSolution& solution,
    double tolerance, SolverBudget* budget) {
  KDC_PROFILE_PHASE(ProfilePhase::VERIFICATION);
    LOG_INFO("Verifier: starting certified continuous check on {} intervals",
             solution.intervals.size());
    VerificationReport report;
    report.kind = VerificationKind::CERTIFIED_CONTINUOUS;
    report.certified_continuous_verification = true;
    if (!valid_instance(instance) || !solution.is_well_formed() ||
        !std::isfinite(tolerance) || tolerance < 0.0) {
      report.errors.emplace_back(
          "invalid instance, solution, or continuous verification tolerance");
      return report;
    }
    for (const auto& trajectory : instance.trajectories) {
      if (trajectory.t_breaks.size() < 2U ||
          solution.intervals.front().t_start < trajectory.t_breaks.front() ||
          solution.intervals.back().t_end > trajectory.t_breaks.back()) {
        report.errors.emplace_back(
            "solution time range exceeds a trajectory's time domain");
        return report;
      }
    }
    for (const auto& interval : solution.intervals) {
      if (interval.supporting_point.size() != static_cast<Index>(instance.m) ||
          interval.assigned_points.size() != static_cast<Index>(instance.n)) {
        report.errors.emplace_back(
            "solution interval dimensions do not match the instance");
        return report;
      }
      for (const int support : interval.supporting_point) {
        if (budget != nullptr) {
          budget->checkpoint();
        }
        if (support < -1 || support >= instance.n) {
          report.errors.emplace_back(
              "solution interval contains an invalid supporting point");
          return report;
        }
      }
      for (const int owner : interval.assigned_points) {
        if (budget != nullptr) {
          budget->checkpoint();
        }
        if (owner < 0 || owner >= instance.m) {
          report.errors.emplace_back(
              "solution interval contains an invalid point assignment");
          return report;
        }
      }
    }

    report.coverage_ok = verify_continuous_coverage(
        instance, solution, tolerance, report.max_coverage_violation, budget);
    report.supporting_points_ok =
        verify_continuous_assignment(instance, solution, tolerance, budget);
    report.assignment_consistent_ok = report.supporting_points_ok;
    report.cost_consistent_ok =
        verify_continuous_cost(instance, solution, tolerance, budget);
    const double analytic_integral = solution.total_integral();
    const double direct_integral = exact_integral(instance, solution, budget);
    const double integral_scale =
        std::max({1.0, std::abs(analytic_integral), std::abs(direct_integral)});
    report.integral_consistent_ok =
        std::isfinite(analytic_integral) && std::isfinite(direct_integral) &&
        std::abs(analytic_integral - direct_integral) <=
            tolerance * integral_scale;

    const auto record_failure = [&report](bool okay, const char* label) {
      if (!okay) {
        report.errors.emplace_back(std::string(label) +
                                   " continuous verification failed");
      }
    };
    record_failure(report.coverage_ok, "coverage");
    record_failure(report.supporting_points_ok, "supporting-points");
    record_failure(report.cost_consistent_ok, "cost");
    record_failure(report.integral_consistent_ok, "integral");
    record_failure(report.assignment_consistent_ok, "assignment");
    LOG_INFO("Verifier: kind={}, coverage_ok={}, supporting_points_ok={}, "
             "cost_consistent_ok={}, integral_consistent_ok={}, "
             "assignment_consistent_ok={}",
             verification_kind_to_string(report.kind), report.coverage_ok,
             report.supporting_points_ok, report.cost_consistent_ok,
             report.integral_consistent_ok, report.assignment_consistent_ok);
    return report;
  }

bool verify_solution(const KineticSolution& solution) {
  return solution.is_well_formed();
}
}
