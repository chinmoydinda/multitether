#include "multitether/usb/context.h"

#include <format>

#include "multitether/common/i18n.h"
#include "multitether/common/logging.h"
#include "multitether/common/scheduling.h"

namespace multitether::usb {
namespace {
constexpr long kEventLoopTimeoutSeconds = 0;
constexpr long kEventLoopTimeoutMicros = 250'000;
}  // namespace

Result<std::unique_ptr<Context>> Context::Create() {
  auto context = std::unique_ptr<Context>(new Context());

  const int rc = ::libusb_init(&context->context_);
  if (rc != LIBUSB_SUCCESS) {
    return std::unexpected(Error::FromLibUsb(rc, Tr(Msg::kUsbInitFailed)));
  }

  MULTITETHER_INFO_TR(Msg::kUsbInitialized, VersionString(),
                    Text(SupportsHotplug() ? Msg::kUsbHotplugSupported
                                           : Msg::kUsbHotplugUnsupported));

  context->running_.store(true, std::memory_order_release);
  context->event_thread_ = std::thread([raw = context.get()] { raw->RunEventLoop(); });
  return context;
}

Context::~Context() {
  RequestStop();

  if (event_thread_.joinable()) {
    if (context_ != nullptr) {
      ::libusb_interrupt_event_handler(context_);
    }
    event_thread_.join();
  }

  if (context_ != nullptr) {
    ::libusb_exit(context_);
    context_ = nullptr;
  }
  MULTITETHER_DEBUG_TR(Msg::kUsbContextReleased);
}

std::string Context::VersionString() {
  const ::libusb_version* version = ::libusb_get_version();
  if (version == nullptr) {
    return std::string{Text(Msg::kUsbUnknownVersion)};
  }
  return std::format("{}.{}.{}.{}", version->major, version->minor, version->micro,
                     version->nano);
}

bool Context::SupportsHotplug() noexcept {
  return ::libusb_has_capability(LIBUSB_CAP_HAS_HOTPLUG) != 0;
}

void Context::RequestStop() noexcept {
  const bool already = stop_requested_.exchange(true, std::memory_order_acq_rel);
  if (!already && context_ != nullptr) {
    ::libusb_interrupt_event_handler(context_);
  }
}

void Context::RunEventLoop() noexcept {
  ConfigureCurrentThread("usb-event", ThreadRole::kDataPath);
  MULTITETHER_DEBUG_TR(Msg::kUsbEventThreadStarted);

  ::timeval timeout{};
  timeout.tv_sec = kEventLoopTimeoutSeconds;
  timeout.tv_usec = kEventLoopTimeoutMicros;

  while (!stop_requested_.load(std::memory_order_acquire)) {
    const int rc = ::libusb_handle_events_timeout_completed(context_, &timeout, nullptr);

    if (rc == LIBUSB_SUCCESS || rc == LIBUSB_ERROR_INTERRUPTED) {
      continue;
    }
    if (rc == LIBUSB_ERROR_NO_DEVICE) {
      MULTITETHER_DEBUG_TR(Msg::kUsbEventLoopNoDevice);
      continue;
    }
    MULTITETHER_ERROR_TR(Msg::kUsbHandleEventsFailed, ::libusb_error_name(rc), rc);
    break;
  }

  running_.store(false, std::memory_order_release);
  MULTITETHER_DEBUG_TR(Msg::kUsbEventThreadExited);
}
}  // namespace multitether::usb
