#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "multitether/common/error.h"
#include "multitether/rndis/protocol.h"

namespace multitether::rndis {
using MacAddress = std::array<std::uint8_t, 6>;

[[nodiscard]] std::array<char, 18> FormatMac(const MacAddress& mac) noexcept;

struct MessageHeader {
  std::uint32_t message_type = 0;
  std::uint32_t message_length = 0;
};

[[nodiscard]] Result<MessageHeader> DecodeMessageHeader(std::span<const std::byte> buffer);

// =============================================================================
// INITIALIZE
// =============================================================================

struct InitializeRequest {
  std::uint32_t request_id = 0;
  std::uint32_t major_version = kMajorVersion;
  std::uint32_t minor_version = kMinorVersion;

  std::uint32_t max_transfer_size = 0;
};

[[nodiscard]] Result<std::uint32_t> Encode(const InitializeRequest& request,
                                          std::span<std::byte> buffer);

struct InitializeComplete {
  std::uint32_t request_id = 0;
  std::uint32_t status = 0;
  std::uint32_t major_version = 0;
  std::uint32_t minor_version = 0;
  std::uint32_t device_flags = 0;
  std::uint32_t medium = 0;

  std::uint32_t max_packets_per_message = 0;

  std::uint32_t max_transfer_size = 0;

  std::uint32_t packet_alignment_factor = 0;
  std::uint32_t af_list_offset = 0;
  std::uint32_t af_list_size = 0;
};

[[nodiscard]] Result<InitializeComplete> DecodeInitializeComplete(std::span<const std::byte> buffer);

struct NegotiatedParameters {
  std::uint32_t device_max_transfer_size = 0;
  std::uint32_t max_packets_per_message = 1;
  std::uint32_t tx_alignment_bytes = 1;
  std::uint32_t mtu = kDefaultMtu;
  bool connectionless = true;
};

[[nodiscard]] Result<NegotiatedParameters> Negotiate(const InitializeComplete& complete,
                                                     std::uint32_t requested_mtu,
                                                     std::uint32_t host_transfer_size_limit);

// =============================================================================
// HALT
// =============================================================================

[[nodiscard]] Result<std::uint32_t> EncodeHalt(std::uint32_t request_id,
                                               std::span<std::byte> buffer);

// =============================================================================
// QUERY / SET
// =============================================================================

struct QueryRequest {
  std::uint32_t request_id = 0;
  Oid oid = Oid::kGenSupportedList;

  std::uint32_t expected_response_bytes = 0;
};

[[nodiscard]] Result<std::uint32_t> Encode(const QueryRequest& request,
                                          std::span<std::byte> buffer);

struct QueryComplete {
  std::uint32_t request_id = 0;
  std::uint32_t status = 0;

  std::span<const std::byte> information;
};

[[nodiscard]] Result<QueryComplete> DecodeQueryComplete(std::span<const std::byte> buffer);

struct SetRequest {
  std::uint32_t request_id = 0;
  Oid oid = Oid::kGenCurrentPacketFilter;
  std::span<const std::byte> information;
};

[[nodiscard]] Result<std::uint32_t> Encode(const SetRequest& request, std::span<std::byte> buffer);

[[nodiscard]] Result<std::uint32_t> EncodeSetUint32(std::uint32_t request_id, Oid oid,
                                                    std::uint32_t value,
                                                    std::span<std::byte> buffer);

struct SetComplete {
  std::uint32_t request_id = 0;
  std::uint32_t status = 0;
};

[[nodiscard]] Result<SetComplete> DecodeSetComplete(std::span<const std::byte> buffer);

// =============================================================================
// RESET
// =============================================================================

[[nodiscard]] Result<std::uint32_t> EncodeReset(std::span<std::byte> buffer);

struct ResetComplete {
  std::uint32_t status = 0;

  bool addressing_reset = false;
};

[[nodiscard]] Result<ResetComplete> DecodeResetComplete(std::span<const std::byte> buffer);

// =============================================================================
// KEEPALIVE
// =============================================================================

[[nodiscard]] Result<std::uint32_t> EncodeKeepAlive(std::uint32_t request_id,
                                                    std::span<std::byte> buffer);

struct KeepAliveComplete {
  std::uint32_t request_id = 0;
  std::uint32_t status = 0;
};

[[nodiscard]] Result<KeepAliveComplete> DecodeKeepAliveComplete(std::span<const std::byte> buffer);

[[nodiscard]] Result<std::uint32_t> EncodeKeepAliveComplete(std::uint32_t request_id,
                                                            std::uint32_t status,
                                                            std::span<std::byte> buffer);

// =============================================================================
// INDICATE_STATUS
// =============================================================================

struct IndicateStatus {
  std::uint32_t status = 0;

  std::span<const std::byte> status_buffer;

  bool has_diagnostic_info = false;
  std::uint32_t diagnostic_status = 0;
  std::uint32_t diagnostic_error_offset = 0;
};

[[nodiscard]] Result<IndicateStatus> DecodeIndicateStatus(std::span<const std::byte> buffer);

[[nodiscard]] Result<std::uint32_t> ParseUint32(std::span<const std::byte> information);

[[nodiscard]] Result<std::uint64_t> ParseCounter(std::span<const std::byte> information);

[[nodiscard]] Result<MacAddress> ParseMac(std::span<const std::byte> information);
}  // namespace multitether::rndis
