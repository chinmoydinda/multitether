#pragma once

#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

#include "multitether/common/error.h"
#include "multitether/common/time.h"
#include "multitether/rndis/messages.h"
#include "multitether/rndis/protocol.h"
#include "multitether/rndis/control_channel.h"

namespace multitether::rndis {
enum class State : std::uint8_t {
  kUninitialized,
  kInitializing,
  kInitialized,
  kDataInitialized,
  kHalting,
};

[[nodiscard]] std::string_view StateName(State state) noexcept;

struct DeviceInfo {
  MacAddress permanent_address{};

  MacAddress current_address{};
  bool has_permanent_address = false;
  bool has_current_address = false;

  std::uint32_t link_speed_100bps = 0;

  std::uint32_t maximum_frame_size = 0;

  PhysicalMedium physical_medium = PhysicalMedium::kUnspecified;

  MediaState media_state = MediaState::kConnected;

  std::string vendor_description;
  std::uint32_t vendor_id = 0;

  [[nodiscard]] double LinkSpeedMbps() const noexcept {
    return static_cast<double>(link_speed_100bps) * 100.0 / 1e6;
  }
};

struct StateMachineConfig {
  std::uint32_t requested_mtu = kDefaultMtu;

  std::uint32_t host_max_transfer_size = 16 * 1024;

  std::uint32_t max_tx_packets_per_message = 0;

  std::uint32_t packet_filter = kDefaultPacketFilter;

  std::uint32_t control_timeout_millis = kControlTimeoutMillis;

  std::uint32_t keepalive_interval_millis = kKeepAliveTimeoutMillis;

  std::uint32_t response_poll_attempts = 10;

  std::uint32_t response_poll_interval_millis = 40;

  std::uint32_t keepalive_failure_threshold = 3;
};

class StateMachineObserver {
 public:
  StateMachineObserver() = default;
  StateMachineObserver(const StateMachineObserver&) = delete;
  StateMachineObserver& operator=(const StateMachineObserver&) = delete;
  StateMachineObserver(StateMachineObserver&&) = delete;
  StateMachineObserver& operator=(StateMachineObserver&&) = delete;
  virtual ~StateMachineObserver() = default;

  virtual void OnStateChanged(State from, State to) = 0;

  virtual void OnNegotiated(const NegotiatedParameters& parameters, const DeviceInfo& info) = 0;

  virtual void OnLinkStateChanged(bool connected) = 0;

  virtual void OnDeviceReset(bool addressing_lost) = 0;

  virtual void OnFatalError(const Error& error) = 0;
};

class StateMachine {
 public:
  StateMachine(ControlChannel& channel, StateMachineObserver& observer,
               const StateMachineConfig& config);

  StateMachine(const StateMachine&) = delete;
  StateMachine& operator=(const StateMachine&) = delete;
  StateMachine(StateMachine&&) = delete;
  StateMachine& operator=(StateMachine&&) = delete;
  ~StateMachine() = default;

  [[nodiscard]] State CurrentState() const noexcept { return state_; }

  [[nodiscard]] const DeviceInfo& Info() const noexcept { return info_; }

  [[nodiscard]] const NegotiatedParameters& Parameters() const noexcept { return parameters_; }

  [[nodiscard]] Status Start();

  void Stop();

  [[nodiscard]] Status Poll();

  [[nodiscard]] std::uint32_t MillisUntilNextPoll() const noexcept;

  [[nodiscard]] Status Reset();

 private:
  struct Exchange {
    std::span<const std::byte> response;
  };

  [[nodiscard]] Result<Exchange> Transact(std::span<const std::byte> request,
                                          MessageType expected_reply);

  [[nodiscard]] Result<bool> PumpOnce(std::optional<MessageType> expected_reply,
                                      std::span<const std::byte>& out_response);

  void HandleIndicateStatus(std::span<const std::byte> message);

  [[nodiscard]] Status HandleDeviceKeepAlive(std::span<const std::byte> message);

  [[nodiscard]] Result<std::span<const std::byte>> QueryOid(Oid oid,
                                                            std::uint32_t expected_bytes,
                                                            bool fatal);

  [[nodiscard]] Status SetOidUint32(Oid oid, std::uint32_t value);

  [[nodiscard]] Status CollectDeviceInfo();

  void SendHalt() noexcept;

  void TransitionTo(State next);

  [[nodiscard]] std::uint32_t NextRequestId() noexcept;

  void MarkDeviceActivity() noexcept { last_device_activity_ = MonotonicNanos(); }

  ControlChannel* channel_;
  StateMachineObserver* observer_;
  StateMachineConfig config_;

  State state_ = State::kUninitialized;
  DeviceInfo info_;
  NegotiatedParameters parameters_;

  std::uint32_t next_request_id_ = 1;
  Nanos last_device_activity_ = 0;
  Nanos next_keepalive_deadline_ = 0;
  std::uint32_t consecutive_keepalive_failures_ = 0;

  std::uint32_t active_packet_filter_ = 0;

  std::vector<std::byte> request_buffer_;

  bool link_connected_ = false;
};
}  // namespace multitether::rndis
