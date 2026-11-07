#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <memory>
#include <mutex>
#include <new>
#include <tuple>
#include <utility>

#include "capi_support.h"
#include "multitether/capi/multitether_c.h"
#include "multitether/common/i18n.h"
#include "multitether/common/time.h"
#include "multitether/core/runtime.h"
#include "multitether/net/feth_device.h"
#include "multitether/rndis/state_machine.h"

namespace {
using multitether::capi::ClearError;
using multitether::capi::CopyText;
using multitether::capi::FillError;
using multitether::core::RunState;

static_assert(static_cast<int>(RunState::kIdle) == TK_RUN_IDLE);
static_assert(static_cast<int>(RunState::kStarting) == TK_RUN_STARTING);
static_assert(static_cast<int>(RunState::kRunning) == TK_RUN_RUNNING);
static_assert(static_cast<int>(RunState::kStopping) == TK_RUN_STOPPING);
static_assert(static_cast<int>(RunState::kStopped) == TK_RUN_STOPPED);
static_assert(static_cast<int>(RunState::kFailed) == TK_RUN_FAILED);

[[nodiscard]] multitether::core::RuntimeConfig ToRuntimeConfig(const tk_session_config_t& config) {
  multitether::core::RuntimeConfig result;

  result.device_filter.vendor_id = config.vendor_id;
  result.device_filter.product_id = config.product_id;
  result.device_filter.bus_number = config.bus_number;
  result.device_filter.device_address = config.device_address;

  return result;
}
}  // namespace

struct tk_session {
  std::unique_ptr<multitether::core::Runtime> runtime;
};

void tk_session_config_init(tk_session_config_t* out_config) {
  if (out_config == nullptr) {
    return;
  }
  *out_config = tk_session_config_t{};
}

tk_session_t* tk_session_create(const tk_session_config_t* config, tk_error_t* out_error) {
  ClearError(out_error);
  if (config == nullptr) {
    multitether::capi::FillGenericError(out_error,
                                      multitether::Tr(multitether::Msg::kCapiSessionConfigNull));
    return nullptr;
  }

  multitether::capi::InstallInterfaceRegistry();

  auto session = std::unique_ptr<tk_session>(new (std::nothrow) tk_session());
  if (session == nullptr) {
    multitether::capi::FillGenericError(out_error,
                                      multitether::Tr(multitether::Msg::kCapiSessionOutOfMemory));
    return nullptr;
  }

  auto runtime = multitether::core::Runtime::Create(ToRuntimeConfig(*config));
  if (!runtime) {
    FillError(out_error, runtime.error());
    return nullptr;
  }
  session->runtime = std::move(*runtime);
  return session.release();
}

tk_result_t tk_session_start(tk_session_t* session, tk_error_t* out_error) {
  ClearError(out_error);
  if (session == nullptr) {
    return TK_ERR_INVALID_ARGUMENT;
  }

  if (const auto status = session->runtime->Start(); !status) {
    FillError(out_error, status.error());

    return multitether::net::IsRunningAsRoot() ? TK_ERR_FAILED : TK_ERR_PERMISSION;
  }
  return TK_OK;
}

tk_result_t tk_session_stop(tk_session_t* session) {
  if (session == nullptr) {
    return TK_ERR_INVALID_ARGUMENT;
  }
  session->runtime->Stop();
  return TK_OK;
}

void tk_session_destroy(tk_session_t* session) {
  delete session;  // NOLINT(cppcoreguidelines-owning-memory)
}

tk_result_t tk_session_status_get(tk_session_t* session, tk_session_status_t* out_status) {
  if (session == nullptr || out_status == nullptr) {
    return TK_ERR_INVALID_ARGUMENT;
  }

  const multitether::core::RuntimeSnapshot snapshot = session->runtime->Snapshot();
  *out_status = tk_session_status_t{};

  out_status->run_state = static_cast<std::int32_t>(snapshot.run_state);

  CopyText(out_status->system_interface, snapshot.system_interface);
  CopyText(out_status->driver_interface, snapshot.driver_interface);
  CopyText(out_status->device_description, snapshot.device_description);

  out_status->rx_bytes = snapshot.bridge.rx.bytes;
  out_status->tx_bytes = snapshot.bridge.tx.bytes;

  out_status->monotonic_nanos = static_cast<std::int64_t>(multitether::MonotonicNanos());

  CopyText(out_status->fatal, snapshot.fatal_message);
  return TK_OK;
}

