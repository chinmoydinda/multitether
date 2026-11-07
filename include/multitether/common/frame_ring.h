#pragma once

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <span>

#include "multitether/common/cache.h"
#include "multitether/common/spsc_ring.h"

namespace multitether {
inline constexpr std::uint32_t kMaxEthernetFrameBytes = 2048;

inline constexpr std::uint32_t kMinEthernetFrameBytes = 14;

struct FrameView {
  const std::byte* data = nullptr;
  std::uint32_t length = 0;

  [[nodiscard]] std::span<const std::byte> Bytes() const noexcept { return {data, length}; }

  [[nodiscard]] bool Empty() const noexcept { return length == 0; }
};

class FrameRing {
 public:
  explicit FrameRing(std::size_t capacity_frames,
                     std::uint32_t max_frame_bytes = kMaxEthernetFrameBytes)
      : cursor_(RoundUpToPowerOfTwo(capacity_frames)),
        max_frame_bytes_(max_frame_bytes),
        slot_stride_(ComputeSlotStride(max_frame_bytes)),
        storage_(AllocateStorage(RoundUpToPowerOfTwo(capacity_frames),
                                 ComputeSlotStride(max_frame_bytes))) {
    void* base = storage_.get();
    std::size_t space = cursor_.Capacity() * slot_stride_ + kCacheLineSize;
    void* aligned = std::align(kCacheLineSize, cursor_.Capacity() * slot_stride_, base, space);
    assert(aligned != nullptr);
    aligned_base_ = static_cast<std::byte*>(aligned);
  }

  FrameRing(const FrameRing&) = delete;
  FrameRing& operator=(const FrameRing&) = delete;
  FrameRing(FrameRing&&) = delete;
  FrameRing& operator=(FrameRing&&) = delete;
  ~FrameRing() = default;

  [[nodiscard]] std::size_t Capacity() const noexcept { return cursor_.Capacity(); }

  [[nodiscard]] std::uint32_t MaxFrameBytes() const noexcept { return max_frame_bytes_; }

  [[nodiscard]] std::size_t StorageBytes() const noexcept {
    return cursor_.Capacity() * slot_stride_;
  }

  [[nodiscard]] std::span<std::byte> BeginWrite() noexcept {
    if (!cursor_.TryAcquireWrite(pending_write_index_)) [[unlikely]] {
      return {};
    }
    return {FrameDataAt(pending_write_index_), max_frame_bytes_};
  }

  void CommitWrite(std::uint32_t length) noexcept {
    assert(length <= max_frame_bytes_);
    StoreLength(pending_write_index_, length);
    cursor_.PublishWrite();
  }

  [[nodiscard]] bool TryPush(std::span<const std::byte> frame) noexcept {
    if (frame.size() > max_frame_bytes_) [[unlikely]] {
      return false;
    }
    const std::span<std::byte> dst = BeginWrite();
    if (dst.empty()) [[unlikely]] {
      return false;
    }
    std::memcpy(dst.data(), frame.data(), frame.size());
    CommitWrite(static_cast<std::uint32_t>(frame.size()));
    return true;
  }

  [[nodiscard]] std::size_t WritableApprox() noexcept { return cursor_.WritableApprox(); }

  class BatchWrite {
   public:
    explicit BatchWrite(FrameRing& ring) noexcept : ring_(&ring) {}

    BatchWrite(const BatchWrite&) = delete;
    BatchWrite& operator=(const BatchWrite&) = delete;
    BatchWrite(BatchWrite&&) = delete;
    BatchWrite& operator=(BatchWrite&&) = delete;

    ~BatchWrite() { Publish(); }

    [[nodiscard]] std::span<std::byte> Begin() noexcept {
      if (!ring_->cursor_.TryAcquireWriteAt(staged_, staged_index_)) [[unlikely]] {
        return {};
      }
      return {ring_->FrameDataAt(staged_index_), ring_->max_frame_bytes_};
    }

    void Commit(std::uint32_t length) noexcept {
      assert(length <= ring_->max_frame_bytes_);
      ring_->StoreLength(staged_index_, length);
      ++staged_;
    }

    [[nodiscard]] bool Push(std::span<const std::byte> frame) noexcept {
      if (frame.size() > ring_->max_frame_bytes_) [[unlikely]] {
        return false;
      }
      const std::span<std::byte> dst = Begin();
      if (dst.empty()) [[unlikely]] {
        return false;
      }
      std::memcpy(dst.data(), frame.data(), frame.size());
      Commit(static_cast<std::uint32_t>(frame.size()));
      return true;
    }

    void Publish() noexcept {
      if (staged_ != 0) {
        ring_->cursor_.PublishWrite(staged_);
        published_ += staged_;
        staged_ = 0;
      }
    }

    [[nodiscard]] std::uint32_t Staged() const noexcept { return staged_; }

    [[nodiscard]] std::uint32_t Published() const noexcept { return published_; }

   private:
    FrameRing* ring_;
    std::uint32_t staged_ = 0;
    std::uint32_t published_ = 0;
    std::size_t staged_index_ = 0;
  };

  [[nodiscard]] BatchWrite BeginBatchWrite() noexcept { return BatchWrite{*this}; }

  [[nodiscard]] FrameView BeginRead() noexcept {
    if (!cursor_.TryAcquireRead(pending_read_index_)) [[unlikely]] {
      return FrameView{.data = nullptr, .length = 0};
    }
    return FrameView{.data = FrameDataAt(pending_read_index_),
                     .length = LoadLength(pending_read_index_)};
  }

  void CommitRead() noexcept { cursor_.PublishRead(); }

  [[nodiscard]] std::size_t ReadableApprox() noexcept { return cursor_.ReadableApprox(); }

  class BatchRead {
   public:
    explicit BatchRead(FrameRing& ring) noexcept : ring_(&ring) {}

    BatchRead(const BatchRead&) = delete;
    BatchRead& operator=(const BatchRead&) = delete;
    BatchRead(BatchRead&&) = delete;
    BatchRead& operator=(BatchRead&&) = delete;

    ~BatchRead() { Release(); }

    [[nodiscard]] FrameView Next() noexcept {
      std::size_t index = 0;
      if (!ring_->cursor_.TryAcquireReadAt(staged_, index)) [[unlikely]] {
        return FrameView{};
      }
      ++staged_;
      return FrameView{.data = ring_->FrameDataAt(index),
                       .length = ring_->LoadLength(index)};
    }

    void Release() noexcept {
      if (staged_ != 0) {
        ring_->cursor_.PublishRead(staged_);
        released_ += staged_;
        staged_ = 0;
      }
    }

    [[nodiscard]] std::uint32_t Staged() const noexcept { return staged_; }

    [[nodiscard]] std::uint32_t Released() const noexcept { return released_; }

   private:
    FrameRing* ring_;
    std::uint32_t staged_ = 0;
    std::uint32_t released_ = 0;
  };

  [[nodiscard]] BatchRead BeginBatchRead() noexcept { return BatchRead{*this}; }

  [[nodiscard]] std::size_t SizeSnapshot() const noexcept { return cursor_.SizeSnapshot(); }

  [[nodiscard]] std::uint64_t TotalEnqueued() const noexcept { return cursor_.TotalEnqueued(); }

  [[nodiscard]] std::uint64_t TotalDequeued() const noexcept { return cursor_.TotalDequeued(); }

 private:
  friend class BatchWrite;
  friend class BatchRead;

  static constexpr std::size_t kSlotHeaderBytes = kCacheLineSize;

  static std::size_t RoundUpToPowerOfTwo(std::size_t requested) noexcept {
    std::size_t value = 1;
    while (value < requested) {
      value <<= 1U;
    }
    return value;
  }

  static std::size_t ComputeSlotStride(std::uint32_t max_frame_bytes) noexcept {
    const std::size_t raw = kSlotHeaderBytes + max_frame_bytes;
    return ((raw + kCacheLineSize - 1) / kCacheLineSize) * kCacheLineSize;
  }

  static std::unique_ptr<std::byte[]> AllocateStorage(std::size_t capacity, std::size_t stride) {
    return std::make_unique<std::byte[]>(capacity * stride + kCacheLineSize);
  }

  [[nodiscard]] std::byte* SlotAt(std::size_t index) const noexcept {
    return aligned_base_ + index * slot_stride_;
  }

  [[nodiscard]] std::byte* FrameDataAt(std::size_t index) const noexcept {
    return SlotAt(index) + kSlotHeaderBytes;
  }

  void StoreLength(std::size_t index, std::uint32_t length) noexcept {
    std::memcpy(SlotAt(index), &length, sizeof(length));
  }

  [[nodiscard]] std::uint32_t LoadLength(std::size_t index) const noexcept {
    std::uint32_t length = 0;
    std::memcpy(&length, SlotAt(index), sizeof(length));
    return length;
  }

  SpscCursor cursor_;

  std::uint32_t max_frame_bytes_;
  std::size_t slot_stride_;
  std::unique_ptr<std::byte[]> storage_;
  std::byte* aligned_base_ = nullptr;

  MULTITETHER_CACHE_ALIGNED std::size_t pending_write_index_ = 0;
  MULTITETHER_CACHE_ALIGNED std::size_t pending_read_index_ = 0;
};
}  // namespace multitether
