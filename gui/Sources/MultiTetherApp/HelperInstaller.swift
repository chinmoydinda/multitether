import Foundation
import Security
import MultiTetherIPC

enum HelperInstaller {
    enum Action {
        case install
        case uninstall

        var flag: String {
            switch self {
            case .install: return "--install"
            case .uninstall: return "--uninstall"
            }
        }
    }

    enum Failure: LocalizedError {
        case payloadMissing

        case executorUnavailable

        case launchFailed(OSStatus)
        case userCancelled

        var errorDescription: String? {
            switch self {
            case .payloadMissing:
                return L(.installerNoPayload)
            case .executorUnavailable:
                return L(.installerAPIUnavailable)
            case .launchFailed(let status):
                return L(.installerLaunchFailed, Int(status))
            case .userCancelled:
                return L(.authorizationCancelled)
            }
        }
    }

    static var payloadDirectory: URL? {
        let directory = Bundle.main.bundleURL
            .appendingPathComponent("Contents/Library/HelperTools", isDirectory: true)

        let required = [helperBinaryName, installScriptName, uninstallScriptName]
            .map { directory.appendingPathComponent($0).path }
        let complete = required.allSatisfy { FileManager.default.isExecutableFile(atPath: $0) }
        return complete ? directory : nil
    }

    static func preflightError() -> Failure? {
        if payloadDirectory == nil { return .payloadMissing }
        if loadExecutor() == nil { return .executorUnavailable }
        return nil
    }

    static func run(_ action: Action, with token: AuthorizationToken) async throws -> String {
        guard let payload = payloadDirectory else { throw Failure.payloadMissing }
        guard let execute = loadExecutor() else { throw Failure.executorUnavailable }
        let installer = payload.appendingPathComponent(helperBinaryName).path

        return try await withCheckedThrowingContinuation { continuation in
            DispatchQueue.global(qos: .userInitiated).async {
                continuation.resume(with: Result {
                    try withExtendedLifetime(token) {
                        try run(execute, token: token, installer: installer,
                                flag: action.flag)
                    }
                })
            }
        }
    }

    private static let installScriptName = "install-helper.sh"
    private static let uninstallScriptName = "uninstall-helper.sh"

    private static let helperBinaryName = HelperConstants.machServiceName

    private typealias ExecuteFunction = @convention(c) (
        AuthorizationRef,
        UnsafePointer<CChar>,
        UInt32,
        UnsafePointer<UnsafeMutablePointer<CChar>?>,
        UnsafeMutablePointer<UnsafeMutablePointer<FILE>?>?
    ) -> OSStatus

    private static func loadExecutor() -> ExecuteFunction? {
        guard let symbol = dlsym(UnsafeMutableRawPointer(bitPattern: -2),
                                 "AuthorizationExecuteWithPrivileges") else { return nil }
        return unsafeBitCast(symbol, to: ExecuteFunction.self)
    }

    private static func run(_ execute: ExecuteFunction,
                            token: AuthorizationToken,
                            installer: String,
                            flag: String) throws -> String {
        var pipe: UnsafeMutablePointer<FILE>?
        let status = token.withReference { reference in
            withArgv([flag]) { argv in
                execute(reference, installer, 0 /* kAuthorizationFlagDefaults */,
                        argv, &pipe)
            }
        }

        switch status {
        case errAuthorizationSuccess:
            break
        case errAuthorizationCanceled:
            throw Failure.userCancelled
        default:
            throw Failure.launchFailed(status)
        }

        guard let pipe else { return "" }
        defer { fclose(pipe) }

        var output = Data()
        var buffer = [UInt8](repeating: 0, count: 4096)
        let descriptor = fileno(pipe)
        while true {
            let count = read(descriptor, &buffer, buffer.count)
            if count > 0 {
                output.append(buffer, count: count)
            } else if count == 0 {
                break
            } else if errno != EINTR {
                break
            }
        }
        return String(decoding: output, as: UTF8.self)
    }

    private static func withArgv<T>(
        _ arguments: [String],
        _ body: (UnsafePointer<UnsafeMutablePointer<CChar>?>) -> T
    ) -> T {
        var argv: [UnsafeMutablePointer<CChar>?] = arguments.map { strdup($0) }
        argv.append(nil)
        defer { argv.forEach { free($0) } }
        return argv.withUnsafeBufferPointer { body($0.baseAddress!) }
    }
}
