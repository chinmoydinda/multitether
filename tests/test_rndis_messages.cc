#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include <doctest.h>

#include "multitether/common/byte_order.h"
#include "multitether/rndis/messages.h"
#include "multitether/rndis/protocol.h"

using namespace multitether;        // NOLINT(google-build-using-namespace)
using namespace multitether::rndis;  // NOLINT(google-build-using-namespace)

namespace {
std::vector<std::byte> MakeMessage(MessageType type, std::uint32_t message_length,
                                   std::uint32_t buffer_bytes = 0) {
  std::vector<std::byte> buffer(buffer_bytes == 0 ? message_length : buffer_bytes);
  StoreLe32(buffer.data() + kMessageTypeOffset, ToRaw(type));
  StoreLe32(buffer.data() + kMessageLengthOffset, message_length);
  return buffer;
}

std::vector<std::byte> MakeInitializeComplete(std::uint32_t max_transfer_size = 2048,
                                              std::uint32_t max_packets = 1,
                                              std::uint32_t alignment_factor = 0,
                                              std::uint32_t status = 0,
                                              std::uint32_t medium = 0,
                                              std::uint32_t device_flags = 1) {
  auto buffer = MakeMessage(MessageType::kInitializeComplete, kInitializeCmpltBytes);
  std::byte* base = buffer.data();
  StoreLe32(base + kInitializeCmpltRequestIdOffset, 0x1234'5678U);
  StoreLe32(base + kInitializeCmpltStatusOffset, status);
  StoreLe32(base + kInitializeCmpltMajorVersionOffset, 1);
  StoreLe32(base + kInitializeCmpltMinorVersionOffset, 0);
  StoreLe32(base + kInitializeCmpltDeviceFlagsOffset, device_flags);
  StoreLe32(base + kInitializeCmpltMediumOffset, medium);
  StoreLe32(base + kInitializeCmpltMaxPacketsPerMessageOffset, max_packets);
  StoreLe32(base + kInitializeCmpltMaxTransferSizeOffset, max_transfer_size);
  StoreLe32(base + kInitializeCmpltPacketAlignmentFactorOffset, alignment_factor);
  return buffer;
}
}  // namespace

TEST_SUITE("rndis.messages") {
TEST_CASE("rndis_messages scenario 1") {
  CHECK(ToRaw(MessageType::kPacket) == 0x0000'0001U);
  CHECK(ToRaw(MessageType::kInitialize) == 0x0000'0002U);
  CHECK(ToRaw(MessageType::kInitializeComplete) == 0x8000'0002U);
  CHECK(ToRaw(MessageType::kHalt) == 0x0000'0003U);
  CHECK(ToRaw(MessageType::kQueryComplete) == 0x8000'0004U);
  CHECK(ToRaw(MessageType::kResetComplete) == 0x8000'0006U);
  CHECK(ToRaw(MessageType::kIndicateStatus) == 0x0000'0007U);
  CHECK(ToRaw(MessageType::kKeepAliveComplete) == 0x8000'0008U);

  CHECK(ToRaw(StatusCode::kNotSupported) == 0xC000'00BBU);
  CHECK(ToRaw(StatusCode::kInvalidLength) == 0xC001'0014U);
  CHECK(ToRaw(StatusCode::kInvalidData) == 0xC001'0015U);
  CHECK(ToRaw(StatusCode::kInvalidOid) == 0xC001'0017U);
  CHECK(ToRaw(StatusCode::kMediaConnect) == 0x4001'000BU);
  CHECK(ToRaw(StatusCode::kMediaDisconnect) == 0x4001'000CU);

  CHECK(ToRaw(Oid::kGenCurrentPacketFilter) == 0x0001'010EU);
  CHECK(ToRaw(Oid::kGenMediaConnectStatus) == 0x0001'0114U);
  CHECK(ToRaw(Oid::kEthernetPermanentAddress) == 0x0101'0101U);
  CHECK(ToRaw(Oid::kGenPhysicalMedium) == 0x0001'0202U);

  CHECK(kDefaultPacketFilter == 0x0000'002DU);
  CHECK(kHardHeaderBytes == 58);
}

TEST_CASE("rndis_messages scenario 2: DataOffset") {
  CHECK(kPacketInlineDataOffset == 36);
  CHECK(kPacketMsgHeaderBytes == 44);
  CHECK(kOffsetFieldBase == 8);
  CHECK(kQuerySetInlineInfoOffset == 20);
  CHECK(kQueryCmpltInlineInfoOffset == 16);
}

TEST_CASE("rndis_messages scenario 3: MaxTransferSize Linux") {
  CHECK(MaxTransferSizeFor(1500, 512) == 2048);
  CHECK(MaxTransferSizeFor(1500, 64) == 1600);
  CHECK(HardMtuFor(1500) == 1558);
}

TEST_CASE("rndis_messages scenario 4: OID") {
  CHECK(StatusName(0xC000'00BBU) == "RNDIS_STATUS_NOT_SUPPORTED");
  CHECK(StatusName(0x4001'000BU) == "RNDIS_STATUS_MEDIA_CONNECT");
  CHECK(StatusName(0xDEAD'BEEFU).empty());
  CHECK(OidName(0x0001'010EU) == "OID_GEN_CURRENT_PACKET_FILTER");
  CHECK(OidName(0xDEAD'BEEFU).empty());
  CHECK(MessageTypeName(0x8000'0002U) == "REMOTE_NDIS_INITIALIZE_CMPLT");

  CHECK(MessageTypeName(0x0000'8001U) == "RNDIS_MSG_MP_CREATE_VC");
  CHECK(IsCondis(0x0000'8001U));
  CHECK_FALSE(IsCondis(ToRaw(MessageType::kPacket)));
}

TEST_CASE("rndis_messages scenario 5: OID") {
  CHECK(IsVariableLengthOid(Oid::kGenSupportedList));
  CHECK(IsVariableLengthOid(Oid::kGenVendorDescription));
  CHECK_FALSE(IsVariableLengthOid(Oid::kEthernetPermanentAddress));
  CHECK_FALSE(IsVariableLengthOid(Oid::kGenCurrentPacketFilter));
}

// INITIALIZE

TEST_CASE("rndis_messages scenario 6: INITIALIZE_MSG") {
  std::array<std::byte, 64> buffer{};
  const auto written = Encode(
      InitializeRequest{.request_id = 7, .max_transfer_size = 2048}, buffer);
  REQUIRE(written.has_value());
  CHECK(written.value() == kInitializeMsgBytes);

  CHECK(LoadLe32(buffer.data() + kMessageTypeOffset) == ToRaw(MessageType::kInitialize));
  CHECK(LoadLe32(buffer.data() + kMessageLengthOffset) == 24);
  CHECK(LoadLe32(buffer.data() + kInitializeRequestIdOffset) == 7);
  CHECK(LoadLe32(buffer.data() + kInitializeMajorVersionOffset) == 1);
  CHECK(LoadLe32(buffer.data() + kInitializeMinorVersionOffset) == 0);
  CHECK(LoadLe32(buffer.data() + kInitializeMaxTransferSizeOffset) == 2048);
}

TEST_CASE("rndis_messages scenario 7: INITIALIZE_MSG") {
  std::array<std::byte, 16> too_small{};
  CHECK_FALSE(Encode(InitializeRequest{}, too_small).has_value());
}

TEST_CASE("rndis_messages scenario 8: INITIALIZE_CMPLT") {
  const auto buffer = MakeInitializeComplete();
  const auto complete = DecodeInitializeComplete(buffer);
  REQUIRE(complete.has_value());
  CHECK(complete->request_id == 0x1234'5678U);
  CHECK(complete->status == 0);
  CHECK(complete->major_version == 1);
  CHECK(complete->max_transfer_size == 2048);
  CHECK(complete->max_packets_per_message == 1);
}

TEST_CASE("rndis_messages scenario 9: INITIALIZE_CMPLT") {
  const auto buffer = MakeInitializeComplete(2048, 1, 0, ToRaw(StatusCode::kFailure));
  const auto complete = DecodeInitializeComplete(buffer);
  REQUIRE_FALSE(complete.has_value());
  CHECK(complete.error().Code() == static_cast<std::int64_t>(ToRaw(StatusCode::kFailure)));

  CHECK(complete.error().ToString().find("RNDIS_STATUS_FAILURE") != std::string::npos);
}

TEST_CASE("rndis_messages scenario 10: INITIALIZE_CMPLT") {
  SUBCASE("rndis_messages scenario 11") {
    std::array<std::byte, 8> tiny{};
    StoreLe32(tiny.data(), ToRaw(MessageType::kInitializeComplete));
    StoreLe32(tiny.data() + 4, kInitializeCmpltBytes);
    CHECK_FALSE(DecodeInitializeComplete(tiny).has_value());
  }
  SUBCASE("rndis_messages scenario 12") {
    auto buffer = MakeInitializeComplete();
    StoreLe32(buffer.data() + kMessageTypeOffset, ToRaw(MessageType::kQueryComplete));
    CHECK_FALSE(DecodeInitializeComplete(buffer).has_value());
  }
  SUBCASE("rndis_messages scenario 13: MessageLength") {
    auto buffer = MakeInitializeComplete();
    StoreLe32(buffer.data() + kMessageLengthOffset, 0xFFFF'FFFFU);
    CHECK_FALSE(DecodeInitializeComplete(buffer).has_value());
  }
  SUBCASE("rndis_messages scenario 14: MessageLength") {
    auto buffer = MakeInitializeComplete();
    StoreLe32(buffer.data() + kMessageLengthOffset, 12);
    CHECK_FALSE(DecodeInitializeComplete(buffer).has_value());
  }
  SUBCASE("rndis_messages scenario 15") {
    CHECK_FALSE(DecodeInitializeComplete({}).has_value());
  }
}

TEST_CASE("rndis_messages scenario 16: Android gadget") {
  const auto buffer = MakeInitializeComplete(2048, 1, 0);
  const auto complete = DecodeInitializeComplete(buffer);
  REQUIRE(complete.has_value());

  const auto params = Negotiate(*complete, 1500, 65536);
  REQUIRE(params.has_value());
  CHECK(params->mtu == 1500);
  CHECK(params->device_max_transfer_size == 2048);
  CHECK(params->max_packets_per_message == 1);
  CHECK(params->tx_alignment_bytes == 1);
}

TEST_CASE("rndis_messages scenario 17: quirk MaxPacketsPerMessage") {
  const auto buffer = MakeInitializeComplete(2048, 0, 0);
  const auto complete = DecodeInitializeComplete(buffer);
  REQUIRE(complete.has_value());
  const auto params = Negotiate(*complete, 1500, 65536);
  REQUIRE(params.has_value());
  CHECK(params->max_packets_per_message == 1);
}

TEST_CASE("rndis_messages scenario 18: quirk PacketAlignmentFactor") {
  const auto buffer = MakeInitializeComplete(2048, 1, 31);
  const auto complete = DecodeInitializeComplete(buffer);
  REQUIRE(complete.has_value());
  const auto params = Negotiate(*complete, 1500, 65536);
  REQUIRE(params.has_value());

  CHECK(params->tx_alignment_bytes == 128);
}

TEST_CASE("rndis_messages scenario 19: quirk MaxPacketsPerMessage") {
  const auto buffer = MakeInitializeComplete(3 * 1580, 3, 2);
  const auto complete = DecodeInitializeComplete(buffer);
  REQUIRE(complete.has_value());
  const auto params = Negotiate(*complete, 1500, 65536);
  REQUIRE(params.has_value());
  CHECK(params->max_packets_per_message == 3);
  CHECK(params->tx_alignment_bytes == 4);  // 1 << 2
}

TEST_CASE("rndis_messages scenario 20: quirk MaxTransferSize MTU") {
  const auto buffer = MakeInitializeComplete(1536, 1, 0);
  const auto complete = DecodeInitializeComplete(buffer);
  REQUIRE(complete.has_value());
  const auto params = Negotiate(*complete, 1500, 65536);
  REQUIRE(params.has_value());
  CHECK(params->mtu == 1536 - kHardHeaderBytes);  // 1478
}

TEST_CASE("rndis_messages scenario 21: quirk WinCE KB host") {
  const auto buffer = MakeInitializeComplete(16384, 1, 0);
  const auto complete = DecodeInitializeComplete(buffer);
  REQUIRE(complete.has_value());
  const auto params = Negotiate(*complete, 1500, 4096);
  REQUIRE(params.has_value());
  CHECK(params->device_max_transfer_size == 4096);
  CHECK(params->mtu == 1500);
}

TEST_CASE("rndis_messages scenario 22: MaxTransferSize") {
  const auto buffer = MakeInitializeComplete(40, 1, 0);
  const auto complete = DecodeInitializeComplete(buffer);
  REQUIRE(complete.has_value());
  CHECK_FALSE(Negotiate(*complete, 1500, 65536).has_value());
}

TEST_CASE("rndis_messages scenario 23") {
  const auto buffer =
      MakeInitializeComplete(2048, 1, 0, 0, static_cast<std::uint32_t>(Medium::kWirelessLan));
  const auto complete = DecodeInitializeComplete(buffer);
  REQUIRE(complete.has_value());
  CHECK_FALSE(Negotiate(*complete, 1500, 65536).has_value());
}

TEST_CASE("rndis_messages scenario 24") {
  const auto buffer = MakeInitializeComplete(
      2048, 1, 0, 0, 0, static_cast<std::uint32_t>(DeviceFlags::kConnectionOriented));
  const auto complete = DecodeInitializeComplete(buffer);
  REQUIRE(complete.has_value());
  CHECK_FALSE(Negotiate(*complete, 1500, 65536).has_value());
}

// QUERY / SET

TEST_CASE("rndis_messages scenario 25: QUERY_MSG OID") {
  std::array<std::byte, 128> buffer{};
  const auto written = Encode(QueryRequest{.request_id = 3,
                                           .oid = Oid::kEthernetPermanentAddress,
                                           .expected_response_bytes = 48},
                              buffer);
  REQUIRE(written.has_value());
  CHECK(written.value() == kQuerySetHeaderBytes + 48);

  CHECK(LoadLe32(buffer.data() + kQuerySetOidOffset) == ToRaw(Oid::kEthernetPermanentAddress));
  CHECK(LoadLe32(buffer.data() + kQuerySetInfoBufferLengthOffset) == 48);

  CHECK(LoadLe32(buffer.data() + kQuerySetInfoBufferOffsetOffset) == 20);
  CHECK(LoadLe32(buffer.data() + kQuerySetDeviceVcHandleOffset) == 0);
}

TEST_CASE("rndis_messages scenario 26: QUERY_MSG OID") {
  std::array<std::byte, 128> buffer{};
  const auto written = Encode(QueryRequest{.request_id = 4,
                                           .oid = Oid::kGenSupportedList,
                                           .expected_response_bytes = 256},
                              buffer);
  REQUIRE(written.has_value());
  CHECK(written.value() == kQuerySetHeaderBytes);
  CHECK(LoadLe32(buffer.data() + kQuerySetInfoBufferLengthOffset) == 0);

  CHECK(LoadLe32(buffer.data() + kQuerySetInfoBufferOffsetOffset) == 0);
}

TEST_CASE("rndis_messages scenario 27: QUERY_CMPLT") {
  auto buffer = MakeMessage(MessageType::kQueryComplete, kQueryCmpltHeaderBytes + 6);
  StoreLe32(buffer.data() + kQueryCmpltRequestIdOffset, 9);
  StoreLe32(buffer.data() + kQueryCmpltStatusOffset, 0);
  StoreLe32(buffer.data() + kQueryCmpltInfoBufferLengthOffset, 6);
  StoreLe32(buffer.data() + kQueryCmpltInfoBufferOffsetOffset, kQueryCmpltInlineInfoOffset);
  const std::array<std::uint8_t, 6> mac{0x02, 0x11, 0x22, 0x33, 0x44, 0x55};
  for (std::size_t i = 0; i < mac.size(); ++i) {
    buffer[kQueryCmpltHeaderBytes + i] = std::byte{mac[i]};
  }

  const auto complete = DecodeQueryComplete(buffer);
  REQUIRE(complete.has_value());
  CHECK(complete->request_id == 9);
  REQUIRE(complete->information.size() == 6);

  const auto parsed = ParseMac(complete->information);
  REQUIRE(parsed.has_value());
  CHECK(*parsed == MacAddress{0x02, 0x11, 0x22, 0x33, 0x44, 0x55});
}

TEST_CASE("rndis_messages scenario 28: QUERY_CMPLT") {
  auto buffer = MakeMessage(MessageType::kQueryComplete, kQueryCmpltHeaderBytes);
  StoreLe32(buffer.data() + kQueryCmpltStatusOffset, ToRaw(StatusCode::kNotSupported));
  const auto complete = DecodeQueryComplete(buffer);
  REQUIRE(complete.has_value());
  CHECK(complete->status == ToRaw(StatusCode::kNotSupported));
  CHECK(complete->information.empty());
}

TEST_CASE("rndis_messages scenario 29: QUERY_CMPLT") {
  auto buffer = MakeMessage(MessageType::kQueryComplete, kQueryCmpltHeaderBytes + 4);
  StoreLe32(buffer.data() + kQueryCmpltStatusOffset, 0);
  StoreLe32(buffer.data() + kQueryCmpltInfoBufferLengthOffset, 1024);
  StoreLe32(buffer.data() + kQueryCmpltInfoBufferOffsetOffset, kQueryCmpltInlineInfoOffset);
  CHECK_FALSE(DecodeQueryComplete(buffer).has_value());
}

TEST_CASE("rndis_messages scenario 30: SET_MSG SET LE32") {
  std::array<std::byte, 64> buffer{};
  const auto written = EncodeSetUint32(11, Oid::kGenCurrentPacketFilter, kDefaultPacketFilter,
                                       buffer);
  REQUIRE(written.has_value());
  CHECK(written.value() == kQuerySetHeaderBytes + 4);
  CHECK(LoadLe32(buffer.data() + kMessageTypeOffset) == ToRaw(MessageType::kSet));
  CHECK(LoadLe32(buffer.data() + kQuerySetOidOffset) == ToRaw(Oid::kGenCurrentPacketFilter));
  CHECK(LoadLe32(buffer.data() + kQuerySetInfoBufferLengthOffset) == 4);
  CHECK(LoadLe32(buffer.data() + kQuerySetInfoBufferOffsetOffset) == 20);
  CHECK(LoadLe32(buffer.data() + kQuerySetHeaderBytes) == kDefaultPacketFilter);
}

TEST_CASE("rndis_messages scenario 31: SET_CMPLT") {
  auto buffer = MakeMessage(MessageType::kSetComplete, kSetCmpltBytes);
  StoreLe32(buffer.data() + kSetCmpltRequestIdOffset, 11);
  StoreLe32(buffer.data() + kSetCmpltStatusOffset, 0);
  const auto complete = DecodeSetComplete(buffer);
  REQUIRE(complete.has_value());
  CHECK(complete->request_id == 11);
  CHECK(complete->status == 0);
}

TEST_CASE("rndis_messages scenario 32: RESET_MSG offset Reserved RequestId") {
  std::array<std::byte, 32> buffer{};
  buffer[kResetReservedOffset] = std::byte{0xFF};
  const auto written = EncodeReset(buffer);
  REQUIRE(written.has_value());
  CHECK(written.value() == kResetMsgBytes);
  CHECK(LoadLe32(buffer.data() + kMessageTypeOffset) == ToRaw(MessageType::kReset));
  CHECK(LoadLe32(buffer.data() + kMessageLengthOffset) == 12);
  CHECK(LoadLe32(buffer.data() + kResetReservedOffset) == 0);
}

TEST_CASE("rndis_messages scenario 33: RESET_CMPLT Status offset") {
  auto buffer = MakeMessage(MessageType::kResetComplete, kResetCmpltBytes);
  StoreLe32(buffer.data() + kResetCmpltStatusOffset, 0);
  StoreLe32(buffer.data() + kResetCmpltAddressingResetOffset, 1);

  const auto complete = DecodeResetComplete(buffer);
  REQUIRE(complete.has_value());
  CHECK(complete->status == 0);

  CHECK(complete->addressing_reset);
}

TEST_CASE("rndis_messages scenario 34: RESET_CMPLT AddressingReset") {
  auto buffer = MakeMessage(MessageType::kResetComplete, kResetCmpltBytes);
  StoreLe32(buffer.data() + kResetCmpltAddressingResetOffset, 0);
  const auto complete = DecodeResetComplete(buffer);
  REQUIRE(complete.has_value());
  CHECK_FALSE(complete->addressing_reset);
}

// KEEPALIVE

TEST_CASE("rndis_messages scenario 35: KEEPALIVE") {
  std::array<std::byte, 32> request{};
  const auto written = EncodeKeepAlive(42, request);
  REQUIRE(written.has_value());
  CHECK(written.value() == kKeepAliveMsgBytes);
  CHECK(LoadLe32(request.data() + kKeepAliveRequestIdOffset) == 42);

  auto response = MakeMessage(MessageType::kKeepAliveComplete, kKeepAliveCmpltBytes);
  StoreLe32(response.data() + kKeepAliveCmpltRequestIdOffset, 42);
  const auto complete = DecodeKeepAliveComplete(response);
  REQUIRE(complete.has_value());
  CHECK(complete->request_id == 42);
}

TEST_CASE("rndis_messages scenario 36: host KEEPALIVE_CMPLT") {
  std::array<std::byte, 32> buffer{};
  const auto written = EncodeKeepAliveComplete(77, ToRaw(StatusCode::kSuccess), buffer);
  REQUIRE(written.has_value());
  CHECK(written.value() == kKeepAliveCmpltBytes);
  CHECK(LoadLe32(buffer.data() + kMessageTypeOffset) == ToRaw(MessageType::kKeepAliveComplete));
  CHECK(LoadLe32(buffer.data() + kKeepAliveCmpltRequestIdOffset) == 77);
  CHECK(LoadLe32(buffer.data() + kKeepAliveCmpltStatusOffset) == 0);
}

TEST_CASE("rndis_messages scenario 37: INDICATE_STATUS MEDIA_CONNECT") {
  auto buffer = MakeMessage(MessageType::kIndicateStatus, kIndicateStatusHeaderBytes);
  StoreLe32(buffer.data() + kIndicateStatusStatusOffset, ToRaw(StatusCode::kMediaConnect));
  StoreLe32(buffer.data() + kIndicateStatusBufferLengthOffset, 0);

  const auto indication = DecodeIndicateStatus(buffer);
  REQUIRE(indication.has_value());
  CHECK(indication->status == ToRaw(StatusCode::kMediaConnect));
  CHECK(indication->status_buffer.empty());
  CHECK_FALSE(indication->has_diagnostic_info);
}

TEST_CASE("rndis_messages scenario 38: INDICATE_STATUS") {
  const std::uint32_t total = kIndicateStatusHeaderBytes + kDiagnosticInfoBytes;
  auto buffer = MakeMessage(MessageType::kIndicateStatus, total);
  StoreLe32(buffer.data() + kIndicateStatusStatusOffset, ToRaw(StatusCode::kInvalidData));
  StoreLe32(buffer.data() + kIndicateStatusBufferLengthOffset, kDiagnosticInfoBytes);

  StoreLe32(buffer.data() + kIndicateStatusBufferOffsetOffset,
            kIndicateStatusHeaderBytes - kOffsetFieldBase);
  StoreLe32(buffer.data() + kIndicateStatusHeaderBytes + kDiagnosticInfoDiagStatusOffset,
            ToRaw(StatusCode::kInvalidData));
  StoreLe32(buffer.data() + kIndicateStatusHeaderBytes + kDiagnosticInfoErrorOffsetOffset, 36);

  const auto indication = DecodeIndicateStatus(buffer);
  REQUIRE(indication.has_value());
  CHECK(indication->status_buffer.size() == kDiagnosticInfoBytes);
  REQUIRE(indication->has_diagnostic_info);
  CHECK(indication->diagnostic_status == ToRaw(StatusCode::kInvalidData));
  CHECK(indication->diagnostic_error_offset == 36);
}

TEST_CASE("rndis_messages scenario 39: INDICATE_STATUS") {
  const std::uint32_t total = kIndicateStatusHeaderBytes + kDiagnosticInfoBytes;
  auto buffer = MakeMessage(MessageType::kIndicateStatus, total);
  StoreLe32(buffer.data() + kIndicateStatusStatusOffset, ToRaw(StatusCode::kInvalidData));
  StoreLe32(buffer.data() + kIndicateStatusBufferLengthOffset, kDiagnosticInfoBytes);

  StoreLe32(buffer.data() + kIndicateStatusBufferOffsetOffset, kIndicateStatusHeaderBytes);
  StoreLe32(buffer.data() + kIndicateStatusHeaderBytes + kDiagnosticInfoDiagStatusOffset, 0xAAU);

  const auto indication = DecodeIndicateStatus(buffer);
  REQUIRE(indication.has_value());

  REQUIRE(indication->status_buffer.size() == kDiagnosticInfoBytes);
  CHECK(indication->diagnostic_status == 0xAAU);
}

TEST_CASE("rndis_messages scenario 40: INDICATE_STATUS") {
  auto buffer = MakeMessage(MessageType::kIndicateStatus, kIndicateStatusHeaderBytes);
  StoreLe32(buffer.data() + kIndicateStatusStatusOffset, ToRaw(StatusCode::kMediaDisconnect));
  StoreLe32(buffer.data() + kIndicateStatusBufferLengthOffset, 4096);
  StoreLe32(buffer.data() + kIndicateStatusBufferOffsetOffset, 12);

  const auto indication = DecodeIndicateStatus(buffer);
  REQUIRE(indication.has_value());
  CHECK(indication->status == ToRaw(StatusCode::kMediaDisconnect));
  CHECK(indication->status_buffer.empty());
}

TEST_CASE("rndis_messages scenario 41: INDICATE_STATUS") {
  auto buffer = MakeMessage(MessageType::kQueryComplete, kIndicateStatusHeaderBytes);
  CHECK_FALSE(DecodeIndicateStatus(buffer).has_value());
}

TEST_CASE("ParseUint32 / ParseCounter / ParseMac") {
  std::array<std::byte, 8> payload{};
  StoreLe32(payload.data(), 0x0000'002DU);

  const auto value = ParseUint32(std::span<const std::byte>{payload}.first(4));
  REQUIRE(value.has_value());
  CHECK(*value == 0x0000'002DU);

  SUBCASE("rndis_messages scenario 42: OID") {
    const auto counter = ParseCounter(std::span<const std::byte>{payload}.first(4));
    REQUIRE(counter.has_value());
    CHECK(*counter == 0x0000'002DU);
  }
  SUBCASE("rndis_messages scenario 43: OID") {
    StoreLe64(payload.data(), 0x0000'0001'0000'002DULL);
    const auto counter = ParseCounter(payload);
    REQUIRE(counter.has_value());
    CHECK(*counter == 0x0000'0001'0000'002DULL);
  }
  SUBCASE("rndis_messages scenario 44: OID") {
    CHECK_FALSE(ParseCounter(std::span<const std::byte>{payload}.first(2)).has_value());
  }
  SUBCASE("rndis_messages scenario 45: MAC") {
    CHECK_FALSE(ParseMac(std::span<const std::byte>{payload}.first(4)).has_value());
  }
}

TEST_CASE("rndis_messages scenario 46: FormatMac") {
  const MacAddress mac{0x02, 0x00, 0xAB, 0xCD, 0xEF, 0x01};
  const auto text = FormatMac(mac);
  CHECK(std::string_view{text.data()} == "02:00:ab:cd:ef:01");
}

TEST_CASE("rndis_messages scenario 47: RNDIS") {
  CHECK(IsRndisControlSignature({0x02, 0x02, 0xFF}));
  CHECK(IsRndisControlSignature({0xEF, 0x01, 0x01}));  // ActiveSync
  CHECK(IsRndisControlSignature({0xE0, 0x01, 0x03}));
  CHECK(IsRndisControlSignature({0xEF, 0x04, 0x01}));

  CHECK_FALSE(IsRndisControlSignature({0x02, 0x02, 0x01}));

  CHECK_FALSE(IsRndisControlSignature({0x02, 0x06, 0x00}));
  CHECK(kDataSignature == InterfaceSignature{0x0A, 0x00, 0x00});
}
}  // TEST_SUITE("rndis.messages")
