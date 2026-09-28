#include "test_utils.hpp"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

namespace {
std::filesystem::path temporary_path(const std::string& suffix) {
  const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
  return std::filesystem::temp_directory_path() /
         ("kdc-" + std::to_string(stamp) + suffix);
}

kdc::Instance sample_instance() {
  kdc::Instance instance;
  instance.id = 17;
  instance.name = "round-trip";
  instance.n = 2;
  instance.m = 2;
  instance.T_end = 2.0;
  instance.stations = {{3, {10.0, 20.0}}, {4, {90.0, 80.0}}};
  instance.trajectories.emplace_back(
      std::vector<double>{0.0, 0.5, 2.0},
      std::vector<kdc::Point>{{0.0, 0.0}, {5.0, 10.0}, {20.0, 40.0}});
  instance.trajectories.emplace_back(
      std::vector<double>{0.0, 2.0},
      std::vector<kdc::Point>{{100.0, 100.0}, {50.0, 25.0}});
  return instance;
}

void write_text(const std::filesystem::path& path, const std::string& contents) {
  std::ofstream output(path);
  REQUIRE(output.good());
  output << contents;
  REQUIRE(output.good());
}
}

TEST_CASE("DatasetReader handles JSON and simple datasets") {
  SECTION("json round-trip") {
    const auto path = temporary_path("-round-trip.json");
    const auto instance = sample_instance();
    kdc::DatasetReader::write_json(instance, path.string());
    const auto loaded = kdc::DatasetReader::read_json(path.string());

    REQUIRE(loaded.id == instance.id);
    REQUIRE(loaded.name == instance.name);
    REQUIRE(loaded.n == instance.n);
    REQUIRE(loaded.m == instance.m);
    REQUIRE(kdc::test::near(loaded.T_end, instance.T_end));
    REQUIRE(loaded.stations.size() == instance.stations.size());
    for (kdc::Index index = 0; index < instance.stations.size(); ++index) {
      REQUIRE(loaded.stations[index].id == instance.stations[index].id);
      REQUIRE(kdc::test::near(loaded.stations[index].pos.x,
                              instance.stations[index].pos.x));
      REQUIRE(kdc::test::near(loaded.stations[index].pos.y,
                              instance.stations[index].pos.y));
    }
    REQUIRE(loaded.trajectories.size() == instance.trajectories.size());
    for (kdc::Index trajectory_index = 0;
         trajectory_index < instance.trajectories.size(); ++trajectory_index) {
      const auto& expected = instance.trajectories[trajectory_index];
      const auto& actual = loaded.trajectories[trajectory_index];
      REQUIRE(actual.t_breaks.size() == expected.t_breaks.size());
      REQUIRE(actual.waypoints.size() == expected.waypoints.size());
      for (kdc::Index index = 0; index < expected.t_breaks.size(); ++index) {
        REQUIRE(kdc::test::near(actual.t_breaks[index],
                                expected.t_breaks[index]));
        REQUIRE(kdc::test::near(actual.waypoints[index].x,
                                expected.waypoints[index].x));
        REQUIRE(kdc::test::near(actual.waypoints[index].y,
                                expected.waypoints[index].y));
      }
    }
    std::filesystem::remove(path);
  }

  SECTION("simple format") {
    const auto path = temporary_path("-simple.txt");
    write_text(path, "3 2\n0 0\n100 100\n0 0 100 100\n"
                     "50 0 50 100\n100 0 0 100\n");
    const auto loaded = kdc::DatasetReader::read_simple(path.string());
    REQUIRE(loaded.n == 3);
    REQUIRE(loaded.m == 2);
    REQUIRE(loaded.stations[0].pos == kdc::Point(0.0, 0.0));
    REQUIRE(loaded.stations[1].pos == kdc::Point(100.0, 100.0));
    REQUIRE(loaded.trajectories[0].position(1.0) ==
            kdc::Point(100.0, 100.0));
    REQUIRE(loaded.trajectories[1].position(0.5) ==
            kdc::Point(50.0, 50.0));
    REQUIRE(loaded.trajectories[2].position(1.0) ==
            kdc::Point(0.0, 100.0));
    std::filesystem::remove(path);
  }

  SECTION("invalid json throws") {
    const auto path = temporary_path("-missing-stations.json");
    write_text(path,
               R"({"id":0,"name":"invalid","T_end":1.0,"trajectories":[]})");
    REQUIRE_THROWS_AS(kdc::DatasetReader::read_json(path.string()),
                      std::runtime_error);
    std::filesystem::remove(path);
  }

  SECTION("invalid t_breaks throws") {
    const auto path = temporary_path("-invalid-breaks.json");
    write_text(path,
               R"({"id":0,"name":"invalid","T_end":1.0,"stations":[],"trajectories":[{"t_breaks":[0.0,0.8,0.5,1.0],"waypoints":[{"x":0,"y":0},{"x":1,"y":1},{"x":2,"y":2},{"x":3,"y":3}]}]})");
    REQUIRE_THROWS_AS(kdc::DatasetReader::read_json(path.string()),
                      std::runtime_error);
    std::filesystem::remove(path);
  }

  SECTION("random deterministic") {
    const auto first = kdc::DatasetReader::generate_random(12, 5, 7U);
    const auto second = kdc::DatasetReader::generate_random(12, 5, 7U);
    REQUIRE(first.id == second.id);
    REQUIRE(first.name == second.name);
    REQUIRE(first.n == second.n);
    REQUIRE(first.m == second.m);
    REQUIRE(first.T_end == second.T_end);
    REQUIRE(first.stations.size() == second.stations.size());
    REQUIRE(first.trajectories.size() == second.trajectories.size());
    for (kdc::Index index = 0; index < first.stations.size(); ++index) {
      REQUIRE(first.stations[index].id == second.stations[index].id);
      REQUIRE(first.stations[index].pos == second.stations[index].pos);
    }
    for (kdc::Index trajectory_index = 0;
         trajectory_index < first.trajectories.size(); ++trajectory_index) {
      const auto& lhs = first.trajectories[trajectory_index];
      const auto& rhs = second.trajectories[trajectory_index];
      REQUIRE(lhs.t_breaks == rhs.t_breaks);
      REQUIRE(lhs.waypoints.size() == rhs.waypoints.size());
      for (kdc::Index index = 0; index < lhs.waypoints.size(); ++index) {
        REQUIRE(lhs.waypoints[index] == rhs.waypoints[index]);
      }
    }
  }

  SECTION("generate_random bounds") {
    const auto instance = kdc::DatasetReader::generate_random(100, 20, 15U);
    const auto in_bounds = [](const kdc::Point& point) {
      return point.x >= 0.0 && point.x <= 100.0 && point.y >= 0.0 &&
             point.y <= 100.0;
    };
    for (const auto& station : instance.stations) {
      REQUIRE(in_bounds(station.pos));
    }
    for (const auto& trajectory : instance.trajectories) {
      REQUIRE(in_bounds(trajectory.waypoints.front()));
      REQUIRE(in_bounds(trajectory.waypoints.back()));
    }
  }
}
