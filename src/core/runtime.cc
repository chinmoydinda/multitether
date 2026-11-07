#include "multitether/core/runtime.h"

#include <algorithm>
#include <chrono>
#include <format>
#include <string>
#include <utility>

#include "multitether/common/i18n.h"
#include "multitether/common/logging.h"
#include "multitether/common/scheduling.h"
#include "multitether/common/time.h"

namespace multitether::core {
Result<std::unique_ptr<Runtime>> Runtime::Create(const RuntimeConfig& config) {
  auto runtime = std::unique_ptr<Runtime>(new Runtime());
  runtime->config_ = config;
  return runtime;
}

Runtime::~Runtime() {
  Stop();
}

Status Runtime::Start() {
  if (started_) {
    return std::unexpected(Error::Generic(Tr(Msg::kCoreRuntimeAlreadyStarted)));
  }

  if (!net::IsRunningAsRoot()) {
    return std::unexpected(Error::Generic(Tr(Msg::kCoreNeedsRoot)));
  }

  started_ = true;
  SetRunState(RunState::kStarting);
  control_thread_ = std::thread([this] { RunControlThread(); });
  return Ok();
}

void Runtime::Stop() {
  if (stopped_) {
    return;
  }
  stopped_ = true;
  if (!started_) {
    return;
  }

  {
    const std::lock_guard<std::mutex> guard(stop_mutex_);
    stop_requested_.store(true, std::memory_order_release);
  }
  stop_condition_.notify_all();

  if (control_thread_.joinable()) {
    control_thread_.join();
  }
}

RuntimeSnapshot Runtime::Snapshot() const {
  const std::lock_guard<std::mutex> guard(snapshot_mutex_);
  return snapshot_;
}

void Runtime::RunControlThread() noexcept {
  ConfigureCurrentThread("rndis-ctl", ThreadRole::kControl);

  if (const auto status = RunStartSequence(); !status) {
    RecordFatal(status.error());
  } else {
    SetRunState(RunState::kRunning);
    RunControlLoop();
    SetRunState(RunState::kStopping);
  }

  Teardown();

  SetRunState(fatal_error_.load(std::memory_order_acquire) ? RunState::kFailed
                                                           : RunState::kStopped);
}

Status Runtime::RunStartSequence() {
  MULTITETHER_ASSIGN_OR_RETURN(usb_context_, usb::Context::Create());

  MULTITETHER_ASSIGN_OR_RETURN(const auto candidates,
                             usb::FindRndisDevices(*usb_context_, config_.device_filter));
  if (candidates.empty()) {
    return std::unexpected(Error::Generic(Tr(Msg::kCoreNoDeviceFound)));
  }
  if (candidates.size() > 1) {
    MULTITETHER_INFO_TR(Msg::kCoreMultipleDevices, candidates.size());
  }
  MULTITETHER_ASSIGN_OR_RETURN(device_, usb::Device::Open(*usb_context_, candidates.front()));
  RefreshSnapshot();

  control_channel_ =
      std::make_unique<usb::UsbControlChannel>(*device_, config_.rndis.control_timeout_millis);

  MULTITETHER_RETURN_IF_ERROR(control_channel_->StartNotificationListener());

  rndis::StateMachineConfig rndis_config = config_.rndis;
  rndis_config.requested_mtu = config_.mtu;
  state_machine_ = std::make_unique<rndis::StateMachine>(*control_channel_, *this, rndis_config);

  MULTITETHER_RETURN_IF_ERROR(state_machine_->Start());

  const rndis::NegotiatedParameters& parameters = state_machine_->Parameters();
  const rndis::DeviceInfo& info = state_machine_->Info();

  {
    const net::MacAddress* system_mac = nullptr;
    net::MacAddress adopted{};
    if (config_.adopt_device_mac && info.has_permanent_address) {
      std::ranges::copy(info.permanent_address, adopted.begin());
      system_mac = &adopted;
    }
    MULTITETHER_ASSIGN_OR_RETURN(auto pair, net::FethPair::Create(parameters.mtu, system_mac));
    feth_pair_ = std::make_unique<net::FethPair>(std::move(pair));
  }

  RefreshSnapshot();

  {
    net::BpfConfig bpf_config = config_.bpf;

    bpf_config.max_frame_bytes = parameters.mtu + rndis::kEthernetHeaderBytes;
    MULTITETHER_ASSIGN_OR_RETURN(bpf_link_,
                               net::BpfLink::Open(feth_pair_->DriverSide().Name(), bpf_config));
  }

  {
    usb::DataChannelConfig data_config = config_.data_channel;

    data_config.rx_transfer_bytes =
        std::max(data_config.rx_transfer_bytes, config_.rndis.host_max_transfer_size);
    MULTITETHER_ASSIGN_OR_RETURN(data_channel_,
                               usb::UsbDataChannel::Create(*device_, parameters, data_config));
  }

  {
    BridgeConfig bridge_config = config_.bridge;
    bridge_config.max_frame_bytes = parameters.mtu + rndis::kEthernetHeaderBytes;
    bridge_ = std::make_unique<Bridge>(*data_channel_, *bpf_link_, bridge_config);
    MULTITETHER_RETURN_IF_ERROR(bridge_->Start());
  }

  RefreshSnapshot();
  return Ok();
}

void Runtime::RunControlLoop() {
  BridgeStats previous = bridge_->Snapshot();
  Nanos last_stats_nanos = MonotonicNanos();

  const Nanos stats_interval_nanos =
      static_cast<Nanos>(config_.stats_interval_millis) * kNanosPerMilli;

  while (!stop_requested_.load(std::memory_order_acquire) &&
         !fatal_error_.load(std::memory_order_acquire)) {
    if (const auto status = state_machine_->Poll(); !status) {
      RecordFatal(std::move(status).error());
      break;
    }

    RefreshSnapshot();

    const Nanos now = MonotonicNanos();
    if (stats_interval_nanos != 0 && now - last_stats_nanos >= stats_interval_nanos) {
      const BridgeStats current = bridge_->Snapshot();
      const double seconds =
          static_cast<double>(now - last_stats_nanos) / static_cast<double>(kNanosPerSecond);
      MULTITETHER_INFO("{}", FormatStatsLine(previous, current, seconds));
      previous = current;
      last_stats_nanos = now;
    }

    std::uint32_t sleep_millis =
        std::min<std::uint32_t>(state_machine_->MillisUntilNextPoll(), 250);
    if (stats_interval_nanos != 0) {
      const Nanos elapsed = now - last_stats_nanos;
      const Nanos remaining = elapsed >= stats_interval_nanos ? 0 : stats_interval_nanos - elapsed;
      sleep_millis = std::min<std::uint32_t>(sleep_millis,
                                             static_cast<std::uint32_t>(remaining / kNanosPerMilli));
    }

    sleep_millis = std::max<std::uint32_t>(sleep_millis, 10);

    std::unique_lock<std::mutex> lock(stop_mutex_);
    stop_condition_.wait_for(lock, std::chrono::milliseconds(sleep_millis),
                             [this] { return stop_requested_.load(std::memory_order_acquire); });
  }

  if (fatal_error_.load(std::memory_order_acquire)) {
    MULTITETHER_ERROR_TR(Msg::kCoreExitingOnFatal);
  }
}

void Runtime::Teardown() {
  MULTITETHER_INFO_TR(Msg::kCoreStopping);

  if (bridge_ != nullptr) {
    bridge_->Stop();
    bridge_.reset();
  }
  data_channel_.reset();

  bpf_link_.reset();

  feth_pair_.reset();

  if (state_machine_ != nullptr) {
    state_machine_->Stop();
    state_machine_.reset();
  }

  if (control_channel_ != nullptr) {
    control_channel_->StopNotificationListener();
    control_channel_.reset();
  }

  device_.reset();

  usb_context_.reset();

  RefreshSnapshot();
  MULTITETHER_INFO_TR(Msg::kCoreStopped);
}

void Runtime::RefreshSnapshot() {
  RuntimeSnapshot fresh;
  if (device_ != nullptr) {
    fresh.device_description = std::string{device_->Describe()};
  }
  if (feth_pair_ != nullptr) {
    fresh.system_interface = std::string{feth_pair_->SystemSide().Name()};
    fresh.driver_interface = std::string{feth_pair_->DriverSide().Name()};
  }
  if (bridge_ != nullptr) {
    fresh.bridge = bridge_->Snapshot();
  }

  const std::lock_guard<std::mutex> guard(snapshot_mutex_);

  fresh.run_state = snapshot_.run_state;
  fresh.fatal_message = snapshot_.fatal_message;
  snapshot_ = std::move(fresh);
}

void Runtime::SetRunState(RunState next) {
  const std::lock_guard<std::mutex> guard(snapshot_mutex_);
  snapshot_.run_state = next;
}

void Runtime::RecordFatal(const Error& error) {
  const std::string message = error.ToString();
  MULTITETHER_ERROR("{}", message);

  {
    const std::lock_guard<std::mutex> guard(snapshot_mutex_);

    if (snapshot_.fatal_message.empty()) {
      snapshot_.fatal_message = message;
    }
  }
  fatal_error_.store(true, std::memory_order_release);
}

void Runtime::OnStateChanged(rndis::State, rndis::State) {}

void Runtime::OnNegotiated(const rndis::NegotiatedParameters& parameters,
                           const rndis::DeviceInfo& info) {
  MULTITETHER_INFO_TR(Msg::kCoreRndisReady, rndis::FormatMac(info.permanent_address).data(),
                    parameters.mtu, info.LinkSpeedMbps());
}

void Runtime::OnLinkStateChanged(bool connected) {
  MULTITETHER_INFO_TR(Msg::kCoreLinkState,
                    Text(connected ? Msg::kCoreLinkConnected : Msg::kCoreLinkDisconnected));

  if (bridge_ != nullptr) {
    bridge_->SetPaused(!connected);
  }
}

void Runtime::OnDeviceReset(bool addressing_lost) {
  MULTITETHER_WARN_TR(Msg::kCoreDeviceReset,
                    Text(addressing_lost ? Msg::kCoreAddressingLost : Msg::kCoreAddressingKept));

  if (bridge_ != nullptr) {
    bridge_->SetPaused(true);
    bridge_->SetPaused(false);
  }
}

void Runtime::OnFatalError(const Error& error) {
  Error annotated = error;
  RecordFatal(std::move(annotated).WithContext(Tr(Msg::kCoreLinkUnrecoverable)));
}
}  // namespace multitether::core
