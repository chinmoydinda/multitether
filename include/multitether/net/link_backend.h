#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "multitether/common/error.h"
#include "multitether/common/frame_ring.h"

namespace multitether::net {
using FrameBatch = std::span<const FrameView>;

struct ReadBatch {
  std::span<const FrameView> frames;

  std::uint64_t kernel_drops = 0;
};

struct WriteResult {
  std::uint32_t frames_written = 0;
  std::uint64_t bytes_written = 0;

  std::uint32_t frames_skipped = 0;
};

class LinkBackend {
 public:
  LinkBackend() = default;
  LinkBackend(const LinkBackend&) = delete;
  LinkBackend& operator=(const LinkBackend&) = delete;
  LinkBackend(LinkBackend&&) = delete;
  LinkBackend& operator=(LinkBackend&&) = delete;
  virtual ~LinkBackend() = default;

  [[nodiscard]] virtual Result<ReadBatch> ReadFrames() = 0;

  [[nodiscard]] virtual Result<WriteResult> WriteFrames(FrameBatch frames) = 0;

  [[nodiscard]] virtual std::uint32_t MaxFrameBytes() const noexcept = 0;

  [[nodiscard]] virtual bool SupportsBatchWrite() const noexcept = 0;

  virtual void Interrupt() noexcept = 0;
};
}  // namespace multitether::net
