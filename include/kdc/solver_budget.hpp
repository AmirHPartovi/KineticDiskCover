#pragma once

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <stdexcept>

namespace kdc {
class SolverBudgetExpired final : public std::runtime_error {
 public:
  SolverBudgetExpired() : std::runtime_error("solver budget expired") {}
};

class SolverBudget {
 public:
  using Clock = std::chrono::steady_clock;
  using TimePoint = Clock::time_point;

  explicit SolverBudget(double seconds)
      : deadline_(make_deadline(seconds)) {
  }

  explicit SolverBudget(TimePoint deadline) : deadline_(deadline) {}

  TimePoint deadline() const noexcept { return deadline_; }

  bool expired() const noexcept {
    return cancelled_.load(std::memory_order_relaxed) ||
           Clock::now() >= deadline_;
  }

  double remaining_seconds() const noexcept {
    if (expired()) {
      return 0.0;
    }
    return std::max(
        0.0, std::chrono::duration<double>(deadline_ - Clock::now()).count());
  }

  double limit_seconds(double local_limit) const noexcept {
    return std::min(std::max(0.0, local_limit), remaining_seconds());
  }

  void checkpoint() const {
    if (expired()) {
      throw SolverBudgetExpired();
    }
  }

  void cancel() noexcept {
    cancelled_.store(true, std::memory_order_relaxed);
  }

 private:
  static TimePoint make_deadline(double seconds) {
    if (!std::isfinite(seconds)) {
      throw std::invalid_argument("solver budget duration must be finite");
    }
    const TimePoint now = Clock::now();
    if (seconds <= 0.0) {
      return now;
    }
    const double maximum_seconds =
        std::chrono::duration<double>(TimePoint::max() - now).count();
    if (seconds >= maximum_seconds) {
      return TimePoint::max();
    }
    return now + std::chrono::duration_cast<Clock::duration>(
                     std::chrono::duration<double>(seconds));
  }

  TimePoint deadline_;
  std::atomic<bool> cancelled_{false};
};
}  // namespace kdc
