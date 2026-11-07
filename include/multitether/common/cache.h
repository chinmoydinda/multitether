#pragma once

#include <cstddef>

namespace multitether {
inline constexpr std::size_t kCacheLineSize = 128;

#define MULTITETHER_CACHE_ALIGNED alignas(::multitether::kCacheLineSize)
}  // namespace multitether
