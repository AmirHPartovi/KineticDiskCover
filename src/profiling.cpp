#include "kdc/profiling.hpp"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace kdc {
namespace {
std::atomic<PhaseProfiler*> active_profiler{nullptr};

constexpr const char* phase_name(ProfilePhase phase) {
  switch (phase) {
    case ProfilePhase::CANDIDATE_BUILD:
      return "candidate_build";
    case ProfilePhase::TRAJECTORY_POSITION:
      return "trajectory_position";
    case ProfilePhase::DISTANCE_MATRIX:
      return "distance_matrix";
    case ProfilePhase::COVERAGE_MATRIX:
      return "coverage_matrix";
    case ProfilePhase::MODEL_BUILD:
      return "model_build";
    case ProfilePhase::LP_ILP_BUILD:
      return "lp_ilp_build";
    case ProfilePhase::LP_ILP_SOLVE:
      return "lp_ilp_solve";
    case ProfilePhase::SUPPORT_EVENT_DETECTION:
      return "support_event_detection";
    case ProfilePhase::HANDOVER_DETECTION:
      return "handover_detection";
    case ProfilePhase::KINETIC_EXTENSION:
      return "kinetic_extension";
    case ProfilePhase::COMBINATION:
      return "combination";
    case ProfilePhase::LOCAL_IMPROVEMENT:
      return "local_improvement";
    case ProfilePhase::VERIFICATION:
      return "verification";
    case ProfilePhase::SERIALIZATION:
      return "serialization";
    case ProfilePhase::COUNT:
      break;
  }
  return "unknown";
}
}  // namespace

void PhaseProfiler::record(ProfilePhase phase,
                           std::uint64_t elapsed_nanoseconds) noexcept {
  const auto index = static_cast<std::size_t>(phase);
  if (index >= phase_count) {
    return;
  }
  calls_[index].fetch_add(1U, std::memory_order_relaxed);
  nanoseconds_[index].fetch_add(elapsed_nanoseconds,
                                std::memory_order_relaxed);
}

nlohmann::json PhaseProfiler::snapshot() const {
  nlohmann::json phases = nlohmann::json::object();
  for (std::size_t index = 0; index < phase_count; ++index) {
    const auto phase = static_cast<ProfilePhase>(index);
    phases[phase_name(phase)] = {
        {"calls", calls_[index].load(std::memory_order_relaxed)},
        {"seconds",
         static_cast<double>(
             nanoseconds_[index].load(std::memory_order_relaxed)) /
             1'000'000'000.0}};
  }
  return {{"unit", "seconds"}, {"phases", std::move(phases)}};
}

ProfileSession::ProfileSession() {
  const char* configured_path = std::getenv("KDC_PROFILE_PHASES");
  if (configured_path == nullptr || configured_path[0] == '\0') {
    return;
  }
  output_path_ = configured_path[0] == '1' && configured_path[1] == '\0'
                     ? "profile_phases.json"
                     : configured_path;
  profiler_ = std::make_unique<PhaseProfiler>();
  PhaseProfiler* expected = nullptr;
  if (!active_profiler.compare_exchange_strong(
          expected, profiler_.get(), std::memory_order_release,
          std::memory_order_relaxed)) {
    profiler_.reset();
    throw std::logic_error("a phase profiling session is already active");
  }
}

ProfileSession::~ProfileSession() {
  if (profiler_ != nullptr) {
    active_profiler.store(nullptr, std::memory_order_release);
    try {
      write();
    } catch (const std::exception& error) {
      std::cerr << "phase profiling output failed: " << error.what() << '\n';
    }
  }
}

void ProfileSession::write() const {
  if (profiler_ == nullptr) {
    return;
  }
  std::ofstream output(output_path_);
  if (!output) {
    throw std::runtime_error("cannot open phase profile output: " +
                             output_path_);
  }
  output << profiler_->snapshot().dump(2) << '\n';
  if (!output) {
    throw std::runtime_error("failed writing phase profile output: " +
                             output_path_);
  }
}

ScopedPhaseTimer::ScopedPhaseTimer(ProfilePhase phase) noexcept
    : profiler_(active_profiler.load(std::memory_order_acquire)),
      phase_(phase) {
  if (profiler_ != nullptr) {
    start_ = std::chrono::steady_clock::now();
  }
}

ScopedPhaseTimer::~ScopedPhaseTimer() {
  stop();
}

void ScopedPhaseTimer::stop() noexcept {
  if (profiler_ == nullptr) {
    return;
  }
  const auto elapsed =
      std::chrono::duration_cast<std::chrono::nanoseconds>(
          std::chrono::steady_clock::now() - start_)
          .count();
  profiler_->record(phase_, static_cast<std::uint64_t>(elapsed));
  profiler_ = nullptr;
}
}  // namespace kdc
