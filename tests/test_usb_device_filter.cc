#include "doctest.h"

#include "multitether/usb/device.h"

TEST_SUITE("usb.device_filter") {
TEST_CASE("bus zero identifies one phone instead of disabling the address filter") {
  multitether::usb::DeviceFilter filter;
  filter.bus_number = 0;
  filter.device_address = 6;
  CHECK(filter.Matches(0x22d9, 0x276a, 0, 6));
  CHECK_FALSE(filter.Matches(0x22d9, 0x276a, 1, 6));
  CHECK_FALSE(filter.Matches(0x22d9, 0x276a, 0, 7));
  CHECK_FALSE(filter.Matches(0x04e8, 0x6863, 1, 1));
}

TEST_CASE("identical phone models remain independently selectable") {
  multitether::usb::DeviceFilter filter;
  filter.vendor_id = 0x22d9;
  filter.product_id = 0x276a;
  filter.bus_number = 1;
  filter.device_address = 3;
  CHECK(filter.Matches(0x22d9, 0x276a, 1, 3));
  CHECK_FALSE(filter.Matches(0x22d9, 0x276a, 1, 4));
  CHECK_FALSE(filter.Matches(0x04e8, 0x6863, 1, 3));
}

TEST_CASE("VID PID and unspecified device selection preserve legacy behavior") {
  multitether::usb::DeviceFilter filter;
  CHECK(filter.Matches(0x04e8, 0x6863, 1, 1));
  CHECK(filter.Matches(0x22d9, 0x276a, 0, 6));
  filter.vendor_id = 0x04e8;
  filter.product_id = 0x6863;
  CHECK(filter.Matches(0x04e8, 0x6863, 1, 1));
  CHECK_FALSE(filter.Matches(0x22d9, 0x276a, 0, 6));
}
}
