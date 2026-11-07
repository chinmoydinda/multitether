// CDC encapsulated command transport:
//   SEND_ENCAPSULATED_COMMAND  (bmRequestType=0x21, bRequest=0x00)
//   GET_ENCAPSULATED_RESPONSE  (bmRequestType=0xA1, bRequest=0x01)

#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include "multitether/common/error.h"

namespace multitether::rndis {
inline constexpr std::uint32_t kProbeOnlyTimeoutMillis = 1;

enum class NotificationResult : std::uint8_t {
  kResponseAvailable,
  kTimeout,
  kNotSupported,
};

class ControlChannel {
 public:
  ControlChannel() = default;
  ControlChannel(const ControlChannel&) = delete;
  ControlChannel& operator=(const ControlChannel&) = delete;
  ControlChannel(ControlChannel&&) = delete;
  ControlChannel& operator=(ControlChannel&&) = delete;
  virtual ~ControlChannel() = default;

  [[nodiscard]] virtual Status SendMessage(std::span<const std::byte> message) = 0;

  [[nodiscard]] virtual Result<std::span<const std::byte>> ReceiveMessage() = 0;

  [[nodiscard]] virtual NotificationResult WaitForNotification(std::uint32_t timeout_millis) = 0;

  [[nodiscard]] virtual std::uint32_t TimeoutMillis() const noexcept = 0;

  [[nodiscard]] virtual std::string_view Describe() const noexcept = 0;
};
}  // namespace multitether::rndis
