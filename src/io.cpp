#include "kdc/io.hpp"

#include "kdc/logging.hpp"

#include <nlohmann/json.hpp>

#include <cmath>
#include <fstream>
#include <iomanip>
#include <random>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace kdc {
namespace {
using Json = nlohmann::json;

bool near(Value lhs, Value rhs, Value tolerance = 1e-9) {
  return std::abs(lhs - rhs) <= tolerance;
}

[[noreturn]] void invalid_instance(const std::string& message) {
  LOG_ERROR("invalid instance: {}", message);
  throw std::runtime_error("invalid instance: " + message);
}

void validate_instance(const Instance& instance) {
  if (!std::isfinite(instance.T_end) || instance.T_end <= 0.0) {
    invalid_instance("T_end must be finite and positive");
  }
  if (instance.n != static_cast<int>(instance.trajectories.size())) {
    invalid_instance("n does not match trajectories.size()");
  }
  if (instance.m != static_cast<int>(instance.stations.size())) {
    invalid_instance("m does not match stations.size()");
  }
  for (const auto& trajectory : instance.trajectories) {
    if (trajectory.t_breaks.size() < 2U) {
      invalid_instance("trajectory t_breaks must contain at least two values");
    }
    if (trajectory.t_breaks.front() != 0.0) {
      invalid_instance("trajectory t_breaks.front() must be 0.0");
    }
    for (Index index = 0; index < trajectory.t_breaks.size(); ++index) {
      if (!std::isfinite(trajectory.t_breaks[index])) {
        invalid_instance("trajectory t_breaks must be finite");
      }
      if (index > 0U &&
          trajectory.t_breaks[index] <= trajectory.t_breaks[index - 1U]) {
        invalid_instance("trajectory t_breaks must be strictly increasing");
      }
    }
    if (!near(trajectory.t_breaks.back(), instance.T_end)) {
      invalid_instance("trajectory t_breaks.back() must equal T_end");
    }
    if (trajectory.waypoints.size() != trajectory.t_breaks.size()) {
      invalid_instance("trajectory waypoints.size() must equal t_breaks.size()");
    }
  }
}

Point parse_point(const Json& json) {
  return {json.at("x").get<Value>(), json.at("y").get<Value>()};
}

Instance read_json_stream(std::istream& input) {
  const Json json = Json::parse(input);
  Instance instance;
  instance.id = json.at("id").get<int>();
  instance.name = json.at("name").get<std::string>();
  instance.T_end = json.at("T_end").get<Value>();
  const auto& stations = json.at("stations");
  const auto& trajectories = json.at("trajectories");
  if (json.contains("m") &&
      json.at("m").get<int>() != static_cast<int>(stations.size())) {
    invalid_instance("stations.size() does not match m");
  }
  if (json.contains("n") &&
      json.at("n").get<int>() != static_cast<int>(trajectories.size())) {
    invalid_instance("trajectories.size() does not match n");
  }
  instance.stations.reserve(stations.size());
  for (const auto& station_json : stations) {
    instance.stations.push_back(
        {station_json.at("id").get<int>(), parse_point(station_json)});
  }
  instance.trajectories.reserve(trajectories.size());
  for (const auto& trajectory_json : trajectories) {
    std::vector<Value> breaks =
        trajectory_json.at("t_breaks").get<std::vector<Value>>();
    std::vector<Point> waypoints;
    const auto& points_json = trajectory_json.at("waypoints");
    waypoints.reserve(points_json.size());
    for (const auto& point_json : points_json) {
      waypoints.push_back(parse_point(point_json));
    }
    Trajectory trajectory;
    trajectory.t_breaks = std::move(breaks);
    trajectory.waypoints = std::move(waypoints);
    instance.trajectories.push_back(std::move(trajectory));
  }
  instance.n = static_cast<int>(instance.trajectories.size());
  instance.m = static_cast<int>(instance.stations.size());
  validate_instance(instance);
  return instance;
}

Value reflect_coordinate(Value coordinate) {
  constexpr Value upper_bound = 100.0;
  constexpr Value period = 2.0 * upper_bound;
  Value reflected = std::fmod(coordinate, period);
  if (reflected < 0.0) {
    reflected += period;
  }
  if (reflected > upper_bound) {
    reflected = period - reflected;
  }
  return reflected;
}
}

bool file_exists(const std::string& path) {
  return static_cast<bool>(std::ifstream(path));
}

Instance DatasetReader::read_json(const std::string& path) {
  LOG_DEBUG("read_json: reading {}", path);
  std::ifstream input(path);
  if (!input) {
    LOG_ERROR("read_json: cannot open {}", path);
    throw std::runtime_error("cannot open JSON instance: " + path);
  }
  try {
    Instance instance = read_json_stream(input);
    LOG_INFO("read_json: loaded {} points, {} stations from {}", instance.n,
             instance.m, path);
    return instance;
  } catch (const Json::exception& error) {
    LOG_ERROR("invalid instance: {}", error.what());
    throw std::runtime_error(std::string("invalid instance: ") + error.what());
  }
}

void DatasetReader::write_json(const Instance& instance,
                               const std::string& path) {
  validate_instance(instance);
  Json json;
  json["id"] = instance.id;
  json["name"] = instance.name;
  json["T_end"] = instance.T_end;
  json["stations"] = Json::array();
  for (const auto& station : instance.stations) {
    json["stations"].push_back(
        {{"id", station.id}, {"x", station.pos.x}, {"y", station.pos.y}});
  }
  json["trajectories"] = Json::array();
  for (const auto& trajectory : instance.trajectories) {
    Json waypoints = Json::array();
    for (const Point& point : trajectory.waypoints) {
      waypoints.push_back({{"x", point.x}, {"y", point.y}});
    }
    json["trajectories"].push_back(
        {{"t_breaks", trajectory.t_breaks}, {"waypoints", waypoints}});
  }

  std::ofstream output(path);
  if (!output) {
    LOG_ERROR("write_json: cannot open {}", path);
    throw std::runtime_error("cannot open JSON output: " + path);
  }
  output << std::setw(4) << json << '\n';
  if (!output) {
    LOG_ERROR("write_json: failed writing {}", path);
    throw std::runtime_error("failed writing JSON output: " + path);
  }
}

Instance DatasetReader::read_simple(const std::string& path) {
  std::ifstream input(path);
  if (!input) {
    LOG_ERROR("read_simple: cannot open {}", path);
    throw std::runtime_error("cannot open simple instance: " + path);
  }
  int n = 0;
  int m = 0;
  if (!(input >> n >> m) || n < 0 || m < 0) {
    invalid_instance("simple format must begin with non-negative n and m");
  }
  Instance instance;
  instance.name = "simple";
  instance.T_end = 1.0;
  instance.m = m;
  instance.n = n;
  instance.stations.reserve(static_cast<Index>(m));
  instance.trajectories.reserve(static_cast<Index>(n));
  for (int index = 0; index < m; ++index) {
    Point station;
    if (!(input >> station.x >> station.y)) {
      invalid_instance("simple format contains an invalid station");
    }
    instance.stations.push_back({index, station});
  }
  for (int index = 0; index < n; ++index) {
    Point start;
    Point end;
    if (!(input >> start.x >> start.y >> end.x >> end.y)) {
      invalid_instance("simple format contains an invalid trajectory");
    }
    instance.trajectories.emplace_back(std::vector<Value>{0.0, 1.0},
                                       std::vector<Point>{start, end});
  }
  std::string trailing;
  if (input >> trailing) {
    invalid_instance("simple format contains unexpected trailing data");
  }
  validate_instance(instance);
  return instance;
}

Instance DatasetReader::generate_random(int n, int m, unsigned seed,
                                        double T_end) {
  if (n < 0 || m < 0) {
    invalid_instance("n and m must be non-negative");
  }
  if (!std::isfinite(T_end) || T_end <= 0.0) {
    invalid_instance("T_end must be finite and positive");
  }

  std::mt19937 rng(seed);
  std::uniform_real_distribution<Value> pos_dist(0.0, 100.0);
  std::uniform_real_distribution<Value> vel_dist(0.1, 1.0);
  std::uniform_real_distribution<Value> angle_dist(
      0.0, 6.283185307179586476925286766559);
  Instance instance;
  instance.name = "random_n" + std::to_string(n) + "_m" +
                  std::to_string(m) + "_seed" + std::to_string(seed);
  instance.n = n;
  instance.m = m;
  instance.T_end = T_end;
  instance.stations.reserve(static_cast<Index>(m));
  instance.trajectories.reserve(static_cast<Index>(n));
  for (int index = 0; index < m; ++index) {
    instance.stations.push_back(
        {index, Point{pos_dist(rng), pos_dist(rng)}});
  }
  for (int index = 0; index < n; ++index) {
    const Point start{pos_dist(rng), pos_dist(rng)};
    const Value angle = angle_dist(rng);
    const Value speed = vel_dist(rng);
    const Point end{
        reflect_coordinate(start.x + speed * T_end * std::cos(angle)),
        reflect_coordinate(start.y + speed * T_end * std::sin(angle))};
    instance.trajectories.emplace_back(
        std::vector<Value>{0.0, T_end}, std::vector<Point>{start, end});
  }
  return instance;
}
}
