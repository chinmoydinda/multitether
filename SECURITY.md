# Security Policy

## Privilege Model

MultiTether splits responsibilities between an unprivileged menu-bar app (`MultiTether.app`) and a privileged LaunchDaemon (`com.multitether.helper`):

- **Root-owned client signature pin:** During installation (`gui/Scripts/install-helper.sh`), the installer verifies the app bundle's code signature and writes its exact CDHash requirement (`identifier "com.multitether" and cdhash H"<sha1>"`) to `/Library/PrivilegedHelperTools/com.multitether/client-requirement.txt` (`0644`, `root:wheel`).
- **Per-connection and per-message XPC validation:** The helper reads the pinned requirement from disk, applies it to every incoming `NSXPCConnection` before `resume()`, and validates the caller's code-signing requirement on every XPC message.
- **No stored credentials:** Administrator approval is requested only when installing, updating, or removing the helper. No passwords or blanket `sudo` rules are stored.
- **Scoped interface cleanup:** The helper tracks only `feth<number>` virtual interfaces it creates in `/var/run/multitether-interfaces` and refuses to modify or destroy any other network interface.

## Reporting a Vulnerability

Please report security issues privately through GitHub Security Advisories on this repository rather than opening a public issue. Include:

1. Affected version and macOS build.
2. Steps to reproduce or proof-of-concept details.
3. Expected vs. actual security boundary behavior.
