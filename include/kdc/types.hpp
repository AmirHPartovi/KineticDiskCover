#pragma once
#include <cmath>
#include <cstddef>
#include <string>
#include <stdexcept>
#include <utility>
#include <vector>

namespace kdc {
using Index = std::size_t;
using Value = double;
using Vector = std::vector<Value>;

struct Point {
  Value x{0.0};
  Value y{0.0};

  constexpr Point() = default;
  constexpr Point(Value x_value, Value y_value) noexcept
      : x(x_value), y(y_value) {}

  constexpr Point operator+(const Point& other) const noexcept {
    return {x + other.x, y + other.y};
  }
  constexpr Point operator-(const Point& other) const noexcept {
    return {x - other.x, y - other.y};
  }
  constexpr Point operator*(Value scalar) const noexcept {
    return {x * scalar, y * scalar};
  }
  constexpr Value dot(const Point& other) const noexcept {
    return x * other.x + y * other.y;
  }
  constexpr Value cross(const Point& other) const noexcept {
    return x * other.y - y * other.x;
  }
  constexpr Value norm2() const noexcept { return dot(*this); }
  Value norm() const noexcept { return std::sqrt(norm2()); }

  constexpr bool operator==(const Point& other) const noexcept {
    return x == other.x && y == other.y;
  }
  constexpr bool operator!=(const Point& other) const noexcept {
    return !(*this == other);
  }
};

constexpr Point operator*(Value scalar, const Point& point) noexcept {
  return point * scalar;
}

struct Station {
  int id{-1};
  Point pos{};
};

struct Trajectory {
  std::vector<Value> t_breaks;
  std::vector<Point> waypoints;

  Trajectory() = default;
  Trajectory(std::vector<Value> time_breaks, std::vector<Point> points);

  int segment_index(Value time) const;
  Point position(Value time) const;
};

struct Instance {
  int id{-1};
  std::string name{};
  int n{0};
  int m{0};
  Value T_end{1.0};
  std::vector<Station> stations;
  std::vector<Trajectory> trajectories;
};
}
