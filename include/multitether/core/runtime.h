#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>

#include "multitether/common/error.h"
#include "multitether/core/bridge.h"
#include "multitether/net/bpf_link.h"
#include "multitether/net/feth_device.h"
#include "multitether/rndis/state_machine.h"
#include "multitether/usb/context.h"
#include "multitether/usb/data_channel.h"
#include "multitether/usb/device.h"

namespace multitether::core {
enum class RunState : std::uint8_t {
  kIdle,
  kStarting,
  kRunning,
  kStopping,
  kStopped,
  kFailed,
};

struct RuntimeSnapshot {
  RunState run_state = RunState::kIdle;

  std::string system_interface;

  std::string driver_interface;

  std::string device_description;

  BridgeStats bridge;

  std::string fatal_message;
};

struct RuntimeConfig {
  usb::DeviceFilter device_filter;

  std::uint32_t mtu = rndis::kDefaultMtu;

  bool adopt_device_mac = true;

  rndis::StateMachineConfig rndis;
  usb::DataChannelConfig data_channel;
  net::BpfConfig bpf;
  BridgeConfig bridge;

  std::uint32_t stats_interval_millis = 5'000;
};

class Runtime final : public rndis::StateMachineObserver {
 public:
  [[nodiscard]] static Result<std::unique_ptr<Runtime>> Create(const RuntimeConfig& config);

  Runtime(const Runtime&) = delete;
  Runtime& operator=(const Runtime&) = delete;
  Runtime(Runtime&&) = delete;
  Runtime& operator=(Runtime&&) = delete;

  ~Runtime() override;

  [[nodiscard]] Status Start();

  void Stop();

  [[nodiscard]] RuntimeSnapshot Snapshot() const;

  void OnStateChanged(rndis::State from, rndis::State to) override;
  void OnNegotiated(const rndis::NegotiatedParameters& parameters,
                    const rndis::DeviceInfo& info) override;
  void OnLinkStateChanged(bool connected) override;
  void OnDeviceReset(bool addressing_lost) override;
  void OnFatalError(const Error& error) override;

 private:
  Runtime() = default;

  void RunControlThread() noexcept;

  [[nodiscard]] Status RunStartSequence();

  void RunControlLoop();

  void Teardown();

  void SetRunState(RunState next);

  void RecordFatal(const Error& error);

  void RefreshSnapshot();

  RuntimeConfig config_;

  std::unique_ptr<usb::Context> usb_context_;
  std::unique_ptr<usb::Device> device_;
  std::unique_ptr<usb::UsbControlChannel> control_channel_;
  std::unique_ptr<rndis::StateMachine> state_machine_;
  std::unique_ptr<net::FethPair> feth_pair_;
  std::unique_ptr<net::BpfLink> bpf_link_;
  std::unique_ptr<usb::UsbDataChannel> data_channel_;
  std::unique_ptr<Bridge> bridge_;

  std::thread control_thread_;

  mutable std::mutex snapshot_mutex_;
  RuntimeSnapshot snapshot_;

  std::mutex stop_mutex_;
  std::condition_variable stop_condition_;

  std::atomic<bool> stop_requested_{false};
  std::atomic<bool> fatal_error_{false};

  bool started_ = false;
  bool stopped_ = false;
};
}  // namespace multitether::core
