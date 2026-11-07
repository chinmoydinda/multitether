#pragma once

#include <cstdint>
#include <format>
#include <string_view>
#include <utility>

#include "multitether/common/i18n.h"

namespace multitether {
enum class LogLevel : std::uint8_t {
  kTrace = 0,
  kDebug = 1,
  kInfo = 2,
  kWarn = 3,
  kError = 4,
  kOff = 5,
};

#ifndef MULTITETHER_COMPILED_MIN_LOG_LEVEL
#ifdef NDEBUG
#define MULTITETHER_COMPILED_MIN_LOG_LEVEL 2  // kInfo
#else
#define MULTITETHER_COMPILED_MIN_LOG_LEVEL 0  // kTrace
#endif
#endif

void SetLogLevel(LogLevel level) noexcept;

LogLevel GetLogLevel() noexcept;

void SetCurrentThreadName(std::string_view name) noexcept;

namespace detail {
[[nodiscard]] bool IsLogLevelEnabled(LogLevel level) noexcept;

void EmitLogLine(LogLevel level, std::string_view file, unsigned line,
                 std::string_view message) noexcept;

constexpr std::string_view BaseName(std::string_view path) noexcept {
  const auto pos = path.find_last_of('/');
  return pos == std::string_view::npos ? path : path.substr(pos + 1);
}

template <typename... Args>
void LogFormatted(LogLevel level, std::string_view file, unsigned line,
                  std::format_string<Args...> fmt, Args&&... args) noexcept {
  try {
    EmitLogLine(level, file, line, std::format(fmt, std::forward<Args>(args)...));
  } catch (...) {  // NOLINT(bugprone-empty-catch)

    EmitLogLine(LogLevel::kError, file, line, Text(Msg::kCommonLogFormatFailed));
  }
}
}  // namespace detail

#define MULTITETHER_LOG(level, ...)                                                          \
  do {                                                                                     \
    if constexpr (static_cast<int>(level) >= MULTITETHER_COMPILED_MIN_LOG_LEVEL) {            \
      if (::multitether::detail::IsLogLevelEnabled(level)) [[unlikely]] {                     \
        ::multitether::detail::LogFormatted((level), ::multitether::detail::BaseName(__FILE__), \
                                          __LINE__, __VA_ARGS__);                           \
      }                                                                                     \
    }                                                                                       \
  } while (false)

#define MULTITETHER_TRACE(...) MULTITETHER_LOG(::multitether::LogLevel::kTrace, __VA_ARGS__)
#define MULTITETHER_DEBUG(...) MULTITETHER_LOG(::multitether::LogLevel::kDebug, __VA_ARGS__)
#define MULTITETHER_INFO(...) MULTITETHER_LOG(::multitether::LogLevel::kInfo, __VA_ARGS__)
#define MULTITETHER_WARN(...) MULTITETHER_LOG(::multitether::LogLevel::kWarn, __VA_ARGS__)
#define MULTITETHER_ERROR(...) MULTITETHER_LOG(::multitether::LogLevel::kError, __VA_ARGS__)

#define MULTITETHER_LOG_TR(level, id, ...) \
  MULTITETHER_LOG(level, "{}", ::multitether::Tr((id)__VA_OPT__(, ) __VA_ARGS__))

#define MULTITETHER_TRACE_TR(id, ...) \
  MULTITETHER_LOG_TR(::multitether::LogLevel::kTrace, id __VA_OPT__(, ) __VA_ARGS__)
#define MULTITETHER_DEBUG_TR(id, ...) \
  MULTITETHER_LOG_TR(::multitether::LogLevel::kDebug, id __VA_OPT__(, ) __VA_ARGS__)
#define MULTITETHER_INFO_TR(id, ...) \
  MULTITETHER_LOG_TR(::multitether::LogLevel::kInfo, id __VA_OPT__(, ) __VA_ARGS__)
#define MULTITETHER_WARN_TR(id, ...) \
  MULTITETHER_LOG_TR(::multitether::LogLevel::kWarn, id __VA_OPT__(, ) __VA_ARGS__)
#define MULTITETHER_ERROR_TR(id, ...) \
  MULTITETHER_LOG_TR(::multitether::LogLevel::kError, id __VA_OPT__(, ) __VA_ARGS__)
}  // namespace multitether
