#pragma once

#include <cerrno>
#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <utility>

namespace multitether {
namespace detail {
[[nodiscard]] std::string_view ContextSeparator() noexcept;
}  // namespace detail

enum class ErrorDomain : std::uint8_t {
  kGeneric,
  kErrno,
  kLibUsb,
  kRndis,
};

class Error {
 public:
  Error() = default;

  static Error Generic(std::string context) {
    return Error{ErrorDomain::kGeneric, 0, std::move(context)};
  }

  static Error FromErrno(int err, std::string context) {
    return Error{ErrorDomain::kErrno, err != 0 ? err : errno, std::move(context)};
  }

  static Error FromLibUsb(int rc, std::string context) {
    return Error{ErrorDomain::kLibUsb, rc, std::move(context)};
  }

  static Error FromRndisStatus(std::uint32_t status, std::string context) {
    return Error{ErrorDomain::kRndis, static_cast<std::int64_t>(status), std::move(context)};
  }

  [[nodiscard]] ErrorDomain Domain() const noexcept { return domain_; }

  [[nodiscard]] std::int64_t Code() const noexcept { return code_; }

  [[nodiscard]] std::string_view Context() const noexcept { return context_; }

  [[nodiscard]] Error WithContext(std::string_view outer) && {
    context_ = std::string{outer} + std::string{detail::ContextSeparator()} + context_;
    return std::move(*this);
  }

  [[nodiscard]] std::string ToString() const;

 private:
  Error(ErrorDomain domain, std::int64_t code, std::string context)
      : code_(code), context_(std::move(context)), domain_(domain) {}

  std::int64_t code_ = 0;
  std::string context_;
  ErrorDomain domain_ = ErrorDomain::kGeneric;
};

template <typename T>
using Result = std::expected<T, Error>;

using Status = std::expected<void, Error>;

inline Status Ok() noexcept {
  return Status{};
}

namespace detail {
#define MULTITETHER_CONCAT_INNER(a, b) a##b
#define MULTITETHER_CONCAT(a, b) MULTITETHER_CONCAT_INNER(a, b)
#define MULTITETHER_UNIQUE(base) MULTITETHER_CONCAT(base, __LINE__)
}  // namespace detail

#define MULTITETHER_RETURN_IF_ERROR(expr)                                      \
  do {                                                                       \
    auto MULTITETHER_UNIQUE(tk_status_) = (expr);                              \
    if (!MULTITETHER_UNIQUE(tk_status_)) {                                     \
      return std::unexpected(std::move(MULTITETHER_UNIQUE(tk_status_)).error()); \
    }                                                                        \
  } while (false)

// NOLINTNEXTLINE(bugprone-macro-parentheses)
#define MULTITETHER_ASSIGN_OR_RETURN(decl, expr)                              \
  auto MULTITETHER_UNIQUE(tk_result_) = (expr);                               \
  if (!MULTITETHER_UNIQUE(tk_result_)) {                                      \
    return std::unexpected(std::move(MULTITETHER_UNIQUE(tk_result_)).error()); \
  }                                                                         \
  decl = std::move(MULTITETHER_UNIQUE(tk_result_)).value()
}  // namespace multitether
