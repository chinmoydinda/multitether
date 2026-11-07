#pragma once

#include <libusb.h>

#include <atomic>
#include <memory>
#include <string>
#include <thread>

#include "multitether/common/error.h"

namespace multitether::usb {
class Context {
 public:
  [[nodiscard]] static Result<std::unique_ptr<Context>> Create();

  Context(const Context&) = delete;
  Context& operator=(const Context&) = delete;
  Context(Context&&) = delete;
  Context& operator=(Context&&) = delete;

  ~Context();

  [[nodiscard]] ::libusb_context* Raw() const noexcept { return context_; }

  [[nodiscard]] static std::string VersionString();

  [[nodiscard]] static bool SupportsHotplug() noexcept;

  void RequestStop() noexcept;

  [[nodiscard]] bool Running() const noexcept {
    return running_.load(std::memory_order_acquire);
  }

 private:
  Context() = default;

  void RunEventLoop() noexcept;

  ::libusb_context* context_ = nullptr;
  std::thread event_thread_;
  std::atomic<bool> stop_requested_{false};
  std::atomic<bool> running_{false};
};
}  // namespace multitether::usb
