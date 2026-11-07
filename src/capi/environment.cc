#include <libusb.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

#include "capi_support.h"
#include "multitether/capi/multitether_c.h"
#include "multitether/common/i18n.h"
#include "multitether/net/feth_device.h"
#include "multitether/usb/context.h"
#include "multitether/usb/device.h"
#include "multitether/version.h"

namespace {
using multitether::capi::ClearError;
using multitether::capi::CopyText;
using multitether::capi::FillError;

void ReadStringDescriptor(::libusb_device_handle* handle, std::uint8_t index, char* destination,
                          std::size_t capacity) noexcept {
  if (index == 0) {
    return;
  }
  std::array<unsigned char, TK_USB_STRING_CAPACITY> buffer{};
  const int length = ::libusb_get_string_descriptor_ascii(
      handle, index, buffer.data(), static_cast<int>(buffer.size()));
  if (length <= 0) {
    return;
  }
  CopyText(destination, capacity,
           std::string_view{reinterpret_cast<const char*>(buffer.data()),
                            static_cast<std::size_t>(length)});
}

void TryReadStrings(::libusb_context* context, tk_device_info_t& info) noexcept {
  ::libusb_device** list = nullptr;
  const ssize_t count = ::libusb_get_device_list(context, &list);
  if (count < 0) {
    return;
  }

  for (ssize_t i = 0; i < count; ++i) {
    if (::libusb_get_bus_number(list[i]) != info.bus_number ||
        ::libusb_get_device_address(list[i]) != info.device_address) {
      continue;
    }

    ::libusb_device_descriptor descriptor{};
    if (::libusb_get_device_descriptor(list[i], &descriptor) != LIBUSB_SUCCESS) {
      break;
    }
    ::libusb_device_handle* handle = nullptr;
    if (::libusb_open(list[i], &handle) != LIBUSB_SUCCESS) {
      break;
    }
    ReadStringDescriptor(handle, descriptor.iManufacturer, info.manufacturer,
                         sizeof(info.manufacturer));
    ReadStringDescriptor(handle, descriptor.iProduct, info.product, sizeof(info.product));
    ReadStringDescriptor(handle, descriptor.iSerialNumber, info.serial, sizeof(info.serial));
    ::libusb_close(handle);
    break;
  }

  ::libusb_free_device_list(list, 1);
}

multitether::usb::Context* SharedEnumerationContext(tk_error_t* out_error) {
  static std::mutex mutex;
  static std::unique_ptr<multitether::usb::Context> context;

  const std::lock_guard<std::mutex> guard(mutex);
  if (context == nullptr) {
    auto created = multitether::usb::Context::Create();
    if (!created) {
      FillError(out_error, created.error());
      return nullptr;
    }
    context = std::move(*created);
  }
  return context.get();
}

struct StringMemory {
  std::mutex mutex;
  std::vector<multitether::capi::RememberedDeviceStrings> entries;
};

StringMemory& SharedStringMemory() {
  static StringMemory memory;
  return memory;
}

void FillDeviceInfo(const multitether::usb::DeviceCandidate& candidate,
                    tk_device_info_t& info) noexcept {
  info = tk_device_info_t{};
  info.vendor_id = candidate.vendor_id;
  info.product_id = candidate.product_id;
  info.bus_number = candidate.bus_number;
  info.device_address = candidate.device_address;
}
}  // namespace

tk_result_t tk_list_devices(tk_device_info_t* out_devices, size_t capacity, size_t* out_count,
                            bool read_strings, tk_error_t* out_error) {
  if (out_count == nullptr) {
    return TK_ERR_INVALID_ARGUMENT;
  }
  ClearError(out_error);
  *out_count = 0;

  multitether::usb::Context* context = SharedEnumerationContext(out_error);
  if (context == nullptr) {
    return TK_ERR_FAILED;
  }

  auto candidates = multitether::usb::FindRndisDevices(*context, {});
  if (!candidates) {
    FillError(out_error, candidates.error());
    return TK_ERR_FAILED;
  }

  *out_count = candidates->size();
  if (out_devices == nullptr) {
    return TK_OK;
  }

  const std::size_t writable = std::min(capacity, candidates->size());
  for (std::size_t i = 0; i < writable; ++i) {
    FillDeviceInfo((*candidates)[i], out_devices[i]);
    if (read_strings) {
      TryReadStrings(context->Raw(), out_devices[i]);
    }
  }

  {
    std::vector<multitether::capi::DeviceIdentity> present;
    present.reserve(candidates->size());
    for (const multitether::usb::DeviceCandidate& candidate : *candidates) {
      present.push_back({.bus_number = candidate.bus_number,
                         .device_address = candidate.device_address,
                         .vendor_id = candidate.vendor_id,
                         .product_id = candidate.product_id});
    }
    StringMemory& memory = SharedStringMemory();
    const std::lock_guard<std::mutex> guard(memory.mutex);
    multitether::capi::ReconcileDeviceStrings(memory.entries, present, {out_devices, writable});
  }
  return TK_OK;
}
