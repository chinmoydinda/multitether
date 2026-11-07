# Changelog

## 0.2.6

- Refined panel controls to neutral primary/secondary system colors while retaining semantic connection and error status indicators.
- Updated the menu-bar template icon (`MenuBarIcon.pdf`) to a clean 1.6-point monoline `M` stroke with cable terminals.
- Removed unused upstream TetherKit telemetry/configuration fields and dead backend code, and bumped the XPC protocol revision to 8.

## 0.2.5

- Adopted system-controlled light/dark appearance and replaced the menu-bar text/symbol item with an 18-point template PDF `M` icon.
- Added `gui/Scripts/build-icon.swift` to generate `MultiTetherIcon.icns` with modern PNG, Retina, and legacy RLE representations (`16`–`1024` px) for Finder list view and Dock.

## 0.2.4

- Changed startup and polling to probe helper readiness without requesting administrator authorization automatically.
- Added the explicit **Install Helper** button in the panel so administrator approval is requested only when the user clicks to install or update the helper.

## 0.2.3

- Streamlined the repository and runtime to an English-only single menu-bar panel focused on independent multi-phone Android USB tethering.
- Cleaned up unused backend/IPC symbols, build scripts, and metadata for public release.

## 0.2.2

- Added synchronous quit cleanup (`disconnectAll`, XPC revision 7): normal app termination cancels pending connection tasks, releases per-interface DHCP services, and stops all active sessions before exiting.
- Kept app termination open with a retry prompt if helper cleanup fails, preventing orphaned active connections.

## 0.2.1

- Renamed app, helper, C/C++ backend, and Mach/file paths to MultiTether (`com.multitether`).
- Added automatic networking preparation on app launch (`NSApplicationDelegate`), reusing a compatible signed helper without prompting and requesting administrator approval only when installation or an update is required.
- Retained per-interface DHCP services across transient lease timeouts so late phone leases can complete without tearing down healthy sessions.
- Excluded self-assigned (`169.254.0.0/16`) link-local addresses from being reported as `Connected` and added subnet-overlap warnings across connected phones.
- Removed legacy upstream features outside MultiTether's scope: Chinese localization, CLI and benchmark targets, log viewer/feed, manual IP/DNS configuration, global default-route switching, and per-request authorization tokens.
