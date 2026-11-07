#pragma once

#include <cstdint>
#include <string_view>

namespace multitether {
struct Version {
  std::uint16_t major;
  std::uint16_t minor;
  std::uint16_t patch;
};

Version GetVersion() noexcept;

std::string_view GetVersionString() noexcept;
}  // namespace multitether
