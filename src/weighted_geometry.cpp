#include "kdc/weighted_geometry.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace kdc {
namespace {
bool finite_point(const Point& point) {
  return std::isfinite(point.x) && std::isfinite(point.y);
}

double squared_norm(const Point& point) {
  return point.dot(point);
}

double evaluate_quadratic(
    const GeometryQuadraticCoefficientsV1& coefficient,
    double time) {
  return coefficient.a * time * time +
         coefficient.b * time +
         coefficient.c;
}
}

WeightedGeometry::WeightedGeometry(const Instance& instance)
    : point_count_(instance.n),
      station_count_(instance.m),
      domain_start_(0.0),
      domain_end_(instance.T_end) {
  station_ids_.reserve(static_cast<WeightIndex>(instance.m));
  station_positions_.reserve(static_cast<WeightIndex>(instance.m));
  for (const auto& station : instance.stations) {
    if (!finite_point(station.pos)) {
      throw std::invalid_argument(
          "weighted geometry received a non-finite center");
    }
    station_ids_.push_back(station.id);
    station_positions_.push_back(station.pos);
  }
  trajectories_.reserve(static_cast<WeightIndex>(instance.n));
  for (const auto& trajectory : instance.trajectories) {
    trajectories_.push_back(trajectory);
  }
  validate_instance();

  common_partition_.push_back(domain_start_);
  for (const auto& trajectory : trajectories_) {
    for (const double breakpoint : trajectory.t_breaks) {
      common_partition_.push_back(breakpoint);
    }
  }
  common_partition_.push_back(domain_end_);
  std::sort(common_partition_.begin(), common_partition_.end());
  common_partition_.erase(
      std::unique(common_partition_.begin(),
                  common_partition_.end()),
      common_partition_.end());
}

void WeightedGeometry::validate_instance() const {
  if (point_count_ < 0 || station_count_ < 0) {
    throw std::invalid_argument(
        "weighted geometry dimensions must be nonnegative");
  }
  if (static_cast<WeightIndex>(point_count_) !=
      trajectories_.size()) {
    throw std::invalid_argument(
        "weighted geometry point count does not match trajectories");
  }
  if (static_cast<WeightIndex>(station_count_) !=
      station_positions_.size()) {
    throw std::invalid_argument(
        "weighted geometry station count does not match centers");
  }
  if (!std::isfinite(domain_end_) || domain_end_ <= 0.0) {
    throw std::invalid_argument(
        "weighted geometry T_end must be finite and positive");
  }

  for (WeightIndex station = 0U;
       station < station_positions_.size();
       ++station) {
    if (!finite_point(station_positions_[station])) {
      throw std::invalid_argument(
          "weighted geometry received an invalid center");
    }
  }

  for (WeightIndex point = 0U;
       point < trajectories_.size();
       ++point) {
    const auto& trajectory = trajectories_[point];
    if (trajectory.t_breaks.size() < 2U ||
        trajectory.t_breaks.size() !=
            trajectory.waypoints.size()) {
      throw std::invalid_argument(
          "weighted geometry trajectory breakpoints/waypoints are inconsistent");
    }
    if (trajectory.t_breaks.front() != 0.0) {
      throw std::invalid_argument(
          "weighted geometry trajectory must start at 0");
    }
    if (trajectory.t_breaks.back() != domain_end_) {
      throw std::invalid_argument(
          "weighted geometry trajectory end must equal T_end");
    }
    for (WeightIndex index = 0U;
         index < trajectory.t_breaks.size();
         ++index) {
      const double breakpoint = trajectory.t_breaks[index];
      if (!std::isfinite(breakpoint) ||
          !finite_point(trajectory.waypoints[index])) {
        throw std::invalid_argument(
            "weighted geometry trajectory contains non-finite data");
      }
      if (index > 0U &&
          breakpoint <=
              trajectory.t_breaks[index - 1U]) {
        throw std::invalid_argument(
            "weighted geometry trajectory breakpoints are not strictly increasing");
      }
    }
  }
}

WeightIndex WeightedGeometry::find_segment(double time) const {
  if (!std::isfinite(time) ||
      time < domain_start_ ||
      time > domain_end_) {
    throw std::out_of_range(
        "weighted geometry time is outside [0,T]");
  }
  for (WeightIndex point = 0U;
       point < trajectories_.size();
       ++point) {
    const auto& trajectory = trajectories_[point];
    if (time < trajectory.t_breaks.front() ||
        time > trajectory.t_breaks.back()) {
      throw std::invalid_argument(
          "weighted geometry trajectory domain is inconsistent");
    }
  }
  return 0U;
}

GeometryQuadraticCoefficientsV1
WeightedGeometry::coefficients(
    WeightIndex point,
    WeightIndex station,
    double segment_start,
    double segment_end) const {
  return coefficients_on(point, station,
                         segment_start, segment_end);
}

GeometryQuadraticCoefficientsV1
WeightedGeometry::coefficients_on(
    WeightIndex point,
    WeightIndex station,
    double segment_start,
    double segment_end) const {
  if (point >= trajectories_.size()) {
    throw std::out_of_range(
        "weighted geometry point index is out of range");
  }
  if (station >= station_positions_.size()) {
    throw std::out_of_range(
        "weighted geometry station index is out of range");
  }
  if (!std::isfinite(segment_start) ||
      !std::isfinite(segment_end) ||
      segment_start < domain_start_ ||
      segment_end > domain_end_ ||
      segment_start >= segment_end) {
    throw std::invalid_argument(
        "weighted geometry coefficient segment is invalid");
  }

  const auto& trajectory = trajectories_[point];
  const double midpoint =
      segment_start +
      (segment_end - segment_start) / 2.0;
  WeightIndex segment = 0U;
  bool found = false;
  for (WeightIndex index = 0U;
       index + 1U < trajectory.t_breaks.size();
       ++index) {
    if (midpoint >= trajectory.t_breaks[index] &&
        midpoint <= trajectory.t_breaks[index + 1U]) {
      segment = index;
      found = true;
      break;
    }
  }
  if (!found) {
    throw std::invalid_argument(
        "weighted geometry segment is outside trajectory breakpoints");
  }

  const double segment_start_time =
      trajectory.t_breaks[segment];
  const double segment_end_time =
      trajectory.t_breaks[segment + 1U];
  const double duration =
      segment_end_time - segment_start_time;
  if (!std::isfinite(duration) ||
      duration <= 0.0) {
    throw std::invalid_argument(
        "weighted geometry trajectory segment duration is invalid");
  }

  const Point start_position =
      trajectory.waypoints[segment];
  const Point end_position =
      trajectory.waypoints[segment + 1U];
  const Point velocity =
      (end_position - start_position) *
      (1.0 / duration);

  // Absolute-time coefficients:
  // p(t)-c = (p_start-c-v*t_start) + v*t.
  const Point origin =
      start_position -
      station_positions_[station] -
      velocity * segment_start_time;

  GeometryQuadraticCoefficientsV1 coefficient;
  coefficient.segment_start = segment_start;
  coefficient.segment_end = segment_end;
  coefficient.a = squared_norm(velocity);
  coefficient.b = 2.0 * velocity.dot(origin);
  coefficient.c = squared_norm(origin);
  if (!std::isfinite(coefficient.a) ||
      !std::isfinite(coefficient.b) ||
      !std::isfinite(coefficient.c)) {
    throw std::overflow_error(
        "weighted geometry quadratic coefficients are not finite");
  }
  return coefficient;
}

DAtTimeMatrixV1 WeightedGeometry::d_at(double time) const {
  if (!std::isfinite(time) ||
      time < domain_start_ ||
      time > domain_end_) {
    throw std::out_of_range(
        "weighted geometry time is outside [0,T]");
  }
  std::vector<Weight> weights(
      static_cast<WeightIndex>(station_count_) *
      static_cast<WeightIndex>(point_count_),
      0.0);

  for (WeightIndex point = 0U;
       point < trajectories_.size();
       ++point) {
    const Point position =
        trajectories_[point].position(time);
    for (WeightIndex station = 0U;
         station < station_positions_.size();
         ++station) {
      const Point difference =
          position - station_positions_[station];
      weights[station * point_count_ + point] =
          squared_norm(difference);
    }
  }
  WeightMatrix matrix(
      static_cast<WeightIndex>(station_count_),
      static_cast<WeightIndex>(point_count_),
      std::move(weights));
  DAtTimeMatrixV1 result;
  result.time = time;
  result.matrix = std::move(matrix);
  return result;
}

IntervalDistanceBoundsV1 WeightedGeometry::bounds_on(
    double interval_start,
    double interval_end) const {
  if (!std::isfinite(interval_start) ||
      !std::isfinite(interval_end) ||
      interval_start < domain_start_ ||
      interval_end > domain_end_ ||
      interval_start > interval_end) {
    throw std::invalid_argument(
        "weighted geometry interval is outside [0,T]");
  }

  std::vector<Weight> lower_weights(
      static_cast<WeightIndex>(station_count_) *
      static_cast<WeightIndex>(point_count_),
      0.0);
  std::vector<Weight> upper_weights(
      static_cast<WeightIndex>(station_count_) *
      static_cast<WeightIndex>(point_count_),
      0.0);

  for (WeightIndex point = 0U;
       point < trajectories_.size();
       ++point) {
    for (WeightIndex station = 0U;
         station < station_positions_.size();
         ++station) {
      Weight minimum =
          std::numeric_limits<Weight>::infinity();
      Weight maximum =
          -std::numeric_limits<Weight>::infinity();

      std::vector<double> cuts;
      cuts.push_back(interval_start);
      for (const double breakpoint : common_partition_) {
        if (breakpoint > interval_start &&
            breakpoint < interval_end) {
          cuts.push_back(breakpoint);
        }
      }
      cuts.push_back(interval_end);
      std::sort(cuts.begin(), cuts.end());
      cuts.erase(std::unique(cuts.begin(), cuts.end()),
                 cuts.end());

      for (WeightIndex cut = 0U;
           cut + 1U < cuts.size();
           ++cut) {
        const double sub_start = cuts[cut];
        const double sub_end = cuts[cut + 1U];
        if (sub_end <= sub_start) {
          continue;
        }
        const auto coefficient =
            coefficients_on(point, station,
                            sub_start, sub_end);
        const Weight start_value =
            evaluate_quadratic(coefficient, sub_start);
        const Weight end_value =
            evaluate_quadratic(coefficient, sub_end);
        minimum = std::min({minimum, start_value, end_value});
        maximum = std::max({maximum, start_value, end_value});

        if (coefficient.a > 0.0) {
          const double vertex =
              -coefficient.b /
              (2.0 * coefficient.a);
          if (vertex >= sub_start &&
              vertex <= sub_end) {
            const Weight vertex_value =
                evaluate_quadratic(coefficient, vertex);
            minimum =
                std::min(minimum, vertex_value);
          }
        } else if (coefficient.a < 0.0) {
          const double vertex =
              -coefficient.b /
              (2.0 * coefficient.a);
          if (vertex >= sub_start &&
              vertex <= sub_end) {
            const Weight vertex_value =
                evaluate_quadratic(coefficient, vertex);
            maximum =
                std::max(maximum, vertex_value);
          }
        }

        if (!std::isfinite(minimum) ||
            !std::isfinite(maximum)) {
          throw std::overflow_error(
              "weighted geometry bound is not finite");
        }
      }

      lower_weights[station * point_count_ + point] = minimum;
      upper_weights[station * point_count_ + point] = maximum;
    }
  }

  WeightMatrix lower_matrix(
      static_cast<WeightIndex>(station_count_),
      static_cast<WeightIndex>(point_count_),
      std::move(lower_weights));
  WeightMatrix upper_matrix(
      static_cast<WeightIndex>(station_count_),
      static_cast<WeightIndex>(point_count_),
      std::move(upper_weights));

  IntervalDistanceBoundsV1 bounds;
  bounds.interval_start = interval_start;
  bounds.interval_end = interval_end;
  bounds.lower.matrix = std::move(lower_matrix);
  bounds.upper.matrix = std::move(upper_matrix);
  return bounds;
}

}  // namespace kdc
