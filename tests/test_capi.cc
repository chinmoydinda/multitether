#include <array>
#include <cstring>
#include <string>

#include <doctest.h>

#include "capi_support.h"
#include "multitether/capi/multitether_c.h"
#include "multitether/common/logging.h"

using multitether::capi::CopyText;
using multitether::capi::IsValidFethName;

TEST_SUITE("capi.support") {
TEST_CASE("capi scenario 1: CopyText") {
  std::array<char, 16> buffer{};
  CopyText(buffer.data(), buffer.size(), "feth0");
  CHECK(std::string{buffer.data()} == "feth0");
}

TEST_CASE("capi scenario 2: CopyText") {
  CopyText(nullptr, 16, "x");
  std::array<char, 4> buffer{'a', 'b', 'c', 'd'};
  CopyText(buffer.data(), 0, "xyz");
  CHECK(buffer[0] == 'a');
}

TEST_CASE("capi scenario 3: CopyText UTF-8") {
  std::array<char, 6> buffer{};
  CopyText(buffer.data(), buffer.size(), "€€");
  CHECK(std::string{buffer.data()} == "€");
  CHECK(std::strlen(buffer.data()) == 3);
}

TEST_CASE("capi scenario 4: CopyText") {
  std::array<char, 7> buffer{};
  CopyText(buffer.data(), buffer.size(), "€€");
  CHECK(std::string{buffer.data()} == "€€");
}

TEST_CASE("capi scenario 5: CopyText") {
  std::array<char, 3> buffer{};
  CopyText(buffer.data(), buffer.size(), "€€");
  CHECK(std::string{buffer.data()}.empty());
}

TEST_CASE("capi scenario 6: IsValidFethName feth+") {
  CHECK(IsValidFethName("feth0"));
  CHECK(IsValidFethName("feth12"));

  SUBCASE("capi scenario 7: Wi-Fi") {
    CHECK_FALSE(IsValidFethName("en0"));
    CHECK_FALSE(IsValidFethName("bridge0"));
  }
  SUBCASE("capi scenario 8") {
    CHECK_FALSE(IsValidFethName("feth"));
    CHECK_FALSE(IsValidFethName("fethX"));
    CHECK_FALSE(IsValidFethName("feth0a"));
  }
  SUBCASE("capi scenario 9") {
    CHECK_FALSE(IsValidFethName("feth0; rm -rf /"));
    CHECK_FALSE(IsValidFethName("../feth0"));
    CHECK_FALSE(IsValidFethName(""));
  }
  SUBCASE("capi scenario 10: IFNAMSIZ") {
    CHECK_FALSE(IsValidFethName("feth123456789012345"));
  }
}

namespace {
using multitether::capi::DeviceIdentity;
using multitether::capi::ReconcileDeviceStrings;
using multitether::capi::RememberedDeviceStrings;

tk_device_info_t DeviceInfo(const DeviceIdentity& identity, const char* manufacturer,
                            const char* product, const char* serial) {
  tk_device_info_t info{};
  info.vendor_id = identity.vendor_id;
  info.product_id = identity.product_id;
  info.bus_number = identity.bus_number;
  info.device_address = identity.device_address;
  CopyText(info.manufacturer, manufacturer);
  CopyText(info.product, product);
  CopyText(info.serial, serial);
  return info;
}
}  // namespace

TEST_CASE("capi scenario 11: ReconcileDeviceStrings") {
  const DeviceIdentity phone{.bus_number = 0, .device_address = 1,
                             .vendor_id = 0x2d95, .product_id = 0x600b};
  std::vector<RememberedDeviceStrings> memory;
  const std::vector<DeviceIdentity> present{phone};

  std::vector<tk_device_info_t> infos{DeviceInfo(phone, "vivo", "iQOO Z10x", "10AFAC2X72005KT")};
  ReconcileDeviceStrings(memory, present, infos);
  REQUIRE(memory.size() == 1);

  infos = {DeviceInfo(phone, "", "", "")};
  ReconcileDeviceStrings(memory, present, infos);
  CHECK(std::string{infos[0].manufacturer} == "vivo");
  CHECK(std::string{infos[0].product} == "iQOO Z10x");
  CHECK(std::string{infos[0].serial} == "10AFAC2X72005KT");

  SUBCASE("capi scenario 12") {
    infos = {DeviceInfo(phone, "vivo", "iQOO Z10x", "NEWSERIAL")};
    ReconcileDeviceStrings(memory, present, infos);
    infos = {DeviceInfo(phone, "", "", "")};
    ReconcileDeviceStrings(memory, present, infos);
    CHECK(std::string{infos[0].serial} == "NEWSERIAL");
  }
}

TEST_CASE("capi scenario 13: ReconcileDeviceStrings") {
  const DeviceIdentity old_phone{.bus_number = 0, .device_address = 1,
                                 .vendor_id = 0x2d95, .product_id = 0x600b};
  std::vector<RememberedDeviceStrings> memory;
  std::vector<tk_device_info_t> infos{DeviceInfo(old_phone, "vivo", "iQOO Z10x", "SN1")};
  ReconcileDeviceStrings(memory, {std::vector<DeviceIdentity>{old_phone}}, infos);
  REQUIRE(memory.size() == 1);

  SUBCASE("capi scenario 14: VID:PID") {
    const DeviceIdentity other{.bus_number = 0, .device_address = 1,
                               .vendor_id = 0x18d1, .product_id = 0x4ee4};
    infos = {DeviceInfo(other, "", "", "")};
    ReconcileDeviceStrings(memory, {std::vector<DeviceIdentity>{other}}, infos);
    CHECK(std::string{infos[0].product}.empty());
  }

  SUBCASE("capi scenario 15") {
    infos.clear();
    ReconcileDeviceStrings(memory, {}, infos);
    CHECK(memory.empty());

    infos = {DeviceInfo(old_phone, "", "", "")};
    ReconcileDeviceStrings(memory, {std::vector<DeviceIdentity>{old_phone}}, infos);
    CHECK(std::string{infos[0].product}.empty());
  }
}

TEST_CASE("capi scenario 16: ReconcileDeviceStrings") {
  const DeviceIdentity mute{.bus_number = 2, .device_address = 7,
                            .vendor_id = 0x1234, .product_id = 0x5678};
  std::vector<RememberedDeviceStrings> memory;
  std::vector<tk_device_info_t> infos{DeviceInfo(mute, "", "", "")};
  ReconcileDeviceStrings(memory, {std::vector<DeviceIdentity>{mute}}, infos);

  CHECK(memory.empty());
  CHECK(std::string{infos[0].product}.empty());
}

TEST_CASE("capi scenario 17: ReconcileDeviceStrings") {
  const DeviceIdentity first{.bus_number = 0, .device_address = 1,
                             .vendor_id = 0x2d95, .product_id = 0x600b};
  const DeviceIdentity second{.bus_number = 0, .device_address = 2,
                              .vendor_id = 0x18d1, .product_id = 0x4ee4};
  std::vector<RememberedDeviceStrings> memory;
  const std::vector<DeviceIdentity> both{first, second};

  std::vector<tk_device_info_t> infos{DeviceInfo(first, "vivo", "iQOO Z10x", "SN1"),
                                      DeviceInfo(second, "Google", "Pixel", "SN2")};
  ReconcileDeviceStrings(memory, both, infos);
  REQUIRE(memory.size() == 2);

  infos = {DeviceInfo(first, "", "", "")};
  ReconcileDeviceStrings(memory, both, infos);
  CHECK(memory.size() == 2);

  infos = {DeviceInfo(second, "", "", "")};
  ReconcileDeviceStrings(memory, both, infos);
  CHECK(std::string{infos[0].product} == "Pixel");
}
}  // TEST_SUITE("capi.support")

TEST_SUITE("capi.basics") {
TEST_CASE("capi scenario 18: tk_list_devices") {
  std::size_t count = 0;
  tk_error_t error{};

  const tk_result_t result = tk_list_devices(nullptr, 0, &count, false, &error);
  CHECK(result == TK_OK);
  CHECK(std::strlen(error.message) == 0);

  SUBCASE("capi scenario 19: out_count") {
    CHECK(tk_list_devices(nullptr, 0, nullptr, false, nullptr) == TK_ERR_INVALID_ARGUMENT);
  }
}
}

TEST_SUITE("capi.session") {
TEST_CASE("capi scenario 20: tk_session_config_init") {
  tk_session_config_t config{
      .vendor_id = 0x1234, .product_id = 0x5678, .bus_number = 1, .device_address = 2};
  tk_session_config_init(&config);

  CHECK(config.vendor_id == 0);
  CHECK(config.product_id == 0);
  CHECK(config.bus_number == 0);
  CHECK(config.device_address == 0);
}

TEST_CASE("capi scenario 21: root") {
  tk_session_config_t config{};
  tk_session_config_init(&config);

  tk_error_t error{};
  tk_session_t* session = tk_session_create(&config, &error);
  REQUIRE(session != nullptr);
  CHECK(std::strlen(error.message) == 0);

  SUBCASE("capi scenario 22: IDLE") {
    tk_session_status_t status{};
    REQUIRE(tk_session_status_get(session, &status) == TK_OK);
    CHECK(status.run_state == TK_RUN_IDLE);
    CHECK(std::strlen(status.system_interface) == 0);
    CHECK(std::strlen(status.fatal) == 0);
    CHECK(status.rx_bytes == 0);
    CHECK(status.tx_bytes == 0);
    CHECK(status.monotonic_nanos > 0);
  }

  SUBCASE("capi scenario 24: root TK_ERR_PERMISSION") {
    CHECK(tk_session_start(session, &error) == TK_ERR_PERMISSION);
    CHECK(std::strlen(error.message) > 0);
  }

  SUBCASE("capi scenario 25: stop") {
    CHECK(tk_session_stop(session) == TK_OK);
    CHECK(tk_session_stop(session) == TK_OK);
  }

  tk_session_destroy(session);
}

TEST_CASE("capi scenario 26") {
  tk_error_t error{};
  CHECK(tk_session_create(nullptr, &error) == nullptr);
  CHECK(std::strlen(error.message) > 0);

  CHECK(tk_session_start(nullptr, nullptr) == TK_ERR_INVALID_ARGUMENT);
  CHECK(tk_session_stop(nullptr) == TK_ERR_INVALID_ARGUMENT);
  CHECK(tk_session_status_get(nullptr, nullptr) == TK_ERR_INVALID_ARGUMENT);
  tk_session_destroy(nullptr);
  tk_session_config_init(nullptr);
}
}  // TEST_SUITE("capi.session")

TEST_SUITE("capi.net_config") {
TEST_CASE("Network query validates interface and output pointers") {
  tk_error_t error{};
  tk_net_state_t state{};
  CHECK(tk_net_query("en0", &state, &error) == TK_ERR_INVALID_ARGUMENT);
  CHECK(tk_net_query(nullptr, &state, &error) == TK_ERR_INVALID_ARGUMENT);
  CHECK(tk_net_query("feth99", nullptr, &error) == TK_ERR_INVALID_ARGUMENT);
  CHECK(tk_net_query("feth99", &state, &error) == TK_OK);
  CHECK_FALSE(state.has_address);
  CHECK(std::strlen(error.message) == 0);
}
TEST_CASE("Orphan cleanup requires root and resets the output") {
  std::size_t removed = 1;
  tk_error_t error{};
  CHECK(tk_cleanup_orphan_interfaces(&removed, &error) == TK_ERR_PERMISSION);
  CHECK(removed == 0);
}
}
