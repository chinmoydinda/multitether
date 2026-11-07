#include "multitether/common/scheduling.h"

#include <pthread/qos.h>

#include "multitether/common/logging.h"

namespace multitether {
unsigned int QosClassFor(ThreadRole role) noexcept {
  switch (role) {
    case ThreadRole::kDataPath:
      return QOS_CLASS_USER_INTERACTIVE;
    case ThreadRole::kControl:
      return QOS_CLASS_USER_INITIATED;
    case ThreadRole::kAuxiliary:
      return QOS_CLASS_UTILITY;
  }
  return QOS_CLASS_DEFAULT;
}

void ConfigureCurrentThread(std::string_view name, ThreadRole role) noexcept {
  SetCurrentThreadName(name);

  const auto qos = static_cast<qos_class_t>(QosClassFor(role));

  const int rc = ::pthread_set_qos_class_self_np(qos, 0);
  if (rc != 0) {
    MULTITETHER_WARN_TR(Msg::kCommonThreadQosFailed, static_cast<int>(qos), rc);
  }
}
}  // namespace multitether
