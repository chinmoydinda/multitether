#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <vector>

#include "multitether/common/frame_ring.h"
#include "multitether/net/link_backend.h"

namespace multitether::net {
struct LoopbackConfig {
  std::uint32_t max_frame_bytes = 1518;
  std::size_t inbound_capacity = 4096;
  std::size_t sent_capacity = 4096;
  std::size_t max_frames_per_batch = 256;

  bool report_batch_write = true;
};

class LoopbackLink final : public LinkBackend {
 public:
  explicit LoopbackLink(const LoopbackConfig& config = {});

  LoopbackLink(const LoopbackLink&) = delete;
  LoopbackLink& operator=(const LoopbackLink&) = delete;
  LoopbackLink(LoopbackLink&&) = delete;
  LoopbackLink& operator=(LoopbackLink&&) = delete;
  ~LoopbackLink() override = default;

  [[nodiscard]] Result<ReadBatch> ReadFrames() override;
  [[nodiscard]] Result<WriteResult> WriteFrames(FrameBatch frames) override;

  [[nodiscard]] std::uint32_t MaxFrameBytes() const noexcept override {
    return config_.max_frame_bytes;
  }

  [[nodiscard]] bool SupportsBatchWrite() const noexcept override {
    return config_.report_batch_write;
  }

  void Interrupt() noexcept override;

  [[nodiscard]] bool PushInbound(std::span<const std::byte> frame);

  [[nodiscard]] std::vector<std::vector<std::byte>> DrainSent();

  [[nodiscard]] std::uint64_t TotalSentFrames() const noexcept {
    return total_sent_frames_.load(std::memory_order_relaxed);
  }

  [[nodiscard]] std::uint64_t TotalSentBytes() const noexcept {
    return total_sent_bytes_.load(std::memory_order_relaxed);
  }

  [[nodiscard]] std::uint64_t InboundDrops() const noexcept {
    return inbound_drops_.load(std::memory_order_relaxed);
  }

  [[nodiscard]] bool Interrupted() const noexcept {
    return interrupted_.load(std::memory_order_acquire);
  }

  void FailWritesAfter(std::uint32_t successful_calls) noexcept {
    fail_writes_after_.store(successful_calls, std::memory_order_relaxed);
  }

 private:
  LoopbackConfig config_;

  mutable std::mutex mutex_;

  std::vector<std::vector<std::byte>> inbound_;

  std::vector<std::vector<std::byte>> sent_;

  std::vector<std::vector<std::byte>> read_storage_;
  std::vector<FrameView> read_views_;

  std::atomic<bool> interrupted_{false};
  std::atomic<std::uint64_t> total_sent_frames_{0};
  std::atomic<std::uint64_t> total_sent_bytes_{0};
  std::atomic<std::uint64_t> inbound_drops_{0};
  std::atomic<std::uint32_t> write_calls_{0};
  std::atomic<std::uint32_t> fail_writes_after_{0};
};
}  // namespace multitether::net
