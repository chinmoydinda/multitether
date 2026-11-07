#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <thread>
#include <vector>

#include "multitether/common/error.h"
#include "multitether/common/frame_ring.h"
#include "multitether/common/stats.h"
#include "multitether/net/link_backend.h"
#include "multitether/usb/data_channel.h"

namespace multitether::core {
struct BridgeConfig {
  std::size_t rx_ring_frames = 2048;

  std::uint32_t max_frame_bytes = 1518;

  std::uint32_t rx_write_batch = 64;

  std::uint32_t rx_spin_before_yield = 64;

  std::uint32_t tx_submit_batch = 256;
};

struct BridgeStats {
  DirectionSnapshot rx;
  DirectionSnapshot tx;

  std::size_t rx_queue_depth = 0;

  std::uint64_t link_kernel_drops = 0;

  std::uint64_t tx_backpressure_events = 0;
};

class Bridge {
 public:
  Bridge(usb::DataChannel& data_channel, net::LinkBackend& link, const BridgeConfig& config);

  Bridge(const Bridge&) = delete;
  Bridge& operator=(const Bridge&) = delete;
  Bridge(Bridge&&) = delete;
  Bridge& operator=(Bridge&&) = delete;

  ~Bridge();

  [[nodiscard]] Status Start();

  void Stop();

  [[nodiscard]] bool Running() const noexcept {
    return running_.load(std::memory_order_acquire);
  }

  [[nodiscard]] BridgeStats Snapshot() const;

  void SetPaused(bool paused) noexcept;

  [[nodiscard]] bool Paused() const noexcept { return paused_.load(std::memory_order_acquire); }

 private:
  void RunReceiveInjector() noexcept;

  void RunTransmitExtractor() noexcept;

  usb::DataChannel* data_channel_;
  net::LinkBackend* link_;
  BridgeConfig config_;

  std::unique_ptr<FrameRing> rx_ring_;

  PathCounters counters_;
  std::atomic<std::uint64_t> tx_backpressure_events_{0};
  std::atomic<std::uint64_t> link_kernel_drops_{0};

  std::thread receive_injector_;
  std::thread transmit_extractor_;

  std::atomic<bool> running_{false};
  std::atomic<bool> stop_requested_{false};
  std::atomic<bool> paused_{false};

  std::atomic<bool> rx_paused_ack_{false};

  std::vector<FrameView> rx_batch_;
};

[[nodiscard]] std::string FormatStatsLine(const BridgeStats& previous, const BridgeStats& current,
                                          double seconds);
}  // namespace multitether::core
