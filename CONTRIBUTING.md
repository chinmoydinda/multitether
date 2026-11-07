# Contributing

## Requirements

- Apple Silicon Mac running macOS 27.0 SDK (Xcode or standalone Command Line Tools)
- CMake 3.24+
- `libusb` (`brew install libusb`)

## Building

Build the C++ backend, Swift app, privileged helper, and signed `dist/MultiTether.app` bundle:

```sh
./build.sh
```

Use `./build.sh --debug` for a debug build.

## Running Tests

Run the portable C++ and Swift test suites (no root privileges or connected phones required):

```sh
cmake -S . -B build -DMULTITETHER_BUILD_TESTS=ON -DMULTITETHER_WARNINGS_AS_ERRORS=ON
cmake --build build -j6
ctest --test-dir build --output-on-failure
python3 gui/Scripts/test-portable.py
```

## Guidelines

- **English-only:** Keep all user-facing strings, diagnostics, comments, and documentation in English.
- **Zero-allocation data path:** Do not allocate memory or format strings on the packet RX/TX path (`Bridge`, `FrameRing`, `PacketMessageReader`, `PacketMessageWriter`, `BpfLink`, `UsbDataChannel`).
- **ABI & XPC contracts:** Preserve `static_assert` size/offset checks in `darwin_abi.h` and `protocol.h`. Increment `HelperConstants.protocolRevision` whenever XPC methods or payload signatures change, and keep `CMakeLists.txt` and `HelperConstants.buildVersion` versions aligned.
- **Security:** Never weaken the root-owned `client-requirement.txt` code-signing pin or XPC transport verification.
