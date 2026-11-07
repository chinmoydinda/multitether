#include <atomic>
#include <cstddef>
#include <cstdint>
#include <span>
#include <thread>
#include <vector>

#include <doctest.h>

#include "multitether/common/cache.h"
#include "multitether/common/frame_ring.h"

using multitether::FrameRing;
using multitether::FrameView;
using multitether::kCacheLineSize;

namespace {
std::vector<std::byte> MakeFrame(std::uint32_t length, std::uint8_t tag) {
  std::vector<std::byte> frame(length);
  for (std::uint32_t i = 0; i < length; ++i) {
    frame[i] = std::byte{static_cast<unsigned char>((tag + i) & 0xFFU)};
  }
  return frame;
}

bool VerifyFrame(std::span<const std::byte> frame, std::uint8_t tag) {
  for (std::size_t i = 0; i < frame.size(); ++i) {
    if (frame[i] != std::byte{static_cast<unsigned char>((tag + i) & 0xFFU)}) {
      return false;
    }
  }
  return true;
}
}  // namespace

TEST_SUITE("common.frame_ring") {
TEST_CASE("common_frame_ring scenario 1") {
  FrameRing ring(100, 1514);
  CHECK(ring.Capacity() == 128);
  CHECK(ring.MaxFrameBytes() == 1514);

  CHECK(ring.StorageBytes() == 128 * 1664);
}

TEST_CASE("common_frame_ring scenario 2") {
  FrameRing ring(4, 1514);
  const FrameView view = ring.BeginRead();
  CHECK(view.Empty());
  CHECK(view.data == nullptr);
}

TEST_CASE("common_frame_ring scenario 3") {
  FrameRing ring(4, 1514);
  const std::vector<std::byte> frame = MakeFrame(1514, 0x42);
  REQUIRE(ring.TryPush(frame));

  const FrameView view = ring.BeginRead();
  REQUIRE(view.length == 1514);
  CHECK(VerifyFrame(view.Bytes(), 0x42));
  ring.CommitRead();

  CHECK(ring.BeginRead().Empty());
}

TEST_CASE("common_frame_ring scenario 4") {
  FrameRing ring(4, 128);
  const std::vector<std::byte> too_long = MakeFrame(129, 0x01);
  CHECK_FALSE(ring.TryPush(too_long));
  CHECK(ring.SizeSnapshot() == 0);

  CHECK(ring.TryPush(MakeFrame(128, 0x02)));
}

TEST_CASE("common_frame_ring scenario 5") {
  FrameRing ring(4, 64);
  for (std::uint8_t i = 0; i < 4; ++i) {
    REQUIRE(ring.TryPush(MakeFrame(64, i)));
  }
  CHECK(ring.SizeSnapshot() == 4);
  CHECK_FALSE(ring.TryPush(MakeFrame(64, 9)));
}

TEST_CASE("common_frame_ring scenario 6: BeginWrite/CommitWrite") {
  FrameRing ring(4, 1514);
  const std::span<std::byte> dst = ring.BeginWrite();
  REQUIRE(dst.size() == 1514);

  for (std::size_t i = 0; i < 100; ++i) {
    dst[i] = std::byte{static_cast<unsigned char>((0x77 + i) & 0xFFU)};
  }
  ring.CommitWrite(100);

  const FrameView view = ring.BeginRead();
  REQUIRE(view.length == 100);
  CHECK(VerifyFrame(view.Bytes(), 0x77));
  ring.CommitRead();
}

TEST_CASE("common_frame_ring scenario 7") {
  FrameRing ring(4, 2048);

  for (std::uint32_t round = 0; round < 500; ++round) {
    const auto length = static_cast<std::uint32_t>(14 + (round * 37) % 2000);
    const auto tag = static_cast<std::uint8_t>(round & 0xFFU);
    REQUIRE(ring.TryPush(MakeFrame(length, tag)));

    const FrameView view = ring.BeginRead();
    REQUIRE(view.length == length);
    CHECK(VerifyFrame(view.Bytes(), tag));
    ring.CommitRead();
  }
}

TEST_CASE("common_frame_ring scenario 8") {
  FrameRing ring(8, 1514);
  for (int i = 0; i < 8; ++i) {
    const std::span<std::byte> dst = ring.BeginWrite();
    REQUIRE_FALSE(dst.empty());
    CHECK(reinterpret_cast<std::uintptr_t>(dst.data()) % kCacheLineSize == 0);
    ring.CommitWrite(64);
  }
}

TEST_CASE("common_frame_ring scenario 9") {
  constexpr std::uint32_t kTotal = 200'000;
  FrameRing ring(512, 512);

  std::atomic<bool> corrupted{false};

  std::thread producer([&] {
    std::uint32_t sent = 0;
    while (sent < kTotal) {
      const std::span<std::byte> dst = ring.BeginWrite();
      if (dst.empty()) {
        std::this_thread::yield();
        continue;
      }

      const auto length = static_cast<std::uint32_t>(14 + (sent % 400));
      const auto tag = static_cast<std::uint8_t>(sent & 0xFFU);
      for (std::uint32_t i = 0; i < length; ++i) {
        dst[i] = std::byte{static_cast<unsigned char>((tag + i) & 0xFFU)};
      }
      ring.CommitWrite(length);
      ++sent;
    }
  });

  std::uint32_t received = 0;
  while (received < kTotal) {
    const FrameView view = ring.BeginRead();
    if (view.Empty()) {
      std::this_thread::yield();
      continue;
    }
    const auto expected_length = static_cast<std::uint32_t>(14 + (received % 400));
    const auto expected_tag = static_cast<std::uint8_t>(received & 0xFFU);
    if (view.length != expected_length || !VerifyFrame(view.Bytes(), expected_tag)) {
      corrupted.store(true, std::memory_order_relaxed);
      ring.CommitRead();
      break;
    }
    ring.CommitRead();
    ++received;
  }

  producer.join();
  CHECK_FALSE(corrupted.load());
  CHECK(received == kTotal);
  CHECK(ring.TotalEnqueued() == kTotal);
  CHECK(ring.TotalDequeued() == kTotal);
}
}  // TEST_SUITE("common.frame_ring")

TEST_SUITE("common.frame_ring_batch") {
TEST_CASE("common_frame_ring scenario 10") {
  FrameRing ring(16, 1514);
  {
    auto batch = ring.BeginBatchWrite();
    for (std::uint8_t i = 0; i < 5; ++i) {
      REQUIRE(batch.Push(MakeFrame(64, i)));
    }
    CHECK(batch.Staged() == 5);
    CHECK(batch.Published() == 0);

    CHECK(ring.SizeSnapshot() == 0);
    CHECK(ring.BeginRead().Empty());
  }
  CHECK(ring.SizeSnapshot() == 5);
}

TEST_CASE("common_frame_ring scenario 11: Publish") {
  FrameRing ring(16, 1514);
  auto batch = ring.BeginBatchWrite();
  REQUIRE(batch.Push(MakeFrame(64, 1)));
  REQUIRE(batch.Push(MakeFrame(64, 2)));
  batch.Publish();
  CHECK(batch.Published() == 2);
  CHECK(batch.Staged() == 0);
  CHECK(ring.SizeSnapshot() == 2);

  REQUIRE(batch.Push(MakeFrame(64, 3)));
  CHECK(batch.Staged() == 1);
  batch.Publish();
  CHECK(batch.Published() == 3);
  CHECK(ring.SizeSnapshot() == 3);
}

TEST_CASE("common_frame_ring scenario 12: Begin") {
  FrameRing ring(4, 64);
  {
    auto batch = ring.BeginBatchWrite();
    std::uint32_t accepted = 0;
    for (std::uint8_t i = 0; i < 10; ++i) {
      if (!batch.Push(MakeFrame(64, i))) {
        break;
      }
      ++accepted;
    }
    CHECK(accepted == 4);
    CHECK(batch.Staged() == 4);
  }
  CHECK(ring.SizeSnapshot() == 4);
}

TEST_CASE("common_frame_ring scenario 13") {
  FrameRing ring(8, 1514);
  for (std::uint8_t i = 0; i < 4; ++i) {
    REQUIRE(ring.TryPush(MakeFrame(100, static_cast<std::uint8_t>(0x20 + i))));
  }

  std::vector<FrameView> views;
  {
    auto batch = ring.BeginBatchRead();
    for (int i = 0; i < 4; ++i) {
      const FrameView view = batch.Next();
      REQUIRE_FALSE(view.Empty());
      views.push_back(view);
    }
    CHECK(batch.Next().Empty());
    CHECK(batch.Staged() == 4);

    for (std::size_t i = 0; i < views.size(); ++i) {
      CHECK(views[i].length == 100);
      CHECK(VerifyFrame(views[i].Bytes(), static_cast<std::uint8_t>(0x20 + i)));
    }

    CHECK(ring.SizeSnapshot() == 4);
  }
  CHECK(ring.SizeSnapshot() == 0);
}

TEST_CASE("common_frame_ring scenario 14: Release") {
  FrameRing ring(8, 1514);
  for (std::uint8_t i = 0; i < 6; ++i) {
    REQUIRE(ring.TryPush(MakeFrame(64, i)));
  }
  auto batch = ring.BeginBatchRead();
  for (int i = 0; i < 3; ++i) {
    REQUIRE_FALSE(batch.Next().Empty());
  }
  batch.Release();
  CHECK(batch.Released() == 3);
  CHECK(ring.SizeSnapshot() == 3);
  for (int i = 0; i < 3; ++i) {
    REQUIRE_FALSE(batch.Next().Empty());
  }
  batch.Release();
  CHECK(batch.Released() == 6);
  CHECK(ring.SizeSnapshot() == 0);
}

TEST_CASE("common_frame_ring scenario 15: FIFO") {
  FrameRing ring(16, 1514);

  REQUIRE(ring.TryPush(MakeFrame(64, 0)));
  REQUIRE(ring.TryPush(MakeFrame(64, 1)));
  {
    auto batch = ring.BeginBatchWrite();
    for (std::uint8_t i = 2; i < 5; ++i) {
      REQUIRE(batch.Push(MakeFrame(64, i)));
    }
  }
  auto batch = ring.BeginBatchRead();
  for (std::uint8_t i = 0; i < 5; ++i) {
    const FrameView view = batch.Next();
    REQUIRE_FALSE(view.Empty());
    CHECK(VerifyFrame(view.Bytes(), i));
  }
  CHECK(batch.Next().Empty());
}

TEST_CASE("common_frame_ring scenario 16") {
  constexpr std::uint32_t kTotal = 200'000;
  constexpr std::uint32_t kBatch = 32;
  FrameRing ring(1024, 512);
  std::atomic<bool> corrupted{false};

  std::thread producer([&] {
    std::uint32_t sent = 0;
    while (sent < kTotal) {
      auto batch = ring.BeginBatchWrite();
      for (std::uint32_t i = 0; i < kBatch && sent < kTotal; ++i) {
        const std::span<std::byte> dst = batch.Begin();
        if (dst.empty()) {
          break;
        }
        const auto length = static_cast<std::uint32_t>(14 + (sent % 400));
        const auto tag = static_cast<std::uint8_t>(sent & 0xFFU);
        for (std::uint32_t k = 0; k < length; ++k) {
          dst[k] = std::byte{static_cast<unsigned char>((tag + k) & 0xFFU)};
        }
        batch.Commit(length);
        ++sent;
      }
    }
  });

  std::uint32_t received = 0;
  while (received < kTotal) {
    auto batch = ring.BeginBatchRead();
    for (std::uint32_t i = 0; i < kBatch && received < kTotal; ++i) {
      const FrameView view = batch.Next();
      if (view.Empty()) {
        break;
      }
      const auto expected_length = static_cast<std::uint32_t>(14 + (received % 400));
      const auto expected_tag = static_cast<std::uint8_t>(received & 0xFFU);
      if (view.length != expected_length || !VerifyFrame(view.Bytes(), expected_tag)) {
        corrupted.store(true, std::memory_order_relaxed);
        received = kTotal;
        break;
      }
      ++received;
    }
  }

  producer.join();
  CHECK_FALSE(corrupted.load());
  CHECK(ring.TotalDequeued() == ring.TotalEnqueued());
}
}  // TEST_SUITE("common.frame_ring_batch")
