#!/usr/bin/env bash
# Build MultiTether against the active developer toolchain's macOS SDK.
set -euo pipefail
ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
SDK_PATH="${MULTITETHER_SDK:-$(xcrun --sdk macosx --show-sdk-path)}"
BUILD_DIR="${MULTITETHER_BUILD_DIR:-${ROOT}/build}"
CONFIGURATION=Release
for argument in "$@"; do
  if [[ "${argument}" == --debug ]]; then CONFIGURATION=Debug; fi
done
export MULTITETHER_SDK="${SDK_PATH}"
export SDKROOT="${SDK_PATH}"
cmake -S "${ROOT}" -B "${BUILD_DIR}" \
  -DCMAKE_BUILD_TYPE="${CONFIGURATION}" \
  -DCMAKE_OSX_SYSROOT="${SDK_PATH}" \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=27.0
cmake --build "${BUILD_DIR}" --target multitether_shared --parallel "${MULTITETHER_BUILD_JOBS:-6}"
exec "${ROOT}/gui/Scripts/build-gui.sh" "$@"
