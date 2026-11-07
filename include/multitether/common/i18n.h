#pragma once
#include <cstdint>
#include <format>
#include <string>
#include <string_view>

namespace multitether {
enum class Msg : std::uint16_t {
#define MULTITETHER_MESSAGE(id, text) id,
#include "multitether/common/messages.def"
#undef MULTITETHER_MESSAGE
  kMessageCount,
};
[[nodiscard]] std::string_view Text(Msg id) noexcept;
namespace detail {
[[nodiscard]] std::string FormatMessage(Msg id, std::format_args args) noexcept;
}
// Formatting allocates; use only on initialization/control paths.
template <typename... Args>
[[nodiscard]] std::string Tr(Msg id, const Args&... args) {
  return detail::FormatMessage(id, std::make_format_args(args...));
}
}
