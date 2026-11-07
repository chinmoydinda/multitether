#include "core_foundation_support.h"

#include <vector>

namespace multitether::capi {
ScopedCFRef<CFStringRef> MakeCFString(std::string_view text) {
  return ScopedCFRef<CFStringRef>{::CFStringCreateWithBytes(
      kCFAllocatorDefault, reinterpret_cast<const UInt8*>(text.data()),  // NOLINT
      static_cast<CFIndex>(text.size()), kCFStringEncodingUTF8,
      /*isExternalRepresentation=*/static_cast<Boolean>(false))};
}

std::string CopyToStdString(CFStringRef text) {
  if (text == nullptr) {
    return {};
  }

  if (const char* direct = ::CFStringGetCStringPtr(text, kCFStringEncodingUTF8);
      direct != nullptr) {
    return std::string{direct};
  }

  const CFIndex length = ::CFStringGetLength(text);
  const CFIndex capacity = ::CFStringGetMaximumSizeForEncoding(length, kCFStringEncodingUTF8) + 1;
  std::vector<char> buffer(static_cast<std::size_t>(capacity), '\0');
  if (::CFStringGetCString(text, buffer.data(), capacity, kCFStringEncodingUTF8) == 0) {
    return {};
  }
  return std::string{buffer.data()};
}

SCDynamicStoreRef SharedDynamicStore() {
  static SCDynamicStoreRef store = ::SCDynamicStoreCreate(
      kCFAllocatorDefault, CFSTR("MultiTether"), /*callout=*/nullptr, /*context=*/nullptr);
  return store;
}
}  // namespace multitether::capi
