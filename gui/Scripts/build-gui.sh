#!/usr/bin/env bash
# Usage: MULTITETHER_BUILD_DIR=/path ./gui/Scripts/build-gui.sh [--debug]
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd -- "${SCRIPT_DIR}/../.." && pwd)"
GUI_DIR="${REPO_ROOT}/gui"
BUILD_DIR="${MULTITETHER_BUILD_DIR:-${REPO_ROOT}/build}"
DIST_DIR="${REPO_ROOT}/dist"

SWIFT_CONFIGURATION="release"
SWIFT_BUILD_FLAGS=()
for argument in "$@"; do
  case "${argument}" in
    --debug) SWIFT_CONFIGURATION="debug" ;;
    --swift-build-flags=*) IFS=' ' read -r -a SWIFT_BUILD_FLAGS <<< "${argument#*=}" ;;
    -h|--help) echo "Usage: $0 [--debug] [--swift-build-flags=FLAGS]"; exit 0 ;;
    *) echo "Unknown argument: ${argument}" >&2; exit 2 ;;
  esac
done

log() { printf '\033[32m==>\033[0m %s\n' "$1"; }
die() { printf '\033[31mError: \033[0m%s\n' "$1" >&2; exit 1; }

command -v swift >/dev/null || die "Swift was not found. Install Xcode or Command Line Tools first."
SDK_PATH="${MULTITETHER_SDK:-$(xcrun --sdk macosx --show-sdk-path)}"
[[ -d "${SDK_PATH}" ]] || die "macOS SDK does not exist: ${SDK_PATH}"
export SDKROOT="${SDK_PATH}"
export MACOSX_DEPLOYMENT_TARGET=27.0
export CLANG_MODULE_CACHE_PATH="${CLANG_MODULE_CACHE_PATH:-${REPO_ROOT}/.cache/clang}"
export SWIFTPM_MODULECACHE_OVERRIDE="${SWIFTPM_MODULECACHE_OVERRIDE:-${REPO_ROOT}/.cache/swift}"
mkdir -p "${CLANG_MODULE_CACHE_PATH}" "${SWIFTPM_MODULECACHE_OVERRIDE}"
BASE_FLAGS=(--sdk "${SDK_PATH}" --cache-path "${REPO_ROOT}/.cache/swiftpm" --manifest-cache local)
# Command Line Tools 27's swiftbuild engine assumes a full Xcode layout.
# Native SwiftPM avoids that packaging bug; full Xcode keeps its default engine.
BUILD_SYSTEM="${MULTITETHER_SWIFT_BUILD_SYSTEM:-}"
if [[ -z "${BUILD_SYSTEM}" && "${DEVELOPER_DIR:-$(xcode-select -p)}" == */CommandLineTools ]]; then
  BUILD_SYSTEM=native
fi
if [[ -n "${BUILD_SYSTEM}" ]]; then
  BASE_FLAGS+=(--build-system "${BUILD_SYSTEM}")
fi
# Allow callers in restricted environments to disable SwiftPM's sandbox-exec.
if [[ "${MULTITETHER_DISABLE_SANDBOX:-0}" == 1 ]]; then
  BASE_FLAGS+=(--disable-sandbox)
fi
log "macOS SDK: ${SDK_PATH}; deployment target: ${MACOSX_DEPLOYMENT_TARGET}"

LIB_DIR="${BUILD_DIR}/lib"
if [[ ! -f "${LIB_DIR}/libmultitether.dylib" ]]; then
  die "Missing ${LIB_DIR}/libmultitether.dylib
  Build the C++ backend first (for example via ./build.sh)"
fi

VERSION="$(sed -n 's/^  VERSION \([0-9.]*\)$/\1/p' "${REPO_ROOT}/CMakeLists.txt" | head -1)"
HELPER_VERSION="$(sed -n 's/.*buildVersion = "MultiTether \([0-9.]*\)".*/\1/p' "${GUI_DIR}/Sources/MultiTetherIPC/HelperConstants.swift")"
if [[ "${HELPER_VERSION}" != "${VERSION}" ]]; then
  die "Version mismatch: CMakeLists.txt (${VERSION}) != HelperConstants.swift (${HELPER_VERSION})"
fi

log "MultiTether ${VERSION} (Swift ${SWIFT_CONFIGURATION})"

log "Building Swift targets"
MULTITETHER_LIB_DIR="${LIB_DIR}" swift build \
  --package-path "${GUI_DIR}" \
  --configuration "${SWIFT_CONFIGURATION}" \
  "${BASE_FLAGS[@]}" \
  ${SWIFT_BUILD_FLAGS[@]+"${SWIFT_BUILD_FLAGS[@]}"}

SWIFT_BIN_DIR="$(MULTITETHER_LIB_DIR="${LIB_DIR}" swift build \
  --package-path "${GUI_DIR}" \
  --configuration "${SWIFT_CONFIGURATION}" \
  "${BASE_FLAGS[@]}" \
  ${SWIFT_BUILD_FLAGS[@]+"${SWIFT_BUILD_FLAGS[@]}"} \
  --show-bin-path)"

APP_DIR="${DIST_DIR}/MultiTether.app"
log "Assembling ${APP_DIR}"

rm -rf "${APP_DIR}"
mkdir -p "${APP_DIR}/Contents/MacOS" \
         "${APP_DIR}/Contents/Resources"

cp "${SWIFT_BIN_DIR}/MultiTetherApp" "${APP_DIR}/Contents/MacOS/MultiTether"
sed "s/__MULTITETHER_VERSION__/${VERSION}/g" \
  "${GUI_DIR}/Resources/App-Info.plist" > "${APP_DIR}/Contents/Info.plist"
log "Building Finder icon representations"
xcrun swift -sdk "${SDK_PATH}" -module-cache-path "${CLANG_MODULE_CACHE_PATH}" \
  "${SCRIPT_DIR}/build-icon.swift" "${GUI_DIR}/Resources/MultiTetherIcon.png" \
  "${GUI_DIR}/Resources/MultiTetherIcon.icns"
cp "${GUI_DIR}/Resources/MultiTetherIcon.icns" "${GUI_DIR}/Resources/MenuBarIcon.pdf" "${APP_DIR}/Contents/Resources/"
cp "${REPO_ROOT}/LICENSE" "${APP_DIR}/Contents/Resources/ThirdParty-LICENSE.txt"
cp "${GUI_DIR}/Resources/libusb-COPYING.txt" "${APP_DIR}/Contents/Resources/"


PAYLOAD_DIR="${APP_DIR}/Contents/Library/HelperTools"
log "Assembling ${PAYLOAD_DIR}"

mkdir -p "${PAYLOAD_DIR}"
cp "${SWIFT_BIN_DIR}/multitether-helper" "${PAYLOAD_DIR}/com.multitether.helper"
# Copy only this release; old build outputs must not enter the bundle.
cp -a "${LIB_DIR}/libmultitether.dylib" \
      "${LIB_DIR}/libmultitether.${VERSION%%.*}.dylib" \
      "${LIB_DIR}/libmultitether.${VERSION}.dylib" "${PAYLOAD_DIR}/"
cp "${GUI_DIR}/Resources/com.multitether.helper.plist" "${PAYLOAD_DIR}/com.multitether.helper.plist"
cp "${SCRIPT_DIR}/install-helper.sh" "${SCRIPT_DIR}/uninstall-helper.sh" "${PAYLOAD_DIR}/"
chmod 755 "${PAYLOAD_DIR}/install-helper.sh" "${PAYLOAD_DIR}/uninstall-helper.sh"

rm -rf "${DIST_DIR}/helper"

embed_libusb() {
  local destination_dir="$1"
  local real_dylib
  real_dylib="$(ls "${destination_dir}"/libmultitether.*.*.*.dylib 2>/dev/null | head -1)"
  [[ -n "${real_dylib}" ]] || die "No versioned libmultitether library in ${destination_dir}"

  local libusb_path
  libusb_path="$(otool -L "${real_dylib}" | awk '/libusb-1\.0/ {print $1; exit}')"
  if [[ -z "${libusb_path}" || "${libusb_path}" == @* ]]; then
    return 0
  fi
  [[ -f "${libusb_path}" ]] || die "Required libusb library does not exist: ${libusb_path}"

  local libusb_name
  libusb_name="$(basename "${libusb_path}")"
  cp -a "${libusb_path}" "${destination_dir}/${libusb_name}"
  chmod u+w "${destination_dir}/${libusb_name}"

  install_name_tool -change "${libusb_path}" "@loader_path/${libusb_name}" "${real_dylib}"
  install_name_tool -id "@loader_path/${libusb_name}" "${destination_dir}/${libusb_name}"

  codesign --force --sign - --timestamp=none "${destination_dir}/${libusb_name}" >/dev/null
  codesign --force --sign - --timestamp=none "${real_dylib}" >/dev/null
}

log "Embedding libusb"
embed_libusb "${PAYLOAD_DIR}"

log "Removing development library paths"
strip_build_rpath() {
  local binary="$1"
  if otool -l "${binary}" | grep -q "path ${LIB_DIR} (offset"; then
    install_name_tool -delete_rpath "${LIB_DIR}" "${binary}"
    codesign --force --sign - --timestamp=none "${binary}" >/dev/null
  fi
}
strip_build_rpath "${APP_DIR}/Contents/MacOS/MultiTether"
strip_build_rpath "${PAYLOAD_DIR}/com.multitether.helper"

log "Signing locally"
codesign --force --sign - --timestamp=none "${PAYLOAD_DIR}/com.multitether.helper" >/dev/null
codesign --force --sign - --timestamp=none --deep "${APP_DIR}" >/dev/null

log "Validating bundle"
codesign --verify --deep --strict "${APP_DIR}" || die "App signature validation failed"

if otool -L "${PAYLOAD_DIR}"/libmultitether.*.*.*.dylib | grep -q '/opt/homebrew'; then
  die "The bundle still depends on Homebrew paths."
else
  echo "  No absolute Homebrew dependencies."
fi

cat <<EOF

Build complete:
  ${APP_DIR}
  (Helper payload: Contents/Library/HelperTools/)

Next:
  open ${APP_DIR}
  Click Install Helper in the app when the networking component is needed.
  Terminal installation: sudo ./gui/Scripts/install-helper.sh
EOF
