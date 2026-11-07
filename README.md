# MultiTether

MultiTether is a lightweight macOS menu-bar application for tethering multiple Android phones over USB simultaneously.

macOS does not include a built-in USB RNDIS driver for Android tethering. MultiTether provides a user-space RNDIS driver, per-device virtual Ethernet (`feth`) interfaces, and independent DHCP lease management through a single menu-bar panel.

---

## Features

- **Independent Multi-Phone Connections:** Connect and disconnect each Android phone individually without disrupting other active phone sessions.
- **Single Menu-Bar Panel:** View connected phones, live download/upload speeds, assigned IPv4 addresses, and virtual interface names (`feth*`) from the macOS menu bar.
- **Explicit Helper Installation:** Checks helper readiness on launch without prompting for administrator credentials. When installation or an update is needed, the panel displays an **Install Helper** button and requests administrator approval only when clicked.
- **Custom Menu-Bar & Finder Artwork:** Displays a clean monoline `M` template icon in the macOS menu bar and bundles full `16`–`1024` px, Retina, and legacy Finder list-view icon representations (`MultiTetherIcon.icns`) with neutral system light/dark appearance.
- **Strict Lease Validation:** Distinguishes real routable IPv4 leases from self-assigned link-local (`169.254.x.x`) addresses and warns when two phones share an identical IP or overlapping subnet.
- **Clean Shutdown:** Quitting the app cancels pending connection attempts, releases all DHCP services, and tears down every virtual interface before exiting.

---

## Requirements

- **macOS:** Apple Silicon Mac running macOS 27.0 or newer
- **Build Tools:** Xcode or Xcode Command Line Tools (`xcrun`, Swift, Clang with C++23 support)
- **Dependencies:** `cmake` and `libusb` (e.g., via Homebrew: `brew install cmake libusb`)

---

## Quick Start

### 1. Build the Application

```sh
./build.sh
```

This compiles the C++23 RNDIS/BPF/feth backend, builds the Swift menu-bar app and privileged helper, bundles `libusb`, and produces a signed application bundle at:

```text
dist/MultiTether.app
```

Pass `./build.sh --debug` for a Debug build.

### 2. Install and Run

1. Copy `dist/MultiTether.app` to `/Applications` and launch it.
2. Click the MultiTether icon in the menu bar. If the networking helper is not installed or needs an update, click **Install Helper** and approve the macOS administrator prompt.
3. Connect one or more Android phones with a USB data cable and enable **USB tethering** on each phone.
4. Click **Connect** on each phone card in the MultiTether panel.

To uninstall the privileged helper at any time, open the MultiTether panel and click **Remove networking helper…** in the footer.

---

## How It Works

MultiTether splits responsibilities across two processes communicating over Mach XPC (`com.multitether.helper`):

1. **Menu-Bar App (`MultiTetherApp`):**
   - Runs as the logged-in user with no root privileges.
   - Polls session states and network configuration over XPC and renders the menu-bar popover.
2. **Privileged Helper (`MultiTetherHelper` + `MultiTetherCore`):**
   - Runs as a `launchd` daemon (`root:wheel`) because creating `feth` interface pairs, opening `/dev/bpf*`, and managing persistent DHCP services require root privileges.
   - Claims the phone's USB RNDIS control and bulk data endpoints via `libusb`, pairs a dedicated `feth` interface (`feth0` ↔ `feth1`), bridges Ethernet frames between BPF and USB, and registers a DHCP service for the system-side interface.
   - Tracks created interfaces in `/var/run/multitether-interfaces` so leftover interfaces from a crash or power loss are cleaned up automatically on startup.

### Security Model

- **Pinned Code Requirement:** During helper installation, the installer extracts the app bundle's exact CDHash requirement and writes it to `/Library/PrivilegedHelperTools/com.multitether/client-requirement.txt` (`root:wheel`, mode `0644`).
- **Per-Connection & Per-Message Verification:** The helper applies this pinned requirement to every incoming `NSXPCConnection` before resuming it and verifies the caller on every XPC message.
- **Scoped Interface Operations:** Network queries, DHCP configuration, and interface teardown are restricted strictly to `feth<N>` interfaces owned by active MultiTether sessions.

---

## Project Structure

```text
.
├── build.sh                        # End-to-end C++, Swift, and .app bundle build script
├── CMakeLists.txt                  # CMake configuration for the C++23 backend and C API
├── include/multitether/            # C++ headers (RNDIS protocol, USB, BPF, feth, C API)
├── src/                            # C++ implementation (core runtime, RNDIS, USB, net, C API)
├── tests/                          # Doctest C++ unit and integration test suites
└── gui/
    ├── Package.swift               # Swift Package Manager manifest
    ├── Resources/                  # App icon and bundled third-party license notices
    ├── Scripts/                    # Helper install/uninstall scripts and portable test runner
    ├── Sources/
    │   ├── CMultiTether/           # C ABI module map for Swift interop
    │   ├── MultiTetherIPC/         # Shared XPC protocol, models, workflow, and code-signing pin
    │   ├── MultiTetherCore/        # Swift wrapper around the C++ RNDIS/network C API
    │   ├── MultiTetherHelper/      # Privileged launchd helper daemon and DHCP manager
    │   └── MultiTetherApp/         # SwiftUI menu-bar app, state model, and XPC client
    └── Tests/MultiTetherIPCTests/  # Swift regression tests for workflow, lifecycle, and IPC
```

---

## Development & Testing

All unit tests run without root privileges or attached phones:

```sh
# 1. Configure and build C++ backend and test suites with warnings-as-errors
cmake -S . -B build -DMULTITETHER_BUILD_TESTS=ON -DMULTITETHER_WARNINGS_AS_ERRORS=ON
cmake --build build -j6

# 2. Run C++ Doctest suites
ctest --test-dir build --output-on-failure

# 3. Run portable Swift regression tests (works with both Xcode and standalone Command Line Tools)
python3 gui/Scripts/test-portable.py
```

Helper logs are written to `/var/log/multitether-helper.log`.

---

## Credits & License

- **MultiTether** is released under the [MIT License](LICENSE).
- Based on [TetherKit](https://github.com/XiaoMiku01/TetherKit) by XiaoMiku01, licensed under the MIT License.
- Bundles [libusb](https://libusb.info/), licensed under the GNU Lesser General Public License v2.1 (`gui/Resources/libusb-COPYING.txt`).
