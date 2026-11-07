#include "capi_support.h"

#include <algorithm>
#include <ctime>

namespace multitether::capi {
void ReconcileDeviceStrings(std::vector<RememberedDeviceStrings>& memory,
                            std::span<const DeviceIdentity> present,
                            std::span<tk_device_info_t> infos) {
  std::erase_if(memory, [present](const RememberedDeviceStrings& entry) {
    return std::ranges::find(present, entry.identity) == present.end();
  });

  for (tk_device_info_t& info : infos) {
    const bool read_succeeded =
        info.manufacturer[0] != '\0' || info.product[0] != '\0' || info.serial[0] != '\0';
    const DeviceIdentity identity = IdentityOf(info);
    const auto entry =
        std::ranges::find_if(memory, [identity](const RememberedDeviceStrings& candidate) {
          return candidate.identity == identity;
        });

    if (read_succeeded) {
      RememberedDeviceStrings& slot =
          entry != memory.end()
              ? *entry
              : memory.emplace_back(RememberedDeviceStrings{.identity = identity});
      CopyText(slot.manufacturer, info.manufacturer);
      CopyText(slot.product, info.product);
      CopyText(slot.serial, info.serial);
    } else if (entry != memory.end()) {
      CopyText(info.manufacturer, entry->manufacturer);
      CopyText(info.product, entry->product);
      CopyText(info.serial, entry->serial);
    }
  }
}

bool IsValidFethName(std::string_view name) noexcept {
  constexpr std::string_view kPrefix = "feth";

  if (name.size() <= kPrefix.size() || name.size() >= TK_INTERFACE_NAME_CAPACITY) {
    return false;
  }
  if (!name.starts_with(kPrefix)) {
    return false;
  }
  return std::ranges::all_of(name.substr(kPrefix.size()),
                             [](char character) { return character >= '0' && character <= '9'; });
}
}  // namespace multitether::capi
