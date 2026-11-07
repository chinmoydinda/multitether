#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

#include "multitether/common/error.h"

namespace multitether::net {
using MacAddress = std::array<std::uint8_t, 6>;

inline constexpr std::size_t kInterfaceNameCapacity = 16;

[[nodiscard]] Result<std::uint32_t> QueryFethMaxMtu();

[[nodiscard]] Status VerifyFethSysctls();

using InterfaceRegistry = void (*)(std::string_view name, bool created) noexcept;

void SetInterfaceRegistry(InterfaceRegistry registry) noexcept;

[[nodiscard]] Status DestroyInterfaceByName(std::string_view name);

class FethInterface {
 public:
  FethInterface() = default;

  FethInterface(const FethInterface&) = delete;
  FethInterface& operator=(const FethInterface&) = delete;
  FethInterface(FethInterface&& other) noexcept;
  FethInterface& operator=(FethInterface&& other) noexcept;

  ~FethInterface();

  [[nodiscard]] static Result<FethInterface> Create(std::string_view requested_name = {});

  [[nodiscard]] bool Valid() const noexcept { return !name_.empty(); }

  [[nodiscard]] std::string_view Name() const noexcept { return name_; }

  [[nodiscard]] Status PeerWith(const FethInterface& peer);

  [[nodiscard]] Result<std::string> QueryPeer() const;

  [[nodiscard]] Status SetMtu(std::uint32_t mtu);

  [[nodiscard]] Result<std::uint32_t> QueryMtu() const;

  [[nodiscard]] Status SetMacAddress(const MacAddress& mac);

  [[nodiscard]] Result<MacAddress> QueryMacAddress() const;

  [[nodiscard]] Status SetUp(bool up);

  [[nodiscard]] Result<bool> IsUp() const;

  void Release() noexcept { name_.clear(); }

 private:
  explicit FethInterface(std::string name) : name_(std::move(name)) {}

  void Destroy() noexcept;

  std::string name_;
};

class FethPair {
 public:
  [[nodiscard]] static Result<FethPair> Create(std::uint32_t mtu,
                                               const MacAddress* system_mac = nullptr);

  [[nodiscard]] const FethInterface& SystemSide() const noexcept { return system_side_; }

  [[nodiscard]] const FethInterface& DriverSide() const noexcept { return driver_side_; }

  [[nodiscard]] FethInterface& SystemSide() noexcept { return system_side_; }

  [[nodiscard]] FethInterface& DriverSide() noexcept { return driver_side_; }

 private:
  FethPair(FethInterface system_side, FethInterface driver_side)
      : system_side_(std::move(system_side)), driver_side_(std::move(driver_side)) {}

  FethInterface system_side_;
  FethInterface driver_side_;
};

[[nodiscard]] std::array<char, 18> FormatMac(const MacAddress& mac) noexcept;

[[nodiscard]] bool IsRunningAsRoot() noexcept;
}  // namespace multitether::net
