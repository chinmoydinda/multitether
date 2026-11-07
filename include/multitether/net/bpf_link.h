#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "multitether/common/error.h"
#include "multitether/net/link_backend.h"

namespace multitether::net {
struct BpfConfig {
  std::uint32_t kernel_buffer_bytes = 4U * 1024 * 1024;

  std::uint32_t max_frame_bytes = 1518;

  std::size_t max_frames_per_batch = 8192;

  bool try_batch_write = true;

  bool try_disable_timestamp = true;

  std::uint32_t read_timeout_millis = 200;
};

class BpfLink final : public LinkBackend {
 public:
  [[nodiscard]] static Result<std::unique_ptr<BpfLink>> Open(std::string_view interface_name,
                                                            const BpfConfig& config);

  BpfLink(const BpfLink&) = delete;
  BpfLink& operator=(const BpfLink&) = delete;
  BpfLink(BpfLink&&) = delete;
  BpfLink& operator=(BpfLink&&) = delete;
  ~BpfLink() override;

  [[nodiscard]] Result<ReadBatch> ReadFrames() override;
  [[nodiscard]] Result<WriteResult> WriteFrames(FrameBatch frames) override;

  [[nodiscard]] std::uint32_t MaxFrameBytes() const noexcept override { return max_frame_bytes_; }

  [[nodiscard]] bool SupportsBatchWrite() const noexcept override { return batch_write_enabled_; }

  void Interrupt() noexcept override;

  [[nodiscard]] std::uint32_t KernelBufferBytes() const noexcept { return kernel_buffer_bytes_; }

  [[nodiscard]] std::string_view DevicePath() const noexcept { return device_path_; }

  struct KernelStats {
    std::uint64_t received = 0;
    std::uint64_t dropped = 0;
  };

  [[nodiscard]] Result<KernelStats> QueryKernelStats() const;

 private:
  BpfLink() = default;

  [[nodiscard]] Result<WriteResult> WriteFramesIndividually(FrameBatch frames);

  [[nodiscard]] Result<WriteResult> WriteFramesBatched(FrameBatch frames);

  int fd_ = -1;
  std::string device_path_;
  std::string interface_name_;

  std::uint32_t kernel_buffer_bytes_ = 0;
  std::uint32_t max_frame_bytes_ = 0;
  bool batch_write_enabled_ = false;
  bool timestamp_disabled_ = false;

  std::vector<std::byte> read_buffer_;

  std::vector<FrameView> read_frames_;

  std::uint64_t last_kernel_drops_ = 0;

  std::vector<std::byte> write_buffer_;

  std::atomic<bool> interrupted_{false};
};
}  // namespace multitether::net
