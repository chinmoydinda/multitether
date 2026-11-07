// swift-tools-version: 5.9

// Usage:
//   export MULTITETHER_LIB_DIR=$PWD/build/lib
//   swift build --package-path gui

import Foundation
import PackageDescription

let libraryDirectory: String = {
    if let fromEnvironment = ProcessInfo.processInfo.environment["MULTITETHER_LIB_DIR"],
       !fromEnvironment.isEmpty {
        return fromEnvironment
    }
    let packageDirectory = URL(fileURLWithPath: #filePath).deletingLastPathComponent()
    return packageDirectory
        .deletingLastPathComponent()
        .appendingPathComponent("build/lib")
        .path
}()

let multitetherLinkerSettings: [LinkerSetting] = [
    .unsafeFlags([
        "-L\(libraryDirectory)",
        "-Xlinker", "-rpath", "-Xlinker", "@executable_path/../Library/HelperTools",
        "-Xlinker", "-rpath", "-Xlinker", "@executable_path",
        "-Xlinker", "-rpath", "-Xlinker", libraryDirectory,
    ]),
    .linkedLibrary("multitether"),
]

let package = Package(
    name: "MultiTether",
    platforms: [.macOS("27.0")],
    products: [
        .executable(name: "MultiTetherApp", targets: ["MultiTetherApp"]),
        .executable(name: "multitether-helper", targets: ["MultiTetherHelper"]),
    ],
    targets: [
        .target(name: "CMultiTether"),
        .target(name: "MultiTetherIPC"),
        .target(name: "MultiTetherCore",
                dependencies: ["CMultiTether", "MultiTetherIPC"],
                linkerSettings: multitetherLinkerSettings),
        .executableTarget(name: "MultiTetherHelper",
                          dependencies: ["MultiTetherCore", "MultiTetherIPC"]),
        .executableTarget(name: "MultiTetherApp",
                          dependencies: ["MultiTetherIPC"]),
        .testTarget(name: "MultiTetherIPCTests", dependencies: ["MultiTetherIPC"]),
    ])
