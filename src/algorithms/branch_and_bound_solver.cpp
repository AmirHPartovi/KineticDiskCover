#include "kdc/algorithms/branch_and_bound_solver.hpp"

#include "kdc/algorithms/nn_static_solver.hpp"
#include "kdc/candidate.hpp"
#include "kdc/logging.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <utility>

namespace kdc {
namespace {
using Clock = std::chrono::steady_clock;
constexpr double kTolerance = 1e-9;

StaticSolution infeasible_solution(int station_count,
                                   const std::string& solver_name) {
  StaticSolution result;
  result.supporting_point.assign(static_cast<Index>(station_count), -1);
  result.radius.assign(static_cast<Index>(station_count), 0.0);
  result.feasible = false;
  result.lower_bound = 0.0;
  result.upper_bound = std::numeric_limits<double>::infinity();
  result.solver_name = solver_name;
  return result;
}
}

BranchAndBoundSolver::BranchAndBoundSolver(ILPSolver* ilp)
    : BranchAndBoundSolver(ilp, Config{}) {}

BranchAndBoundSolver::BranchAndBoundSolver(ILPSolver* ilp, Config config)
    : ilp_(ilp), config_(config) {
  if (!std::isfinite(config_.time_limit_sec) ||
      config_.time_limit_sec <= 0.0 || !std::isfinite(config_.gap_target) ||
      config_.gap_target < 0.0 || config_.gap_target >= 1.0 ||
      config_.node_limit == 0U) {
    throw std::invalid_argument(
        "branch-and-bound solver configuration is invalid");
  }
}

void BranchAndBoundSolver::set_time_limit(double time_limit_sec) {
  if (!std::isfinite(time_limit_sec) || time_limit_sec <= 0.0) {
    throw std::invalid_argument(
        "branch-and-bound time limit must be positive");
  }
  config_.time_limit_sec = time_limit_sec;
}

int BranchAndBoundSolver::select_branching_point(
    const BBNode& node,
    const std::vector<std::vector<double>>& distances) const {
  if (!config_.use_presolve) {
    return node.remaining.empty() ? -1 : node.remaining.front();
  }
  int selected = -1;
  int fewest_choices = std::numeric_limits<int>::max();
  double largest_nearest_distance = -1.0;
  for (const int point : node.remaining) {
    double nearest_distance = std::numeric_limits<double>::infinity();
    for (int station = 0; station < static_cast<int>(node.current_radius.size());
         ++station) {
      nearest_distance =
          std::min(nearest_distance,
                   distances[static_cast<Index>(point)]
                            [static_cast<Index>(station)]);
    }
    int choices = 0;
    for (int station = 0; station < static_cast<int>(node.current_radius.size());
         ++station) {
      if (distances[static_cast<Index>(point)][static_cast<Index>(station)] <=
          2.0 * nearest_distance + kTolerance) {
        ++choices;
      }
    }
    if (choices < fewest_choices ||
        (choices == fewest_choices &&
         nearest_distance > largest_nearest_distance)) {
      selected = point;
      fewest_choices = choices;
      largest_nearest_distance = nearest_distance;
    }
  }
  return selected;
}

double BranchAndBoundSolver::compute_lower_bound(
    const BBNode& node,
    const std::vector<std::vector<double>>& distances, int station_count,
    int point_count, double time, const Instance& instance) {
  (void)point_count;
  (void)time;
  double incremental_lower_bound = 0.0;
  for (const int point : node.remaining) {
    double least_increment = std::numeric_limits<double>::infinity();
    for (int station = 0; station < station_count; ++station) {
      const double distance =
          distances[static_cast<Index>(point)][static_cast<Index>(station)];
      const double old_radius = node.current_radius[static_cast<Index>(station)];
      const double increment =
          std::max(0.0, distance * distance - old_radius * old_radius);
      least_increment = std::min(least_increment, increment);
    }
    incremental_lower_bound =
        std::max(incremental_lower_bound, least_increment);
  }
  const double pi = std::acos(-1.0);
  double lower_bound = std::max(
      node.partial_cost + pi * incremental_lower_bound,
      global_lp_lower_bound_);
  if (config_.use_lp_lower_bound && ilp_ != nullptr && node.depth == 0 &&
      config_.time_limit_sec >= 30.0 && point_count > 0 &&
      station_count > 0) {
    global_lp_lower_bound_ =
        compute_lp_lower_bound(node, distances, station_count, point_count,
                               time, instance);
    lower_bound = std::max(lower_bound, global_lp_lower_bound_);
  }
  return lower_bound;
}

double BranchAndBoundSolver::compute_lp_lower_bound(
    const BBNode& node,
    const std::vector<std::vector<double>>& distances, int station_count,
    int point_count, double time, const Instance& instance) const {
  (void)node;
  (void)distances;
  (void)station_count;
  const auto disks = CandidateSet::build(instance);
  const auto coverage = CandidateSet::build_coverage(instance, disks, time);
  Eigen::VectorXd costs(static_cast<Eigen::Index>(disks.size()));
  Eigen::SparseMatrix<double> constraints(point_count,
                                           static_cast<int>(disks.size()));
  Eigen::VectorXd rhs = Eigen::VectorXd::Ones(point_count);
  std::vector<Eigen::Triplet<double>> entries;
  entries.reserve(coverage.col_idx.size());
  for (int point = 0; point < coverage.num_points; ++point) {
    const int begin = coverage.row_ptr[static_cast<Index>(point)];
    const int end = coverage.row_ptr[static_cast<Index>(point) + 1U];
    for (int offset = begin; offset < end; ++offset) {
      entries.emplace_back(
          point, coverage.col_idx[static_cast<Index>(offset)], 1.0);
    }
  }
  constraints.setFromTriplets(entries.begin(), entries.end());
  const double pi = std::acos(-1.0);
  for (Index disk = 0; disk < disks.size(); ++disk) {
    const auto& candidate = disks[disk];
    const double distance =
        distances[static_cast<Index>(candidate.supporting_point)]
                 [static_cast<Index>(candidate.station_id)];
    costs[static_cast<Eigen::Index>(disk)] = pi * distance * distance;
  }
  const ILPResult result =
      ilp_->solve(costs, constraints, rhs, {}, 30.0, 0.0);
  if (result.status != ILPResult::Status::OPTIMAL &&
      result.status != ILPResult::Status::FEASIBLE &&
      result.status != ILPResult::Status::TIME_LIMIT) {
    throw std::runtime_error("branch-and-bound LP lower-bound solve failed: " +
                             result.solver_message);
  }
  if (!std::isfinite(result.lower_bound) || result.lower_bound < 0.0) {
    throw std::runtime_error(
        "branch-and-bound LP returned an invalid lower bound");
  }
  return result.lower_bound;
}

StaticSolution BranchAndBoundSolver::solve(const Instance& instance,
                                           double time) {
  if (!std::isfinite(time) || !std::isfinite(instance.T_end) ||
      instance.T_end <= 0.0 || time < 0.0 || time > instance.T_end) {
    throw std::out_of_range(
        "branch-and-bound solve time is outside [0, T_end]");
  }
  if (instance.n < 0 || instance.m < 0 ||
      static_cast<Index>(instance.n) != instance.trajectories.size() ||
      static_cast<Index>(instance.m) != instance.stations.size()) {
    throw std::invalid_argument(
        "branch-and-bound received inconsistent instance dimensions");
  }
  LOG_DEBUG("BnB::solve t={:.6f} n={} m={}", time, instance.n, instance.m);
  if (instance.n == 0) {
    StaticSolution trivial;
    trivial.supporting_point.assign(static_cast<Index>(instance.m), -1);
    trivial.radius.assign(static_cast<Index>(instance.m), 0.0);
    trivial.feasible = true;
    trivial.lower_bound = 0.0;
    trivial.upper_bound = 0.0;
    trivial.solver_name = name();
    return trivial;
  }
  if (instance.m == 0) {
    return infeasible_solution(instance.m, name());
  }
  global_lp_lower_bound_ = 0.0;

  const auto start = Clock::now();
  std::vector<std::vector<double>> distances(
      static_cast<Index>(instance.n),
      std::vector<double>(static_cast<Index>(instance.m), 0.0));
  for (int point = 0; point < instance.n; ++point) {
    const Point position =
        instance.trajectories[static_cast<Index>(point)].position(time);
    for (int station = 0; station < instance.m; ++station) {
      distances[static_cast<Index>(point)][static_cast<Index>(station)] =
          (instance.stations[static_cast<Index>(station)].pos - position).norm();
    }
  }

  NNStaticSolver nn_solver;
  StaticSolution incumbent = nn_solver.solve(instance, time);
  if (!incumbent.feasible) {
    return infeasible_solution(instance.m, name());
  }
  double best_cost = incumbent.cost;
  std::vector<int> best_assignment(static_cast<Index>(instance.n), -1);
  for (int point = 0; point < instance.n; ++point) {
    for (int station = 0; station < instance.m; ++station) {
      if (distances[static_cast<Index>(point)][static_cast<Index>(station)] <=
          incumbent.radius[static_cast<Index>(station)] + kTolerance) {
        best_assignment[static_cast<Index>(point)] = station;
        break;
      }
    }
    if (best_assignment[static_cast<Index>(point)] < 0) {
      throw std::runtime_error(
          "nearest-neighbor incumbent does not cover every point");
    }
  }

  BBNode root;
  root.assignment.assign(static_cast<Index>(instance.n), -1);
  root.remaining.resize(static_cast<Index>(instance.n));
  std::iota(root.remaining.begin(), root.remaining.end(), 0);
  root.current_radius.assign(static_cast<Index>(instance.m), 0.0);
  root.lower_bound = compute_lower_bound(
      root, distances, instance.m, instance.n, time, instance);

  std::priority_queue<BBNode, std::vector<BBNode>, NodeCompare> best_first;
  std::vector<BBNode> depth_first;
  if (config_.use_best_first) {
    best_first.push(root);
  } else {
    depth_first.push_back(root);
  }
  std::uint64_t nodes_explored = 0U;
  bool terminated_early = false;
  while ((config_.use_best_first && !best_first.empty()) ||
         (!config_.use_best_first && !depth_first.empty())) {
    const double elapsed =
        std::chrono::duration<double>(Clock::now() - start).count();
    if (nodes_explored >= config_.node_limit) {
      LOG_WARN("BnB: node limit {} reached", config_.node_limit);
      terminated_early = true;
      break;
    }
    if (elapsed >= config_.time_limit_sec) {
      LOG_WARN("BnB: time limit {:.4f}s reached", elapsed);
      terminated_early = true;
      break;
    }
    const double queue_lower_bound =
        config_.use_best_first ? best_first.top().lower_bound
                               : std::min_element(
                                     depth_first.begin(), depth_first.end(),
                                     [](const BBNode& lhs, const BBNode& rhs) {
                                       return lhs.lower_bound < rhs.lower_bound;
                                     })
                                     ->lower_bound;
    if (queue_lower_bound >= best_cost * (1.0 - config_.gap_target)) {
      LOG_INFO("BnB: configured gap target reached");
      terminated_early = true;
      break;
    }

    BBNode current;
    if (config_.use_best_first) {
      current = best_first.top();
      best_first.pop();
    } else {
      current = std::move(depth_first.back());
      depth_first.pop_back();
    }
    ++nodes_explored;
    if (current.lower_bound >= best_cost - kTolerance) {
      continue;
    }
    if (current.remaining.empty()) {
      if (current.partial_cost < best_cost) {
        best_cost = current.partial_cost;
        best_assignment = current.assignment;
      }
      continue;
    }

    const int point = select_branching_point(current, distances);
    if (point < 0) {
      throw std::logic_error("branch-and-bound failed to select a point");
    }
    for (int station = 0; station < instance.m; ++station) {
      BBNode child = current;
      child.assignment[static_cast<Index>(point)] = station;
      child.current_radius[static_cast<Index>(station)] =
          std::max(child.current_radius[static_cast<Index>(station)],
                   distances[static_cast<Index>(point)]
                            [static_cast<Index>(station)]);
      double squared_radius_sum = 0.0;
      for (const double radius : child.current_radius) {
        squared_radius_sum += radius * radius;
      }
      child.partial_cost = std::acos(-1.0) * squared_radius_sum;
      const auto position =
          std::find(child.remaining.begin(), child.remaining.end(), point);
      if (position == child.remaining.end()) {
        throw std::logic_error("selected branching point is not remaining");
      }
      child.remaining.erase(position);
      child.depth = current.depth + 1;
      child.lower_bound = compute_lower_bound(
          child, distances, instance.m, instance.n, time, instance);
      if (child.lower_bound < best_cost - kTolerance) {
        if (config_.use_best_first) {
          best_first.push(std::move(child));
        } else {
          depth_first.push_back(std::move(child));
        }
      }
    }
  }

  double lower_bound = best_cost;
  if (config_.use_best_first && !best_first.empty()) {
    lower_bound = std::min(best_cost, best_first.top().lower_bound);
  } else if (!config_.use_best_first && !depth_first.empty()) {
    const auto best_open = std::min_element(
        depth_first.begin(), depth_first.end(),
        [](const BBNode& lhs, const BBNode& rhs) {
          return lhs.lower_bound < rhs.lower_bound;
        });
    lower_bound = std::min(best_cost, best_open->lower_bound);
  }
  if (!terminated_early && best_first.empty() && depth_first.empty()) {
    lower_bound = best_cost;
  }

  StaticSolution result;
  result.supporting_point.assign(static_cast<Index>(instance.m), -1);
  result.radius.assign(static_cast<Index>(instance.m), 0.0);
  for (int point = 0; point < instance.n; ++point) {
    const int station = best_assignment[static_cast<Index>(point)];
    if (station < 0 || station >= instance.m) {
      throw std::runtime_error(
          "branch-and-bound incumbent has an invalid point assignment");
    }
    const Index station_index = static_cast<Index>(station);
    if (result.supporting_point[station_index] == -1 ||
        distances[static_cast<Index>(point)][station_index] >
            result.radius[station_index]) {
      result.supporting_point[station_index] = point;
      result.radius[station_index] =
          distances[static_cast<Index>(point)][station_index];
    }
  }
  const double pi = std::acos(-1.0);
  for (const double radius : result.radius) {
    result.cost += pi * radius * radius;
  }
  result.feasible = true;
  result.lower_bound = std::min(lower_bound, result.cost);
  result.upper_bound = result.cost;
  result.solve_time_sec =
      std::chrono::duration<double>(Clock::now() - start).count();
  result.solver_name = name();
  LOG_INFO("BnB: cost={:.9f} LB={:.9f} nodes={} t={:.4f}s", result.cost,
           result.lower_bound, nodes_explored, result.solve_time_sec);
  return result;
}
}
