#include "kdc/algorithms/genetic_solver.hpp"

#include "kdc/logging.hpp"
#include "kdc/stationary.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

namespace kdc {
namespace {
using Clock = std::chrono::steady_clock;

StaticSolution empty_solution(int station_count, const std::string& solver_name,
                              bool feasible) {
  StaticSolution solution;
  solution.supporting_point.assign(static_cast<Index>(station_count), -1);
  solution.radius.assign(static_cast<Index>(station_count), 0.0);
  solution.feasible = feasible;
  solution.lower_bound = 0.0;
  solution.upper_bound =
      feasible ? 0.0 : std::numeric_limits<double>::infinity();
  solution.solver_name = solver_name;
  return solution;
}
}

GeneticSolver::GeneticSolver() : GeneticSolver(Config{}) {}

GeneticSolver::GeneticSolver(Config config) : config_(config) {
  if (config_.population_size <= 0 || config_.generations < 0 ||
      !std::isfinite(config_.crossover_rate) ||
      config_.crossover_rate < 0.0 || config_.crossover_rate > 1.0 ||
      !std::isfinite(config_.mutation_rate) ||
      config_.mutation_rate < 0.0 || config_.mutation_rate > 1.0 ||
      config_.tournament_size <= 0 ||
      !std::isfinite(config_.elitism_fraction) ||
      config_.elitism_fraction < 0.0 || config_.elitism_fraction > 1.0) {
    throw std::invalid_argument("genetic solver configuration is invalid");
  }
}

double GeneticSolver::evaluate(
    const Individual& individual,
    const std::vector<std::vector<double>>& distances, int point_count,
    int station_count) const {
  std::vector<double> radii(static_cast<Index>(station_count), 0.0);
  for (int point = 0; point < point_count; ++point) {
    const int station = individual.assignment[static_cast<Index>(point)];
    if (station < 0 || station >= station_count) {
      throw std::invalid_argument(
          "genetic individual contains an invalid station assignment");
    }
    const Index station_index = static_cast<Index>(station);
    radii[station_index] =
        std::max(radii[station_index],
                 distances[static_cast<Index>(point)][station_index]);
  }
  double squared_radius_sum = 0.0;
  for (const double radius : radii) {
    squared_radius_sum += radius * radius;
  }
  return std::acos(-1.0) * squared_radius_sum;
}

GeneticSolver::Individual GeneticSolver::tournament_select(
    const std::vector<Individual>& population, int tournament_size,
    std::mt19937& random) const {
  std::uniform_int_distribution<Index> choose(0U, population.size() - 1U);
  Individual best = population[choose(random)];
  for (int candidate = 1; candidate < tournament_size; ++candidate) {
    const Individual& contender = population[choose(random)];
    if (contender.cost < best.cost) {
      best = contender;
    }
  }
  return best;
}

GeneticSolver::Individual GeneticSolver::uniform_crossover(
    const Individual& first, const Individual& second,
    std::mt19937& random) const {
  Individual child;
  child.assignment.resize(first.assignment.size());
  std::bernoulli_distribution choose_first(0.5);
  for (Index point = 0; point < child.assignment.size(); ++point) {
    child.assignment[point] =
        choose_first(random) ? first.assignment[point]
                             : second.assignment[point];
  }
  return child;
}

void GeneticSolver::mutate(Individual& individual, int station_count,
                           std::mt19937& random) const {
  if (individual.assignment.empty() || station_count <= 0) {
    return;
  }
  std::uniform_int_distribution<Index> choose_point(
      0U, individual.assignment.size() - 1U);
  std::uniform_int_distribution<int> choose_station(0, station_count - 1);
  individual.assignment[choose_point(random)] = choose_station(random);
}

StaticSolution GeneticSolver::solve(const Instance& instance, double time) {
  if (!std::isfinite(time) || !std::isfinite(instance.T_end) ||
      instance.T_end <= 0.0 || time < 0.0 || time > instance.T_end) {
    throw std::out_of_range("genetic solve time is outside [0, T_end]");
  }
  if (instance.n < 0 || instance.m < 0 ||
      static_cast<Index>(instance.n) != instance.trajectories.size() ||
      static_cast<Index>(instance.m) != instance.stations.size()) {
    throw std::invalid_argument(
        "genetic solver received inconsistent instance dimensions");
  }
  LOG_DEBUG("GA::solve t={:.6f} pop={} gens={}", time,
            config_.population_size, config_.generations);
  const auto started = Clock::now();
  if (instance.n == 0) {
    StaticSolution result = empty_solution(instance.m, name(), true);
    result.solve_time_sec =
        std::chrono::duration<double>(Clock::now() - started).count();
    return result;
  }
  if (instance.m == 0) {
    StaticSolution result = empty_solution(instance.m, name(), false);
    result.solve_time_sec =
        std::chrono::duration<double>(Clock::now() - started).count();
    return result;
  }

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

  const StaticAssignment nearest =
      StationarySolver::solve_nn(instance, time);
  Individual nearest_individual;
  nearest_individual.assignment.assign(static_cast<Index>(instance.n), -1);
  for (int point = 0; point < instance.n; ++point) {
    const Point position =
        instance.trajectories[static_cast<Index>(point)].position(time);
    for (int station = 0; station < instance.m; ++station) {
      const Index station_index = static_cast<Index>(station);
      if (nearest.supporting_point[station_index] < 0) {
        continue;
      }
      if ((instance.stations[station_index].pos - position).norm() <=
          nearest.radius[station_index] + 1e-9) {
        nearest_individual.assignment[static_cast<Index>(point)] = station;
        break;
      }
    }
    if (nearest_individual.assignment[static_cast<Index>(point)] < 0) {
      throw std::runtime_error("nearest-neighbor solution leaves a point uncovered");
    }
  }
  nearest_individual.cost =
      evaluate(nearest_individual, distances, instance.n, instance.m);

  std::mt19937 random(config_.seed);
  std::uniform_real_distribution<double> uniform(0.0, 1.0);
  std::uniform_int_distribution<int> choose_station(0, instance.m - 1);
  std::vector<Individual> population;
  population.reserve(static_cast<Index>(config_.population_size));
  population.push_back(nearest_individual);
  for (int member = 1; member < config_.population_size; ++member) {
    Individual individual;
    individual.assignment.resize(static_cast<Index>(instance.n));
    for (int point = 0; point < instance.n; ++point) {
      individual.assignment[static_cast<Index>(point)] = choose_station(random);
    }
    individual.cost = evaluate(individual, distances, instance.n, instance.m);
    population.push_back(std::move(individual));
  }
  Individual best = nearest_individual;

  for (int generation = 0; generation < config_.generations; ++generation) {
    std::sort(population.begin(), population.end(),
              [](const Individual& lhs, const Individual& rhs) {
                return lhs.cost < rhs.cost;
              });
    const int elite_count = std::min(
        config_.population_size,
        std::max(1, static_cast<int>(config_.elitism_fraction *
                                     static_cast<double>(population.size()))));
    std::vector<Individual> next_population;
    next_population.reserve(static_cast<Index>(config_.population_size));
    next_population.insert(
        next_population.end(), population.begin(),
        population.begin() + static_cast<std::ptrdiff_t>(elite_count));
    while (static_cast<int>(next_population.size()) <
           config_.population_size) {
      const Individual first = tournament_select(
          population, config_.tournament_size, random);
      const Individual second = tournament_select(
          population, config_.tournament_size, random);
      Individual child =
          uniform(random) < config_.crossover_rate
              ? uniform_crossover(first, second, random)
              : first;
      if (uniform(random) < config_.mutation_rate) {
        mutate(child, instance.m, random);
      }
      child.cost = evaluate(child, distances, instance.n, instance.m);
      next_population.push_back(std::move(child));
    }
    population = std::move(next_population);
    for (const Individual& individual : population) {
      if (individual.cost < best.cost) {
        best = individual;
      }
    }
    LOG_TRACE("GA gen {}: best={:.9f}", generation, best.cost);
  }

  std::vector<int> support(static_cast<Index>(instance.m), -1);
  std::vector<double> radius(static_cast<Index>(instance.m), 0.0);
  for (int point = 0; point < instance.n; ++point) {
    const int station = best.assignment[static_cast<Index>(point)];
    const Index station_index = static_cast<Index>(station);
    const double distance =
        distances[static_cast<Index>(point)][station_index];
    if (support[station_index] < 0 || distance > radius[station_index]) {
      support[station_index] = point;
      radius[station_index] = distance;
    }
  }

  double lower_bound = 0.0;
  const double pi = std::acos(-1.0);
  for (int point = 0; point < instance.n; ++point) {
    double nearest_distance = std::numeric_limits<double>::infinity();
    for (int station = 0; station < instance.m; ++station) {
      nearest_distance =
          std::min(nearest_distance,
                   distances[static_cast<Index>(point)]
                            [static_cast<Index>(station)]);
    }
    lower_bound =
        std::max(lower_bound, pi * nearest_distance * nearest_distance);
  }
  StaticSolution result;
  result.supporting_point = std::move(support);
  result.radius = std::move(radius);
  result.cost = best.cost;
  result.feasible = true;
  result.lower_bound = lower_bound;
  result.upper_bound = result.cost;
  result.solve_time_sec =
      std::chrono::duration<double>(Clock::now() - started).count();
  result.solver_name = name();
  LOG_INFO("GA: cost={:.9f} gens={}", result.cost, config_.generations);
  return result;
}
}
