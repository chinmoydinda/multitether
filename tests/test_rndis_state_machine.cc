#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <doctest.h>

#include "mock_control_channel.h"
#include "multitether/rndis/state_machine.h"

namespace {
template <typename T>
std::string Why(const T& result) {
  return result.has_value() ? std::string{} : result.error().ToString();
}
}  // namespace

using namespace multitether;                   // NOLINT(google-build-using-namespace)
using namespace multitether::rndis;            // NOLINT(google-build-using-namespace)
using multitether::testing::MakeDeviceKeepAlive;
using multitether::testing::MakeIndicateStatus;
using multitether::testing::MakeInitializeComplete;
using multitether::testing::MakeKeepAliveComplete;
using multitether::testing::MakeQueryComplete;
using multitether::testing::MakeQueryCompleteMac;
using multitether::testing::MakeQueryCompleteUint32;
using multitether::testing::MakeResetComplete;
using multitether::testing::MakeSetComplete;
using multitether::testing::MakeWellBehavedDevice;
using multitether::testing::MockControlChannel;
using multitether::testing::OidOf;
using multitether::testing::RecordingObserver;
using multitether::testing::RequestIdOf;

namespace {
constexpr MacAddress kDeviceMac{0x02, 0x1A, 0x11, 0x22, 0x33, 0x44};

[[nodiscard]] StateMachineConfig FastConfig() {
  StateMachineConfig config;
  config.response_poll_interval_millis = 0;
  config.response_poll_attempts = 4;
  config.keepalive_interval_millis = 0;
  return config;
}
}  // namespace

TEST_SUITE("rndis.state_machine") {
TEST_CASE("rndis_state_machine scenario 1") {
  CHECK_FALSE(StateName(State::kUninitialized).empty());
  CHECK_FALSE(StateName(State::kInitializing).empty());
  CHECK_FALSE(StateName(State::kInitialized).empty());
  CHECK_FALSE(StateName(State::kDataInitialized).empty());
  CHECK_FALSE(StateName(State::kHalting).empty());
}

TEST_CASE("rndis_state_machine scenario 2: kDataInitialized") {
  MockControlChannel channel;
  channel.SetRequestHandler(MakeWellBehavedDevice(kDeviceMac));
  RecordingObserver observer;
  StateMachine machine(channel, observer, FastConfig());

  CHECK(machine.CurrentState() == State::kUninitialized);
  const auto status = machine.Start();
  REQUIRE_MESSAGE(status.has_value(), Why(status));

  CHECK(machine.CurrentState() == State::kDataInitialized);

  REQUIRE(observer.transitions.size() == 3);
  CHECK(observer.transitions[0].to == State::kInitializing);
  CHECK(observer.transitions[1].to == State::kInitialized);
  CHECK(observer.transitions[2].to == State::kDataInitialized);

  CHECK(observer.negotiated_count == 1);
  CHECK(observer.parameters_snapshot.mtu == 1500);
  CHECK(observer.parameters_snapshot.device_max_transfer_size == 2048);
  CHECK(observer.parameters_snapshot.max_packets_per_message == 1);
  CHECK(observer.parameters_snapshot.tx_alignment_bytes == 1);

  CHECK(machine.Info().has_permanent_address);
  CHECK(machine.Info().permanent_address == kDeviceMac);
  CHECK(machine.Info().LinkSpeedMbps() == doctest::Approx(480.0));

  REQUIRE(observer.link_events.size() == 1);
  CHECK(observer.link_events[0]);
}

TEST_CASE("rndis_state_machine scenario 3: INITIALIZE SET") {
  MockControlChannel channel;
  channel.SetRequestHandler(MakeWellBehavedDevice(kDeviceMac));
  RecordingObserver observer;
  StateMachine machine(channel, observer, FastConfig());
  REQUIRE(machine.Start().has_value());

  REQUIRE_FALSE(channel.SentMessages().empty());

  CHECK(channel.SentMessageType(0) == ToRaw(MessageType::kInitialize));

  const std::size_t last = channel.SentMessages().size() - 1;
  CHECK(channel.SentMessageType(last) == ToRaw(MessageType::kSet));

  CHECK(channel.CountSent(MessageType::kInitialize) == 1);

  std::uint32_t filter = 0;
  REQUIRE(channel.FindSetUint32(Oid::kGenCurrentPacketFilter, filter));
  CHECK(filter == kDefaultPacketFilter);
  CHECK(filter == 0x0000'002DU);
}

TEST_CASE("rndis_state_machine scenario 4: OID InformationBufferLength") {
  MockControlChannel channel;
  std::vector<std::pair<std::uint32_t, std::uint32_t>> query_oid_and_length;
  channel.SetRequestHandler(
      [&](std::span<const std::byte> request, MockControlChannel& mock) {
        const std::uint32_t type = LoadLe32(request.data() + kMessageTypeOffset);
        if (type == ToRaw(MessageType::kQuery)) {
          query_oid_and_length.emplace_back(
              OidOf(request), LoadLe32(request.data() + kQuerySetInfoBufferLengthOffset));
        }
        MakeWellBehavedDevice(kDeviceMac)(request, mock);
      });
  RecordingObserver observer;
  StateMachine machine(channel, observer, FastConfig());
  REQUIRE(machine.Start().has_value());

  bool saw_fixed = false;
  bool saw_variable = false;
  for (const auto& [oid, length] : query_oid_and_length) {
    if (IsVariableLengthOid(static_cast<Oid>(oid))) {
      CHECK_MESSAGE(length == 0, "OID " << OidName(oid) << " must have zero request length");
      saw_variable = true;
    } else {
      CHECK_MESSAGE(length > 0, "OID " << OidName(oid) << " must have a nonzero request length");
      saw_fixed = true;
    }
  }
  CHECK(saw_fixed);
  CHECK(saw_variable);
}

TEST_CASE("rndis_state_machine scenario 5: OID NOT_SUPPORTED") {
  MockControlChannel channel;
  channel.SetRequestHandler(MakeWellBehavedDevice(kDeviceMac));
  RecordingObserver observer;
  StateMachine machine(channel, observer, FastConfig());
  REQUIRE(machine.Start().has_value());
  CHECK(machine.CurrentState() == State::kDataInitialized);
  CHECK(machine.Info().physical_medium == PhysicalMedium::kUnspecified);
  CHECK(observer.fatal_errors.empty());
}

TEST_CASE("rndis_state_machine scenario 6: MAC HALT") {
  MockControlChannel channel;
  channel.SetRequestHandler([](std::span<const std::byte> request, MockControlChannel& mock) {
    const std::uint32_t type = LoadLe32(request.data() + kMessageTypeOffset);
    const std::uint32_t request_id = RequestIdOf(request);
    if (type == ToRaw(MessageType::kInitialize)) {
      mock.EnqueueResponse(MakeInitializeComplete(request_id));
      return;
    }
    if (type == ToRaw(MessageType::kQuery)) {
      mock.EnqueueResponse(
          MakeQueryComplete(request_id, {}, ToRaw(StatusCode::kNotSupported)));
    }
  });
  RecordingObserver observer;
  StateMachine machine(channel, observer, FastConfig());

  const auto status = machine.Start();
  REQUIRE_FALSE(status.has_value());

  CHECK(machine.CurrentState() == State::kUninitialized);

  CHECK(channel.CountSent(MessageType::kHalt) == 1);
}

TEST_CASE("rndis_state_machine scenario 7: QUERY") {
  MockControlChannel channel;
  channel.SetRequestHandler([](std::span<const std::byte> request, MockControlChannel& mock) {
    const std::uint32_t type = LoadLe32(request.data() + kMessageTypeOffset);
    if (type == ToRaw(MessageType::kInitialize)) {
      mock.EnqueueResponse(MakeInitializeComplete(RequestIdOf(request), 2048, 1, 0,
                                                  ToRaw(StatusCode::kFailure)));
    }
  });
  RecordingObserver observer;
  StateMachine machine(channel, observer, FastConfig());

  REQUIRE_FALSE(machine.Start().has_value());
  CHECK(machine.CurrentState() == State::kUninitialized);
  CHECK(channel.CountSent(MessageType::kQuery) == 0);
}

TEST_CASE("rndis_state_machine scenario 8") {
  MockControlChannel channel;
  channel.SetRequestHandler([](std::span<const std::byte> request, MockControlChannel& mock) {
    if (LoadLe32(request.data() + kMessageTypeOffset) == ToRaw(MessageType::kInitialize)) {
      auto message = MakeInitializeComplete(RequestIdOf(request));
      StoreLe32(message.data() + kInitializeCmpltMediumOffset,
                static_cast<std::uint32_t>(Medium::kWirelessLan));
      mock.EnqueueResponse(std::move(message));
    }
  });
  RecordingObserver observer;
  StateMachine machine(channel, observer, FastConfig());
  REQUIRE_FALSE(machine.Start().has_value());
}

TEST_CASE("rndis_state_machine scenario 9: INDICATE_STATUS") {
  MockControlChannel channel;
  channel.SetRequestHandler([](std::span<const std::byte> request, MockControlChannel& mock) {
    const std::uint32_t type = LoadLe32(request.data() + kMessageTypeOffset);
    if (type == ToRaw(MessageType::kInitialize)) {
      mock.EnqueueResponse(MakeIndicateStatus(StatusCode::kMediaDisconnect));
      mock.EnqueueResponse(MakeInitializeComplete(RequestIdOf(request)));
      return;
    }
    MakeWellBehavedDevice(kDeviceMac)(request, mock);
  });
  RecordingObserver observer;
  StateMachine machine(channel, observer, FastConfig());

  const auto status = machine.Start();
  REQUIRE_MESSAGE(status.has_value(), Why(status));
  CHECK(machine.CurrentState() == State::kDataInitialized);

  CHECK(channel.PendingResponseCount() == 0);

  REQUIRE(observer.link_events.size() == 1);
  CHECK(observer.link_events[0]);
}

TEST_CASE("rndis_state_machine scenario 10: up MEDIA_DISCONNECT") {
  MockControlChannel channel;
  channel.SetRequestHandler(MakeWellBehavedDevice(kDeviceMac));
  RecordingObserver observer;
  auto config = FastConfig();
  config.keepalive_interval_millis = 60'000;
  StateMachine machine(channel, observer, config);
  REQUIRE(machine.Start().has_value());

  REQUIRE(observer.link_events.size() == 1);
  REQUIRE(observer.link_events[0]);

  channel.EnqueueResponse(MakeIndicateStatus(StatusCode::kMediaDisconnect));
  REQUIRE(machine.Poll().has_value());

  REQUIRE(observer.link_events.size() == 2);
  CHECK_FALSE(observer.link_events[1]);

  channel.EnqueueResponse(MakeIndicateStatus(StatusCode::kMediaDisconnect));
  REQUIRE(machine.Poll().has_value());
  CHECK(observer.link_events.size() == 2);

  channel.EnqueueResponse(MakeIndicateStatus(StatusCode::kMediaConnect));
  REQUIRE(machine.Poll().has_value());
  REQUIRE(observer.link_events.size() == 3);
  CHECK(observer.link_events[2]);
}

TEST_CASE("rndis_state_machine scenario 11: KEEPALIVE_MSG KEEPALIVE_CMPLT") {
  MockControlChannel channel;
  constexpr std::uint32_t kDeviceKeepAliveId = 0xABCD'1234;
  channel.SetRequestHandler([](std::span<const std::byte> request, MockControlChannel& mock) {
    const std::uint32_t type = LoadLe32(request.data() + kMessageTypeOffset);
    if (type == ToRaw(MessageType::kInitialize)) {
      mock.EnqueueResponse(MakeDeviceKeepAlive(kDeviceKeepAliveId));
      mock.EnqueueResponse(MakeInitializeComplete(RequestIdOf(request)));
      return;
    }
    MakeWellBehavedDevice(kDeviceMac)(request, mock);
  });
  RecordingObserver observer;
  StateMachine machine(channel, observer, FastConfig());
  REQUIRE(machine.Start().has_value());

  bool found = false;
  for (const std::vector<std::byte>& message : channel.SentMessages()) {
    if (message.size() >= kKeepAliveCmpltBytes &&
        LoadLe32(message.data() + kMessageTypeOffset) ==
            ToRaw(MessageType::kKeepAliveComplete)) {
      CHECK(LoadLe32(message.data() + kKeepAliveCmpltRequestIdOffset) == kDeviceKeepAliveId);
      CHECK(LoadLe32(message.data() + kKeepAliveCmpltStatusOffset) ==
            ToRaw(StatusCode::kSuccess));
      found = true;
    }
  }
  CHECK_MESSAGE(found, "Host must reply to device KEEPALIVE");
}

TEST_CASE("rndis_state_machine scenario 12") {
  MockControlChannel channel;
  channel.SetRequestHandler([](std::span<const std::byte> request, MockControlChannel& mock) {
    if (LoadLe32(request.data() + kMessageTypeOffset) == ToRaw(MessageType::kInitialize)) {
      std::vector<std::byte> condis(kMessageHeaderBytes);
      StoreLe32(condis.data() + kMessageTypeOffset, 0x0000'8001U);  // MP_CREATE_VC
      StoreLe32(condis.data() + kMessageLengthOffset, kMessageHeaderBytes);
      mock.EnqueueResponse(std::move(condis));
      mock.EnqueueResponse(MakeInitializeComplete(RequestIdOf(request)));
    }
  });
  RecordingObserver observer;
  StateMachine machine(channel, observer, FastConfig());

  const auto status = machine.Start();
  REQUIRE_FALSE(status.has_value());

  CHECK(status.error().ToString().find("connection-oriented") != std::string::npos);
}

TEST_CASE("rndis_state_machine scenario 13") {
  MockControlChannel channel;
  channel.SetRequestHandler(MakeWellBehavedDevice(kDeviceMac));
  RecordingObserver observer;
  auto config = FastConfig();

  config.keepalive_interval_millis = 60'000;
  StateMachine machine(channel, observer, config);
  REQUIRE(machine.Start().has_value());

  channel.ClearSentMessages();
  REQUIRE(machine.Poll().has_value());
  CHECK(channel.CountSent(MessageType::kKeepAlive) == 0);
}

TEST_CASE("rndis_state_machine scenario 14: KEEPALIVE") {
  MockControlChannel channel;
  channel.SetRequestHandler(MakeWellBehavedDevice(kDeviceMac));
  RecordingObserver observer;
  auto config = FastConfig();
  config.keepalive_interval_millis = 0;
  StateMachine machine(channel, observer, config);
  REQUIRE(machine.Start().has_value());

  channel.ClearSentMessages();
  REQUIRE(machine.Poll().has_value());
  CHECK(channel.CountSent(MessageType::kKeepAlive) == 1);
  CHECK(observer.fatal_errors.empty());
}

TEST_CASE("rndis_state_machine scenario 15") {
  MockControlChannel channel;

  channel.SetRequestHandler([](std::span<const std::byte> request, MockControlChannel& mock) {
    const std::uint32_t type = LoadLe32(request.data() + kMessageTypeOffset);
    if (type == ToRaw(MessageType::kKeepAlive)) {
      return;
    }
    MakeWellBehavedDevice(kDeviceMac)(request, mock);
  });
  RecordingObserver observer;
  auto config = FastConfig();
  config.keepalive_interval_millis = 0;
  config.keepalive_failure_threshold = 3;
  StateMachine machine(channel, observer, config);
  REQUIRE(machine.Start().has_value());

  CHECK(machine.Poll().has_value());
  CHECK(observer.fatal_errors.empty());
  CHECK(machine.Poll().has_value());
  CHECK(observer.fatal_errors.empty());

  const auto third = machine.Poll();
  CHECK_FALSE(third.has_value());
  REQUIRE(observer.fatal_errors.size() == 1);
  CHECK(observer.fatal_errors[0].find("keep-alive") != std::string::npos);
}

TEST_CASE("rndis_state_machine scenario 16: AddressingReset") {
  MockControlChannel channel;
  channel.SetRequestHandler(MakeWellBehavedDevice(kDeviceMac));
  RecordingObserver observer;
  StateMachine machine(channel, observer, FastConfig());
  REQUIRE(machine.Start().has_value());

  channel.ClearSentMessages();
  REQUIRE(machine.Reset().has_value());

  REQUIRE(observer.reset_events.size() == 1);
  CHECK(observer.reset_events[0]);

  CHECK(channel.CountSent(MessageType::kReset) == 1);

  std::uint32_t filter = 0;
  REQUIRE_MESSAGE(channel.FindSetUint32(Oid::kGenCurrentPacketFilter, filter),
                  "Fatal keepalive error must be reported");
  CHECK(filter == kDefaultPacketFilter);
}

TEST_CASE("rndis_state_machine scenario 17: AddressingReset") {
  MockControlChannel channel;
  channel.SetRequestHandler([](std::span<const std::byte> request, MockControlChannel& mock) {
    const std::uint32_t type = LoadLe32(request.data() + kMessageTypeOffset);
    if (type == ToRaw(MessageType::kReset)) {
      mock.EnqueueResponse(MakeResetComplete(/*addressing_reset=*/false));
      return;
    }
    MakeWellBehavedDevice(kDeviceMac)(request, mock);
  });
  RecordingObserver observer;
  StateMachine machine(channel, observer, FastConfig());
  REQUIRE(machine.Start().has_value());

  channel.ClearSentMessages();
  REQUIRE(machine.Reset().has_value());
  REQUIRE(observer.reset_events.size() == 1);
  CHECK_FALSE(observer.reset_events[0]);

  std::uint32_t filter = 0;
  CHECK_FALSE(channel.FindSetUint32(Oid::kGenCurrentPacketFilter, filter));
}

TEST_CASE("rndis_state_machine scenario 18: HALT") {
  MockControlChannel channel;
  channel.SetRequestHandler(MakeWellBehavedDevice(kDeviceMac));
  RecordingObserver observer;
  StateMachine machine(channel, observer, FastConfig());
  REQUIRE(machine.Start().has_value());

  channel.ClearSentMessages();
  machine.Stop();
  CHECK(machine.CurrentState() == State::kUninitialized);

  std::uint32_t filter = 0xFFFF'FFFFU;
  REQUIRE(channel.FindSetUint32(Oid::kGenCurrentPacketFilter, filter));
  CHECK(filter == 0);
  CHECK(channel.CountSent(MessageType::kHalt) == 1);

  const std::size_t last = channel.SentMessages().size() - 1;
  CHECK(channel.SentMessageType(last) == ToRaw(MessageType::kHalt));
}

TEST_CASE("rndis_state_machine scenario 19") {
  MockControlChannel channel;
  channel.SetRequestHandler(MakeWellBehavedDevice(kDeviceMac));
  RecordingObserver observer;
  StateMachine machine(channel, observer, FastConfig());
  REQUIRE(machine.Start().has_value());

  machine.Stop();
  const std::size_t after_first = channel.SentMessages().size();
  machine.Stop();
  CHECK(channel.SentMessages().size() == after_first);
}

TEST_CASE("rndis_state_machine scenario 20: Start") {
  MockControlChannel channel;
  channel.SetRequestHandler(MakeWellBehavedDevice(kDeviceMac));
  RecordingObserver observer;
  StateMachine machine(channel, observer, FastConfig());
  REQUIRE(machine.Start().has_value());
  CHECK_FALSE(machine.Start().has_value());
}

TEST_CASE("rndis_state_machine scenario 21") {
  MockControlChannel channel;
  channel.SetHasInterruptEndpoint(false);
  channel.SetRequestHandler(MakeWellBehavedDevice(kDeviceMac));
  RecordingObserver observer;
  StateMachine machine(channel, observer, FastConfig());

  const auto status = machine.Start();
  REQUIRE_MESSAGE(status.has_value(), Why(status));
  CHECK(machine.CurrentState() == State::kDataInitialized);
}

TEST_CASE("rndis_state_machine scenario 22") {
  MockControlChannel channel;
  channel.SetRequestHandler(MakeWellBehavedDevice(kDeviceMac));
  channel.FailSendOnCall(1);
  RecordingObserver observer;
  StateMachine machine(channel, observer, FastConfig());

  REQUIRE_FALSE(machine.Start().has_value());
  CHECK(machine.CurrentState() == State::kUninitialized);
}

TEST_CASE("rndis_state_machine scenario 23: quirk MaxPacketsPerMessage") {
  MockControlChannel channel;
  channel.SetRequestHandler(
      MakeWellBehavedDevice(kDeviceMac, /*max_transfer_size=*/3 * 1600,
                            /*max_packets=*/3, /*alignment_factor=*/2));
  RecordingObserver observer;
  StateMachine machine(channel, observer, FastConfig());
  REQUIRE(machine.Start().has_value());

  CHECK(observer.parameters_snapshot.max_packets_per_message == 3);
  CHECK(observer.parameters_snapshot.tx_alignment_bytes == 4);  // 1 << 2
}

TEST_CASE("rndis_state_machine scenario 24: quirk") {
  MockControlChannel channel;
  channel.SetRequestHandler(MakeWellBehavedDevice(kDeviceMac, 2048, 1, /*alignment_factor=*/31));
  RecordingObserver observer;
  StateMachine machine(channel, observer, FastConfig());
  REQUIRE(machine.Start().has_value());
  CHECK(observer.parameters_snapshot.tx_alignment_bytes == 128);
}

TEST_CASE("rndis_state_machine scenario 25: MTU MTU") {
  MockControlChannel channel;
  channel.SetRequestHandler([](std::span<const std::byte> request, MockControlChannel& mock) {
    const std::uint32_t type = LoadLe32(request.data() + kMessageTypeOffset);
    const std::uint32_t request_id = RequestIdOf(request);
    if (type == ToRaw(MessageType::kQuery) &&
        OidOf(request) == ToRaw(Oid::kGenMaximumFrameSize)) {
      mock.EnqueueResponse(MakeQueryCompleteUint32(request_id, 1400));
      return;
    }
    MakeWellBehavedDevice(kDeviceMac)(request, mock);
  });
  RecordingObserver observer;
  StateMachine machine(channel, observer, FastConfig());
  REQUIRE(machine.Start().has_value());
  CHECK(machine.Parameters().mtu == 1400);
}

TEST_CASE("rndis_state_machine scenario 26: MillisUntilNextPoll") {
  MockControlChannel channel;
  channel.SetRequestHandler(MakeWellBehavedDevice(kDeviceMac));
  RecordingObserver observer;
  auto config = FastConfig();
  config.keepalive_interval_millis = 5'000;
  StateMachine machine(channel, observer, config);
  REQUIRE(machine.Start().has_value());
  CHECK(machine.MillisUntilNextPoll() > 0);
  CHECK(machine.MillisUntilNextPoll() <= 5'000);
}
}  // TEST_SUITE("rndis.state_machine")

TEST_SUITE("rndis.state_machine_timeout") {
TEST_CASE("rndis_state_machine scenario 27: WaitForNotification darwin") {
  MockControlChannel channel;
  channel.SetRequestHandler(MakeWellBehavedDevice(kDeviceMac));
  RecordingObserver observer;

  auto config = FastConfig();
  config.response_poll_interval_millis = 0;
  StateMachine machine(channel, observer, config);
  REQUIRE(machine.Start().has_value());
  REQUIRE(machine.Poll().has_value());

  REQUIRE_FALSE(channel.NotificationTimeouts().empty());
  for (const std::uint32_t timeout : channel.NotificationTimeouts()) {
    CHECK_MESSAGE(timeout > 0,
                  "WaitForNotification must not receive zero (infinite timeout on Darwin)");
  }
}

TEST_CASE("rndis_state_machine scenario 28: kProbeOnlyTimeoutMillis") {
  CHECK(kProbeOnlyTimeoutMillis > 0);
}
}  // TEST_SUITE("rndis.state_machine_timeout")
