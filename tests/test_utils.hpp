#pragma once

#include "kdc/io.hpp"
#include "kdc/kont_solver.hpp"
#include "kdc/logging.hpp"
#include "kdc/mock_ilp_solver.hpp"
#include "kdc/solution.hpp"
#include "kdc/types.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <memory>
#include <random>
#include <stdexcept>
#include <utility>
#include <vector>

namespace kdc::test {
inline std::unique_ptr<ILPSolver> make_test_ilp_solver() {
#if defined(KDC_HAS_KONT)
  if (std::getenv("KDC_USE_KONT") != nullptr) {
    return std::make_unique<KontSolver>();
  }
#else
  if (std::getenv("KDC_USE_KONT") != nullptr) {
    LOG_WARN("KDC_USE_KONT requested, but KONT is unavailable");
  }
#endif
  LOG_WARN("KONT not enabled; falling back to MockILPSolver");
  return std::make_unique<MockILPSolver>();
}

inline Instance make_dummy_instance(int n, int m, unsigned seed = 42U) {
  return DatasetReader::generate_random(n, m, seed);
}

inline Instance make_instance_linear(
    const std::vector<std::pair<Point, Point>>& trajectories,
    const std::vector<Point>& stations, double T_end = 1.0) {
  if (!std::isfinite(T_end) || T_end <= 0.0) {
    throw std::invalid_argument("instance end time must be finite and positive");
  }

  Instance instance;
  instance.id = 0;
  instance.name = "linear";
  instance.n = static_cast<int>(trajectories.size());
  instance.m = static_cast<int>(stations.size());
  instance.trajectories.reserve(trajectories.size());
  instance.stations.reserve(stations.size());
  for (Index index = 0; index < stations.size(); ++index) {
    instance.stations.push_back({static_cast<int>(index), stations[index]});
  }
  instance.T_end = T_end;
  for (const auto& trajectory : trajectories) {
    instance.trajectories.emplace_back(
        std::vector<Value>{0.0, T_end},
        std::vector<Point>{trajectory.first, trajectory.second});
  }
  return instance;
}

inline bool near(double a, double b, double rel_tol = 1e-9,
                 double abs_tol = 1e-12) {
  if (a == b) {
    return true;
  }
  if (!std::isfinite(a) || !std::isfinite(b) || rel_tol < 0.0 ||
      abs_tol < 0.0) {
    return false;
  }
  return std::abs(a - b) <=
         std::max(abs_tol, rel_tol * std::max(std::abs(a), std::abs(b)));
}

inline bool solutions_equal(const KineticSolution& a,
                            const KineticSolution& b, double tol = 1e-9) {
  if (!a.is_well_formed() || !b.is_well_formed() ||
      !near(a.intervals.front().t_start, b.intervals.front().t_start, tol) ||
      !near(a.intervals.back().t_end, b.intervals.back().t_end, tol)) {
    return false;
  }

  std::vector<Value> sample_times;
  sample_times.reserve(2U * (a.intervals.size() + b.intervals.size()));
  for (const auto& interval : a.intervals) {
    sample_times.push_back(interval.t_start);
    sample_times.push_back(interval.t_end);
  }
  for (const auto& interval : b.intervals) {
    sample_times.push_back(interval.t_start);
    sample_times.push_back(interval.t_end);
  }
  std::sort(sample_times.begin(), sample_times.end());
  sample_times.erase(
      std::unique(sample_times.begin(), sample_times.end(),
                  [tol](Value lhs, Value rhs) {
                    return near(lhs, rhs, tol);
                  }),
      sample_times.end());

  for (Index index = 0; index < sample_times.size(); ++index) {
    if (!near(a.cost_at(sample_times[index]), b.cost_at(sample_times[index]),
              tol)) {
      return false;
    }
    if (index + 1U < sample_times.size()) {
      const Value midpoint =
          sample_times[index] +
          (sample_times[index + 1U] - sample_times[index]) / 2.0;
      if (!near(a.cost_at(midpoint), b.cost_at(midpoint), tol)) {
        return false;
      }
    }
  }
  return true;
}
}
