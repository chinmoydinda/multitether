#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string_view>
#include <vector>

#include "multitether/capi/multitether_c.h"
#include "multitether/common/error.h"

namespace multitether::capi {
inline void CopyText(char* destination, std::size_t capacity, std::string_view text) noexcept {
  if (destination == nullptr || capacity == 0) {
    return;
  }
  std::size_t length = text.size() < capacity - 1 ? text.size() : capacity - 1;

  while (length > 0 && length < text.size() &&
         (static_cast<unsigned char>(text[length]) & 0xC0U) == 0x80U) {
    --length;
  }

  std::memcpy(destination, text.data(), length);
  destination[length] = '\0';
}

template <std::size_t N>
inline void CopyText(char (&destination)[N], std::string_view text) noexcept {
  CopyText(destination, N, text);
}

inline void FillError(tk_error_t* out_error, const Error& error) noexcept {
  if (out_error == nullptr) {
    return;
  }
  out_error->domain = static_cast<std::int32_t>(error.Domain());
  out_error->code = error.Code();

  CopyText(out_error->message, error.ToString());
}

inline void FillGenericError(tk_error_t* out_error, std::string_view message) noexcept {
  if (out_error == nullptr) {
    return;
  }
  out_error->domain = TK_ERROR_DOMAIN_GENERIC;
  out_error->code = 0;
  CopyText(out_error->message, message);
}

inline void ClearError(tk_error_t* out_error) noexcept {
  if (out_error == nullptr) {
    return;
  }
  out_error->domain = TK_ERROR_DOMAIN_GENERIC;
  out_error->code = 0;
  out_error->message[0] = '\0';
}

struct DeviceIdentity {
  std::uint8_t bus_number = 0;
  std::uint8_t device_address = 0;
  std::uint16_t vendor_id = 0;
  std::uint16_t product_id = 0;

  [[nodiscard]] bool operator==(const DeviceIdentity&) const noexcept = default;
};

[[nodiscard]] inline DeviceIdentity IdentityOf(const tk_device_info_t& info) noexcept {
  return DeviceIdentity{.bus_number = info.bus_number,
                        .device_address = info.device_address,
                        .vendor_id = info.vendor_id,
                        .product_id = info.product_id};
}

struct RememberedDeviceStrings {
  DeviceIdentity identity;
  char manufacturer[TK_USB_STRING_CAPACITY]{};
  char product[TK_USB_STRING_CAPACITY]{};
  char serial[TK_USB_STRING_CAPACITY]{};
};

void ReconcileDeviceStrings(std::vector<RememberedDeviceStrings>& memory,
                            std::span<const DeviceIdentity> present,
                            std::span<tk_device_info_t> infos);

[[nodiscard]] bool IsValidFethName(std::string_view name) noexcept;

void InstallInterfaceRegistry();
}  // namespace multitether::capi
