#include "multitether/common/logging.h"

#include <pthread.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <mutex>

namespace multitether {
namespace {
// NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables)

std::atomic<LogLevel> g_runtime_level{LogLevel::kInfo};

std::atomic<int> g_color_enabled{-1};

std::mutex& OutputMutex() {
  static std::mutex mutex;
  return mutex;
}

constexpr std::size_t kThreadNameCapacity = 16;
thread_local std::array<char, kThreadNameCapacity> t_thread_name{};

std::string_view CurrentThreadName() {
  if (t_thread_name[0] == '\0') {
    return "main";
  }
  return {t_thread_name.data(), std::strlen(t_thread_name.data())};
}

struct LevelStyle {
  const char* label;
  const char* color;
};

LevelStyle StyleFor(LogLevel level) {
  switch (level) {
    case LogLevel::kTrace:
      return {"TRACE", "\033[90m"};
    case LogLevel::kDebug:
      return {"DEBUG", "\033[36m"};
    case LogLevel::kInfo:
      return {"INFO ", "\033[32m"};
    case LogLevel::kWarn:
      return {"WARN ", "\033[33m"};
    case LogLevel::kError:
      return {"ERROR", "\033[31m"};
    case LogLevel::kOff:
      break;
  }
  return {"?????", ""};
}

bool ColorEnabled() {
  int cached = g_color_enabled.load(std::memory_order_relaxed);
  if (cached < 0) {
    cached = ::isatty(STDERR_FILENO) != 0 ? 1 : 0;
    g_color_enabled.store(cached, std::memory_order_relaxed);
  }
  return cached != 0;
}

void FormatTimestamp(std::array<char, 16>& out) {
  ::timespec ts{};
  ::clock_gettime(CLOCK_REALTIME, &ts);
  ::tm local{};
  ::localtime_r(&ts.tv_sec, &local);
  const long millis = ts.tv_nsec / 1'000'000;
  std::snprintf(out.data(), out.size(), "%02d:%02d:%02d.%03ld", local.tm_hour, local.tm_min,
                local.tm_sec, millis);
}

// NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables)
}  // namespace

void SetLogLevel(LogLevel level) noexcept {
  g_runtime_level.store(level, std::memory_order_relaxed);
}

LogLevel GetLogLevel() noexcept {
  return g_runtime_level.load(std::memory_order_relaxed);
}

void SetCurrentThreadName(std::string_view name) noexcept {
  const std::size_t copy_len = std::min(name.size(), kThreadNameCapacity - 1);
  std::memcpy(t_thread_name.data(), name.data(), copy_len);
  t_thread_name[copy_len] = '\0';

  ::pthread_setname_np(t_thread_name.data());
}

namespace detail {
bool IsLogLevelEnabled(LogLevel level) noexcept {
  return level >= g_runtime_level.load(std::memory_order_relaxed);
}

void EmitLogLine(LogLevel level, std::string_view file, unsigned line,
                 std::string_view message) noexcept {
  const LevelStyle style = StyleFor(level);
  std::array<char, 16> timestamp{};
  FormatTimestamp(timestamp);

  const bool color = ColorEnabled();
  const char* color_on = color ? style.color : "";
  const char* color_off = color ? "\033[0m" : "";
  const std::string_view thread_name = CurrentThreadName();

  const std::lock_guard<std::mutex> guard(OutputMutex());
  std::fprintf(stderr, "%s%s%s %s [%-10.*s] %.*s:%u  %.*s\n", color_on, style.label, color_off,
               timestamp.data(), static_cast<int>(thread_name.size()), thread_name.data(),
               static_cast<int>(file.size()), file.data(), line, static_cast<int>(message.size()),
               message.data());
}
}  // namespace detail
}  // namespace multitether
