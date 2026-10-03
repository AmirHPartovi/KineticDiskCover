#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include <nlohmann/json.hpp>

namespace kdc {
enum class ProfilePhase : std::size_t {
  CANDIDATE_BUILD = 0,
  TRAJECTORY_POSITION,
  DISTANCE_MATRIX,
  COVERAGE_MATRIX,
  MODEL_BUILD,
  LP_ILP_BUILD,
  LP_ILP_SOLVE,
  SUPPORT_EVENT_DETECTION,
  HANDOVER_DETECTION,
  KINETIC_EXTENSION,
  COMBINATION,
  LOCAL_IMPROVEMENT,
  VERIFICATION,
  SERIALIZATION,
  COUNT
};

class PhaseProfiler {
 public:
  static constexpr std::size_t phase_count =
      static_cast<std::size_t>(ProfilePhase::COUNT);

  void record(ProfilePhase phase, std::uint64_t elapsed_nanoseconds) noexcept;
  nlohmann::json snapshot() const;

 private:
  std::array<std::atomic<std::uint64_t>, phase_count> calls_{};
  std::array<std::atomic<std::uint64_t>, phase_count> nanoseconds_{};
};

class ProfileSession {
 public:
  ProfileSession();
  ~ProfileSession();
  ProfileSession(const ProfileSession&) = delete;
  ProfileSession& operator=(const ProfileSession&) = delete;

  bool enabled() const noexcept { return profiler_ != nullptr; }
  void write() const;

 private:
  std::unique_ptr<PhaseProfiler> profiler_;
  std::string output_path_;
};

class ScopedPhaseTimer {
 public:
  explicit ScopedPhaseTimer(ProfilePhase phase) noexcept;
  ~ScopedPhaseTimer();
  void stop() noexcept;
  ScopedPhaseTimer(const ScopedPhaseTimer&) = delete;
  ScopedPhaseTimer& operator=(const ScopedPhaseTimer&) = delete;

 private:
  PhaseProfiler* profiler_{nullptr};
  ProfilePhase phase_;
  std::chrono::steady_clock::time_point start_{};
};
}  // namespace kdc

#define KDC_PROFILE_JOIN_IMPL(lhs, rhs) lhs##rhs
#define KDC_PROFILE_JOIN(lhs, rhs) KDC_PROFILE_JOIN_IMPL(lhs, rhs)
#define KDC_PROFILE_PHASE(phase) \
  ::kdc::ScopedPhaseTimer KDC_PROFILE_JOIN(kdc_profile_timer_, __LINE__)(phase)
