#include "multitether/core/bridge.h"

#include <algorithm>
#include <chrono>
#include <format>
#include <thread>

#include "multitether/common/i18n.h"
#include "multitether/common/logging.h"
#include "multitether/common/scheduling.h"

namespace multitether::core {
namespace {
constexpr std::uint32_t kTxCapacityWaitMillis = 50;

[[nodiscard]] double ToMegabitsPerSecond(std::uint64_t bytes, double seconds) {
  if (seconds <= 0.0) {
    return 0.0;
  }
  return static_cast<double>(bytes) * 8.0 / 1'000'000.0 / seconds;
}

[[nodiscard]] double ToPacketsPerSecond(std::uint64_t frames, double seconds) {
  if (seconds <= 0.0) {
    return 0.0;
  }
  return static_cast<double>(frames) / seconds;
}
}  // namespace

Bridge::Bridge(usb::DataChannel& data_channel, net::LinkBackend& link,
               const BridgeConfig& config)
    : data_channel_(&data_channel), link_(&link), config_(config) {
  rx_ring_ = std::make_unique<FrameRing>(config_.rx_ring_frames, config_.max_frame_bytes);
  rx_batch_.reserve(config_.rx_write_batch);
}

Bridge::~Bridge() {
  Stop();
}

Status Bridge::Start() {
  if (running_.load(std::memory_order_acquire)) {
    return std::unexpected(Error::Generic(Tr(Msg::kCoreBridgeAlreadyRunning)));
  }
  stop_requested_.store(false, std::memory_order_release);

  MULTITETHER_RETURN_IF_ERROR(data_channel_->StartReceiving(*rx_ring_, counters_.rx));

  running_.store(true, std::memory_order_release);
  receive_injector_ = std::thread([this] { RunReceiveInjector(); });
  transmit_extractor_ = std::thread([this] { RunTransmitExtractor(); });

  MULTITETHER_INFO_TR(Msg::kCoreDataPathStarted, rx_ring_->Capacity(),
                    rx_ring_->StorageBytes() / 1024, config_.rx_write_batch,
                    config_.tx_submit_batch,
                    Text(link_->SupportsBatchWrite() ? Msg::kCoreBatchWriteAvailable
                                                     : Msg::kCoreBatchWriteUnavailable));
  return Ok();
}

void Bridge::Stop() {
  if (!running_.exchange(false, std::memory_order_acq_rel)) {
    return;
  }

  stop_requested_.store(true, std::memory_order_release);

  link_->Interrupt();

  if (receive_injector_.joinable()) {
    receive_injector_.join();
  }
  if (transmit_extractor_.joinable()) {
    transmit_extractor_.join();
  }

  data_channel_->Shutdown();

  MULTITETHER_INFO_TR(Msg::kCoreDataPathStopped);
}

void Bridge::SetPaused(bool paused) noexcept {
  paused_.store(paused, std::memory_order_release);
  if (!paused) {
    return;
  }

  while (running_.load(std::memory_order_acquire) &&
         !stop_requested_.load(std::memory_order_acquire) &&
         !rx_paused_ack_.load(std::memory_order_acquire)) {
    std::this_thread::yield();
  }
}

void Bridge::RunReceiveInjector() noexcept {
  ConfigureCurrentThread("rx-inject", ThreadRole::kDataPath);
  MULTITETHER_DEBUG_TR(Msg::kCoreRxInjectorStarted);

  std::uint32_t idle_spins = 0;

  while (!stop_requested_.load(std::memory_order_acquire)) {
    rx_paused_ack_.store(false, std::memory_order_release);

    if (paused_.load(std::memory_order_acquire)) {
      rx_paused_ack_.store(true, std::memory_order_release);
      std::this_thread::yield();
      continue;
    }

    rx_batch_.clear();
    {
      auto batch = rx_ring_->BeginBatchRead();
      while (rx_batch_.size() < config_.rx_write_batch) {
        const FrameView view = batch.Next();
        if (view.Empty()) {
          break;
        }
        rx_batch_.push_back(view);
      }

      if (rx_batch_.empty()) {
        if (++idle_spins < config_.rx_spin_before_yield) {
          continue;
        }
        idle_spins = 0;
        std::this_thread::yield();
        continue;
      }
      idle_spins = 0;

      const auto result = link_->WriteFrames(rx_batch_);
      if (!result) {
        counters_.rx.AddIoError();
        MULTITETHER_WARN_TR(Msg::kCoreLinkWriteFailed, rx_batch_.size(),
                          result.error().ToString());
      } else {
        if (result->frames_skipped != 0) {
          counters_.rx.AddDroppedOversize(result->frames_skipped);
        }
        const auto written = result->frames_written;
        if (written < rx_batch_.size()) {
          counters_.rx.AddDroppedFull(rx_batch_.size() - written);
        }
      }
    }
  }

  MULTITETHER_DEBUG_TR(Msg::kCoreRxInjectorExited);
}

void Bridge::RunTransmitExtractor() noexcept {
  ConfigureCurrentThread("tx-extract", ThreadRole::kDataPath);
  MULTITETHER_DEBUG_TR(Msg::kCoreTxExtractorStarted);

  while (!stop_requested_.load(std::memory_order_acquire)) {
    const auto batch = link_->ReadFrames();
    if (!batch) {
      counters_.tx.AddIoError();
      MULTITETHER_WARN_TR(Msg::kCoreLinkReadFailed, batch.error().ToString());

      std::this_thread::sleep_for(std::chrono::milliseconds(10));
      continue;
    }

    link_kernel_drops_.store(batch->kernel_drops, std::memory_order_relaxed);

    if (batch->frames.empty()) {
      continue;
    }
    if (paused_.load(std::memory_order_acquire)) {
      counters_.tx.AddDroppedFull(batch->frames.size());
      continue;
    }

    std::size_t offset = 0;
    while (offset < batch->frames.size()) {
      if (stop_requested_.load(std::memory_order_acquire) ||
          paused_.load(std::memory_order_acquire)) {
        counters_.tx.AddDroppedFull(batch->frames.size() - offset);
        break;
      }

      const std::size_t chunk =
          std::min<std::size_t>(config_.tx_submit_batch, batch->frames.size() - offset);
      const std::span<const FrameView> slice = batch->frames.subspan(offset, chunk);

      const auto sent = data_channel_->SendFrames(slice);
      if (!sent) {
        counters_.tx.AddIoError();

        counters_.tx.AddDroppedFull(batch->frames.size() - offset);
        MULTITETHER_WARN_TR(Msg::kCoreUsbSubmitFailed, chunk, sent.error().ToString());
        break;
      }

      if (sent->sent_frames != 0) {
        counters_.tx.AddBatch(sent->sent_frames, sent->sent_bytes);
      }
      if (sent->skipped != 0) {
        counters_.tx.AddDroppedOversize(sent->skipped);
      }

      if (sent->consumed == 0) {
        tx_backpressure_events_.fetch_add(1, std::memory_order_relaxed);
        (void)data_channel_->WaitForSendCapacity(kTxCapacityWaitMillis);
        continue;
      }
      offset += sent->consumed;
    }
  }

  MULTITETHER_DEBUG_TR(Msg::kCoreTxExtractorExited);
}

BridgeStats Bridge::Snapshot() const {
  DirectionSnapshot tx = multitether::Snapshot(counters_.tx);

  tx.io_errors += data_channel_->AsyncSendErrors();

  return BridgeStats{
      .rx = multitether::Snapshot(counters_.rx),
      .tx = tx,
      .rx_queue_depth = rx_ring_->SizeSnapshot(),
      .link_kernel_drops = link_kernel_drops_.load(std::memory_order_relaxed),
      .tx_backpressure_events = tx_backpressure_events_.load(std::memory_order_relaxed),
  };
}

std::string FormatStatsLine(const BridgeStats& previous, const BridgeStats& current,
                            double seconds) {
  const DirectionSnapshot rx_delta = current.rx - previous.rx;
  const DirectionSnapshot tx_delta = current.tx - previous.tx;

  return Tr(
      Msg::kCoreStatsLine,
      ToPacketsPerSecond(rx_delta.frames, seconds),
      ToMegabitsPerSecond(rx_delta.bytes, seconds), rx_delta.TotalDropped(),
      ToPacketsPerSecond(tx_delta.frames, seconds),
      ToMegabitsPerSecond(tx_delta.bytes, seconds), tx_delta.TotalDropped(),
      current.rx_queue_depth, current.link_kernel_drops - previous.link_kernel_drops,
      current.tx_backpressure_events - previous.tx_backpressure_events);
}
}  // namespace multitether::core
