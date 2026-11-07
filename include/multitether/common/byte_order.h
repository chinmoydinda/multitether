#pragma once

#include <bit>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace multitether {
template <typename T>
concept WireUnsigned = std::unsigned_integral<T> && !std::same_as<T, bool> &&
                       (sizeof(T) == 1 || sizeof(T) == 2 || sizeof(T) == 4 || sizeof(T) == 8);

template <WireUnsigned T>
[[nodiscard]] inline T LoadLe(const std::byte* src) noexcept {
  T value{};
  std::memcpy(&value, src, sizeof(T));
  if constexpr (sizeof(T) > 1 && std::endian::native == std::endian::big) {
    value = std::byteswap(value);
  }
  return value;
}

template <WireUnsigned T>
inline void StoreLe(std::byte* dst, T value) noexcept {
  if constexpr (sizeof(T) > 1 && std::endian::native == std::endian::big) {
    value = std::byteswap(value);
  }
  std::memcpy(dst, &value, sizeof(T));
}

template <WireUnsigned T>
[[nodiscard]] inline T LoadBe(const std::byte* src) noexcept {
  T value{};
  std::memcpy(&value, src, sizeof(T));
  if constexpr (sizeof(T) > 1 && std::endian::native == std::endian::little) {
    value = std::byteswap(value);
  }
  return value;
}

template <WireUnsigned T>
inline void StoreBe(std::byte* dst, T value) noexcept {
  if constexpr (sizeof(T) > 1 && std::endian::native == std::endian::little) {
    value = std::byteswap(value);
  }
  std::memcpy(dst, &value, sizeof(T));
}

[[nodiscard]] inline std::uint16_t LoadLe16(const std::byte* p) noexcept {
  return LoadLe<std::uint16_t>(p);
}

[[nodiscard]] inline std::uint32_t LoadLe32(const std::byte* p) noexcept {
  return LoadLe<std::uint32_t>(p);
}

[[nodiscard]] inline std::uint64_t LoadLe64(const std::byte* p) noexcept {
  return LoadLe<std::uint64_t>(p);
}

inline void StoreLe16(std::byte* p, std::uint16_t v) noexcept {
  StoreLe<std::uint16_t>(p, v);
}

inline void StoreLe32(std::byte* p, std::uint32_t v) noexcept {
  StoreLe<std::uint32_t>(p, v);
}

inline void StoreLe64(std::byte* p, std::uint64_t v) noexcept {
  StoreLe<std::uint64_t>(p, v);
}

[[nodiscard]] inline std::uint16_t LoadBe16(const std::byte* p) noexcept {
  return LoadBe<std::uint16_t>(p);
}

inline void StoreBe16(std::byte* p, std::uint16_t v) noexcept {
  StoreBe<std::uint16_t>(p, v);
}

template <std::unsigned_integral T>
[[nodiscard]] constexpr T AlignUp(T value, T alignment) noexcept {
  return (value + alignment - 1) & ~(alignment - 1);
}

template <std::unsigned_integral T>
[[nodiscard]] constexpr bool IsPowerOfTwo(T value) noexcept {
  return value != 0 && (value & (value - 1)) == 0;
}
}  // namespace multitether
