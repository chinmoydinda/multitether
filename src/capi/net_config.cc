#include <SystemConfiguration/SystemConfiguration.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "capi_support.h"
#include "core_foundation_support.h"
#include "multitether/capi/multitether_c.h"
#include "multitether/common/i18n.h"
#include "multitether/common/logging.h"

namespace {
using multitether::Error;
using multitether::Msg;
using multitether::Status;
using multitether::Tr;
using multitether::capi::ClearError;
using multitether::capi::CopyText;
using multitether::capi::CopyToStdString;
using multitether::capi::FillError;
using multitether::capi::FillGenericError;
using multitether::capi::IsValidFethName;
using multitether::capi::MakeCFString;
using multitether::capi::ScopedCFRef;
using multitether::capi::SharedDynamicStore;
[[nodiscard]] Status ValidateInterface(const char* interface_name) {
  if (interface_name == nullptr) {
    return std::unexpected(Error::Generic(Tr(Msg::kCapiInterfaceNameNull)));
  }
  if (!IsValidFethName(interface_name)) {
    return std::unexpected(Error::Generic(Tr(Msg::kCapiInterfaceNotOurs, interface_name)));
  }
  return multitether::Ok();
}

[[nodiscard]] std::optional<std::string> QueryAddress(std::string_view interface_name,
                                                      unsigned long request) noexcept {
  const int fd = ::socket(AF_INET, SOCK_DGRAM, 0);
  if (fd < 0) {
    return std::nullopt;
  }

  ::ifreq ifr{};
  const std::size_t copy_length = std::min(interface_name.size(), sizeof(ifr.ifr_name) - 1);
  std::memcpy(ifr.ifr_name, interface_name.data(), copy_length);
  ifr.ifr_addr.sa_family = AF_INET;

  std::optional<std::string> result;
  if (::ioctl(fd, request, &ifr) == 0) {
    std::array<char, INET_ADDRSTRLEN> text{};
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    const auto* address = reinterpret_cast<const ::sockaddr_in*>(&ifr.ifr_addr);
    if (::inet_ntop(AF_INET, &address->sin_addr, text.data(), text.size()) != nullptr) {
      result = std::string{text.data()};
    }
  }
  ::close(fd);
  return result;
}

[[nodiscard]] std::optional<std::string> FindServiceId(SCDynamicStoreRef store,
                                                       std::string_view interface_name) {
  const ScopedCFRef<CFStringRef> pattern = MakeCFString("State:/Network/Service/[^/]+/IPv4");
  const ScopedCFRef<CFArrayRef> keys{
      ::SCDynamicStoreCopyKeyList(store, pattern.Get())};
  if (!keys) {
    return std::nullopt;
  }

  const CFIndex count = ::CFArrayGetCount(keys.Get());
  for (CFIndex i = 0; i < count; ++i) {
    const auto* const key = static_cast<CFStringRef>(::CFArrayGetValueAtIndex(keys.Get(), i));
    const ScopedCFRef<CFDictionaryRef> entry{
        static_cast<CFDictionaryRef>(::SCDynamicStoreCopyValue(store, key))};
    if (!entry) {
      continue;
    }
    const auto* const name = static_cast<CFStringRef>(
        ::CFDictionaryGetValue(entry.Get(), CFSTR("InterfaceName")));
    if (name == nullptr || CopyToStdString(name) != interface_name) {
      continue;
    }

    const std::string full = CopyToStdString(key);
    constexpr std::string_view kPrefix = "State:/Network/Service/";
    constexpr std::string_view kSuffix = "/IPv4";
    if (full.size() <= kPrefix.size() + kSuffix.size()) {
      continue;
    }
    return full.substr(kPrefix.size(), full.size() - kPrefix.size() - kSuffix.size());
  }
  return std::nullopt;
}

[[nodiscard]] ScopedCFRef<CFDictionaryRef> CopyServiceEntry(SCDynamicStoreRef store,
                                                            std::string_view service_id,
                                                            std::string_view leaf) {
  const ScopedCFRef<CFStringRef> key =
      MakeCFString(std::format("State:/Network/Service/{}/{}", service_id, leaf));
  return ScopedCFRef<CFDictionaryRef>{
      static_cast<CFDictionaryRef>(::SCDynamicStoreCopyValue(store, key.Get()))};
}

[[nodiscard]] std::string StringField(CFDictionaryRef dictionary, CFStringRef field) {
  if (dictionary == nullptr) {
    return {};
  }
  const auto* const value = static_cast<CFStringRef>(::CFDictionaryGetValue(dictionary, field));
  if (value == nullptr || ::CFGetTypeID(value) != ::CFStringGetTypeID()) {
    return {};
  }
  return CopyToStdString(value);
}
}  // namespace

tk_result_t tk_net_query(const char* interface_name, tk_net_state_t* out_state,
                         tk_error_t* out_error) {
  ClearError(out_error);
  if (out_state == nullptr) {
    return TK_ERR_INVALID_ARGUMENT;
  }
  if (const auto status = ValidateInterface(interface_name); !status) {
    FillError(out_error, status.error());
    return TK_ERR_INVALID_ARGUMENT;
  }
  *out_state = tk_net_state_t{};

  if (const std::optional<std::string> address = QueryAddress(interface_name, SIOCGIFADDR);
      address.has_value()) {
    out_state->has_address = true;
    CopyText(out_state->address, *address);
  }
  if (const std::optional<std::string> netmask = QueryAddress(interface_name, SIOCGIFNETMASK);
      netmask.has_value()) {
    CopyText(out_state->netmask, *netmask);
  }

  SCDynamicStoreRef store = SharedDynamicStore();
  if (store == nullptr) {
    return TK_OK;
  }

  const std::optional<std::string> service_id = FindServiceId(store, interface_name);
  if (service_id.has_value()) {
    const ScopedCFRef<CFDictionaryRef> ipv4 = CopyServiceEntry(store, *service_id, "IPv4");
    const std::string router = StringField(ipv4.Get(), CFSTR("Router"));
    if (!router.empty()) {
      CopyText(out_state->router, router);
      out_state->has_default_route = true;
    }
  }

  return TK_OK;
}
