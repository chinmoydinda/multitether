#!/usr/bin/env bash
# Usage: sudo ./gui/Scripts/uninstall-helper.sh
set -euo pipefail

LABEL="com.multitether.helper"
TOOLS_DIR="/Library/PrivilegedHelperTools/com.multitether"
INSTALL_PATH="${TOOLS_DIR}/${LABEL}"
PLIST_PATH="/Library/LaunchDaemons/${LABEL}.plist"
STATE_FILE="/var/run/multitether-interfaces"

log() { printf '\033[32m==>\033[0m %s\n' "$1"; }

[[ "${EUID}" -eq 0 ]] || { printf 'Root is required. Run with sudo.\n' >&2; exit 1; }

if launchctl print "system/${LABEL}" >/dev/null 2>&1; then
  log "Stopping and unregistering the service"
  launchctl bootout "system/${LABEL}" 2>/dev/null || true
  sleep 1
fi

log "Removing files"
rm -f "${PLIST_PATH}" "${INSTALL_PATH}" "${TOOLS_DIR}/client-requirement.txt"
rm -f "${TOOLS_DIR}"/lib*.dylib

if [[ -f "${STATE_FILE}" ]]; then
  while read -r interface; do
    [[ "${interface}" =~ ^feth[0-9]+$ ]] || continue
    log "Destroying orphan interface ${interface}"
    ifconfig "${interface}" destroy 2>/dev/null || true
  done < "${STATE_FILE}"
  rm -f "${STATE_FILE}"
fi

log "Uninstalled"
