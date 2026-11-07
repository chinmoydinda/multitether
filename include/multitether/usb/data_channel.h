#pragma once

#include <libusb.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <span>
#include <vector>

#include "multitether/common/error.h"
#include "multitether/common/frame_ring.h"
#include "multitether/common/spsc_ring.h"
#include "multitether/common/stats.h"
#include "multitether/rndis/messages.h"
#include "multitether/usb/device.h"

namespace multitether::usb {
struct DataChannelConfig {
  std::uint32_t rx_transfer_count = 16;

  std::uint32_t rx_transfer_bytes = 16 * 1024;

  std::uint32_t tx_transfer_count = 4;

  std::uint32_t tx_transfer_bytes = 16 * 1024;

  std::uint32_t rx_timeout_millis = 0;

  std::uint32_t tx_timeout_millis = 5'000;
};

struct SendOutcome {
  std::uint32_t consumed = 0;

  std::uint32_t sent_frames = 0;

  std::uint64_t sent_bytes = 0;

  std::uint32_t skipped = 0;
};

class DataChannel {
 public:
  DataChannel() = default;
  DataChannel(const DataChannel&) = delete;
  DataChannel& operator=(const DataChannel&) = delete;
  DataChannel(DataChannel&&) = delete;
  DataChannel& operator=(DataChannel&&) = delete;
  virtual ~DataChannel() = default;

  [[nodiscard]] virtual Status StartReceiving(FrameRing& rx_ring,
                                              DirectionCounters& rx_counters) = 0;

  [[nodiscard]] virtual Result<SendOutcome> SendFrames(std::span<const FrameView> frames) = 0;

  [[nodiscard]] virtual bool WaitForSendCapacity(std::uint32_t timeout_millis) = 0;

  virtual void Shutdown() = 0;

  [[nodiscard]] virtual bool CanSend() const noexcept = 0;

  [[nodiscard]] virtual std::uint32_t MaxTransferBytes() const noexcept = 0;

  [[nodiscard]] virtual std::uint64_t AsyncSendErrors() const noexcept = 0;
};

class UsbDataChannel final : public DataChannel {
 public:
  [[nodiscard]] static Result<std::unique_ptr<UsbDataChannel>> Create(
      Device& device, const rndis::NegotiatedParameters& parameters,
      const DataChannelConfig& config);

  UsbDataChannel(const UsbDataChannel&) = delete;
  UsbDataChannel& operator=(const UsbDataChannel&) = delete;
  UsbDataChannel(UsbDataChannel&&) = delete;
  UsbDataChannel& operator=(UsbDataChannel&&) = delete;
  ~UsbDataChannel() override;

  [[nodiscard]] Status StartReceiving(FrameRing& rx_ring,
                                      DirectionCounters& rx_counters) override;
  [[nodiscard]] Result<SendOutcome> SendFrames(std::span<const FrameView> frames) override;
  [[nodiscard]] bool WaitForSendCapacity(std::uint32_t timeout_millis) override;
  void Shutdown() override;

  [[nodiscard]] bool CanSend() const noexcept override;

  [[nodiscard]] std::uint32_t MaxTransferBytes() const noexcept override {
    return tx_transfer_bytes_;
  }

  [[nodiscard]] std::uint64_t AsyncSendErrors() const noexcept override {
    return async_send_errors_.load(std::memory_order_relaxed);
  }

 private:
  UsbDataChannel() = default;

  struct Slot {
    ::libusb_transfer* transfer = nullptr;
    std::byte* buffer = nullptr;
    std::uint32_t buffer_bytes = 0;
    UsbDataChannel* owner = nullptr;
    std::uint32_t index = 0;
  };

  static void ReceiveCallbackTrampoline(::libusb_transfer* transfer);
  static void SendCallbackTrampoline(::libusb_transfer* transfer);

  void OnReceiveComplete(Slot& slot) noexcept;
  void OnSendComplete(Slot& slot) noexcept;

  [[nodiscard]] Status AllocatePool(std::vector<Slot>& pool, std::uint32_t count,
                                    std::uint32_t buffer_bytes);

  static void FreePool(std::vector<Slot>& pool) noexcept;

  [[nodiscard]] Status SubmitReceive(Slot& slot);

  Device* device_ = nullptr;
  rndis::NegotiatedParameters parameters_{};
  DataChannelConfig config_{};

  std::uint32_t tx_transfer_bytes_ = 0;

  std::vector<Slot> rx_pool_;
  std::vector<Slot> tx_pool_;

  std::unique_ptr<SpscRing<std::uint32_t>> tx_free_slots_;

  FrameRing* rx_ring_ = nullptr;
  DirectionCounters* rx_counters_ = nullptr;

  std::atomic<std::uint64_t> async_send_errors_{0};

  std::atomic<std::uint32_t> outstanding_{0};

  std::atomic<bool> shutting_down_{false};

  bool shutdown_complete_ = false;

  std::mutex drain_mutex_;
  std::condition_variable drain_condition_;

  std::mutex send_capacity_mutex_;
  std::condition_variable send_capacity_cv_;
};
}  // namespace multitether::usb
