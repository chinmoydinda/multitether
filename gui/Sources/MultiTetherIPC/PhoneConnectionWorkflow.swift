import Foundation

public enum PhoneConnectionStage: Sendable {
    case connecting, configuring
}

/// The app uses XPC; tests supply a backend without USB or administrator access.
public protocol PhoneConnectionBackend {
    func start(_ configuration: SessionConfiguration) async throws
    func status(deviceID: String) async throws -> SessionStatus?
    func applyDHCP(interface: String) async throws
    func network(interface: String) async throws -> NetworkState
    func stop(deviceID: String) async throws
}

public enum PhoneConnectionWorkflow {
    public static func connect(
        configuration: SessionConfiguration,
        backend: PhoneConnectionBackend,
        maxPolls: Int = 100,
        maxLeasePolls: Int = 120,
        pause: () async throws -> Void = { try await Task.sleep(for: .milliseconds(250)) },
        progress: (PhoneConnectionStage) async -> Void
    ) async throws -> NetworkState {
        var startedHere = false
        var reachedRunning = false
        do {
            try Task.checkCancellation()
            await progress(.connecting)
            let existing = try await backend.status(deviceID: configuration.deviceID)
            if existing == nil || existing?.runState == .failed || existing?.runState == .stopped {
                try await backend.start(configuration)
                startedHere = true
            }
            var interface: String?
            for _ in 0..<maxPolls {
                try Task.checkCancellation()
                if let status = try await backend.status(deviceID: configuration.deviceID) {
                    if status.runState == .failed {
                        throw Failure.connection(status.fatalMessage.isEmpty
                            ? L(.mtConnectionFailed) : status.fatalMessage)
                    }
                    if status.runState == .running && !status.systemInterface.isEmpty {
                        interface = status.systemInterface
                        break
                    }
                    if status.runState == .stopped { throw Failure.connection(L(.mtConnectionStopped)) }
                }
                try await pause()
            }
            guard let interface else { throw Failure.connection(L(.mtConnectionTimeout)) }
            reachedRunning = true
            try Task.checkCancellation()
            await progress(.configuring)
            try await backend.applyDHCP(interface: interface)
            for _ in 0..<maxLeasePolls {
                try Task.checkCancellation()
                let result = try await backend.network(interface: interface)
                if result.hasUsableIPv4Lease { return result }
                try await pause()
            }
            throw Failure.connection(L(.mtDHCPStillWaiting))
        } catch {
            // A slow DHCP server must not cause its virtual interface to be
            // destroyed. Keep a running session/retained DHCP service alive for
            // late leases and Retry; still clean up failed handshakes/cancellation.
            if startedHere && (!reachedRunning || error is CancellationError) {
                try? await backend.stop(deviceID: configuration.deviceID)
            }
            throw error
        }
    }

    public enum Failure: LocalizedError {
        case connection(String)
        public var errorDescription: String? {
            switch self { case .connection(let message): return message }
        }
    }
}
