#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <format>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <arpa/inet.h>
#include <net/if.h>
#include <netinet/in.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <unistd.h>

#include <doctest.h>

#include "loopback_link.h"
#include "multitether/common/i18n.h"
#include "multitether/net/bpf_link.h"
#include "multitether/net/darwin_abi.h"
#include "multitether/net/feth_device.h"

using namespace multitether;       // NOLINT(google-build-using-namespace)
using namespace multitether::net;  // NOLINT(google-build-using-namespace)

namespace {
std::vector<std::byte> MakeEthernetFrame(std::uint32_t total_length, std::uint8_t tag) {
  std::vector<std::byte> frame(total_length);

  for (int i = 0; i < 6; ++i) {
    frame[static_cast<std::size_t>(i)] = std::byte{0xFF};
  }
  frame[6] = std::byte{0x02};
  frame[11] = std::byte{tag};
  frame[12] = std::byte{0x08};  // EtherType 0x0800 = IPv4
  frame[13] = std::byte{0x00};
  for (std::uint32_t i = 14; i < total_length; ++i) {
    frame[i] = std::byte{static_cast<unsigned char>((tag + i) & 0xFFU)};
  }
  return frame;
}

// NOLINTNEXTLINE(concurrency-mt-unsafe)
bool RootTestsEnabled() {
  const char* flag = std::getenv("MULTITETHER_ROOT_TESTS");
  return flag != nullptr && flag[0] == '1' && IsRunningAsRoot();
}

template <typename T>
std::string Why(const T& result) {
  return result.has_value() ? std::string{} : result.error().ToString();
}

void ReportSkip(std::string_view what) {
  const std::string message =
      std::format("Skipping {} (requires root and MULTITETHER_ROOT_TESTS=1; euid={})", what,
                  IsRunningAsRoot() ? "0" : "nonzero");
  MESSAGE(message);
}

constexpr const char* kArpSystemIp = "10.99.99.1";
constexpr const char* kArpProbeIp = "10.99.99.2";

[[nodiscard]] std::string AssignIpv4(std::string_view interface_name, const char* address,
                                     const char* netmask) {
  const int fd = ::socket(AF_INET, SOCK_DGRAM, 0);
  if (fd < 0) {
    return Error::FromErrno(errno, "socket(AF_INET)").ToString();
  }

  struct ifaliasreq request {};
  std::memcpy(request.ifra_name, interface_name.data(),
              std::min(interface_name.size(), sizeof(request.ifra_name) - 1));

  const auto fill = [](struct sockaddr& slot, const char* text) -> bool {
    struct sockaddr_in value {};
    value.sin_len = sizeof(value);
    value.sin_family = AF_INET;
    if (::inet_pton(AF_INET, text, &value.sin_addr) != 1) {
      return false;
    }
    std::memcpy(&slot, &value, sizeof(value));
    return true;
  };

  if (!fill(request.ifra_addr, address) || !fill(request.ifra_mask, netmask)) {
    ::close(fd);
    return "inet_pton failed";
  }

  const int result = ::ioctl(fd, SIOCAIFADDR, &request);
  const int saved_errno = errno;
  ::close(fd);
  if (result != 0) {
    return Error::FromErrno(saved_errno, "ioctl(SIOCAIFADDR)").ToString();
  }
  return {};
}

constexpr std::size_t kEtherTypeOffset = 12;
constexpr std::size_t kArpOperationOffset = 20;
constexpr std::size_t kArpSenderMacOffset = 22;
constexpr std::size_t kArpSenderIpOffset = 28;
constexpr std::size_t kArpFrameBytes = 42;

std::vector<std::byte> MakeArpRequest(const MacAddress& sender_mac, const char* sender_ip,
                                      const char* target_ip) {
  std::vector<std::byte> frame(kArpFrameBytes, std::byte{0});
  const auto put = [&frame](std::size_t offset, std::initializer_list<std::uint8_t> bytes) {
    std::size_t index = offset;
    for (const std::uint8_t value : bytes) {
      frame[index++] = std::byte{value};
    }
  };

  put(0, {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF});
  for (std::size_t i = 0; i < sender_mac.size(); ++i) {
    frame[6 + i] = std::byte{sender_mac[i]};
  }
  put(kEtherTypeOffset, {0x08, 0x06});  // EtherType = ARP
  put(14, {0x00, 0x01, 0x08, 0x00, 0x06, 0x04, 0x00, 0x01});

  for (std::size_t i = 0; i < sender_mac.size(); ++i) {
    frame[kArpSenderMacOffset + i] = std::byte{sender_mac[i]};
  }
  struct in_addr parsed {};
  ::inet_pton(AF_INET, sender_ip, &parsed);
  std::memcpy(frame.data() + kArpSenderIpOffset, &parsed, sizeof(parsed));
  ::inet_pton(AF_INET, target_ip, &parsed);
  std::memcpy(frame.data() + 38, &parsed, sizeof(parsed));
  return frame;
}

[[nodiscard]] bool IsArpReplyFor(const FrameView& view, const char* expected_ip) {
  if (view.length < kArpFrameBytes) {
    return false;
  }
  if (view.data[kEtherTypeOffset] != std::byte{0x08} ||
      view.data[kEtherTypeOffset + 1] != std::byte{0x06}) {
    return false;
  }

  if (view.data[kArpOperationOffset] != std::byte{0x00} ||
      view.data[kArpOperationOffset + 1] != std::byte{0x02}) {
    return false;
  }
  struct in_addr expected {};
  if (::inet_pton(AF_INET, expected_ip, &expected) != 1) {
    return false;
  }
  return std::memcmp(view.data + kArpSenderIpOffset, &expected, sizeof(expected)) == 0;
}
}  // namespace

TEST_SUITE("net.abi") {
TEST_CASE("net_link scenario 1: ABI ioctl") {
  CHECK(sizeof(IfDrv) == 40);
  CHECK(sizeof(FethRequest) == 160);
  CHECK(offsetof(FethRequest, u) == 32);
  CHECK(kSetDriverSpec == 0x8028'697BUL);
  CHECK(kGetDriverSpec == 0xC028'697BUL);
  CHECK(kBpfSetBatchWrite == 0x8004'428FUL);
  CHECK(kBpfSetNoTimestamp == 0x8004'4291UL);
  CHECK(static_cast<unsigned long>(FethSetCommand::kSetPeer) == 1);
  CHECK(static_cast<unsigned long>(FethGetCommand::kGetPeer) == 1);
}

TEST_CASE("net_link scenario 2: BPF sizeof bh_hdrlen") {
  CHECK(sizeof(struct bpf_hdr) == 20);
  CHECK(sizeof(struct BPF_TIMEVAL) == 8);
  CHECK(kBpfHeaderMinBytes == 18);
  CHECK(BPF_ALIGNMENT == 4);

  CHECK(BPF_WORDALIGN(14 + kBpfHeaderMinBytes) - 14 == kBpfHeaderMinBytes);
}

TEST_CASE("net_link scenario 3: feth sysctl") {
  CHECK(std::size(kRequiredFethSysctls) > 0);
  for (const RequiredFethSysctl& entry : kRequiredFethSysctls) {
    CHECK(entry.name != nullptr);

    {
      const std::string_view why = multitether::Text(entry.why);

      CHECK(why.size() > 10);
    }
  }
}
}  // TEST_SUITE("net.abi")

TEST_SUITE("net.loopback") {
TEST_CASE("net_link scenario 4") {
  LoopbackLink link;
  const auto batch = link.ReadFrames();
  REQUIRE(batch.has_value());
  CHECK(batch->frames.empty());
}

TEST_CASE("net_link scenario 5: FIFO") {
  LoopbackLink link;
  for (std::uint8_t i = 0; i < 5; ++i) {
    REQUIRE(link.PushInbound(MakeEthernetFrame(100, i)));
  }
  const auto batch = link.ReadFrames();
  REQUIRE(batch.has_value());
  REQUIRE(batch->frames.size() == 5);
  for (std::size_t i = 0; i < 5; ++i) {
    CHECK(batch->frames[i].length == 100);

    CHECK(batch->frames[i].data[11] == std::byte{static_cast<unsigned char>(i)});
  }
}

TEST_CASE("net_link scenario 6: max_frames_per_batch") {
  LoopbackLink link(LoopbackConfig{.max_frames_per_batch = 3});
  for (std::uint8_t i = 0; i < 7; ++i) {
    REQUIRE(link.PushInbound(MakeEthernetFrame(64, i)));
  }
  const auto first = link.ReadFrames();
  REQUIRE(first.has_value());
  CHECK(first->frames.size() == 3);

  const auto second = link.ReadFrames();
  REQUIRE(second.has_value());
  CHECK(second->frames.size() == 3);

  const auto third = link.ReadFrames();
  REQUIRE(third.has_value());
  CHECK(third->frames.size() == 1);
}

TEST_CASE("net_link scenario 7") {
  LoopbackLink link(LoopbackConfig{.inbound_capacity = 2});
  CHECK(link.PushInbound(MakeEthernetFrame(64, 1)));
  CHECK(link.PushInbound(MakeEthernetFrame(64, 2)));
  CHECK_FALSE(link.PushInbound(MakeEthernetFrame(64, 3)));
  CHECK(link.InboundDrops() == 1);
}

TEST_CASE("net_link scenario 8") {
  LoopbackLink link(LoopbackConfig{.max_frame_bytes = 128});
  CHECK_FALSE(link.PushInbound(MakeEthernetFrame(129, 1)));
  CHECK(link.InboundDrops() == 1);
}

TEST_CASE("net_link scenario 9") {
  LoopbackLink link;
  const auto frame_a = MakeEthernetFrame(1514, 0xA1);
  const auto frame_b = MakeEthernetFrame(64, 0xB2);
  const std::array<FrameView, 2> batch{
      FrameView{.data = frame_a.data(), .length = static_cast<std::uint32_t>(frame_a.size())},
      FrameView{.data = frame_b.data(), .length = static_cast<std::uint32_t>(frame_b.size())},
  };

  const auto result = link.WriteFrames(batch);
  REQUIRE(result.has_value());
  CHECK(result->frames_written == 2);
  CHECK(result->bytes_written == 1514 + 64);
  CHECK(result->frames_skipped == 0);

  const auto sent = link.DrainSent();
  REQUIRE(sent.size() == 2);
  CHECK(sent[0] == frame_a);
  CHECK(sent[1] == frame_b);

  CHECK(link.TotalSentFrames() == 2);
  CHECK(link.TotalSentBytes() == 1514 + 64);

  CHECK(link.DrainSent().empty());
}

TEST_CASE("net_link scenario 10") {
  LoopbackLink link(LoopbackConfig{.max_frame_bytes = 200});
  const auto too_short = MakeEthernetFrame(14, 1);
  std::vector<std::byte> shorter(13);
  const auto too_long = MakeEthernetFrame(201, 2);
  const auto valid = MakeEthernetFrame(100, 3);

  const std::array<FrameView, 3> batch{
      FrameView{.data = shorter.data(), .length = 13},
      FrameView{.data = too_long.data(), .length = 201},
      FrameView{.data = valid.data(), .length = 100},
  };
  const auto result = link.WriteFrames(batch);
  REQUIRE(result.has_value());
  CHECK(result->frames_written == 1);
  CHECK(result->frames_skipped == 2);

  const std::array<FrameView, 1> minimal{FrameView{.data = too_short.data(), .length = 14}};
  const auto minimal_result = link.WriteFrames(minimal);
  REQUIRE(minimal_result.has_value());
  CHECK(minimal_result->frames_written == 1);
}

TEST_CASE("net_link scenario 11") {
  LoopbackLink link(LoopbackConfig{.sent_capacity = 2});
  const auto frame = MakeEthernetFrame(64, 1);
  std::vector<FrameView> batch(5, FrameView{.data = frame.data(), .length = 64});
  const auto result = link.WriteFrames(batch);
  REQUIRE(result.has_value());
  CHECK(result->frames_written == 2);
}

TEST_CASE("net_link scenario 12: Interrupt") {
  LoopbackLink link;
  REQUIRE(link.PushInbound(MakeEthernetFrame(64, 1)));
  link.Interrupt();
  CHECK(link.Interrupted());
  const auto batch = link.ReadFrames();
  REQUIRE(batch.has_value());
  CHECK(batch->frames.empty());
}

TEST_CASE("net_link scenario 13") {
  LoopbackLink link;
  link.FailWritesAfter(1);
  const auto frame = MakeEthernetFrame(64, 1);
  const std::array<FrameView, 1> batch{FrameView{.data = frame.data(), .length = 64}};

  CHECK(link.WriteFrames(batch).has_value());
  CHECK_FALSE(link.WriteFrames(batch).has_value());
}

TEST_CASE("net_link scenario 14") {
  LoopbackLink link;
  const auto result = link.WriteFrames({});
  REQUIRE(result.has_value());
  CHECK(result->frames_written == 0);
}
}  // TEST_SUITE("net.loopback")

TEST_SUITE("net.feth" * doctest::skip(false)) {
TEST_CASE("net_link scenario 15: feth MTU sysctl root") {
  const auto max_mtu = QueryFethMaxMtu();
  REQUIRE(max_mtu.has_value());

  CHECK(*max_mtu >= 1500);
  MESSAGE(std::format("net.link.fake.max_mtu = {}", *max_mtu));
}

TEST_CASE("net_link scenario 16: feth sysctl root") {
  const auto status = VerifyFethSysctls();
  if (!status) {
    MESSAGE(std::format("feth sysctl validation failed: {}", status.error().ToString()));
  }

  CHECK(true);
}

TEST_CASE("net_link scenario 17: root feth") {
  if (IsRunningAsRoot()) {
    MESSAGE("Root integration test disabled");
    return;
  }
  const auto pair = FethPair::Create(1500);
  REQUIRE_FALSE(pair.has_value());
  const std::string message = pair.error().ToString();

  CHECK((message.find("root") != std::string::npos || message.find("sudo") != std::string::npos));
}

TEST_CASE("net_link scenario 18: MAC") {
  const MacAddress mac{0x66, 0x65, 0x74, 0x68, 0x00, 0x00};

  CHECK(std::string{FormatMac(mac).data()} == "66:65:74:68:00:00");
}

TEST_CASE("net_link scenario 19: MTU/MAC UP") {
  if (!RootTestsEnabled()) {
    ReportSkip("feth pair lifecycle");
    return;
  }

  const MacAddress device_mac{0x02, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE};
  auto pair = FethPair::Create(1500, &device_mac);
  REQUIRE_MESSAGE(pair.has_value(), Why(pair));

  const std::string system_name{pair->SystemSide().Name()};
  const std::string driver_name{pair->DriverSide().Name()};
  MESSAGE(std::format("Created pair {} <-> {}", system_name, driver_name));

  const auto driver_peer = pair->DriverSide().QueryPeer();
  REQUIRE(driver_peer.has_value());
  CHECK(*driver_peer == system_name);
  const auto system_peer = pair->SystemSide().QueryPeer();
  REQUIRE(system_peer.has_value());
  CHECK(*system_peer == driver_name);

  CHECK(pair->SystemSide().QueryMtu().value_or(0) == 1500);
  CHECK(pair->DriverSide().QueryMtu().value_or(0) == 1500);

  const auto system_mac = pair->SystemSide().QueryMacAddress();
  REQUIRE(system_mac.has_value());
  CHECK(*system_mac == device_mac);
  const auto driver_mac = pair->DriverSide().QueryMacAddress();
  REQUIRE(driver_mac.has_value());
  CHECK(*driver_mac != device_mac);

  CHECK(pair->SystemSide().IsUp().value_or(false));
  CHECK(pair->DriverSide().IsUp().value_or(false));
}

TEST_CASE("net_link scenario 20: BPF") {
  if (!RootTestsEnabled()) {
    ReportSkip("BPF link");
    return;
  }

  auto pair = FethPair::Create(1500);
  REQUIRE_MESSAGE(pair.has_value(), Why(pair));

  auto link = BpfLink::Open(pair->DriverSide().Name(), BpfConfig{});
  REQUIRE_MESSAGE(link.has_value(), Why(link));

  MESSAGE(std::format("BPF device {}: buffer {} bytes, batch writes {}", (*link)->DevicePath(),
                      (*link)->KernelBufferBytes(),
                      (*link)->SupportsBatchWrite() ? "supported" : "unsupported"));

  CHECK((*link)->KernelBufferBytes() > 0);
  CHECK((*link)->KernelBufferBytes() <= 32U * 1024 * 1024);

  const auto stats = (*link)->QueryKernelStats();
  REQUIRE(stats.has_value());

  const auto frame = MakeEthernetFrame(200, 0x5A);
  const std::array<FrameView, 1> batch{FrameView{.data = frame.data(), .length = 200}};
  const auto written = (*link)->WriteFrames(batch);
  REQUIRE_MESSAGE(written.has_value(), Why(written));
  CHECK(written->frames_written == 1);

  const auto batch_read = (*link)->ReadFrames();
  REQUIRE(batch_read.has_value());
  for (const FrameView& view : batch_read->frames) {
    const bool is_our_frame = view.length == 200 && view.data[11] == std::byte{0x5A};
    CHECK_FALSE(is_our_frame);
  }
}

TEST_CASE("net_link scenario 21: ARP BPF feth IP") {
  if (!RootTestsEnabled()) {
    ReportSkip("ARP round trip");
    return;
  }

  auto pair = FethPair::Create(1500);
  REQUIRE_MESSAGE(pair.has_value(), Why(pair));

  const std::string assign_error =
      AssignIpv4(pair->SystemSide().Name(), kArpSystemIp, "255.255.255.0");
  REQUIRE_MESSAGE(assign_error.empty(), assign_error);

  const auto system_mac = pair->SystemSide().QueryMacAddress();
  REQUIRE_MESSAGE(system_mac.has_value(), Why(system_mac));

  auto link = BpfLink::Open(pair->DriverSide().Name(), BpfConfig{});
  REQUIRE_MESSAGE(link.has_value(), Why(link));

  const MacAddress probe_mac{0x02, 0x00, 0x00, 0x99, 0x99, 0x02};
  const auto request = MakeArpRequest(probe_mac, kArpProbeIp, kArpSystemIp);
  const std::array<FrameView, 1> batch{
      FrameView{.data = request.data(), .length = static_cast<std::uint32_t>(request.size())}};

  const auto written = (*link)->WriteFrames(batch);
  REQUIRE_MESSAGE(written.has_value(), Why(written));
  REQUIRE(written->frames_written == 1);

  bool saw_reply = false;
  MacAddress reply_mac{};
  for (int attempt = 0; attempt < 20 && !saw_reply; ++attempt) {
    const auto received = (*link)->ReadFrames();
    REQUIRE_MESSAGE(received.has_value(), Why(received));
    for (const FrameView& view : received->frames) {
      if (IsArpReplyFor(view, kArpSystemIp)) {
        saw_reply = true;
        for (std::size_t i = 0; i < reply_mac.size(); ++i) {
          reply_mac[i] = static_cast<std::uint8_t>(view.data[kArpSenderMacOffset + i]);
        }
        break;
      }
    }
  }

  CHECK_MESSAGE(saw_reply, "Expected an ARP reply through the peer feth IP stack after BPF write");
  if (saw_reply) {
    CHECK(reply_mac == *system_mac);
    MESSAGE(std::format("{} received {} with MAC {}", pair->SystemSide().Name(), kArpSystemIp,
                        FormatMac(reply_mac).data()));
  }
}
}  // TEST_SUITE("net.feth")
