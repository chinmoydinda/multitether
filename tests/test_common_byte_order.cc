#include <array>
#include <cstddef>
#include <cstdint>

#include <doctest.h>

#include "multitether/common/byte_order.h"

using multitether::AlignUp;
using multitether::IsPowerOfTwo;
using multitether::LoadBe16;
using multitether::LoadLe16;
using multitether::LoadLe32;
using multitether::LoadLe64;
using multitether::StoreBe16;
using multitether::StoreLe32;
using multitether::StoreLe64;

TEST_SUITE("common.byte_order") {
TEST_CASE("common_byte_order scenario 1") {
  const std::array<std::byte, 8> bytes{std::byte{0x11}, std::byte{0x22}, std::byte{0x33},
                                       std::byte{0x44}, std::byte{0x55}, std::byte{0x66},
                                       std::byte{0x77}, std::byte{0x88}};
  CHECK(LoadLe16(bytes.data()) == 0x2211U);
  CHECK(LoadLe32(bytes.data()) == 0x44332211U);
  CHECK(LoadLe64(bytes.data()) == 0x8877665544332211ULL);
}

TEST_CASE("common_byte_order scenario 2") {
  const std::array<std::byte, 2> bytes{std::byte{0x08}, std::byte{0x00}};
  // 0x0800 = ETHERTYPE_IP
  CHECK(LoadBe16(bytes.data()) == 0x0800U);
}

TEST_CASE("common_byte_order scenario 3") {
  std::array<std::byte, 16> buffer{};
  StoreLe32(buffer.data(), 0xDEADBEEFU);
  CHECK(LoadLe32(buffer.data()) == 0xDEADBEEFU);

  StoreLe64(buffer.data() + 8, 0x0123456789ABCDEFULL);
  CHECK(LoadLe64(buffer.data() + 8) == 0x0123456789ABCDEFULL);

  StoreBe16(buffer.data() + 4, 0x86DDU);  // ETHERTYPE_IPV6
  CHECK(LoadBe16(buffer.data() + 4) == 0x86DDU);

  CHECK(buffer[4] == std::byte{0x86});
  CHECK(buffer[5] == std::byte{0xDD});
}

TEST_CASE("common_byte_order scenario 4") {
  std::array<std::byte, 32> buffer{};
  for (std::size_t offset = 0; offset < 8; ++offset) {
    StoreLe32(buffer.data() + offset, 0xCAFEBABEU);
    CHECK(LoadLe32(buffer.data() + offset) == 0xCAFEBABEU);

    StoreLe64(buffer.data() + offset + 8, 0xFEEDFACEDEADBEEFULL);
    CHECK(LoadLe64(buffer.data() + offset + 8) == 0xFEEDFACEDEADBEEFULL);
  }
}

TEST_CASE("common_byte_order scenario 5") {
  std::array<std::byte, 1> buffer{};
  multitether::StoreLe<std::uint8_t>(buffer.data(), 0xA5U);
  CHECK(multitether::LoadLe<std::uint8_t>(buffer.data()) == 0xA5U);
}

TEST_CASE("common_byte_order scenario 6: AlignUp") {
  CHECK(AlignUp<std::uint32_t>(0, 4) == 0);
  CHECK(AlignUp<std::uint32_t>(1, 4) == 4);
  CHECK(AlignUp<std::uint32_t>(4, 4) == 4);
  CHECK(AlignUp<std::uint32_t>(5, 4) == 8);

  CHECK(AlignUp<std::uint32_t>(1514 + 44, 4) == 1560);
  CHECK(AlignUp<std::uint32_t>(63, 64) == 64);
}

TEST_CASE("common_byte_order scenario 7: IsPowerOfTwo") {
  CHECK_FALSE(IsPowerOfTwo<std::uint32_t>(0));
  CHECK(IsPowerOfTwo<std::uint32_t>(1));
  CHECK(IsPowerOfTwo<std::uint32_t>(2));
  CHECK_FALSE(IsPowerOfTwo<std::uint32_t>(3));
  CHECK(IsPowerOfTwo<std::uint32_t>(1024));
  CHECK_FALSE(IsPowerOfTwo<std::uint32_t>(1023));
}
}  // TEST_SUITE("common.byte_order")
