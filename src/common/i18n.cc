#include "multitether/common/i18n.h"
#include "multitether/common/error.h"
#include <array>
#include <cstddef>

namespace multitether {
namespace {
constexpr std::array<std::string_view, static_cast<std::size_t>(Msg::kMessageCount)> kMessages{
#define MULTITETHER_MESSAGE(id, text) text,
#include "multitether/common/messages.def"
#undef MULTITETHER_MESSAGE
};
}
std::string_view Text(Msg id) noexcept {
  const auto index = static_cast<std::size_t>(id);
  return index < kMessages.size() ? kMessages[index] : std::string_view{};
}
namespace detail {
std::string FormatMessage(Msg id, std::format_args args) noexcept {
  const auto pattern = Text(id);
  try { return std::vformat(pattern, args); }
  catch (...) { return std::string{pattern}; }
}
std::string_view ContextSeparator() noexcept { return ": "; }
}
}
