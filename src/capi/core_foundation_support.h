#pragma once

#include <CoreFoundation/CoreFoundation.h>
#include <SystemConfiguration/SystemConfiguration.h>

#include <string>
#include <string_view>
#include <utility>

namespace multitether::capi {
template <typename T>
class ScopedCFRef {
 public:
  ScopedCFRef() = default;
  explicit ScopedCFRef(T reference) noexcept : reference_(reference) {}

  ScopedCFRef(const ScopedCFRef&) = delete;
  ScopedCFRef& operator=(const ScopedCFRef&) = delete;

  ScopedCFRef(ScopedCFRef&& other) noexcept
      : reference_(std::exchange(other.reference_, nullptr)) {}

  ScopedCFRef& operator=(ScopedCFRef&& other) noexcept {
    if (this != &other) {
      Reset();
      reference_ = std::exchange(other.reference_, nullptr);
    }
    return *this;
  }

  ~ScopedCFRef() { Reset(); }

  [[nodiscard]] T Get() const noexcept { return reference_; }

  explicit operator bool() const noexcept { return reference_ != nullptr; }

 private:
  void Reset() noexcept {
    if (reference_ != nullptr) {
      ::CFRelease(reference_);
      reference_ = nullptr;
    }
  }

  T reference_ = nullptr;
};

[[nodiscard]] ScopedCFRef<CFStringRef> MakeCFString(std::string_view text);

[[nodiscard]] std::string CopyToStdString(CFStringRef text);

/// Returns a process-wide SCDynamicStoreRef, or nullptr on failure.
[[nodiscard]] SCDynamicStoreRef SharedDynamicStore();
}  // namespace multitether::capi
