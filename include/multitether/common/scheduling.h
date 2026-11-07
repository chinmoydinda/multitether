#pragma once

#include <pthread.h>

#include <cstdint>
#include <string_view>

namespace multitether {
enum class ThreadRole : std::uint8_t {
  kDataPath,
  kControl,
  kAuxiliary,
};

void ConfigureCurrentThread(std::string_view name, ThreadRole role) noexcept;

[[nodiscard]] unsigned int QosClassFor(ThreadRole role) noexcept;
}  // namespace multitether
