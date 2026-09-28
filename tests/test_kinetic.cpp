#include "test_utils.hpp"

#include "kdc/kinetic.hpp"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("KineticCore solves quadratic equations robustly") {
  SECTION("quadratic roots") {
    const auto roots = kdc::KineticCore::solve_quadratic(1.0, -3.0, 2.0);
    REQUIRE(roots.size() == 2U);
    REQUIRE(kdc::test::near(roots[0], 1.0));
    REQUIRE(kdc::test::near(roots[1], 2.0));
  }

  SECTION("no real roots") {
    const auto roots = kdc::KineticCore::solve_quadratic(1.0, 0.0, 1.0);
    REQUIRE(roots.empty());
  }

  SECTION("linear case") {
    const auto roots = kdc::KineticCore::solve_quadratic(0.0, 2.0, -4.0);
    REQUIRE(roots.size() == 1U);
    REQUIRE(kdc::test::near(roots[0], 2.0));
  }

  SECTION("degenerate quadratic") {
    const auto roots = kdc::KineticCore::solve_quadratic(
        1.0, 2.0, 1.0 + 1e-13);
    REQUIRE(roots.size() == 1U);
    REQUIRE(kdc::test::near(roots[0], -1.0));
  }
}

TEST_CASE("KineticCore finds support changes and resolves ties") {
  const auto crossing = kdc::test::make_instance_linear(
      {{kdc::Point(-1, 0), kdc::Point(1, 0)},
       {kdc::Point(0, 0), kdc::Point(0, 0)}},
      {{0, 0}});

  SECTION("support change simple") {
    const auto events =
        kdc::KineticCore::find_support_changes(crossing, 0, 0, 0.0, 1.0,
                                               true);
    REQUIRE(events.size() == 1U);
    REQUIRE(kdc::test::near(events.front().time, 0.5));
    REQUIRE(events.front().station_id == 0);
    REQUIRE(events.front().new_supporting_point == 1);
    REQUIRE(events.front().valid);
    const auto next =
        kdc::KineticCore::find_next_event(crossing, {0}, 0.0, 1.0, true);
    REQUIRE(next.valid);
    REQUIRE(kdc::test::near(next.time, 0.5));
    const auto previous = kdc::KineticCore::find_next_event(
        crossing, {0}, 1.0, 0.0, false);
    REQUIRE(previous.valid);
    REQUIRE(kdc::test::near(previous.time, 0.5));
  }

  SECTION("degeneracy") {
    const auto tied = kdc::test::make_instance_linear(
        {{kdc::Point(-1, 0), kdc::Point(-1, 0)},
         {kdc::Point(0, 0), kdc::Point(2, 0)}},
        {{0, 0}});
    REQUIRE(kdc::KineticCore::resolve_degeneracy(tied, 0, {0, 1}, 0.5) ==
            1);
  }
}

TEST_CASE("KineticCore selects assigned supports for handovers") {
  const auto instance = kdc::test::make_instance_linear(
      {{kdc::Point(-4, 0), kdc::Point(-4, 0)},
       {kdc::Point(-2, 0), kdc::Point(-6, 0)},
       {kdc::Point(5, 0), kdc::Point(5, 0)}},
      {{0, 0}, {10, 0}});

  SECTION("second furthest") {
    REQUIRE(kdc::KineticCore::second_furthest_assigned(
                instance, 0, {0, 1}, 0.0) == 1);
  }

  SECTION("handover simple") {
    const auto events = kdc::KineticCore::find_handovers(
        instance, 0, 1, {0, 2}, 0.0, 1.0, true);
    REQUIRE(events.size() == 1U);
    REQUIRE(kdc::test::near(events.front().time, 0.5));
    REQUIRE(events.front().from_station == 0);
    REQUIRE(events.front().to_station == 1);
    REQUIRE(events.front().point_id == 2);
    REQUIRE(events.front().new_support_from == 1);
    REQUIRE(events.front().new_support_to == 2);
    REQUIRE(events.front().valid);
    const auto next =
        kdc::KineticCore::find_next_handover(instance, {0, 2}, 0.0, 1.0,
                                             true);
    REQUIRE(next.valid);
    REQUIRE(kdc::test::near(next.time, 0.5));
  }

  SECTION("no handover if p3 == -1") {
    const auto one_point = kdc::test::make_instance_linear(
        {{kdc::Point(0, 0), kdc::Point(0, 0)}},
        {{0, 0}, {10, 0}});
    REQUIRE(kdc::KineticCore::find_handovers(
                one_point, 0, 1, {0, 0}, 0.0, 1.0, true)
                .empty());
  }
}
