import Foundation

/// The helper owns one runtime per physical USB address. Keeping this independent of
/// libusb lets lifecycle isolation be tested without root or attached phones.
public protocol ManagedDeviceSession: AnyObject {
    func start() throws
    func stop()
    func status() -> SessionStatus
}

public struct DeviceSessionStatus: Codable, Identifiable, Sendable {
    public let configuration: SessionConfiguration
    public let status: SessionStatus
    public var id: String { configuration.deviceID }

    public init(configuration: SessionConfiguration, status: SessionStatus) {
        self.configuration = configuration
        self.status = status
    }
}

public extension SessionConfiguration {
    var deviceID: String { "\(busNumber).\(deviceAddress)" }
}

/// Lifecycle calls are serialized; polling remains available during start/stop.
/// All sessions live in ONE helper so a second helper cannot clean up live feths.
public final class DeviceSessionManager {
    private struct Entry {
        let configuration: SessionConfiguration
        let session: ManagedDeviceSession
    }

    private let lifecycleLock = NSLock()
    private let stateLock = NSLock()
    private var entries: [String: Entry] = [:]
    private var changing = false
    private var shuttingDown = false
    private let makeSession: (SessionConfiguration) throws -> ManagedDeviceSession

    public init(makeSession: @escaping (SessionConfiguration) throws -> ManagedDeviceSession) {
        self.makeSession = makeSession
    }

    public var holdsDevices: Bool {
        stateLock.withLock {
            changing || entries.values.contains { entry in
                let state = entry.session.status().runState
                return state == .idle || state == .running || state.isTransitional
            }
        }
    }

    public func start(configuration: SessionConfiguration) throws {
        try lifecycleLock.withLock {
            guard !shuttingDown else { throw Failure.shuttingDown }
            // USB bus zero is valid; only address zero means no explicit selection.
            guard configuration.deviceAddress != 0 else { throw Failure.selectDevice }
            let id = configuration.deviceID
            if let old = stateLock.withLock({ entries[id] }) {
                let state = old.session.status().runState
                guard state == .failed || state == .stopped else { throw Failure.alreadyRunning }
                old.session.stop()
                stateLock.withLock { _ = entries.removeValue(forKey: id) }
            }
            stateLock.withLock { changing = true }
            defer { stateLock.withLock { changing = false } }
            let session = try makeSession(configuration)
            do {
                try session.start()
            } catch {
                session.stop()
                throw error
            }
            stateLock.withLock { entries[id] = Entry(configuration: configuration, session: session) }
        }
    }

    public func stop(deviceID: String) {
        lifecycleLock.withLock {
            guard let entry = stateLock.withLock({ entries[deviceID] }) else { return }
            // Keep the entry visible until USB and feth teardown completes.
            entry.session.stop()
            stateLock.withLock { _ = entries.removeValue(forKey: deviceID) }
        }
    }

    public func snapshots() -> [DeviceSessionStatus] {
        stateLock.withLock {
            entries.values.map {
                DeviceSessionStatus(configuration: $0.configuration, status: $0.session.status())
            }.sorted { $0.id < $1.id }
        }
    }

    /// Disconnect all phones, but allow the helper to serve the next app launch.
    public func stopAll() {
        lifecycleLock.withLock { stopAllLocked() }
    }

    public func shutdown() {
        lifecycleLock.withLock {
            shuttingDown = true
            stopAllLocked()
        }
    }

    private func stopAllLocked() {
        let sessions = stateLock.withLock { Array(entries.values) }
        for entry in sessions { entry.session.stop() }
        stateLock.withLock { entries.removeAll() }
    }

    public enum Failure: LocalizedError {
        case selectDevice, alreadyRunning, shuttingDown

        public var errorDescription: String? {
            switch self {
            case .selectDevice: return L(.selectDeviceForSession)
            case .alreadyRunning: return L(.helperSessionAlreadyRunning)
            case .shuttingDown: return L(.helperShuttingDown)
            }
        }
    }
}
