#include <unistd.h>

#include <cstddef>
#include <cstdio>
#include <fstream>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#include "capi_support.h"
#include "multitether/capi/multitether_c.h"
#include "multitether/common/i18n.h"
#include "multitether/common/logging.h"
#include "multitether/net/feth_device.h"

namespace {
using multitether::Msg;
using multitether::Tr;
using multitether::capi::ClearError;
using multitether::capi::FillGenericError;
using multitether::capi::IsValidFethName;

constexpr const char* kRegistryPath = "/var/run/multitether-interfaces";

std::mutex& RegistryMutex() {
  static std::mutex mutex;
  return mutex;
}

[[nodiscard]] std::vector<std::string> ReadRegistry() {
  std::vector<std::string> names;
  std::ifstream input{kRegistryPath};
  if (!input) {
    return names;
  }
  std::string line;
  while (std::getline(input, line)) {
    while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) {
      line.pop_back();
    }
    if (IsValidFethName(line)) {
      names.push_back(line);
    }
  }
  return names;
}

void WriteRegistry(const std::vector<std::string>& names) noexcept {
  if (names.empty()) {
    ::unlink(kRegistryPath);
    return;
  }
  std::ofstream output{kRegistryPath, std::ios::trunc};
  if (!output) {
    return;
  }
  for (const std::string& name : names) {
    output << name << '\n';
  }
}

void OnInterfaceChanged(std::string_view name, bool created) noexcept {
  const std::lock_guard<std::mutex> guard(RegistryMutex());
  std::vector<std::string> names = ReadRegistry();

  if (created) {
    for (const std::string& existing : names) {
      if (existing == name) {
        return;
      }
    }
    names.emplace_back(name);
  } else {
    std::erase(names, std::string{name});
  }
  WriteRegistry(names);
}
}  // namespace

namespace multitether::capi {
void InstallInterfaceRegistry() {
  net::SetInterfaceRegistry(&OnInterfaceChanged);
}
}  // namespace multitether::capi

tk_result_t tk_cleanup_orphan_interfaces(size_t* out_removed, tk_error_t* out_error) {
  ClearError(out_error);
  if (out_removed != nullptr) {
    *out_removed = 0;
  }
  if (::geteuid() != 0) {
    FillGenericError(out_error, Tr(Msg::kCapiCleanupNeedsRoot));
    return TK_ERR_PERMISSION;
  }

  std::vector<std::string> names;
  {
    const std::lock_guard<std::mutex> guard(RegistryMutex());
    names = ReadRegistry();
  }
  if (names.empty()) {
    return TK_OK;
  }

  std::size_t removed = 0;
  for (const std::string& name : names) {
    if (const auto status = multitether::net::DestroyInterfaceByName(name); status) {
      ++removed;
      continue;
    }
    MULTITETHER_DEBUG_TR(Msg::kCapiOrphanAlreadyGone, name);
  }

  {
    const std::lock_guard<std::mutex> guard(RegistryMutex());
    WriteRegistry({});
  }

  if (out_removed != nullptr) {
    *out_removed = removed;
  }
  return TK_OK;
}
