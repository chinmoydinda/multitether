#include "multitether/rndis/state_machine.h"

#include <algorithm>
#include <chrono>
#include <format>
#include <thread>

#include "multitether/common/byte_order.h"
#include "multitether/common/i18n.h"
#include "multitether/common/logging.h"

namespace multitether::rndis {
namespace {
constexpr std::uint32_t kFixedOidPlaceholderBytes = 48;

[[nodiscard]] std::string ToPrintableString(std::span<const std::byte> bytes) {
  std::string text;
  text.reserve(bytes.size());
  for (const std::byte byte : bytes) {
    const auto character = static_cast<char>(byte);
    if (character == '\0') {
      break;
    }

    text.push_back(character >= 0x20 && character < 0x7F ? character : '?');
  }
  return text;
}
}  // namespace

std::string_view StateName(State state) noexcept {
  switch (state) {
    case State::kUninitialized:
      return Text(Msg::kRndisStateUninitialized);
    case State::kInitializing:
      return Text(Msg::kRndisStateInitializing);
    case State::kInitialized:
      return Text(Msg::kRndisStateInitialized);
    case State::kDataInitialized:
      return Text(Msg::kRndisStateDataInitialized);
    case State::kHalting:
      return Text(Msg::kRndisStateHalting);
  }
  return Text(Msg::kRndisStateUnknown);
}

StateMachine::StateMachine(ControlChannel& channel, StateMachineObserver& observer,
                           const StateMachineConfig& config)
    : channel_(&channel), observer_(&observer), config_(config) {
  request_buffer_.resize(kControlBufferBytes);
  last_device_activity_ = MonotonicNanos();
  next_keepalive_deadline_ =
      last_device_activity_ +
      static_cast<Nanos>(config_.keepalive_interval_millis) * kNanosPerMilli;
}

std::uint32_t StateMachine::NextRequestId() noexcept {
  const std::uint32_t id = next_request_id_++;
  if (next_request_id_ == 0) {
    next_request_id_ = 1;
  }
  return id;
}

void StateMachine::TransitionTo(State next) {
  if (state_ == next) {
    return;
  }
  const State previous = state_;
  state_ = next;
  MULTITETHER_INFO_TR(Msg::kRndisStateTransition, StateName(previous), StateName(next));
  observer_->OnStateChanged(previous, next);
}

Result<bool> StateMachine::PumpOnce(std::optional<MessageType> expected_reply,
                                   std::span<const std::byte>& out_response) {
  MULTITETHER_ASSIGN_OR_RETURN(const std::span<const std::byte> message,
                             channel_->ReceiveMessage());

  if (message.empty()) {
    return false;
  }
  MarkDeviceActivity();

  MULTITETHER_ASSIGN_OR_RETURN(const MessageHeader header, DecodeMessageHeader(message));
  const std::string_view name = MessageTypeName(header.message_type);
  MULTITETHER_DEBUG_TR(Msg::kRndisControlMessageReceived,
                     name.empty() ? Text(Msg::kRndisUnknownMessage) : name, header.message_type,
                     header.message_length);

  if (header.message_type == ToRaw(MessageType::kIndicateStatus)) {
    HandleIndicateStatus(message);
    return false;
  }
  if (header.message_type == ToRaw(MessageType::kKeepAlive)) {
    MULTITETHER_RETURN_IF_ERROR(HandleDeviceKeepAlive(message));
    return false;
  }

  if (IsCondis(header.message_type)) {
    return std::unexpected(Error::Generic(Tr(Msg::kRndisCondisMessageReceived,
                                             name.empty() ? Text(Msg::kRndisUnknown) : name)));
  }

  if (expected_reply.has_value() && header.message_type == ToRaw(*expected_reply)) {
    out_response = message;
    return true;
  }

  MULTITETHER_WARN_TR(Msg::kRndisDiscardUnexpectedMessage,
                    name.empty() ? Text(Msg::kRndisUnknown) : name, header.message_type);
  return false;
}

Result<StateMachine::Exchange> StateMachine::Transact(std::span<const std::byte> request,
                                                      MessageType expected_reply) {
  MULTITETHER_RETURN_IF_ERROR(channel_->SendMessage(request));

  const std::uint32_t notification_timeout =
      std::max(kProbeOnlyTimeoutMillis,
               std::min(config_.control_timeout_millis, config_.response_poll_interval_millis * 4));
  const NotificationResult notification =
      channel_->WaitForNotification(notification_timeout);
  if (notification == NotificationResult::kResponseAvailable) {
    MULTITETHER_TRACE_TR(Msg::kRndisNotificationReceived);
  }

  std::span<const std::byte> response;
  for (std::uint32_t attempt = 0; attempt < config_.response_poll_attempts; ++attempt) {
    MULTITETHER_ASSIGN_OR_RETURN(const bool matched, PumpOnce(expected_reply, response));
    if (matched) {
      return Exchange{.response = response};
    }

    std::this_thread::sleep_for(
        std::chrono::milliseconds(config_.response_poll_interval_millis));
  }

  const std::string_view name = MessageTypeName(ToRaw(expected_reply));
  return std::unexpected(Error::Generic(
      Tr(Msg::kRndisWaitReplyTimeout, name.empty() ? Text(Msg::kRndisResponse) : name,
         config_.response_poll_attempts, config_.response_poll_interval_millis)));
}

void StateMachine::HandleIndicateStatus(std::span<const std::byte> message) {
  const auto indication = DecodeIndicateStatus(message);
  if (!indication) {
    MULTITETHER_WARN_TR(Msg::kRndisIndicateStatusParseFailed, indication.error().ToString());
    return;
  }

  const std::string_view status_name = StatusName(indication->status);
  if (indication->has_diagnostic_info) {
    MULTITETHER_WARN_TR(Msg::kRndisDeviceIndicatedWithDiagnostics,
                      status_name.empty() ? Text(Msg::kRndisUnknownStatus) : status_name,
                      indication->status, indication->diagnostic_status,
                      indication->diagnostic_error_offset);
  } else {
    MULTITETHER_INFO_TR(Msg::kRndisDeviceIndicated,
                      status_name.empty() ? Text(Msg::kRndisUnknownStatus) : status_name,
                      indication->status);
  }

  switch (static_cast<StatusCode>(indication->status)) {
    case StatusCode::kMediaConnect:
      if (!link_connected_) {
        link_connected_ = true;
        info_.media_state = MediaState::kConnected;
        observer_->OnLinkStateChanged(true);
      }
      break;

    case StatusCode::kMediaDisconnect:
      if (link_connected_) {
        link_connected_ = false;
        info_.media_state = MediaState::kDisconnected;
        observer_->OnLinkStateChanged(false);
      }
      break;

    default:
      break;
  }
}

Status StateMachine::HandleDeviceKeepAlive(std::span<const std::byte> message) {
  if (message.size() < kKeepAliveMsgBytes) {
    return std::unexpected(Error::Generic(Tr(Msg::kRndisKeepAliveMsgTooShort)));
  }
  const std::uint32_t request_id = LoadLe32(message.data() + kKeepAliveRequestIdOffset);
  MULTITETHER_DEBUG_TR(Msg::kRndisDeviceKeepAlive, request_id);

  MULTITETHER_ASSIGN_OR_RETURN(
      const std::uint32_t written,
      EncodeKeepAliveComplete(request_id, ToRaw(StatusCode::kSuccess), request_buffer_));
  return channel_->SendMessage(std::span<const std::byte>{request_buffer_.data(), written});
}

Result<std::span<const std::byte>> StateMachine::QueryOid(Oid oid, std::uint32_t expected_bytes,
                                                          bool fatal) {
  const std::string_view name = OidName(ToRaw(oid));
  MULTITETHER_ASSIGN_OR_RETURN(
      const std::uint32_t written,
      Encode(QueryRequest{.request_id = NextRequestId(),
                          .oid = oid,
                          .expected_response_bytes = expected_bytes},
             request_buffer_));

  auto exchange = Transact(std::span<const std::byte>{request_buffer_.data(), written},
                           MessageType::kQueryComplete);
  if (!exchange) {
    if (!fatal) {
      MULTITETHER_DEBUG_TR(Msg::kRndisOptionalOidQueryFailed, name, exchange.error().ToString());
      return std::span<const std::byte>{};
    }
    return std::unexpected(
        std::move(exchange).error().WithContext(Tr(Msg::kRndisOidQueryFailed, name)));
  }

  MULTITETHER_ASSIGN_OR_RETURN(const QueryComplete complete,
                             DecodeQueryComplete(exchange->response));

  if (complete.status != ToRaw(StatusCode::kSuccess)) {
    const std::string_view status_name = StatusName(complete.status);
    if (!fatal) {
      MULTITETHER_DEBUG_TR(Msg::kRndisOptionalOidUnsupported, name,
                         status_name.empty() ? Text(Msg::kRndisUnknownStatus) : status_name);
      return std::span<const std::byte>{};
    }
    return std::unexpected(Error::FromRndisStatus(
        complete.status,
        Tr(Msg::kRndisOidQueryRejected, name,
           status_name.empty() ? Text(Msg::kRndisUnknownStatus) : status_name)));
  }
  return complete.information;
}

Status StateMachine::SetOidUint32(Oid oid, std::uint32_t value) {
  const std::string_view name = OidName(ToRaw(oid));
  MULTITETHER_ASSIGN_OR_RETURN(
      const std::uint32_t written,
      EncodeSetUint32(NextRequestId(), oid, value, request_buffer_));

  MULTITETHER_ASSIGN_OR_RETURN(
      const Exchange exchange,
      Transact(std::span<const std::byte>{request_buffer_.data(), written},
               MessageType::kSetComplete));

  MULTITETHER_ASSIGN_OR_RETURN(const SetComplete complete, DecodeSetComplete(exchange.response));
  if (complete.status != ToRaw(StatusCode::kSuccess)) {
    const std::string_view status_name = StatusName(complete.status);
    return std::unexpected(Error::FromRndisStatus(
        complete.status,
        Tr(Msg::kRndisOidSetRejected, name, value,
           status_name.empty() ? Text(Msg::kRndisUnknownStatus) : status_name)));
  }
  MULTITETHER_DEBUG_TR(Msg::kRndisOidSet, name, value);
  return Ok();
}

Status StateMachine::CollectDeviceInfo() {
  if (const auto medium = QueryOid(Oid::kGenPhysicalMedium, 4, /*fatal=*/false);
      medium && !medium->empty()) {
    if (const auto value = ParseUint32(*medium)) {
      info_.physical_medium = static_cast<PhysicalMedium>(*value);
    }
  }

  {
    MULTITETHER_ASSIGN_OR_RETURN(
        const std::span<const std::byte> payload,
        QueryOid(Oid::kEthernetPermanentAddress, kFixedOidPlaceholderBytes, /*fatal=*/true));
    MULTITETHER_ASSIGN_OR_RETURN(info_.permanent_address, ParseMac(payload));
    info_.has_permanent_address = true;
    MULTITETHER_INFO_TR(Msg::kRndisPermanentMac, FormatMac(info_.permanent_address).data());
  }

  if (const auto payload =
          QueryOid(Oid::kEthernetCurrentAddress, kFixedOidPlaceholderBytes, /*fatal=*/false);
      payload && !payload->empty()) {
    if (const auto mac = ParseMac(*payload)) {
      info_.current_address = *mac;
      info_.has_current_address = true;
    }
  }
  if (!info_.has_current_address) {
    info_.current_address = info_.permanent_address;
    info_.has_current_address = true;
  }

  if (const auto payload =
          QueryOid(Oid::kGenMaximumFrameSize, 4, /*fatal=*/false);
      payload && !payload->empty()) {
    if (const auto value = ParseUint32(*payload)) {
      info_.maximum_frame_size = *value;

      if (*value != 0 && *value < parameters_.mtu) {
        MULTITETHER_WARN_TR(Msg::kRndisMtuLoweredToDeviceFrameSize, *value, parameters_.mtu);
        parameters_.mtu = *value;
      }
    }
  }

  if (const auto payload = QueryOid(Oid::kGenLinkSpeed, 4, /*fatal=*/false);
      payload && !payload->empty()) {
    if (const auto value = ParseUint32(*payload)) {
      info_.link_speed_100bps = *value;
    }
  }

  if (const auto payload = QueryOid(Oid::kGenMediaConnectStatus, 4, /*fatal=*/false);
      payload && !payload->empty()) {
    if (const auto value = ParseUint32(*payload)) {
      info_.media_state = static_cast<MediaState>(*value);
      link_connected_ = info_.media_state == MediaState::kConnected;
    }
  } else {
    link_connected_ = true;
  }

  if (const auto payload = QueryOid(Oid::kGenVendorId, 4, /*fatal=*/false);
      payload && !payload->empty()) {
    if (const auto value = ParseUint32(*payload)) {
      info_.vendor_id = *value;
    }
  }

  if (const auto payload = QueryOid(Oid::kGenVendorDescription, 0, /*fatal=*/false);
      payload && !payload->empty()) {
    info_.vendor_description = ToPrintableString(*payload);
  }

  MULTITETHER_INFO_TR(Msg::kRndisDeviceInfo, FormatMac(info_.permanent_address).data(),
                    info_.LinkSpeedMbps(), info_.maximum_frame_size,
                    static_cast<std::uint32_t>(info_.physical_medium), info_.vendor_id,
                    info_.vendor_description);

  return Ok();
}

Status StateMachine::Start() {
  if (state_ != State::kUninitialized) {
    return std::unexpected(Error::Generic(Tr(Msg::kRndisStartWrongState, StateName(state_))));
  }

  TransitionTo(State::kInitializing);

  {
    MULTITETHER_ASSIGN_OR_RETURN(
        const std::uint32_t written,
        Encode(InitializeRequest{.request_id = NextRequestId(),
                                 .max_transfer_size = config_.host_max_transfer_size},
               request_buffer_));

    auto exchange = Transact(std::span<const std::byte>{request_buffer_.data(), written},
                              MessageType::kInitializeComplete);
    if (!exchange) {
      TransitionTo(State::kUninitialized);
      return std::unexpected(std::move(exchange).error().WithContext(Tr(Msg::kRndisInitFailed)));
    }

    auto complete = DecodeInitializeComplete(exchange->response);
    if (!complete) {
      SendHalt();
      TransitionTo(State::kUninitialized);
      return std::unexpected(std::move(complete).error());
    }

    auto negotiated = Negotiate(*complete, config_.requested_mtu, config_.host_max_transfer_size);
    if (!negotiated) {
      SendHalt();
      TransitionTo(State::kUninitialized);
      return std::unexpected(std::move(negotiated).error());
    }
    parameters_ = *negotiated;

    if (config_.max_tx_packets_per_message != 0 &&
        parameters_.max_packets_per_message > config_.max_tx_packets_per_message) {
      MULTITETHER_INFO_TR(Msg::kRndisMaxPacketsClamped, parameters_.max_packets_per_message,
                        config_.max_tx_packets_per_message);
      parameters_.max_packets_per_message = config_.max_tx_packets_per_message;
    }

    MULTITETHER_INFO_TR(Msg::kRndisNegotiated, complete->major_version, complete->minor_version,
                      parameters_.mtu, parameters_.device_max_transfer_size,
                      parameters_.max_packets_per_message, parameters_.tx_alignment_bytes);
  }

  TransitionTo(State::kInitialized);

  if (const auto status = CollectDeviceInfo(); !status) {
    SendHalt();
    TransitionTo(State::kUninitialized);
    return status;
  }

  if (const auto status = SetOidUint32(Oid::kGenCurrentPacketFilter, config_.packet_filter);
      !status) {
    SendHalt();
    TransitionTo(State::kUninitialized);
    return std::unexpected(Error{status.error()}.WithContext(Tr(Msg::kRndisPacketFilterFailed)));
  }
  active_packet_filter_ = config_.packet_filter;

  TransitionTo(State::kDataInitialized);
  observer_->OnNegotiated(parameters_, info_);
  observer_->OnLinkStateChanged(link_connected_);

  MarkDeviceActivity();
  next_keepalive_deadline_ =
      MonotonicNanos() +
      static_cast<Nanos>(config_.keepalive_interval_millis) * kNanosPerMilli;
  return Ok();
}

std::uint32_t StateMachine::MillisUntilNextPoll() const noexcept {
  const Nanos now = MonotonicNanos();
  if (now >= next_keepalive_deadline_) {
    return 0;
  }
  return static_cast<std::uint32_t>((next_keepalive_deadline_ - now) / kNanosPerMilli);
}

Status StateMachine::Poll() {
  if (state_ != State::kDataInitialized && state_ != State::kInitialized) {
    return Ok();
  }

  if (channel_->WaitForNotification(kProbeOnlyTimeoutMillis) ==
      NotificationResult::kResponseAvailable) {
    std::span<const std::byte> ignored;

    for (int i = 0; i < 8; ++i) {
      const auto pumped = PumpOnce(std::nullopt, ignored);
      if (!pumped) {
        return std::unexpected(
            Error{pumped.error()}.WithContext(Tr(Msg::kRndisDrainControlFailed)));
      }

      if (channel_->WaitForNotification(kProbeOnlyTimeoutMillis) !=
          NotificationResult::kResponseAvailable) {
        break;
      }
    }
  }

  const Nanos now = MonotonicNanos();
  if (now < next_keepalive_deadline_) {
    return Ok();
  }

  const Nanos idle_nanos = now - last_device_activity_;
  const Nanos interval_nanos =
      static_cast<Nanos>(config_.keepalive_interval_millis) * kNanosPerMilli;
  next_keepalive_deadline_ = now + interval_nanos;

  if (idle_nanos < interval_nanos) {
    return Ok();
  }

  MULTITETHER_ASSIGN_OR_RETURN(const std::uint32_t written,
                             EncodeKeepAlive(NextRequestId(), request_buffer_));
  auto exchange = Transact(std::span<const std::byte>{request_buffer_.data(), written},
                            MessageType::kKeepAliveComplete);
  if (!exchange) {
    ++consecutive_keepalive_failures_;
    MULTITETHER_WARN_TR(Msg::kRndisKeepAliveFailed, consecutive_keepalive_failures_,
                      config_.keepalive_failure_threshold, exchange.error().ToString());
    if (consecutive_keepalive_failures_ >= config_.keepalive_failure_threshold) {
      Error fatal = std::move(exchange).error().WithContext(
          Tr(Msg::kRndisKeepAliveDeadLink, consecutive_keepalive_failures_));
      observer_->OnFatalError(fatal);
      return std::unexpected(std::move(fatal));
    }
    return Ok();
  }

  const auto complete = DecodeKeepAliveComplete(exchange->response);
  if (!complete) {
    ++consecutive_keepalive_failures_;
    return Ok();
  }

  if (complete->status != ToRaw(StatusCode::kSuccess)) {
    const std::string_view name = StatusName(complete->status);
    MULTITETHER_WARN_TR(Msg::kRndisKeepAliveRejected,
                      name.empty() ? Text(Msg::kRndisUnknownStatus) : name);
    return Reset();
  }

  consecutive_keepalive_failures_ = 0;
  MULTITETHER_TRACE_TR(Msg::kRndisKeepAliveOk);
  return Ok();
}

Status StateMachine::Reset() {
  MULTITETHER_INFO_TR(Msg::kRndisResetStarted);

  MULTITETHER_ASSIGN_OR_RETURN(const std::uint32_t written, EncodeReset(request_buffer_));
  MULTITETHER_ASSIGN_OR_RETURN(
      const Exchange exchange,
      Transact(std::span<const std::byte>{request_buffer_.data(), written},
               MessageType::kResetComplete));

  MULTITETHER_ASSIGN_OR_RETURN(const ResetComplete complete,
                             DecodeResetComplete(exchange.response));

  if (complete.status != ToRaw(StatusCode::kSuccess)) {
    const std::string_view name = StatusName(complete.status);
    return std::unexpected(Error::FromRndisStatus(
        complete.status, Tr(Msg::kRndisResetRejected,
                            name.empty() ? Text(Msg::kRndisUnknownStatus) : name)));
  }

  MULTITETHER_INFO_TR(Msg::kRndisResetDone, Text(complete.addressing_reset
                                                    ? Msg::kRndisAddressingLost
                                                    : Msg::kRndisAddressingKept));
  observer_->OnDeviceReset(complete.addressing_reset);

  if (complete.addressing_reset) {
    const std::uint32_t filter =
        active_packet_filter_ != 0 ? active_packet_filter_ : config_.packet_filter;
    MULTITETHER_RETURN_IF_ERROR(SetOidUint32(Oid::kGenCurrentPacketFilter, filter));
    active_packet_filter_ = filter;
    MULTITETHER_INFO_TR(Msg::kRndisPacketFilterReplayed, filter);
  }

  consecutive_keepalive_failures_ = 0;
  MarkDeviceActivity();
  return Ok();
}

void StateMachine::SendHalt() noexcept {
  const auto written = EncodeHalt(NextRequestId(), request_buffer_);
  if (!written) {
    MULTITETHER_WARN_TR(Msg::kRndisEncodeHaltFailed, written.error().ToString());
    return;
  }
  if (const auto status =
          channel_->SendMessage(std::span<const std::byte>{request_buffer_.data(), *written});
      !status) {
    MULTITETHER_DEBUG_TR(Msg::kRndisSendHaltFailed, status.error().ToString());
    return;
  }
  MULTITETHER_DEBUG_TR(Msg::kRndisHaltSent);
}

void StateMachine::Stop() {
  if (state_ == State::kUninitialized || state_ == State::kHalting) {
    return;
  }
  TransitionTo(State::kHalting);

  if (active_packet_filter_ != 0) {
    if (const auto status = SetOidUint32(Oid::kGenCurrentPacketFilter, 0); !status) {
      MULTITETHER_DEBUG_TR(Msg::kRndisClearPacketFilterFailed, status.error().ToString());
    } else {
      active_packet_filter_ = 0;
    }
  }

  SendHalt();
  TransitionTo(State::kUninitialized);
}
}  // namespace multitether::rndis
