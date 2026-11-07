// RNDIS 1.0 protocol constants and wire layout (MS-RNDIS, Linux rndis_host).

#pragma once

#include <concepts>
#include <cstdint>
#include <string_view>
#include <type_traits>

namespace multitether::rndis {
inline constexpr std::uint32_t kMajorVersion = 1;
inline constexpr std::uint32_t kMinorVersion = 0;

inline constexpr std::uint32_t kControlTimeoutMillis = 5'000;

inline constexpr std::uint32_t kKeepAliveTimeoutMillis = 5'000;

inline constexpr std::uint32_t kControlBufferBytes = 1536;

inline constexpr std::uint32_t kMessageCompletionFlag = 0x8000'0000U;

enum class MessageType : std::uint32_t {
  kPacket = 0x0000'0001U,
  kInitialize = 0x0000'0002U,
  kHalt = 0x0000'0003U,
  kQuery = 0x0000'0004U,
  kSet = 0x0000'0005U,
  kReset = 0x0000'0006U,
  kIndicateStatus = 0x0000'0007U,
  kKeepAlive = 0x0000'0008U,

  kInitializeComplete = kInitialize | kMessageCompletionFlag,  // 0x80000002
  kQueryComplete = kQuery | kMessageCompletionFlag,            // 0x80000004
  kSetComplete = kSet | kMessageCompletionFlag,                // 0x80000005
  kResetComplete = kReset | kMessageCompletionFlag,            // 0x80000006
  kKeepAliveComplete = kKeepAlive | kMessageCompletionFlag,    // 0x80000008

  kBus = 0xFF00'0001U,
};

enum class CondisMessageType : std::uint32_t {
  kCreateVc = 0x0000'8001U,
  kDeleteVc = 0x0000'8002U,
  kActivateVc = 0x0000'8005U,
  kDeactivateVc = 0x0000'8006U,
  kIndicateStatus = 0x0000'8007U,
};

template <typename E>
  requires std::is_enum_v<E> && std::same_as<std::underlying_type_t<E>, std::uint32_t>
[[nodiscard]] constexpr std::uint32_t ToRaw(E value) noexcept {
  return static_cast<std::uint32_t>(value);
}

[[nodiscard]] constexpr bool IsCondis(std::uint32_t message_type) noexcept {
  return (message_type & ~kMessageCompletionFlag) >= 0x0000'8000U &&
         (message_type & ~kMessageCompletionFlag) <= 0x0000'8FFFU;
}

[[nodiscard]] std::string_view MessageTypeName(std::uint32_t message_type) noexcept;

inline constexpr std::uint32_t kMessageTypeOffset = 0;
inline constexpr std::uint32_t kMessageLengthOffset = 4;

inline constexpr std::uint32_t kMessageHeaderBytes = 8;

inline constexpr std::uint32_t kOffsetFieldBase = 8;

inline constexpr std::uint32_t kInitializeMsgBytes = 24;
inline constexpr std::uint32_t kInitializeRequestIdOffset = 8;
inline constexpr std::uint32_t kInitializeMajorVersionOffset = 12;
inline constexpr std::uint32_t kInitializeMinorVersionOffset = 16;
inline constexpr std::uint32_t kInitializeMaxTransferSizeOffset = 20;

inline constexpr std::uint32_t kInitializeCmpltBytes = 52;
inline constexpr std::uint32_t kInitializeCmpltRequestIdOffset = 8;
inline constexpr std::uint32_t kInitializeCmpltStatusOffset = 12;
inline constexpr std::uint32_t kInitializeCmpltMajorVersionOffset = 16;
inline constexpr std::uint32_t kInitializeCmpltMinorVersionOffset = 20;
inline constexpr std::uint32_t kInitializeCmpltDeviceFlagsOffset = 24;
inline constexpr std::uint32_t kInitializeCmpltMediumOffset = 28;
inline constexpr std::uint32_t kInitializeCmpltMaxPacketsPerMessageOffset = 32;
inline constexpr std::uint32_t kInitializeCmpltMaxTransferSizeOffset = 36;
inline constexpr std::uint32_t kInitializeCmpltPacketAlignmentFactorOffset = 40;
inline constexpr std::uint32_t kInitializeCmpltAfListOffsetOffset = 44;
inline constexpr std::uint32_t kInitializeCmpltAfListSizeOffset = 48;

inline constexpr std::uint32_t kHaltMsgBytes = 12;
inline constexpr std::uint32_t kHaltRequestIdOffset = 8;

inline constexpr std::uint32_t kQuerySetHeaderBytes = 28;
inline constexpr std::uint32_t kQuerySetRequestIdOffset = 8;
inline constexpr std::uint32_t kQuerySetOidOffset = 12;
inline constexpr std::uint32_t kQuerySetInfoBufferLengthOffset = 16;
inline constexpr std::uint32_t kQuerySetInfoBufferOffsetOffset = 20;
inline constexpr std::uint32_t kQuerySetDeviceVcHandleOffset = 24;

inline constexpr std::uint32_t kQuerySetInlineInfoOffset = kQuerySetHeaderBytes - kOffsetFieldBase;
static_assert(kQuerySetInlineInfoOffset == 20);

inline constexpr std::uint32_t kQueryCmpltHeaderBytes = 24;
inline constexpr std::uint32_t kQueryCmpltRequestIdOffset = 8;
inline constexpr std::uint32_t kQueryCmpltStatusOffset = 12;
inline constexpr std::uint32_t kQueryCmpltInfoBufferLengthOffset = 16;
inline constexpr std::uint32_t kQueryCmpltInfoBufferOffsetOffset = 20;

inline constexpr std::uint32_t kQueryCmpltInlineInfoOffset = kQueryCmpltHeaderBytes - kOffsetFieldBase;
static_assert(kQueryCmpltInlineInfoOffset == 16);

inline constexpr std::uint32_t kSetCmpltBytes = 16;
inline constexpr std::uint32_t kSetCmpltRequestIdOffset = 8;
inline constexpr std::uint32_t kSetCmpltStatusOffset = 12;

inline constexpr std::uint32_t kResetMsgBytes = 12;

inline constexpr std::uint32_t kResetReservedOffset = 8;

inline constexpr std::uint32_t kResetCmpltBytes = 16;
inline constexpr std::uint32_t kResetCmpltStatusOffset = 8;

inline constexpr std::uint32_t kResetCmpltAddressingResetOffset = 12;

inline constexpr std::uint32_t kIndicateStatusHeaderBytes = 20;
inline constexpr std::uint32_t kIndicateStatusStatusOffset = 8;
inline constexpr std::uint32_t kIndicateStatusBufferLengthOffset = 12;

inline constexpr std::uint32_t kIndicateStatusBufferOffsetOffset = 16;

inline constexpr std::uint32_t kDiagnosticInfoBytes = 8;
inline constexpr std::uint32_t kDiagnosticInfoDiagStatusOffset = 0;
inline constexpr std::uint32_t kDiagnosticInfoErrorOffsetOffset = 4;

inline constexpr std::uint32_t kKeepAliveMsgBytes = 12;
inline constexpr std::uint32_t kKeepAliveRequestIdOffset = 8;
inline constexpr std::uint32_t kKeepAliveCmpltBytes = 16;
inline constexpr std::uint32_t kKeepAliveCmpltRequestIdOffset = 8;
inline constexpr std::uint32_t kKeepAliveCmpltStatusOffset = 12;

inline constexpr std::uint32_t kPacketMsgHeaderBytes = 44;
inline constexpr std::uint32_t kPacketDataOffsetOffset = 8;
inline constexpr std::uint32_t kPacketDataLengthOffset = 12;
inline constexpr std::uint32_t kPacketOobDataOffsetOffset = 16;
inline constexpr std::uint32_t kPacketOobDataLengthOffset = 20;
inline constexpr std::uint32_t kPacketNumOobDataElementsOffset = 24;
inline constexpr std::uint32_t kPacketPerPacketInfoOffsetOffset = 28;
inline constexpr std::uint32_t kPacketPerPacketInfoLengthOffset = 32;
inline constexpr std::uint32_t kPacketVcHandleOffset = 36;
inline constexpr std::uint32_t kPacketReservedOffset = 40;

inline constexpr std::uint32_t kPacketInlineDataOffset = kPacketMsgHeaderBytes - kOffsetFieldBase;
static_assert(kPacketInlineDataOffset == 36, "DataOffset is relative to message start + 8");

inline constexpr std::uint32_t kMaxPacketAlignmentFactor = 7;

enum class StatusCode : std::uint32_t {
  kSuccess = 0x0000'0000U,
  kPending = 0x0000'0103U,

  kNotRecognized = 0x0001'0001U,
  kNotCopied = 0x0001'0002U,
  kNotAccepted = 0x0001'0003U,
  kCallActive = 0x0001'0007U,

  kOnline = 0x4001'0003U,
  kResetStart = 0x4001'0004U,
  kResetEnd = 0x4001'0005U,
  kRingStatus = 0x4001'0006U,
  kClosed = 0x4001'0007U,
  kWanLineUp = 0x4001'0008U,
  kWanLineDown = 0x4001'0009U,
  kWanFragment = 0x4001'000AU,
  kMediaConnect = 0x4001'000BU,
  kMediaDisconnect = 0x4001'000CU,
  kHardwareLineUp = 0x4001'000DU,
  kHardwareLineDown = 0x4001'000EU,
  kInterfaceUp = 0x4001'000FU,
  kInterfaceDown = 0x4001'0010U,
  kMediaBusy = 0x4001'0011U,
  kMediaSpecificIndication = 0x4001'0012U,
  kLinkSpeedChange = 0x4001'0013U,
  kNetworkChange = 0x4001'0018U,

  kBufferOverflow = 0x8000'0005U,
  kNotResettable = 0x8001'0001U,
  kSoftErrors = 0x8001'0003U,
  kHardErrors = 0x8001'0004U,

  kFailure = 0xC000'0001U,
  kResources = 0xC000'009AU,
  kNotSupported = 0xC000'00BBU,
  kClosing = 0xC001'0002U,
  kBadVersion = 0xC001'0004U,
  kBadCharacteristics = 0xC001'0005U,
  kAdapterNotFound = 0xC001'0006U,
  kOpenFailed = 0xC001'0007U,
  kDeviceFailed = 0xC001'0008U,
  kMulticastFull = 0xC001'0009U,
  kMulticastExists = 0xC001'000AU,
  kMulticastNotFound = 0xC001'000BU,
  kRequestAborted = 0xC001'000CU,
  kResetInProgress = 0xC001'000DU,
  kClosingIndicating = 0xC001'000EU,
  kInvalidPacket = 0xC001'000FU,
  kOpenListFull = 0xC001'0010U,
  kAdapterNotReady = 0xC001'0011U,
  kAdapterNotOpen = 0xC001'0012U,
  kNotIndicating = 0xC001'0013U,
  kInvalidLength = 0xC001'0014U,
  kInvalidData = 0xC001'0015U,
  kBufferTooShort = 0xC001'0016U,
  kInvalidOid = 0xC001'0017U,
  kAdapterRemoved = 0xC001'0018U,
  kUnsupportedMedia = 0xC001'0019U,
  kGroupAddressInUse = 0xC001'001AU,
  kNoCable = 0xC001'001FU,
  kTokenRingOpenError = 0xC001'1000U,
};

[[nodiscard]] std::string_view StatusName(std::uint32_t status) noexcept;

// OIDs

enum class Oid : std::uint32_t {
  kGenSupportedList = 0x0001'0101U,
  kGenHardwareStatus = 0x0001'0102U,     ///< LE32: 0=Ready 1=Initializing 2=Reset 3=Closing 4=NotReady
  kGenMediaSupported = 0x0001'0103U,
  kGenMediaInUse = 0x0001'0104U,
  kGenMaximumFrameSize = 0x0001'0106U,
  kGenLinkSpeed = 0x0001'0107U,
  kGenTransmitBlockSize = 0x0001'010AU,  ///< LE32
  kGenReceiveBlockSize = 0x0001'010BU,   ///< LE32
  kGenVendorId = 0x0001'010CU,
  kGenVendorDescription = 0x0001'010DU,
  kGenCurrentPacketFilter = 0x0001'010EU,
  kGenMaximumTotalSize = 0x0001'0111U,
  kGenMacOptions = 0x0001'0113U,
  kGenMediaConnectStatus = 0x0001'0114U,
  kGenPhysicalMedium = 0x0001'0202U,

  kGenXmitOk = 0x0002'0101U,
  kGenRcvOk = 0x0002'0102U,
  kGenXmitError = 0x0002'0103U,
  kGenRcvError = 0x0002'0104U,
  kGenRcvNoBuffer = 0x0002'0105U,

  kEthernetPermanentAddress = 0x0101'0101U,
  kEthernetCurrentAddress = 0x0101'0102U,
  kEthernetMulticastList = 0x0101'0103U,
  kEthernetMaximumListSize = 0x0101'0104U,
  kEthernetMacOptions = 0x0101'0105U,
};

[[nodiscard]] std::string_view OidName(std::uint32_t oid) noexcept;

[[nodiscard]] constexpr bool IsVariableLengthOid(Oid oid) noexcept {
  switch (oid) {
    case Oid::kGenSupportedList:
    case Oid::kGenVendorDescription:
    case Oid::kGenMediaSupported:
    case Oid::kGenMediaInUse:
    case Oid::kEthernetMulticastList:
      return true;
    default:
      return false;
  }
}

enum class PacketFilter : std::uint32_t {
  kDirected = 0x0000'0001U,
  kMulticast = 0x0000'0002U,
  kAllMulticast = 0x0000'0004U,
  kBroadcast = 0x0000'0008U,      ///< ff:ff:ff:ff:ff:ff
  kSourceRouting = 0x0000'0010U,
  kPromiscuous = 0x0000'0020U,
  kSmt = 0x0000'0040U,
  kAllLocal = 0x0000'0080U,
  kGroup = 0x0000'1000U,
  kAllFunctional = 0x0000'2000U,
  kFunctional = 0x0000'4000U,
  kMacFrame = 0x0000'8000U,
};

inline constexpr std::uint32_t kDefaultPacketFilter =
    ToRaw(PacketFilter::kDirected) | ToRaw(PacketFilter::kBroadcast) |
    ToRaw(PacketFilter::kAllMulticast) | ToRaw(PacketFilter::kPromiscuous);
static_assert(kDefaultPacketFilter == 0x0000'002DU);

enum class Medium : std::uint32_t {
  kEthernet = 0x0000'0000U,
  kTokenRing = 1,           ///< 802.5
  kFddi = 2,
  kWan = 3,
  kLocalTalk = 4,
  kArcnetRaw = 6,
  kArcnet8782 = 7,  ///< ARCNET 878.2
  kAtm = 8,
  kWirelessLan = 9,
  kIrda = 0x0A,
  kBpc = 0x0B,
  kCoWan = 0x0C,
  kIeee1394 = 0x0D,
};

enum class PhysicalMedium : std::uint32_t {
  kUnspecified = 0,
  kWirelessLan = 1,
  kCableModem = 2,
  kPhoneLine = 3,
  kPowerLine = 4,
  kDsl = 5,
  kFibreChannel = 6,
  kIeee1394 = 7,
  kWirelessWan = 8,
};

enum class DeviceFlags : std::uint32_t {
  kConnectionless = 0x0000'0001U,
  kConnectionOriented = 0x0000'0002U,
  kRawData = 0x0000'0004U,
};

enum class MediaState : std::uint32_t {
  kConnected = 0,
  kDisconnected = 1,
};

inline constexpr std::uint8_t kRequestSendEncapsulatedCommand = 0x00;

inline constexpr std::uint8_t kRequestGetEncapsulatedResponse = 0x01;

inline constexpr std::uint8_t kControlOutRequestType = 0x21;

inline constexpr std::uint8_t kControlInRequestType = 0xA1;

/// Interrupt notification layout:
///   offset 0..3 = Notification (0x00000001 = RESPONSE_AVAILABLE)
///   offset 4..7 = Reserved (0)
inline constexpr std::uint32_t kNotificationBytes = 8;
inline constexpr std::uint32_t kNotificationResponseAvailable = 0x0000'0001U;
inline constexpr std::uint32_t kNotificationValueOffset = 0;

struct InterfaceSignature {
  std::uint8_t interface_class;
  std::uint8_t interface_subclass;
  std::uint8_t interface_protocol;

  [[nodiscard]] constexpr bool operator==(const InterfaceSignature&) const = default;
};

inline constexpr InterfaceSignature kControlSignatureMicrosoft{0x02, 0x02, 0xFF};
inline constexpr InterfaceSignature kControlSignatureActiveSync{0xEF, 0x01, 0x01};
inline constexpr InterfaceSignature kControlSignatureWireless{0xE0, 0x01, 0x03};
inline constexpr InterfaceSignature kControlSignatureNovatel{0xEF, 0x04, 0x01};

inline constexpr InterfaceSignature kDataSignature{0x0A, 0x00, 0x00};

[[nodiscard]] constexpr bool IsRndisControlSignature(const InterfaceSignature& signature) noexcept {
  return signature == kControlSignatureMicrosoft || signature == kControlSignatureActiveSync ||
         signature == kControlSignatureWireless || signature == kControlSignatureNovatel;
}

inline constexpr std::uint32_t kEthernetHeaderBytes = 14;

inline constexpr std::uint32_t kHardHeaderBytes = kEthernetHeaderBytes + kPacketMsgHeaderBytes;
static_assert(kHardHeaderBytes == 58);

inline constexpr std::uint32_t kDefaultMtu = 1500;

[[nodiscard]] constexpr std::uint32_t HardMtuFor(std::uint32_t mtu) noexcept {
  return mtu + kHardHeaderBytes;
}

[[nodiscard]] constexpr std::uint32_t MaxTransferSizeFor(std::uint32_t mtu,
                                                         std::uint32_t endpoint_max_packet) noexcept {
  const std::uint32_t hard_mtu = HardMtuFor(mtu);
  return (hard_mtu + endpoint_max_packet + 1) & ~(endpoint_max_packet - 1);
}

static_assert(MaxTransferSizeFor(1500, 512) == 2048);
static_assert(MaxTransferSizeFor(1500, 64) == 1600);
}  // namespace multitether::rndis
