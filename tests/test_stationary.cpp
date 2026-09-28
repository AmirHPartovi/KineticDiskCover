#include "test_utils.hpp"

#include "kdc/candidate.hpp"
#include "kdc/kont_solver.hpp"
#include "kdc/stationary.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cmath>
#include <Eigen/Sparse>
#include <vector>

namespace {
struct SmallCoverProblem {
  Eigen::VectorXd costs;
  Eigen::SparseMatrix<double> constraints;
  Eigen::VectorXd rhs;
  std::vector<int> integer_vars;
  double nearest_neighbor_cost{0.0};
};

SmallCoverProblem make_cover_problem(const kdc::Instance& instance) {
  const auto nearest = kdc::StationarySolver::solve_nn(instance, 0.0);
  const auto disks = kdc::CandidateSet::build(instance);
  const auto coverage =
      kdc::CandidateSet::build_coverage(instance, disks, 0.0);

  SmallCoverProblem problem;
  problem.costs.resize(static_cast<Eigen::Index>(disks.size()));
  problem.constraints.resize(instance.n, static_cast<int>(disks.size()));
  problem.rhs = Eigen::VectorXd::Ones(instance.n);
  problem.nearest_neighbor_cost = nearest.cost;
  problem.integer_vars.reserve(disks.size());
  std::vector<Eigen::Triplet<double>> entries;
  for (kdc::Index disk_index = 0; disk_index < disks.size(); ++disk_index) {
    const auto& disk = disks[disk_index];
    const auto& station = instance.stations[
        static_cast<kdc::Index>(disk.station_id)].pos;
    const auto support = instance.trajectories[
        static_cast<kdc::Index>(disk.supporting_point)].position(0.0);
    const double radius = (station - support).norm();
    problem.costs[static_cast<Eigen::Index>(disk_index)] =
        std::acos(-1.0) * radius * radius;
    problem.integer_vars.push_back(static_cast<int>(disk_index));
  }
  for (int point = 0; point < coverage.num_points; ++point) {
    const int begin = coverage.row_ptr[static_cast<kdc::Index>(point)];
    const int end = coverage.row_ptr[static_cast<kdc::Index>(point) + 1U];
    for (int offset = begin; offset < end; ++offset) {
      entries.emplace_back(point, coverage.col_idx[static_cast<kdc::Index>(offset)],
                           1.0);
    }
  }
  problem.constraints.setFromTriplets(entries.begin(), entries.end());
  return problem;
}

SmallCoverProblem make_small_cover_problem() {
  return make_cover_problem(kdc::test::make_dummy_instance(5, 3, 32U));
}
}

TEST_CASE("Nearest-neighbor stationary assignment covers valid instances") {
  const auto instance = kdc::test::make_dummy_instance(10, 5, 104U);
  const auto assignment = kdc::StationarySolver::solve_nn(instance, 0.0);

  SECTION("feasible") {
    REQUIRE(assignment.feasible);
  }

  SECTION("cost nonneg") {
    REQUIRE(assignment.cost >= 0.0);
  }
}

TEST_CASE("Nearest-neighbor stationary assignment matches a simple line") {
  const auto instance = kdc::test::make_instance_linear(
      {{kdc::Point(1, 0), kdc::Point(1, 0)},
       {kdc::Point(2, 0), kdc::Point(2, 0)},
       {kdc::Point(9, 0), kdc::Point(9, 0)},
       {kdc::Point(11, 0), kdc::Point(11, 0)},
       {kdc::Point(19, 0), kdc::Point(19, 0)}},
      {{0, 0}, {10, 0}, {20, 0}});
  const auto assignment = kdc::StationarySolver::solve_nn(instance, 0.0);

  SECTION("simple") {
    REQUIRE(assignment.feasible);
    REQUIRE(assignment.supporting_point ==
            std::vector<int>{1, 2, 4});
    REQUIRE(assignment.radius.size() == 3U);
    REQUIRE(kdc::test::near(assignment.radius[0], 2.0));
    REQUIRE(kdc::test::near(assignment.radius[1], 1.0));
    REQUIRE(kdc::test::near(assignment.radius[2], 1.0));
    REQUIRE(kdc::test::near(assignment.cost, 6.0 * std::acos(-1.0)));
  }

  SECTION("all points covered") {
    for (const auto& trajectory : instance.trajectories) {
      const auto point = trajectory.position(0.0);
      bool covered = false;
      for (kdc::Index station = 0; station < instance.stations.size();
           ++station) {
        if ((point - instance.stations[station].pos).norm() <=
            assignment.radius[station] + 1e-9) {
          covered = true;
        }
      }
      REQUIRE(covered);
    }
  }
}

TEST_CASE("Nearest-neighbor stationary assignment scales to larger instances") {
  const auto instance = kdc::test::make_dummy_instance(500, 25, 67U);
  const auto start = std::chrono::steady_clock::now();
  const auto assignment = kdc::StationarySolver::solve_nn(instance, 0.0);
  const auto elapsed = std::chrono::steady_clock::now() - start;

  SECTION("performance") {
    REQUIRE(assignment.feasible);
    REQUIRE(elapsed < std::chrono::seconds(1));
  }
}

TEST_CASE("ILP solver adapters solve small set-cover models") {
  const auto problem = make_small_cover_problem();

  SECTION("KONT solves small IP") {
    kdc::KontSolver solver;
    const auto result =
        solver.solve(problem.costs, problem.constraints, problem.rhs,
                     problem.integer_vars, 10.0, 0.0);
    REQUIRE(result.status == kdc::ILPResult::Status::OPTIMAL);
    REQUIRE(result.objective <= problem.nearest_neighbor_cost + 1e-8);
  }

  SECTION("KONT lower bound valid") {
    kdc::KontSolver solver;
    const auto result =
        solver.solve(problem.costs, problem.constraints, problem.rhs,
                     problem.integer_vars, 10.0, 0.0);
    REQUIRE(result.status == kdc::ILPResult::Status::OPTIMAL);
    REQUIRE(result.lower_bound <= result.objective + 1e-8);
  }

  SECTION("DummyLP runs") {
    kdc::DummyLPAdapter solver;
    const auto result =
        solver.solve(problem.costs, problem.constraints, problem.rhs,
                     problem.integer_vars, 10.0, 0.0);
    REQUIRE(result.status == kdc::ILPResult::Status::OPTIMAL);
    REQUIRE(result.x.size() == problem.costs.size());
  }
}

TEST_CASE("ILP solver reports an empty-station model infeasible") {
  Eigen::VectorXd costs(0);
  Eigen::SparseMatrix<double> constraints(1, 0);
  Eigen::VectorXd rhs(1);
  rhs[0] = 1.0;
  kdc::KontSolver solver;
  const auto result = solver.solve(costs, constraints, rhs, {}, 1.0, 0.0);

  SECTION("KONT infeasible") {
    REQUIRE(result.status == kdc::ILPResult::Status::INFEASIBLE);
  }
}

TEST_CASE("Stationary IP solution is bounded by nearest-neighbor solution") {
  const auto instance = kdc::test::make_dummy_instance(10, 5, 91U);
  const auto nearest = kdc::StationarySolver::solve_nn(instance, 0.0);
  kdc::KontSolver solver;
  double lower_bound = 0.0;
  kdc::ILPResult::Status status = kdc::ILPResult::Status::ERROR;
  const auto assignment = kdc::StationarySolver::solve_ip(
      instance, 0.0, solver, 10.0, 0.0, &lower_bound, &status);

  SECTION("IP <= NN") {
    REQUIRE(assignment.cost <= nearest.cost + 1e-6);
  }

  SECTION("IP feasible") {
    REQUIRE(assignment.feasible);
  }

  SECTION("lower bound") {
    REQUIRE(lower_bound <= assignment.cost + 1e-6);
  }

  SECTION("status") {
    REQUIRE((status == kdc::ILPResult::Status::OPTIMAL ||
             status == kdc::ILPResult::Status::FEASIBLE));
  }
}
