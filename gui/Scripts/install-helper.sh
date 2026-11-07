#!/usr/bin/env bash
# Usage: sudo ./gui/Scripts/install-helper.sh
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"

if [[ -n "${MULTITETHER_HELPER_DIR:-}" ]]; then
  SOURCE_DIR="${MULTITETHER_HELPER_DIR}"
elif [[ -f "${SCRIPT_DIR}/com.multitether.helper" ]]; then
  SOURCE_DIR="${SCRIPT_DIR}"
else
  SOURCE_DIR="$(cd -- "${SCRIPT_DIR}/../.." && pwd)/dist/MultiTether.app/Contents/Library/HelperTools"
fi

LABEL="com.multitether.helper"
TOOLS_DIR="/Library/PrivilegedHelperTools/com.multitether"
INSTALL_PATH="${TOOLS_DIR}/${LABEL}"
PLIST_PATH="/Library/LaunchDaemons/${LABEL}.plist"

log() { printf '\033[32m==>\033[0m %s\n' "$1"; }
die() { printf '\033[31mError: \033[0m%s\n' "$1" >&2; exit 1; }

[[ "${EUID}" -eq 0 ]] || die "Root is required. Run this script with sudo."
[[ -f "${SOURCE_DIR}/${LABEL}" ]] || die "Missing ${SOURCE_DIR}/${LABEL}
  Build first: ./gui/Scripts/build-gui.sh"

# Pin the administrator-approved app build. XPC validates this requirement on
# every message, so normal connections need no expiring admin authorization.
CLIENT_APP_DIR="$(cd "${SOURCE_DIR}/../../.." && pwd)"
/usr/bin/codesign --verify --deep --strict "${CLIENT_APP_DIR}"
CLIENT_HASH="$(/usr/bin/codesign -d --verbose=4 "${CLIENT_APP_DIR}" 2>&1 | /usr/bin/awk -F= '/^CDHash=/ {print $2; exit}')"
[[ "${CLIENT_HASH}" =~ ^[0-9a-fA-F]{40}$ ]] || die "Cannot identify the signed MultiTether app"

if launchctl print "system/${LABEL}" >/dev/null 2>&1; then
  OLD_PID="$(launchctl print "system/${LABEL}" | /usr/bin/awk '/^[[:space:]]*pid =/ { print $3; exit }')"
  log "Stopping the previous helper"
  launchctl bootout "system/${LABEL}"
  if [[ "${OLD_PID}" =~ ^[0-9]+$ ]]; then
    for ((attempt=0; attempt<60; attempt++)); do
      kill -0 "${OLD_PID}" 2>/dev/null || break
      sleep 0.25
    done
    if kill -0 "${OLD_PID}" 2>/dev/null; then
      die "The old helper is still stopping. Wait a moment and retry connecting."
    fi
  fi
fi

log "Installing to ${INSTALL_PATH}"
mkdir -p "${TOOLS_DIR}"
# Old library versions live only in this app's private directory. The daemon is stopped.
rm -f "${TOOLS_DIR}"/lib*.dylib

cp -aX "${SOURCE_DIR}/${LABEL}" "${INSTALL_PATH}"
cp -aX "${SOURCE_DIR}"/libmultitether*.dylib "${TOOLS_DIR}/"
if compgen -G "${SOURCE_DIR}/libusb-*.dylib" >/dev/null; then
  cp -aX "${SOURCE_DIR}"/libusb-*.dylib "${TOOLS_DIR}/"
fi

chown -R root:wheel "${TOOLS_DIR}"
chmod 755 "${TOOLS_DIR}"
printf 'identifier "com.multitether" and cdhash H"%s"\n' "${CLIENT_HASH}" > "${TOOLS_DIR}/client-requirement.txt"
chown root:wheel "${TOOLS_DIR}/client-requirement.txt"
chmod 644 "${TOOLS_DIR}/client-requirement.txt"
chmod 544 "${INSTALL_PATH}"
find "${TOOLS_DIR}" -maxdepth 1 -name '*.dylib' -type f -exec chmod 444 {} +

log "Installing ${PLIST_PATH}"
cp -X "${SOURCE_DIR}/${LABEL}.plist" "${PLIST_PATH}"
chown root:wheel "${PLIST_PATH}"
chmod 644 "${PLIST_PATH}"

log "Registering LaunchDaemon"
launchctl bootstrap system "${PLIST_PATH}"

if launchctl print "system/${LABEL}" >/dev/null 2>&1; then
  log "Done. MultiTether will detect the helper automatically."
else
  die "Service registration could not be verified. Check /var/log/multitether-helper.log"
fi

cat <<EOF

Installed:
  ${INSTALL_PATH}
  ${PLIST_PATH}

Logs:
  /var/log/multitether-helper.log

Uninstall:
  sudo "${SOURCE_DIR}/uninstall-helper.sh"
EOF
