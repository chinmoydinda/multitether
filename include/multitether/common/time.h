#pragma once

#include <cstdint>
#include <ctime>

namespace multitether {
using Nanos = std::uint64_t;

inline constexpr Nanos kNanosPerMilli = 1'000'000;
inline constexpr Nanos kNanosPerSecond = 1'000'000'000;

[[nodiscard]] inline Nanos MonotonicNanos() noexcept {
  return ::clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
}

class Stopwatch {
 public:
  Stopwatch() noexcept : start_(MonotonicNanos()) {}

  void Reset() noexcept { start_ = MonotonicNanos(); }

  [[nodiscard]] Nanos Elapsed() const noexcept { return MonotonicNanos() - start_; }

  [[nodiscard]] double ElapsedMillis() const noexcept {
    return static_cast<double>(Elapsed()) / static_cast<double>(kNanosPerMilli);
  }

 private:
  Nanos start_;
};

class PeriodicTimer {
 public:
  explicit PeriodicTimer(Nanos period, Nanos start = MonotonicNanos()) noexcept
      : period_(period), next_deadline_(start + period) {}

  [[nodiscard]] bool Expired(Nanos now) noexcept {
    if (now < next_deadline_) {
      return false;
    }
    next_deadline_ += period_;
    if (next_deadline_ <= now) {
      next_deadline_ = now + period_;
    }
    return true;
  }

  [[nodiscard]] bool Expired() noexcept { return Expired(MonotonicNanos()); }

  [[nodiscard]] Nanos RemainingNanos(Nanos now) const noexcept {
    return now >= next_deadline_ ? 0 : next_deadline_ - now;
  }

  [[nodiscard]] Nanos Period() const noexcept { return period_; }

 private:
  Nanos period_;
  Nanos next_deadline_;
};
}  // namespace multitether
