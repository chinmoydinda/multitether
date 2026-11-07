#include <atomic>
#include <cstdint>
#include <thread>

#include <doctest.h>

#include "multitether/common/cache.h"
#include "multitether/common/spsc_ring.h"

using multitether::kCacheLineSize;
using multitether::SpscRing;

namespace {
struct Payload {
  std::uint64_t sequence;
  std::uint32_t checksum;
};

constexpr std::uint32_t Checksum(std::uint64_t sequence) {
  return static_cast<std::uint32_t>((sequence * 2654435761ULL) >> 16U);
}
}  // namespace

TEST_SUITE("common.spsc_ring") {
TEST_CASE("common_spsc_ring scenario 1") {
  CHECK(SpscRing<std::uint32_t>(1).Capacity() == 1);
  CHECK(SpscRing<std::uint32_t>(3).Capacity() == 4);
  CHECK(SpscRing<std::uint32_t>(4).Capacity() == 4);
  CHECK(SpscRing<std::uint32_t>(1000).Capacity() == 1024);
}

TEST_CASE("common_spsc_ring scenario 2") {
  SpscRing<std::uint32_t> ring(4);
  std::uint32_t value = 0xFFFFFFFFU;
  CHECK_FALSE(ring.TryPop(value));
  CHECK(value == 0xFFFFFFFFU);
  CHECK(ring.SizeSnapshot() == 0);
}

TEST_CASE("common_spsc_ring scenario 3") {
  SpscRing<std::uint32_t> ring(4);
  for (std::uint32_t i = 0; i < 4; ++i) {
    CHECK(ring.TryPush(i));
  }
  CHECK(ring.SizeSnapshot() == 4);
  CHECK_FALSE(ring.TryPush(99));
}

TEST_CASE("common_spsc_ring scenario 4: FIFO") {
  SpscRing<Payload> ring(8);
  for (std::uint64_t i = 0; i < 8; ++i) {
    CHECK(ring.TryPush(Payload{.sequence = i, .checksum = Checksum(i)}));
  }
  for (std::uint64_t i = 0; i < 8; ++i) {
    Payload out{};
    REQUIRE(ring.TryPop(out));
    CHECK(out.sequence == i);
    CHECK(out.checksum == Checksum(i));
  }
  CHECK(ring.SizeSnapshot() == 0);
}

TEST_CASE("common_spsc_ring scenario 5") {
  SpscRing<std::uint64_t> ring(4);
  for (std::uint64_t i = 0; i < 1000; ++i) {
    REQUIRE(ring.TryPush(i));
    std::uint64_t out = 0;
    REQUIRE(ring.TryPop(out));
    CHECK(out == i);
  }
}

TEST_CASE("common_spsc_ring scenario 6") {
  SpscRing<std::uint32_t> ring(8);
  const auto& cursor = ring.Cursor();
  const auto base = reinterpret_cast<std::uintptr_t>(&cursor);

  CHECK(sizeof(multitether::SpscCursor) >= 4 * kCacheLineSize);
  CHECK(base % kCacheLineSize == 0);
}

TEST_CASE("common_spsc_ring scenario 7") {
  constexpr std::uint64_t kTotal = 200'000;
  SpscRing<Payload> ring(1024);

  std::atomic<std::uint64_t> produced{0};
  std::atomic<bool> corrupted{false};

  std::thread producer([&] {
    std::uint64_t sequence = 0;
    while (sequence < kTotal) {
      if (ring.TryPush(Payload{.sequence = sequence, .checksum = Checksum(sequence)})) {
        ++sequence;
        produced.store(sequence, std::memory_order_relaxed);
      } else {
        std::this_thread::yield();
      }
    }
  });

  std::uint64_t expected = 0;
  while (expected < kTotal) {
    Payload out{};
    if (ring.TryPop(out)) {
      if (out.sequence != expected || out.checksum != Checksum(out.sequence)) {
        corrupted.store(true, std::memory_order_relaxed);
        break;
      }
      ++expected;
    } else {
      std::this_thread::yield();
    }
  }

  producer.join();
  CHECK_FALSE(corrupted.load());
  CHECK(expected == kTotal);
  CHECK(ring.SizeSnapshot() == 0);
}
}  // TEST_SUITE("common.spsc_ring")
