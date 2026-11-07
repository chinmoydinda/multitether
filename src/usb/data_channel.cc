#include "multitether/usb/data_channel.h"

#include <sys/sysctl.h>

#include <cstdlib>
#include <format>

#include "multitether/common/i18n.h"
#include "multitether/common/logging.h"
#include "multitether/rndis/packet_codec.h"

namespace multitether::usb {
namespace {
[[nodiscard]] std::size_t QueryPageSize() {
  std::int64_t value = 0;
  std::size_t size = sizeof(value);
  if (::sysctlbyname("hw.pagesize", &value, &size, nullptr, 0) != 0 || value <= 0) {
    return 16384;
  }
  return static_cast<std::size_t>(value);
}
}  // namespace

Result<std::unique_ptr<UsbDataChannel>> UsbDataChannel::Create(
    Device& device, const rndis::NegotiatedParameters& parameters,
    const DataChannelConfig& config) {
  auto channel = std::unique_ptr<UsbDataChannel>(new UsbDataChannel());
  channel->device_ = &device;
  channel->parameters_ = parameters;
  channel->config_ = config;

  channel->tx_transfer_bytes_ =
      std::min(config.tx_transfer_bytes, parameters.device_max_transfer_size);
  if (channel->tx_transfer_bytes_ < rndis::kPacketMsgHeaderBytes + parameters.mtu +
                                        rndis::kEthernetHeaderBytes) {
    return std::unexpected(Error::Generic(Tr(
        Msg::kUsbTxBufferTooSmall, channel->tx_transfer_bytes_,
        rndis::kPacketMsgHeaderBytes + parameters.mtu + rndis::kEthernetHeaderBytes)));
  }

  MULTITETHER_RETURN_IF_ERROR(channel->AllocatePool(channel->rx_pool_, config.rx_transfer_count,
                                                  config.rx_transfer_bytes));
  MULTITETHER_RETURN_IF_ERROR(channel->AllocatePool(channel->tx_pool_, config.tx_transfer_count,
                                                  channel->tx_transfer_bytes_));

  channel->tx_free_slots_ = std::make_unique<SpscRing<std::uint32_t>>(config.tx_transfer_count);
  for (std::uint32_t i = 0; i < config.tx_transfer_count; ++i) {
    if (!channel->tx_free_slots_->TryPush(i)) {
      return std::unexpected(Error::Generic(Tr(Msg::kUsbTxSlotQueueInitFailed)));
    }
  }

  MULTITETHER_INFO_TR(
      Msg::kUsbDataChannelReady,
      config.rx_transfer_count, config.rx_transfer_bytes / 1024,
      config.rx_transfer_count * config.rx_transfer_bytes / 1024, config.tx_transfer_count,
      channel->tx_transfer_bytes_ / 1024, parameters.device_max_transfer_size,
      parameters.max_packets_per_message, parameters.tx_alignment_bytes);

  return channel;
}

UsbDataChannel::~UsbDataChannel() {
  Shutdown();
  FreePool(rx_pool_);
  FreePool(tx_pool_);
}

Status UsbDataChannel::AllocatePool(std::vector<Slot>& pool, std::uint32_t count,
                                    std::uint32_t buffer_bytes) {
  const std::size_t alignment = QueryPageSize();
  pool.resize(count);

  for (std::uint32_t i = 0; i < count; ++i) {
    Slot& slot = pool[i];
    slot.owner = this;
    slot.index = i;
    slot.buffer_bytes = buffer_bytes;

    void* raw = nullptr;

    const std::size_t allocation =
        ((buffer_bytes + alignment - 1) / alignment) * alignment;
    if (::posix_memalign(&raw, alignment, allocation) != 0 || raw == nullptr) {
      return std::unexpected(
          Error::Generic(Tr(Msg::kUsbTransferAllocFailed, allocation, alignment)));
    }
    slot.buffer = static_cast<std::byte*>(raw);

    slot.transfer = ::libusb_alloc_transfer(0);
    if (slot.transfer == nullptr) {
      return std::unexpected(Error::Generic(Tr(Msg::kUsbAllocTransferFailed)));
    }
  }
  return Ok();
}

void UsbDataChannel::FreePool(std::vector<Slot>& pool) noexcept {
  for (Slot& slot : pool) {
    if (slot.transfer != nullptr) {
      ::libusb_free_transfer(slot.transfer);
      slot.transfer = nullptr;
    }

    // NOLINTNEXTLINE(cppcoreguidelines-no-malloc,cppcoreguidelines-owning-memory)
    std::free(slot.buffer);
    slot.buffer = nullptr;
  }
  pool.clear();
}

Status UsbDataChannel::StartReceiving(FrameRing& rx_ring, DirectionCounters& rx_counters) {
  rx_ring_ = &rx_ring;
  rx_counters_ = &rx_counters;

  for (Slot& slot : rx_pool_) {
    MULTITETHER_RETURN_IF_ERROR(SubmitReceive(slot));
  }
  MULTITETHER_DEBUG_TR(Msg::kUsbBulkInSubmitted, rx_pool_.size());
  return Ok();
}

Status UsbDataChannel::SubmitReceive(Slot& slot) {
  ::libusb_fill_bulk_transfer(
      slot.transfer, device_->Handle(), device_->BulkInEndpoint(),
      reinterpret_cast<unsigned char*>(slot.buffer), static_cast<int>(slot.buffer_bytes),
      &UsbDataChannel::ReceiveCallbackTrampoline, &slot,
      static_cast<unsigned int>(config_.rx_timeout_millis));

  slot.transfer->flags = 0;

  outstanding_.fetch_add(1, std::memory_order_acq_rel);

  const int rc = ::libusb_submit_transfer(slot.transfer);
  if (rc != LIBUSB_SUCCESS) {
    outstanding_.fetch_sub(1, std::memory_order_acq_rel);
    return std::unexpected(Error::FromLibUsb(rc, Tr(Msg::kUsbSubmitBulkInFailed)));
  }
  return Ok();
}

void UsbDataChannel::ReceiveCallbackTrampoline(::libusb_transfer* transfer) {
  auto* slot = static_cast<Slot*>(transfer->user_data);
  slot->owner->OnReceiveComplete(*slot);
}

void UsbDataChannel::OnReceiveComplete(Slot& slot) noexcept {
  const ::libusb_transfer* transfer = slot.transfer;
  bool should_resubmit = true;

  switch (transfer->status) {
    case LIBUSB_TRANSFER_COMPLETED: {
      const auto received = static_cast<std::size_t>(transfer->actual_length);
      if (received > 0 && rx_ring_ != nullptr) {
        auto batch = rx_ring_->BeginBatchWrite();
        rndis::PacketMessageReader reader(
            std::span<const std::byte>{slot.buffer, received}, rx_ring_->MaxFrameBytes());

        std::span<const std::byte> frame;
        std::uint64_t frame_bytes = 0;
        while (true) {
          const rndis::ReadOutcome outcome = reader.Next(frame);
          if (outcome == rndis::ReadOutcome::kFrame) {
            if (!batch.Push(frame)) [[unlikely]] {
              rx_counters_->AddDroppedFull();
              continue;
            }
            frame_bytes += frame.size();
            continue;
          }
          if (outcome == rndis::ReadOutcome::kMalformed) {
            rx_counters_->AddDroppedMalformed();
            MULTITETHER_TRACE_TR(Msg::kUsbMalformedRndisMessage,
                               rndis::MalformedReasonName(reader.Reason()),
                               reader.FramesDecoded());
          }
          break;
        }
        if (batch.Staged() != 0) {
          const std::uint32_t staged = batch.Staged();
          batch.Publish();
          // NOLINTNEXTLINE(readability-suspicious-call-argument)
          rx_counters_->AddBatch(staged, frame_bytes);
        }
      }
      break;
    }

    case LIBUSB_TRANSFER_STALL:
      rx_counters_->AddIoError();
      MULTITETHER_WARN_TR(Msg::kUsbBulkInStall);
      if (const auto status = device_->ClearHalt(device_->BulkInEndpoint()); !status) {
        MULTITETHER_ERROR_TR(Msg::kUsbBulkInClearHaltFailed, status.error().ToString());
        should_resubmit = false;
      }
      break;

    case LIBUSB_TRANSFER_CANCELLED:
      should_resubmit = false;
      break;

    case LIBUSB_TRANSFER_NO_DEVICE:
      should_resubmit = false;
      MULTITETHER_INFO_TR(Msg::kUsbBulkInDeviceGone);
      break;

    case LIBUSB_TRANSFER_TIMED_OUT:
      rx_counters_->AddIoError();
      break;

    case LIBUSB_TRANSFER_ERROR:
    case LIBUSB_TRANSFER_OVERFLOW:
    default:
      rx_counters_->AddIoError();
      MULTITETHER_WARN_TR(Msg::kUsbBulkInFailed, static_cast<int>(transfer->status));
      break;
  }

  if (shutting_down_.load(std::memory_order_acquire)) {
    should_resubmit = false;
  }

  if (should_resubmit) {
    const int rc = ::libusb_submit_transfer(slot.transfer);
    if (rc == LIBUSB_SUCCESS) {
      return;
    }
    MULTITETHER_WARN_TR(Msg::kUsbBulkInResubmitFailed, ::libusb_error_name(rc));
  }

  if (outstanding_.fetch_sub(1, std::memory_order_acq_rel) == 1) {
    const std::lock_guard<std::mutex> guard(drain_mutex_);
    drain_condition_.notify_all();
  }
}

bool UsbDataChannel::CanSend() const noexcept {
  return tx_free_slots_ != nullptr && tx_free_slots_->SizeSnapshot() > 0;
}

Result<SendOutcome> UsbDataChannel::SendFrames(std::span<const FrameView> frames) {
  if (frames.empty()) {
    return SendOutcome{};
  }
  if (shutting_down_.load(std::memory_order_acquire)) {
    return std::unexpected(Error::Generic(Tr(Msg::kUsbDataChannelShuttingDown)));
  }

  SendOutcome outcome;

  while (outcome.consumed < frames.size()) {
    std::uint32_t slot_index = 0;
    if (!tx_free_slots_->TryPop(slot_index)) {
      break;
    }
    Slot& slot = tx_pool_[slot_index];

    rndis::PacketMessageWriter writer(
        std::span<std::byte>{slot.buffer, slot.buffer_bytes},
        rndis::PacketMessageWriter::Limits{
            .max_transfer_bytes = parameters_.device_max_transfer_size,
            .max_messages = parameters_.max_packets_per_message,
            .alignment_bytes = parameters_.tx_alignment_bytes,
        },
        device_->BulkMaxPacketSize());

    std::uint32_t scanned = 0;
    std::uint32_t appended = 0;
    bool append_failed = false;
    while (outcome.consumed + scanned < frames.size()) {
      const FrameView& frame = frames[outcome.consumed + scanned];
      if (frame.length < kMinEthernetFrameBytes) [[unlikely]] {
        ++scanned;
        ++outcome.skipped;
        continue;
      }
      if (!writer.TryAppend(frame.Bytes())) {
        append_failed = true;
        break;
      }
      ++scanned;
      ++appended;
    }

    if (writer.Empty()) {
      outcome.consumed += scanned;
      if (append_failed && outcome.consumed < frames.size()) {
        ++outcome.consumed;
        ++outcome.skipped;
      }
      if (!tx_free_slots_->TryPush(slot_index)) {
        MULTITETHER_ERROR_TR(Msg::kUsbReturnTxSlotFailed, slot_index);
      }
      continue;
    }

    const std::uint32_t transfer_bytes = writer.Finish();
    const std::uint64_t payload_bytes = writer.PayloadBytes();
    const std::uint32_t message_count = writer.MessageCount();

    ::libusb_fill_bulk_transfer(
        slot.transfer, device_->Handle(), device_->BulkOutEndpoint(),
        reinterpret_cast<unsigned char*>(slot.buffer), static_cast<int>(transfer_bytes),
        &UsbDataChannel::SendCallbackTrampoline, &slot,
        static_cast<unsigned int>(config_.tx_timeout_millis));
    slot.transfer->flags = 0;

    outstanding_.fetch_add(1, std::memory_order_acq_rel);
    const int rc = ::libusb_submit_transfer(slot.transfer);
    if (rc != LIBUSB_SUCCESS) {
      outstanding_.fetch_sub(1, std::memory_order_acq_rel);
      if (!tx_free_slots_->TryPush(slot_index)) {
        MULTITETHER_ERROR_TR(Msg::kUsbReturnTxSlotFailed, slot_index);
      }
      async_send_errors_.fetch_add(1, std::memory_order_relaxed);
      return std::unexpected(Error::FromLibUsb(rc, Tr(Msg::kUsbSubmitBulkOutFailed)));
    }

    MULTITETHER_TRACE_TR(Msg::kUsbBulkOutSubmitted, message_count, payload_bytes, transfer_bytes);
    outcome.consumed += scanned;
    outcome.sent_frames += appended;
    outcome.sent_bytes += payload_bytes;
  }

  return outcome;
}

bool UsbDataChannel::WaitForSendCapacity(std::uint32_t timeout_millis) {
  std::unique_lock<std::mutex> lock(send_capacity_mutex_);
  return send_capacity_cv_.wait_for(lock, std::chrono::milliseconds(timeout_millis), [this] {
    return shutting_down_.load(std::memory_order_acquire) ||
           tx_free_slots_->SizeSnapshot() > 0;
  });
}

void UsbDataChannel::SendCallbackTrampoline(::libusb_transfer* transfer) {
  auto* slot = static_cast<Slot*>(transfer->user_data);
  slot->owner->OnSendComplete(*slot);
}

void UsbDataChannel::OnSendComplete(Slot& slot) noexcept {
  switch (slot.transfer->status) {
    case LIBUSB_TRANSFER_COMPLETED:
      break;

    case LIBUSB_TRANSFER_STALL:
      async_send_errors_.fetch_add(1, std::memory_order_relaxed);
      MULTITETHER_WARN_TR(Msg::kUsbBulkOutStall);
      if (const auto status = device_->ClearHalt(device_->BulkOutEndpoint()); !status) {
        MULTITETHER_ERROR_TR(Msg::kUsbBulkOutClearHaltFailed, status.error().ToString());
      }
      break;

    case LIBUSB_TRANSFER_CANCELLED:
    case LIBUSB_TRANSFER_NO_DEVICE:
      break;

    default:
      async_send_errors_.fetch_add(1, std::memory_order_relaxed);
      MULTITETHER_WARN_TR(Msg::kUsbBulkOutFailed, static_cast<int>(slot.transfer->status));
      break;
  }

  if (!shutting_down_.load(std::memory_order_acquire)) {
    if (!tx_free_slots_->TryPush(slot.index)) {
      MULTITETHER_ERROR_TR(Msg::kUsbReturnTxSlotFailed, slot.index);
    }

    { const std::lock_guard<std::mutex> guard(send_capacity_mutex_); }
    send_capacity_cv_.notify_one();
  }

  if (outstanding_.fetch_sub(1, std::memory_order_acq_rel) == 1) {
    const std::lock_guard<std::mutex> guard(drain_mutex_);
    drain_condition_.notify_all();
  }
}

void UsbDataChannel::Shutdown() {
  if (shutdown_complete_) {
    return;
  }

  shutting_down_.store(true, std::memory_order_release);
  { const std::lock_guard<std::mutex> guard(send_capacity_mutex_); }
  send_capacity_cv_.notify_all();

  for (Slot& slot : rx_pool_) {
    if (slot.transfer != nullptr) {
      const int rc = ::libusb_cancel_transfer(slot.transfer);
      if (rc != LIBUSB_SUCCESS && rc != LIBUSB_ERROR_NOT_FOUND) {
        MULTITETHER_DEBUG_TR(Msg::kUsbCancelBulkInReturned, ::libusb_error_name(rc));
      }
    }
  }
  for (Slot& slot : tx_pool_) {
    if (slot.transfer != nullptr) {
      const int rc = ::libusb_cancel_transfer(slot.transfer);
      if (rc != LIBUSB_SUCCESS && rc != LIBUSB_ERROR_NOT_FOUND) {
        MULTITETHER_DEBUG_TR(Msg::kUsbCancelBulkOutReturned, ::libusb_error_name(rc));
      }
    }
  }

  {
    constexpr auto kDrainTimeout = std::chrono::seconds(5);
    std::unique_lock<std::mutex> lock(drain_mutex_);
    const bool drained = drain_condition_.wait_for(lock, kDrainTimeout, [this] {
      return outstanding_.load(std::memory_order_acquire) == 0;
    });
    if (!drained) {
      const std::uint32_t stuck = outstanding_.load(std::memory_order_acquire);
      MULTITETHER_ERROR_TR(Msg::kUsbTransferReclaimTimeout, stuck,
                         (stuck * config_.rx_transfer_bytes) / 1024);

      for (Slot& slot : rx_pool_) {
        slot.transfer = nullptr;
        slot.buffer = nullptr;
      }
      for (Slot& slot : tx_pool_) {
        slot.transfer = nullptr;
        slot.buffer = nullptr;
      }
    }
  }

  shutdown_complete_ = true;
  MULTITETHER_DEBUG_TR(Msg::kUsbDataChannelStopped);
}
}  // namespace multitether::usb
