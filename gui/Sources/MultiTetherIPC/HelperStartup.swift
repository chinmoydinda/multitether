import Foundation

/// Startup only probes readiness. Installation requires an explicit user action.
public enum HelperStartup {
    @discardableResult
    public static func prepare(
        userRequestedInstallation: Bool = false,
        isReady: () async throws -> Bool,
        install: () async throws -> Void
    ) async throws -> Bool {
        try Task.checkCancellation()
        if (try? await isReady()) == true { return true }
        guard userRequestedInstallation else { return false }
        try Task.checkCancellation()
        try await install()
        return true
    }
}
