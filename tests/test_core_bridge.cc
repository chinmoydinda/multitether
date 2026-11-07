#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <thread>
#include <vector>

#include <doctest.h>

#include "loopback_link.h"
#include "mock_data_channel.h"
#include "multitether/core/bridge.h"

using namespace multitether;        // NOLINT(google-build-using-namespace)
using namespace multitether::core;  // NOLINT(google-build-using-namespace)
using multitether::net::LoopbackConfig;
using multitether::net::LoopbackLink;
using multitether::testing::MockDataChannel;

namespace {
std::vector<std::byte> MakeFrame(std::uint32_t length, std::uint8_t tag) {
  std::vector<std::byte> frame(length);
  for (int i = 0; i < 6; ++i) {
    frame[static_cast<std::size_t>(i)] = std::byte{0xFF};
  }
  frame[6] = std::byte{0x02};
  frame[11] = std::byte{tag};
  frame[12] = std::byte{0x08};
  frame[13] = std::byte{0x00};
  for (std::uint32_t i = 14; i < length; ++i) {
    frame[i] = std::byte{static_cast<unsigned char>((tag + i) & 0xFFU)};
  }
  return frame;
}

std::vector<std::vector<std::byte>> MakeFrames(std::uint32_t count, std::uint32_t length) {
  std::vector<std::vector<std::byte>> frames;
  frames.reserve(count);
  for (std::uint32_t i = 0; i < count; ++i) {
    frames.push_back(MakeFrame(length, static_cast<std::uint8_t>(i & 0xFFU)));
  }
  return frames;
}

template <typename Predicate>
bool WaitFor(Predicate predicate, std::chrono::milliseconds timeout = std::chrono::seconds(5)) {
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    if (predicate()) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  return predicate();
}

[[nodiscard]] BridgeConfig TestConfig() {
  BridgeConfig config;
  config.rx_ring_frames = 256;
  config.max_frame_bytes = 1518;
  config.rx_write_batch = 16;
  config.rx_spin_before_yield = 4;
  config.tx_submit_batch = 32;
  return config;
}
}  // namespace

TEST_SUITE("core.bridge") {
TEST_CASE("core_bridge scenario 1") {
  MockDataChannel channel;
  LoopbackLink link;
  Bridge bridge(channel, link, TestConfig());

  CHECK_FALSE(bridge.Running());
  REQUIRE(bridge.Start().has_value());
  CHECK(bridge.Running());
  CHECK(channel.Receiving());

  CHECK_FALSE(bridge.Start().has_value());

  bridge.Stop();
  CHECK_FALSE(bridge.Running());
  CHECK(channel.ShutdownCalled());

  bridge.Stop();
}

TEST_CASE("core_bridge scenario 2: RX") {
  MockDataChannel channel;
  LoopbackLink link(LoopbackConfig{.sent_capacity = 4096});
  Bridge bridge(channel, link, TestConfig());
  REQUIRE(bridge.Start().has_value());

  constexpr std::uint32_t kCount = 100;
  const auto frames = MakeFrames(kCount, 1514);
  CHECK(channel.InjectFromDevice(frames) == kCount);

  REQUIRE(WaitFor([&] { return link.TotalSentFrames() >= kCount; }));

  const auto received = link.DrainSent();
  REQUIRE(received.size() == kCount);

  for (std::size_t i = 0; i < kCount; ++i) {
    CHECK(received[i] == frames[i]);
  }

  const BridgeStats stats = bridge.Snapshot();
  CHECK(stats.rx.frames == kCount);
  CHECK(stats.rx.bytes == static_cast<std::uint64_t>(kCount) * 1514);

  bridge.Stop();
}

TEST_CASE("core_bridge scenario 3: TX") {
  MockDataChannel channel;
  LoopbackLink link(LoopbackConfig{.inbound_capacity = 4096, .max_frames_per_batch = 64});
  Bridge bridge(channel, link, TestConfig());
  REQUIRE(bridge.Start().has_value());

  constexpr std::uint32_t kCount = 100;
  const auto frames = MakeFrames(kCount, 512);
  for (const std::vector<std::byte>& frame : frames) {
    REQUIRE(link.PushInbound(frame));
  }

  REQUIRE(WaitFor([&] { return channel.SentFrameCount() >= kCount; }));

  const auto delivered = channel.DrainSentToDevice();
  REQUIRE(delivered.size() == kCount);
  for (std::size_t i = 0; i < kCount; ++i) {
    CHECK(delivered[i] == frames[i]);
  }

  const BridgeStats stats = bridge.Snapshot();
  CHECK(stats.tx.frames == kCount);

  bridge.Stop();
}

TEST_CASE("core_bridge scenario 4") {
  MockDataChannel channel;
  LoopbackLink link(LoopbackConfig{.inbound_capacity = 8192, .sent_capacity = 8192,
                                   .max_frames_per_batch = 64});
  Bridge bridge(channel, link, TestConfig());
  REQUIRE(bridge.Start().has_value());

  constexpr std::uint32_t kCount = 500;
  const auto tx_frames = MakeFrames(kCount, 700);

  std::thread pusher([&] {
    for (const std::vector<std::byte>& frame : tx_frames) {
      while (!link.PushInbound(frame)) {
        std::this_thread::yield();
      }
    }
  });

  std::uint32_t injected = 0;
  while (injected < kCount) {
    const auto chunk = MakeFrames(std::min<std::uint32_t>(32, kCount - injected), 1000);
    const std::uint32_t accepted = channel.InjectFromDevice(chunk);
    injected += accepted;
    if (accepted == 0) {
      std::this_thread::yield();
    }
  }
  pusher.join();

  REQUIRE(WaitFor([&] { return channel.SentFrameCount() >= kCount; }));
  REQUIRE(WaitFor([&] { return link.TotalSentFrames() >= kCount; }));

  const BridgeStats stats = bridge.Snapshot();
  CHECK(stats.rx.frames >= kCount);
  CHECK(stats.tx.frames >= kCount);

  bridge.Stop();
}

TEST_CASE("core_bridge scenario 5: RX") {
  MockDataChannel channel;

  LoopbackLink link(LoopbackConfig{.sent_capacity = 8});
  auto config = TestConfig();
  config.rx_ring_frames = 16;
  Bridge bridge(channel, link, config);
  REQUIRE(bridge.Start().has_value());

  std::uint32_t total_accepted = 0;
  for (int round = 0; round < 20; ++round) {
    total_accepted += channel.InjectFromDevice(MakeFrames(64, 1514));
  }

  const BridgeStats stats = bridge.Snapshot();
  CHECK(total_accepted < 20 * 64);
  CHECK(stats.rx.TotalDropped() > 0);

  bridge.Stop();
}

TEST_CASE("core_bridge scenario 6: TX") {
  MockDataChannel channel;
  LoopbackLink link(LoopbackConfig{.inbound_capacity = 4096, .max_frames_per_batch = 64});
  Bridge bridge(channel, link, TestConfig());

  channel.SetAcceptLimit(0);
  REQUIRE(bridge.Start().has_value());

  constexpr std::uint32_t kFrameCount = 200;
  for (const std::vector<std::byte>& frame : MakeFrames(kFrameCount, 512)) {
    REQUIRE(link.PushInbound(frame));
  }

  REQUIRE(WaitFor([&] { return bridge.Snapshot().tx_backpressure_events > 0; }));
  REQUIRE(WaitFor([&] { return channel.WaitCalls() > 0; }));
  CHECK(bridge.Snapshot().tx.TotalDropped() == 0);
  CHECK(channel.SentFrameCount() == 0);

  channel.SetAcceptLimit(0xFFFF'FFFFU);
  REQUIRE(WaitFor([&] {
    return channel.SentFrameCount() >= kFrameCount &&
           bridge.Snapshot().tx.frames >= kFrameCount;
  }));

  const BridgeStats stats = bridge.Snapshot();
  CHECK(stats.tx.TotalDropped() == 0);
  CHECK(stats.tx.frames == kFrameCount);
  CHECK(channel.SentFrameCount() == kFrameCount);

  const auto delivered = channel.DrainSentToDevice();
  REQUIRE(delivered.size() == kFrameCount);
  const auto expected = MakeFrames(kFrameCount, 512);
  for (std::size_t i = 0; i < delivered.size(); ++i) {
    CHECK(delivered[i] == expected[i]);
  }

  bridge.Stop();
}

TEST_CASE("core_bridge scenario 7: TX") {
  MockDataChannel channel;
  LoopbackLink link(LoopbackConfig{.inbound_capacity = 4096, .max_frames_per_batch = 64});
  Bridge bridge(channel, link, TestConfig());

  channel.SetAcceptLimit(7);
  REQUIRE(bridge.Start().has_value());

  constexpr std::uint32_t kFrameCount = 100;
  for (const std::vector<std::byte>& frame : MakeFrames(kFrameCount, 512)) {
    REQUIRE(link.PushInbound(frame));
  }

  REQUIRE(WaitFor([&] { return channel.SentFrameCount() >= kFrameCount; }));
  CHECK(bridge.Snapshot().tx.TotalDropped() == 0);

  const auto delivered = channel.DrainSentToDevice();
  REQUIRE(delivered.size() == kFrameCount);
  const auto expected = MakeFrames(kFrameCount, 512);
  for (std::size_t i = 0; i < delivered.size(); ++i) {
    CHECK(delivered[i] == expected[i]);
  }

  bridge.Stop();
}

TEST_CASE("core_bridge scenario 8: TX Stop") {
  MockDataChannel channel;
  LoopbackLink link(LoopbackConfig{.inbound_capacity = 4096, .max_frames_per_batch = 64});
  Bridge bridge(channel, link, TestConfig());

  channel.SetAcceptLimit(0);
  REQUIRE(bridge.Start().has_value());

  for (const std::vector<std::byte>& frame : MakeFrames(200, 512)) {
    REQUIRE(link.PushInbound(frame));
  }
  REQUIRE(WaitFor([&] { return bridge.Snapshot().tx_backpressure_events > 0; }));

  const auto stop_begin = std::chrono::steady_clock::now();
  bridge.Stop();
  const auto stop_elapsed = std::chrono::steady_clock::now() - stop_begin;

  CHECK_FALSE(bridge.Running());

  CHECK(stop_elapsed < std::chrono::seconds(2));

  CHECK(bridge.Snapshot().tx.TotalDropped() > 0);
}

TEST_CASE("core_bridge scenario 9: TX") {
  MockDataChannel channel;
  LoopbackLink link(LoopbackConfig{.inbound_capacity = 4096, .max_frames_per_batch = 64});
  Bridge bridge(channel, link, TestConfig());

  channel.SetAcceptLimit(0);
  REQUIRE(bridge.Start().has_value());

  for (const std::vector<std::byte>& frame : MakeFrames(100, 512)) {
    REQUIRE(link.PushInbound(frame));
  }
  REQUIRE(WaitFor([&] { return bridge.Snapshot().tx_backpressure_events > 0; }));
  CHECK(bridge.Snapshot().tx.TotalDropped() == 0);

  bridge.SetPaused(true);
  REQUIRE(WaitFor([&] { return bridge.Snapshot().tx.TotalDropped() > 0; }));
  CHECK(channel.SentFrameCount() == 0);

  bridge.SetPaused(false);
  channel.SetAcceptLimit(0xFFFF'FFFFU);
  for (const std::vector<std::byte>& frame : MakeFrames(20, 512)) {
    REQUIRE(link.PushInbound(frame));
  }
  REQUIRE(WaitFor([&] { return channel.SentFrameCount() >= 20; }));

  bridge.Stop();
}

TEST_CASE("core_bridge scenario 10: RX") {
  MockDataChannel channel;
  LoopbackLink link(LoopbackConfig{.sent_capacity = 4096});
  Bridge bridge(channel, link, TestConfig());
  REQUIRE(bridge.Start().has_value());

  bridge.SetPaused(true);
  CHECK(bridge.Paused());

  constexpr std::uint32_t kCount = 32;
  CHECK(channel.InjectFromDevice(MakeFrames(kCount, 256)) == kCount);

  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  CHECK(link.TotalSentFrames() == 0);

  CHECK(bridge.Snapshot().rx_queue_depth == kCount);

  bridge.SetPaused(false);
  REQUIRE(WaitFor([&] { return link.TotalSentFrames() >= kCount; }));
  CHECK(bridge.Snapshot().rx.TotalDropped() == 0);

  bridge.Stop();
}

TEST_CASE("core_bridge scenario 11: SetPaused") {
  MockDataChannel channel;
  LoopbackLink link(LoopbackConfig{.sent_capacity = 4096});
  Bridge bridge(channel, link, TestConfig());

  bridge.SetPaused(true);
  CHECK(bridge.Paused());
  bridge.SetPaused(false);

  REQUIRE(bridge.Start().has_value());

  for (int i = 0; i < 50; ++i) {
    bridge.SetPaused(true);
    bridge.SetPaused(false);
  }

  bridge.SetPaused(true);
  constexpr std::uint32_t kCount = 16;
  CHECK(channel.InjectFromDevice(MakeFrames(kCount, 256)) == kCount);
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  CHECK(link.TotalSentFrames() == 0);

  bridge.Stop();

  bridge.SetPaused(true);
  CHECK(bridge.Paused());
}

TEST_CASE("core_bridge scenario 12: TX") {
  MockDataChannel channel;
  LoopbackLink link(LoopbackConfig{.inbound_capacity = 512, .max_frames_per_batch = 64});
  Bridge bridge(channel, link, TestConfig());
  REQUIRE(bridge.Start().has_value());
  bridge.SetPaused(true);

  for (const std::vector<std::byte>& frame : MakeFrames(50, 256)) {
    REQUIRE(link.PushInbound(frame));
  }

  REQUIRE(WaitFor([&] { return bridge.Snapshot().tx.TotalDropped() > 0; }));
  CHECK(channel.SentFrameCount() == 0);

  bridge.Stop();
}

TEST_CASE("core_bridge scenario 13") {
  MockDataChannel channel;
  LoopbackLink link(LoopbackConfig{.sent_capacity = 4096});
  link.FailWritesAfter(1);
  Bridge bridge(channel, link, TestConfig());
  REQUIRE(bridge.Start().has_value());

  for (int round = 0; round < 5; ++round) {
    (void)channel.InjectFromDevice(MakeFrames(16, 256));
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }

  CHECK(bridge.Running());
  CHECK(bridge.Snapshot().rx.io_errors > 0);

  bridge.Stop();
}

TEST_CASE("core_bridge scenario 14: USB I/O") {
  MockDataChannel channel;
  LoopbackLink link(LoopbackConfig{.inbound_capacity = 512, .max_frames_per_batch = 64});
  Bridge bridge(channel, link, TestConfig());
  channel.SetSendShouldFail(true);
  REQUIRE(bridge.Start().has_value());

  for (const std::vector<std::byte>& frame : MakeFrames(50, 256)) {
    REQUIRE(link.PushInbound(frame));
  }

  REQUIRE(WaitFor([&] { return bridge.Snapshot().tx.io_errors > 0; }));
  CHECK(bridge.Running());

  bridge.Stop();
}

TEST_CASE("core_bridge scenario 15") {
  MockDataChannel channel;
  LoopbackLink link(LoopbackConfig{.sent_capacity = 4096});
  Bridge bridge(channel, link, TestConfig());
  REQUIRE(bridge.Start().has_value());

  const BridgeStats before = bridge.Snapshot();
  constexpr std::uint32_t kCount = 50;
  CHECK(channel.InjectFromDevice(MakeFrames(kCount, 1514)) == kCount);
  REQUIRE(WaitFor([&] { return link.TotalSentFrames() >= kCount; }));
  const BridgeStats after = bridge.Snapshot();

  const std::string line = FormatStatsLine(before, after, 1.0);
  CHECK(line.find("RX") != std::string::npos);
  CHECK(line.find("TX") != std::string::npos);
  CHECK(line.find("pps") != std::string::npos);
  CHECK(line.find("Mbps") != std::string::npos);
  CHECK(line.find("dropped") != std::string::npos);

  bridge.Stop();
}

TEST_CASE("core_bridge scenario 16") {
  MockDataChannel channel;
  LoopbackLink link(LoopbackConfig{.inbound_capacity = 8192, .sent_capacity = 8192,
                                   .max_frames_per_batch = 64});
  Bridge bridge(channel, link, TestConfig());
  REQUIRE(bridge.Start().has_value());

  std::atomic<bool> keep_going{true};
  std::thread producer([&] {
    while (keep_going.load(std::memory_order_acquire)) {
      (void)channel.InjectFromDevice(MakeFrames(8, 512));
      for (const std::vector<std::byte>& frame : MakeFrames(8, 512)) {
        (void)link.PushInbound(frame);
      }
      std::this_thread::yield();
    }
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  bridge.Stop();
  keep_going.store(false, std::memory_order_release);
  producer.join();

  CHECK_FALSE(bridge.Running());
  CHECK(channel.ShutdownCalled());
}
}  // TEST_SUITE("core.bridge")
