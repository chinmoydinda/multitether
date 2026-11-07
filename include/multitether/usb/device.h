#pragma once

#include <libusb.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "multitether/common/error.h"
#include "multitether/rndis/protocol.h"
#include "multitether/usb/context.h"
#include "multitether/rndis/control_channel.h"

namespace multitether::usb {
struct DeviceCandidate {
  std::uint16_t vendor_id = 0;
  std::uint16_t product_id = 0;
  std::uint8_t bus_number = 0;
  std::uint8_t device_address = 0;

  std::uint8_t control_interface = 0;

  std::uint8_t data_interface = 0;

  rndis::InterfaceSignature signature{};

  bool used_android_quirk = false;

  [[nodiscard]] std::string Describe() const;
};

struct DeviceFilter {
  std::uint16_t vendor_id = 0;
  std::uint16_t product_id = 0;

  std::uint8_t bus_number = 0;
  std::uint8_t device_address = 0;

  /// Address zero is the wildcard; bus zero is a valid physical bus.
  [[nodiscard]] bool Matches(std::uint16_t vendor, std::uint16_t product,
                             std::uint8_t bus, std::uint8_t address) const noexcept {
    return (vendor_id == 0 || vendor_id == vendor) &&
           (product_id == 0 || product_id == product) &&
           (device_address == 0 || (bus_number == bus && device_address == address));
  }
};

[[nodiscard]] Result<std::vector<DeviceCandidate>> FindRndisDevices(const Context& context,
                                                                   const DeviceFilter& filter = {});

class Device {
 public:
  [[nodiscard]] static Result<std::unique_ptr<Device>> Open(const Context& context,
                                                            const DeviceCandidate& candidate);

  Device(const Device&) = delete;
  Device& operator=(const Device&) = delete;
  Device(Device&&) = delete;
  Device& operator=(Device&&) = delete;

  ~Device();

  [[nodiscard]] ::libusb_device_handle* Handle() const noexcept { return handle_; }

  [[nodiscard]] const DeviceCandidate& Candidate() const noexcept { return candidate_; }

  [[nodiscard]] std::string_view Describe() const noexcept { return description_; }

  [[nodiscard]] std::uint8_t BulkInEndpoint() const noexcept { return bulk_in_endpoint_; }

  [[nodiscard]] std::uint8_t BulkOutEndpoint() const noexcept { return bulk_out_endpoint_; }

  [[nodiscard]] std::uint8_t InterruptInEndpoint() const noexcept {
    return interrupt_in_endpoint_;
  }

  [[nodiscard]] std::uint16_t BulkMaxPacketSize() const noexcept { return bulk_max_packet_size_; }

  [[nodiscard]] std::uint16_t InterruptMaxPacketSize() const noexcept {
    return interrupt_max_packet_size_;
  }

  [[nodiscard]] std::string_view SpeedName() const noexcept;

  [[nodiscard]] Status ClearHalt(std::uint8_t endpoint);

 private:
  Device() = default;

  [[nodiscard]] Status ResolveEndpoints(const ::libusb_config_descriptor& config);

  ::libusb_device_handle* handle_ = nullptr;
  DeviceCandidate candidate_{};
  std::string description_;

  bool control_interface_claimed_ = false;
  bool data_interface_claimed_ = false;

  std::uint8_t bulk_in_endpoint_ = 0;
  std::uint8_t bulk_out_endpoint_ = 0;
  std::uint8_t interrupt_in_endpoint_ = 0;
  std::uint16_t bulk_max_packet_size_ = 0;
  std::uint16_t interrupt_max_packet_size_ = 0;
  int speed_ = 0;
};

class UsbControlChannel final : public rndis::ControlChannel {
 public:
  UsbControlChannel(Device& device, std::uint32_t timeout_millis);

  UsbControlChannel(const UsbControlChannel&) = delete;
  UsbControlChannel& operator=(const UsbControlChannel&) = delete;
  UsbControlChannel(UsbControlChannel&&) = delete;
  UsbControlChannel& operator=(UsbControlChannel&&) = delete;

  ~UsbControlChannel() override;

  [[nodiscard]] Status StartNotificationListener();

  void StopNotificationListener();

  [[nodiscard]] Status SendMessage(std::span<const std::byte> message) override;
  [[nodiscard]] Result<std::span<const std::byte>> ReceiveMessage() override;
  [[nodiscard]] rndis::NotificationResult WaitForNotification(std::uint32_t timeout_millis) override;

  [[nodiscard]] std::uint32_t TimeoutMillis() const noexcept override { return timeout_millis_; }

  [[nodiscard]] std::string_view Describe() const noexcept override {
    return device_->Describe();
  }

 private:
  static void NotificationCallbackTrampoline(::libusb_transfer* transfer);
  void OnNotificationComplete() noexcept;

  Device* device_;
  std::uint32_t timeout_millis_;

  std::vector<std::byte> response_buffer_;

  std::vector<std::byte> notification_buffer_;

  ::libusb_transfer* notification_transfer_ = nullptr;

  std::atomic<bool> notification_pending_{false};

  std::atomic<bool> notification_in_flight_{false};
  std::atomic<bool> notification_stopping_{false};
};
}  // namespace multitether::usb
