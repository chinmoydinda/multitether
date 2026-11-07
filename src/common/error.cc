#include "multitether/common/error.h"

#include <libusb.h>

#include <array>
#include <cstring>
#include <format>

namespace multitether {
namespace {
std::string SafeStrerror(int err) {
  std::array<char, 128> buffer{};

  if (::strerror_r(err, buffer.data(), buffer.size()) != 0) {
    return std::format("errno {}", err);
  }
  return buffer.data();
}
}  // namespace

std::string Error::ToString() const {
  switch (domain_) {
    case ErrorDomain::kGeneric:
      return context_;

    case ErrorDomain::kErrno: {
      const int err = static_cast<int>(code_);
      return std::format("{} [errno: {}({})]", context_, SafeStrerror(err), err);
    }

    case ErrorDomain::kLibUsb: {
      const int rc = static_cast<int>(code_);
      return std::format("{} [libusb: {}({})]", context_, libusb_error_name(rc), rc);
    }

    case ErrorDomain::kRndis: {
      return std::format("{} [RNDIS_STATUS: {:#010x}]", context_,
                         static_cast<std::uint32_t>(code_));
    }
  }
  return context_;
}
}  // namespace multitether
