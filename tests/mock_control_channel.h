#pragma once

#include <cstddef>
#include <cstdint>
#include <array>
#include <cstring>
#include <deque>
#include <functional>
#include <span>
#include <string>
#include <vector>

#include "multitether/common/byte_order.h"
#include "multitether/rndis/control_channel.h"
#include "multitether/rndis/messages.h"
#include "multitether/rndis/protocol.h"
#include "multitether/rndis/state_machine.h"

namespace multitether::testing {
class MockControlChannel final : public rndis::ControlChannel {
 public:
  MockControlChannel() = default;

  [[nodiscard]] Status SendMessage(std::span<const std::byte> message) override {
    if (send_failure_countdown_ > 0) {
      --send_failure_countdown_;
      if (send_failure_countdown_ == 0) {
        return std::unexpected(Error::Generic("mock: injected send failure"));
      }
    }
    sent_messages_.emplace_back(message.begin(), message.end());

    if (request_handler_) {
      request_handler_(message, *this);
    }
    return Ok();
  }

  [[nodiscard]] Result<std::span<const std::byte>> ReceiveMessage() override {
    ++receive_call_count_;
    if (receive_failure_countdown_ > 0) {
      --receive_failure_countdown_;
      if (receive_failure_countdown_ == 0) {
        return std::unexpected(Error::Generic("mock: injected receive failure"));
      }
    }
    if (pending_responses_.empty()) {
      return std::span<const std::byte>{};
    }
    current_response_ = std::move(pending_responses_.front());
    pending_responses_.pop_front();
    return std::span<const std::byte>{current_response_};
  }

  [[nodiscard]] rndis::NotificationResult WaitForNotification(
      std::uint32_t timeout_millis) override {
    ++notification_call_count_;

    notification_timeouts_.push_back(timeout_millis);
    if (!has_interrupt_endpoint_) {
      return rndis::NotificationResult::kNotSupported;
    }
    return pending_responses_.empty() ? rndis::NotificationResult::kTimeout
                                      : rndis::NotificationResult::kResponseAvailable;
  }

  [[nodiscard]] std::uint32_t TimeoutMillis() const noexcept override { return 100; }

  [[nodiscard]] std::string_view Describe() const noexcept override { return description_; }

  using RequestHandler =
      std::function<void(std::span<const std::byte> request, MockControlChannel& channel)>;

  void SetRequestHandler(RequestHandler handler) { request_handler_ = std::move(handler); }

  void EnqueueResponse(std::vector<std::byte> message) {
    pending_responses_.push_back(std::move(message));
  }

  void SetHasInterruptEndpoint(bool has) noexcept { has_interrupt_endpoint_ = has; }

  void FailSendOnCall(std::uint32_t call_index) noexcept {
    send_failure_countdown_ = call_index;
  }

  void FailReceiveOnCall(std::uint32_t call_index) noexcept {
    receive_failure_countdown_ = call_index;
  }

  [[nodiscard]] const std::vector<std::vector<std::byte>>& SentMessages() const noexcept {
    return sent_messages_;
  }

  [[nodiscard]] std::uint32_t SentMessageType(std::size_t index) const {
    if (index >= sent_messages_.size() ||
        sent_messages_[index].size() < rndis::kMessageHeaderBytes) {
      return 0;
    }
    return LoadLe32(sent_messages_[index].data() + rndis::kMessageTypeOffset);
  }

  [[nodiscard]] std::size_t CountSent(rndis::MessageType type) const {
    std::size_t count = 0;
    for (std::size_t i = 0; i < sent_messages_.size(); ++i) {
      if (SentMessageType(i) == rndis::ToRaw(type)) {
        ++count;
      }
    }
    return count;
  }

  [[nodiscard]] bool FindSetUint32(rndis::Oid oid, std::uint32_t& out_value,
                                   std::size_t skip = 0) const {
    for (const std::vector<std::byte>& message : sent_messages_) {
      if (message.size() < rndis::kQuerySetHeaderBytes + 4) {
        continue;
      }
      if (LoadLe32(message.data() + rndis::kMessageTypeOffset) !=
          rndis::ToRaw(rndis::MessageType::kSet)) {
        continue;
      }
      if (LoadLe32(message.data() + rndis::kQuerySetOidOffset) != rndis::ToRaw(oid)) {
        continue;
      }
      if (skip > 0) {
        --skip;
        continue;
      }
      out_value = LoadLe32(message.data() + rndis::kQuerySetHeaderBytes);
      return true;
    }
    return false;
  }

  [[nodiscard]] std::uint32_t ReceiveCallCount() const noexcept { return receive_call_count_; }

  [[nodiscard]] const std::vector<std::uint32_t>& NotificationTimeouts() const noexcept {
    return notification_timeouts_;
  }

  [[nodiscard]] std::size_t PendingResponseCount() const noexcept {
    return pending_responses_.size();
  }

  void ClearSentMessages() { sent_messages_.clear(); }

 private:
  std::string description_ = "mock device";
  RequestHandler request_handler_;

  std::vector<std::vector<std::byte>> sent_messages_;
  std::deque<std::vector<std::byte>> pending_responses_;
  std::vector<std::byte> current_response_;

  bool has_interrupt_endpoint_ = true;
  std::uint32_t send_failure_countdown_ = 0;
  std::uint32_t receive_failure_countdown_ = 0;
  std::uint32_t receive_call_count_ = 0;
  std::uint32_t notification_call_count_ = 0;
  std::vector<std::uint32_t> notification_timeouts_;
};

[[nodiscard]] inline std::vector<std::byte> MakeInitializeComplete(
    std::uint32_t request_id, std::uint32_t max_transfer_size = 2048,
    std::uint32_t max_packets = 1, std::uint32_t alignment_factor = 0,
    std::uint32_t status = 0) {
  std::vector<std::byte> message(rndis::kInitializeCmpltBytes);
  std::byte* base = message.data();
  StoreLe32(base + rndis::kMessageTypeOffset,
            rndis::ToRaw(rndis::MessageType::kInitializeComplete));
  StoreLe32(base + rndis::kMessageLengthOffset, rndis::kInitializeCmpltBytes);
  StoreLe32(base + rndis::kInitializeCmpltRequestIdOffset, request_id);
  StoreLe32(base + rndis::kInitializeCmpltStatusOffset, status);
  StoreLe32(base + rndis::kInitializeCmpltMajorVersionOffset, rndis::kMajorVersion);
  StoreLe32(base + rndis::kInitializeCmpltMinorVersionOffset, rndis::kMinorVersion);
  StoreLe32(base + rndis::kInitializeCmpltDeviceFlagsOffset,
            static_cast<std::uint32_t>(rndis::DeviceFlags::kConnectionless));
  StoreLe32(base + rndis::kInitializeCmpltMediumOffset,
            static_cast<std::uint32_t>(rndis::Medium::kEthernet));
  StoreLe32(base + rndis::kInitializeCmpltMaxPacketsPerMessageOffset, max_packets);
  StoreLe32(base + rndis::kInitializeCmpltMaxTransferSizeOffset, max_transfer_size);
  StoreLe32(base + rndis::kInitializeCmpltPacketAlignmentFactorOffset, alignment_factor);
  return message;
}

[[nodiscard]] inline std::vector<std::byte> MakeQueryComplete(
    std::uint32_t request_id, std::span<const std::byte> payload, std::uint32_t status = 0) {
  const auto payload_length = static_cast<std::uint32_t>(payload.size());
  const std::uint32_t total = rndis::kQueryCmpltHeaderBytes + payload_length;
  std::vector<std::byte> message(total);
  std::byte* base = message.data();
  StoreLe32(base + rndis::kMessageTypeOffset, rndis::ToRaw(rndis::MessageType::kQueryComplete));
  StoreLe32(base + rndis::kMessageLengthOffset, total);
  StoreLe32(base + rndis::kQueryCmpltRequestIdOffset, request_id);
  StoreLe32(base + rndis::kQueryCmpltStatusOffset, status);
  StoreLe32(base + rndis::kQueryCmpltInfoBufferLengthOffset, payload_length);
  StoreLe32(base + rndis::kQueryCmpltInfoBufferOffsetOffset,
            payload_length == 0 ? 0 : rndis::kQueryCmpltInlineInfoOffset);
  if (payload_length != 0) {
    std::memcpy(base + rndis::kQueryCmpltHeaderBytes, payload.data(), payload_length);
  }
  return message;
}

[[nodiscard]] inline std::vector<std::byte> MakeQueryCompleteUint32(std::uint32_t request_id,
                                                                   std::uint32_t value) {
  std::array<std::byte, 4> payload{};
  StoreLe32(payload.data(), value);
  return MakeQueryComplete(request_id, payload);
}

[[nodiscard]] inline std::vector<std::byte> MakeQueryCompleteMac(std::uint32_t request_id,
                                                                const rndis::MacAddress& mac) {
  std::array<std::byte, 6> payload{};
  std::memcpy(payload.data(), mac.data(), mac.size());
  return MakeQueryComplete(request_id, payload);
}

[[nodiscard]] inline std::vector<std::byte> MakeSetComplete(std::uint32_t request_id,
                                                           std::uint32_t status = 0) {
  std::vector<std::byte> message(rndis::kSetCmpltBytes);
  std::byte* base = message.data();
  StoreLe32(base + rndis::kMessageTypeOffset, rndis::ToRaw(rndis::MessageType::kSetComplete));
  StoreLe32(base + rndis::kMessageLengthOffset, rndis::kSetCmpltBytes);
  StoreLe32(base + rndis::kSetCmpltRequestIdOffset, request_id);
  StoreLe32(base + rndis::kSetCmpltStatusOffset, status);
  return message;
}

[[nodiscard]] inline std::vector<std::byte> MakeKeepAliveComplete(std::uint32_t request_id,
                                                                 std::uint32_t status = 0) {
  std::vector<std::byte> message(rndis::kKeepAliveCmpltBytes);
  std::byte* base = message.data();
  StoreLe32(base + rndis::kMessageTypeOffset,
            rndis::ToRaw(rndis::MessageType::kKeepAliveComplete));
  StoreLe32(base + rndis::kMessageLengthOffset, rndis::kKeepAliveCmpltBytes);
  StoreLe32(base + rndis::kKeepAliveCmpltRequestIdOffset, request_id);
  StoreLe32(base + rndis::kKeepAliveCmpltStatusOffset, status);
  return message;
}

[[nodiscard]] inline std::vector<std::byte> MakeDeviceKeepAlive(std::uint32_t request_id) {
  std::vector<std::byte> message(rndis::kKeepAliveMsgBytes);
  std::byte* base = message.data();
  StoreLe32(base + rndis::kMessageTypeOffset, rndis::ToRaw(rndis::MessageType::kKeepAlive));
  StoreLe32(base + rndis::kMessageLengthOffset, rndis::kKeepAliveMsgBytes);
  StoreLe32(base + rndis::kKeepAliveRequestIdOffset, request_id);
  return message;
}

[[nodiscard]] inline std::vector<std::byte> MakeResetComplete(bool addressing_reset,
                                                             std::uint32_t status = 0) {
  std::vector<std::byte> message(rndis::kResetCmpltBytes);
  std::byte* base = message.data();
  StoreLe32(base + rndis::kMessageTypeOffset, rndis::ToRaw(rndis::MessageType::kResetComplete));
  StoreLe32(base + rndis::kMessageLengthOffset, rndis::kResetCmpltBytes);
  StoreLe32(base + rndis::kResetCmpltStatusOffset, status);
  StoreLe32(base + rndis::kResetCmpltAddressingResetOffset, addressing_reset ? 1 : 0);
  return message;
}

[[nodiscard]] inline std::vector<std::byte> MakeIndicateStatus(rndis::StatusCode status) {
  std::vector<std::byte> message(rndis::kIndicateStatusHeaderBytes);
  std::byte* base = message.data();
  StoreLe32(base + rndis::kMessageTypeOffset, rndis::ToRaw(rndis::MessageType::kIndicateStatus));
  StoreLe32(base + rndis::kMessageLengthOffset, rndis::kIndicateStatusHeaderBytes);
  StoreLe32(base + rndis::kIndicateStatusStatusOffset, rndis::ToRaw(status));
  StoreLe32(base + rndis::kIndicateStatusBufferLengthOffset, 0);
  StoreLe32(base + rndis::kIndicateStatusBufferOffsetOffset, 0);
  return message;
}

[[nodiscard]] inline std::uint32_t RequestIdOf(std::span<const std::byte> request) {
  if (request.size() < 12) {
    return 0;
  }
  return LoadLe32(request.data() + 8);
}

[[nodiscard]] inline std::uint32_t OidOf(std::span<const std::byte> request) {
  if (request.size() < rndis::kQuerySetHeaderBytes) {
    return 0;
  }
  return LoadLe32(request.data() + rndis::kQuerySetOidOffset);
}

[[nodiscard]] inline MockControlChannel::RequestHandler MakeWellBehavedDevice(
    const rndis::MacAddress& mac, std::uint32_t max_transfer_size = 2048,
    std::uint32_t max_packets = 1, std::uint32_t alignment_factor = 0) {
  return [mac, max_transfer_size, max_packets, alignment_factor](
             std::span<const std::byte> request, MockControlChannel& channel) {
    if (request.size() < rndis::kMessageHeaderBytes) {
      return;
    }
    const std::uint32_t type = LoadLe32(request.data() + rndis::kMessageTypeOffset);
    const std::uint32_t request_id = RequestIdOf(request);

    if (type == rndis::ToRaw(rndis::MessageType::kInitialize)) {
      channel.EnqueueResponse(MakeInitializeComplete(request_id, max_transfer_size, max_packets,
                                                     alignment_factor));
      return;
    }
    if (type == rndis::ToRaw(rndis::MessageType::kQuery)) {
      const std::uint32_t oid = OidOf(request);
      if (oid == rndis::ToRaw(rndis::Oid::kEthernetPermanentAddress) ||
          oid == rndis::ToRaw(rndis::Oid::kEthernetCurrentAddress)) {
        channel.EnqueueResponse(MakeQueryCompleteMac(request_id, mac));
      } else if (oid == rndis::ToRaw(rndis::Oid::kGenMaximumFrameSize)) {
        channel.EnqueueResponse(MakeQueryCompleteUint32(request_id, 1500));
      } else if (oid == rndis::ToRaw(rndis::Oid::kGenLinkSpeed)) {
        channel.EnqueueResponse(MakeQueryCompleteUint32(request_id, 4'800'000));
      } else if (oid == rndis::ToRaw(rndis::Oid::kGenMediaConnectStatus)) {
        channel.EnqueueResponse(MakeQueryCompleteUint32(
            request_id, static_cast<std::uint32_t>(rndis::MediaState::kConnected)));
      } else if (oid == rndis::ToRaw(rndis::Oid::kGenVendorId)) {
        channel.EnqueueResponse(MakeQueryCompleteUint32(request_id, 0x0018D1));
      } else {
        channel.EnqueueResponse(MakeQueryComplete(request_id, {},
                                                  rndis::ToRaw(rndis::StatusCode::kNotSupported)));
      }
      return;
    }
    if (type == rndis::ToRaw(rndis::MessageType::kSet)) {
      channel.EnqueueResponse(MakeSetComplete(request_id));
      return;
    }
    if (type == rndis::ToRaw(rndis::MessageType::kKeepAlive)) {
      channel.EnqueueResponse(MakeKeepAliveComplete(request_id));
      return;
    }
    if (type == rndis::ToRaw(rndis::MessageType::kReset)) {
      channel.EnqueueResponse(MakeResetComplete(/*addressing_reset=*/true));
      return;
    }
  };
}

class RecordingObserver final : public rndis::StateMachineObserver {
 public:
  struct Transition {
    rndis::State from;
    rndis::State to;
  };

  void OnStateChanged(rndis::State from, rndis::State to) override {
    transitions.push_back(Transition{.from = from, .to = to});
  }

  void OnNegotiated(const rndis::NegotiatedParameters& parameters,
                    const rndis::DeviceInfo& info) override {
    ++negotiated_count;
    parameters_snapshot = parameters;
    info_snapshot = info;
  }

  void OnLinkStateChanged(bool connected) override { link_events.push_back(connected); }

  void OnDeviceReset(bool addressing_lost) override { reset_events.push_back(addressing_lost); }

  void OnFatalError(const Error& error) override { fatal_errors.push_back(error.ToString()); }

  std::vector<Transition> transitions;
  std::vector<bool> link_events;
  std::vector<bool> reset_events;
  std::vector<std::string> fatal_errors;
  std::uint32_t negotiated_count = 0;
  rndis::NegotiatedParameters parameters_snapshot{};
  rndis::DeviceInfo info_snapshot{};
};
}  // namespace multitether::testing
