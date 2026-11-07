#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <type_traits>

#include "multitether/common/cache.h"

namespace multitether {
class SpscCursor {
 public:
  explicit SpscCursor(std::size_t capacity) noexcept : capacity_(capacity), mask_(capacity - 1) {}

  [[nodiscard]] std::size_t Capacity() const noexcept { return capacity_; }

  [[nodiscard]] bool TryAcquireWriteAt(std::uint64_t offset, std::size_t& index) noexcept {
    const std::uint64_t position = write_pos_.load(std::memory_order_relaxed) + offset;
    if (position - cached_read_pos_ >= capacity_) [[unlikely]] {
      cached_read_pos_ = read_pos_.load(std::memory_order_acquire);
      if (position - cached_read_pos_ >= capacity_) {
        return false;
      }
    }
    index = position & mask_;
    return true;
  }

  [[nodiscard]] bool TryAcquireWrite(std::size_t& index) noexcept {
    return TryAcquireWriteAt(0, index);
  }

  void PublishWrite(std::uint64_t count = 1) noexcept {
    write_pos_.store(write_pos_.load(std::memory_order_relaxed) + count,
                     std::memory_order_release);
  }

  [[nodiscard]] std::size_t WritableApprox() noexcept {
    const std::uint64_t write_pos = write_pos_.load(std::memory_order_relaxed);
    cached_read_pos_ = read_pos_.load(std::memory_order_acquire);
    return capacity_ - static_cast<std::size_t>(write_pos - cached_read_pos_);
  }

  [[nodiscard]] bool TryAcquireReadAt(std::uint64_t offset, std::size_t& index) noexcept {
    const std::uint64_t position = read_pos_.load(std::memory_order_relaxed) + offset;
    if (position >= cached_write_pos_) [[unlikely]] {
      cached_write_pos_ = write_pos_.load(std::memory_order_acquire);
      if (position >= cached_write_pos_) {
        return false;
      }
    }
    index = position & mask_;
    return true;
  }

  [[nodiscard]] bool TryAcquireRead(std::size_t& index) noexcept {
    return TryAcquireReadAt(0, index);
  }

  void PublishRead(std::uint64_t count = 1) noexcept {
    read_pos_.store(read_pos_.load(std::memory_order_relaxed) + count,
                    std::memory_order_release);
  }

  [[nodiscard]] std::size_t ReadableApprox() noexcept {
    const std::uint64_t read_pos = read_pos_.load(std::memory_order_relaxed);
    cached_write_pos_ = write_pos_.load(std::memory_order_acquire);
    return static_cast<std::size_t>(cached_write_pos_ - read_pos);
  }

  [[nodiscard]] std::size_t SizeSnapshot() const noexcept {
    const std::uint64_t write_pos = write_pos_.load(std::memory_order_acquire);
    const std::uint64_t read_pos = read_pos_.load(std::memory_order_acquire);
    return write_pos >= read_pos ? static_cast<std::size_t>(write_pos - read_pos) : 0;
  }

  [[nodiscard]] std::uint64_t TotalEnqueued() const noexcept {
    return write_pos_.load(std::memory_order_relaxed);
  }

  [[nodiscard]] std::uint64_t TotalDequeued() const noexcept {
    return read_pos_.load(std::memory_order_relaxed);
  }

 private:
  MULTITETHER_CACHE_ALIGNED const std::size_t capacity_;
  const std::size_t mask_;

  MULTITETHER_CACHE_ALIGNED std::atomic<std::uint64_t> write_pos_{0};
  MULTITETHER_CACHE_ALIGNED std::uint64_t cached_read_pos_{0};
  MULTITETHER_CACHE_ALIGNED std::atomic<std::uint64_t> read_pos_{0};
  MULTITETHER_CACHE_ALIGNED std::uint64_t cached_write_pos_{0};
};

template <typename T>
  requires std::is_trivially_copyable_v<T> && std::is_default_constructible_v<T>
class SpscRing {
 public:
  explicit SpscRing(std::size_t capacity)
      : cursor_(RoundUpCapacity(capacity)),
        slots_(std::make_unique<T[]>(RoundUpCapacity(capacity))) {}

  SpscRing(const SpscRing&) = delete;
  SpscRing& operator=(const SpscRing&) = delete;
  SpscRing(SpscRing&&) = delete;
  SpscRing& operator=(SpscRing&&) = delete;
  ~SpscRing() = default;

  [[nodiscard]] std::size_t Capacity() const noexcept { return cursor_.Capacity(); }

  [[nodiscard]] bool TryPush(const T& value) noexcept {
    std::size_t index = 0;
    if (!cursor_.TryAcquireWrite(index)) {
      return false;
    }
    slots_[index] = value;
    cursor_.PublishWrite();
    return true;
  }

  [[nodiscard]] bool TryPop(T& out) noexcept {
    std::size_t index = 0;
    if (!cursor_.TryAcquireRead(index)) {
      return false;
    }
    out = slots_[index];
    cursor_.PublishRead();
    return true;
  }

  [[nodiscard]] std::size_t SizeSnapshot() const noexcept { return cursor_.SizeSnapshot(); }

  [[nodiscard]] SpscCursor& Cursor() noexcept { return cursor_; }

 private:
  static std::size_t RoundUpCapacity(std::size_t requested) noexcept {
    std::size_t capacity = 1;
    while (capacity < requested) {
      capacity <<= 1U;
    }
    return capacity;
  }

  SpscCursor cursor_;
  std::unique_ptr<T[]> slots_;
};
}  // namespace multitether
