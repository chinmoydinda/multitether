import Foundation
import MultiTetherIPC

final class HelperClient {
    enum Failure: LocalizedError {
        case unreachable(String)

        case helper(String)

        case malformedResponse

        var errorDescription: String? {
            switch self {
            case .unreachable(let detail):
                return L(.helperConnectFailed, detail)
            case .helper(let message):
                return message
            case .malformedResponse:
                return L(.helperReplyUnparsable)
            }
        }
    }

    private final class ContinuationGuard<T>: @unchecked Sendable {
        private let lock = NSLock()
        private var continuation: CheckedContinuation<T, Error>?

        init(_ continuation: CheckedContinuation<T, Error>) {
            self.continuation = continuation
        }

        func resume(returning value: T) {
            take()?.resume(returning: value)
        }

        func resume(throwing error: Error) {
            take()?.resume(throwing: error)
        }

        private func take() -> CheckedContinuation<T, Error>? {
            lock.withLock {
                defer { continuation = nil }
                return continuation
            }
        }
    }

    private let connectionLock = NSLock()
    private var cachedConnection: NSXPCConnection?

    deinit {
        cachedConnection?.invalidate()
    }

    private func connection() -> NSXPCConnection {
        connectionLock.withLock {
            if let existing = cachedConnection {
                return existing
            }
            let created = NSXPCConnection(machServiceName: HelperConstants.machServiceName,
                                          options: .privileged)
            created.remoteObjectInterface = NSXPCInterface(with: MultiTetherHelperProtocol.self)

            let invalidate: @Sendable () -> Void = { [weak self] in
                self?.connectionLock.withLock { self?.cachedConnection = nil }
            }
            created.invalidationHandler = invalidate
            created.interruptionHandler = invalidate

            created.resume()
            cachedConnection = created
            return created
        }
    }

    private func invoke<T>(
        _ body: @escaping (MultiTetherHelperProtocol, ContinuationGuard<T>) -> Void
    ) async throws -> T {
        try await withCheckedThrowingContinuation { continuation in
            let guarded = ContinuationGuard(continuation)
            let timeout = Failure.unreachable(L(.mtRequestTimeout))
            DispatchQueue.global().asyncAfter(deadline: .now() + HelperConstants.requestTimeout) {
                guarded.resume(throwing: timeout)
            }
            let proxy = connection().remoteObjectProxyWithErrorHandler { error in
                guarded.resume(throwing: Failure.unreachable(error.localizedDescription))
            }
            guard let typed = proxy as? MultiTetherHelperProtocol else {
                guarded.resume(throwing: Failure.malformedResponse)
                return
            }
            body(typed, guarded)
        }
    }

    private func decode<T: Decodable>(_ type: T.Type, data: Data?, message: String?,
                                      into guarded: ContinuationGuard<T>) {
        if let message {
            guarded.resume(throwing: Failure.helper(message))
            return
        }
        guard let data, let value = try? JSONDecoder().decode(type, from: data) else {
            guarded.resume(throwing: Failure.malformedResponse)
            return
        }
        guarded.resume(returning: value)
    }

    func helperVersion() async throws -> String {
        try await invoke { proxy, guarded in
            proxy.helperVersion { guarded.resume(returning: $0) }
        }
    }

    func listDevices() async throws -> [DeviceDescriptor] {
        try await invoke { proxy, guarded in
            proxy.listDevices { data, message in
                self.decode([DeviceDescriptor].self, data: data, message: message, into: guarded)
            }
        }
    }

    func sessionStatuses() async throws -> [DeviceSessionStatus] {
        try await invoke { proxy, guarded in
            proxy.sessionStatuses { data, message in
                self.decode([DeviceSessionStatus].self, data: data, message: message, into: guarded)
            }
        }
    }

    func queryNetwork(interface: String) async throws -> NetworkState {
        try await invoke { proxy, guarded in
            proxy.queryNetwork(interface: interface) { data, message in
                self.decode(NetworkState.self, data: data, message: message, into: guarded)
            }
        }
    }

    func startSession(configuration: SessionConfiguration) async throws {
        let payload = try JSONEncoder().encode(configuration)
        try await invokeVoid { proxy, guarded in
            proxy.startSession(configuration: payload) { Self.finish($0, guarded) }
        }
    }

    func stopSession(deviceID: String) async throws {
        try await invokeVoid { proxy, guarded in
            proxy.stopSession(deviceID: deviceID) { Self.finish($0, guarded) }
        }
    }

    func disconnectAll() async throws {
        try await invokeVoid { proxy, guarded in
            proxy.disconnectAll { Self.finish($0, guarded) }
        }
    }

    func applyDHCP(interface: String) async throws {
        try await invokeVoid { proxy, guarded in
            proxy.applyDHCP(interface: interface) { Self.finish($0, guarded) }
        }
    }

    private func invokeVoid(
        _ body: @escaping (MultiTetherHelperProtocol, ContinuationGuard<Void>) -> Void
    ) async throws {
        _ = try await invoke(body)
    }

    private static func finish(_ message: String?, _ guarded: ContinuationGuard<Void>) {
        if let message { guarded.resume(throwing: Failure.helper(message)) }
        else { guarded.resume(returning: ()) }
    }
}
