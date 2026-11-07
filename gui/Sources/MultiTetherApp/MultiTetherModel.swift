import Foundation
import Observation
import MultiTetherIPC

private struct DeviceBackend: PhoneConnectionBackend {
    let client: HelperClient
    func start(_ configuration: SessionConfiguration) async throws {
        try await client.startSession(configuration: configuration)
    }
    func status(deviceID: String) async throws -> SessionStatus? {
        try await client.sessionStatuses().first { $0.id == deviceID }?.status
    }
    func applyDHCP(interface: String) async throws {
        try await client.applyDHCP(interface: interface)
    }
    func network(interface: String) async throws -> NetworkState {
        try await client.queryNetwork(interface: interface)
    }
    func stop(deviceID: String) async throws {
        try await client.stopSession(deviceID: deviceID)
    }
}

struct PhoneRate {
    var download: Double = 0
    var upload: Double = 0
    static let zero = PhoneRate()
}

enum PhoneOperation { case connecting, configuring, disconnecting }

@Observable @MainActor
final class MultiTetherModel {
    var confirmRemoveHelper = false
    private(set) var devices: [DeviceDescriptor] = []
    private(set) var sessions: [String: DeviceSessionStatus] = [:]
    private(set) var networks: [String: NetworkState] = [:]
    private(set) var rates: [String: PhoneRate] = [:]
    private(set) var operations: [String: PhoneOperation] = [:]
    private(set) var errors: [String: String] = [:]
    private(set) var quitting = false
    private(set) var helperReady = false
    private(set) var starting = true
    private(set) var updatingHelper = false
    private(set) var serviceError: String?
    private let client = HelperClient()
    private var polling: Task<Void, Never>?
    private var actionTasks: [String: Task<Void, Never>] = [:]
    private var hasUsedHelper = false
    private var refreshing = false
    private var lastEnumeration = Date.distantPast
    private var previous: [String: SessionStatus] = [:]
    private var knownDevices: [String: DeviceDescriptor] = [:]

    var connectedCount: Int {
        guard helperReady else { return 0 }
        return sessions.values.filter { $0.status.runState == .running && networks[$0.id]?.hasUsableIPv4Lease == true }.count
    }
    var totalRate: PhoneRate {
        rates.values.reduce(.zero) { PhoneRate(download: $0.download + $1.download, upload: $0.upload + $1.upload) }
    }
    var anyOperation: Bool { !operations.isEmpty }

    func start(prepareNetworking: Bool = true) {
        guard polling == nil else { return }
        polling = Task { [weak self] in
            guard let self else { return }
            if prepareNetworking { await self.prepareNetworking() }
            while !Task.isCancelled {
                await self.refresh()
                try? await Task.sleep(for: .seconds(self.sessions.isEmpty && !self.anyOperation ? 2 : 1))
            }
        }
    }

    func installHelper() async {
        guard !quitting, actionTasks["helper"] == nil else { return }
        let task = Task { await self.performInstallHelper() }
        actionTasks["helper"] = task
        await task.value
        actionTasks.removeValue(forKey: "helper")
    }

    private func performInstallHelper() async {
        guard !starting, !updatingHelper, !anyOperation else { return }
        await prepareNetworking(userRequestedInstallation: true)
    }

    private func prepareNetworking(userRequestedInstallation: Bool = false) async {
        starting = true
        serviceError = nil
        helperReady = false
        defer { starting = false }
        do {
            _ = try await HelperStartup.prepare(userRequestedInstallation: userRequestedInstallation, isReady: {
                let current = try await self.helperIsCurrent()
                self.helperReady = current
                if current { self.hasUsedHelper = true }
                return current
            }, install: { try await self.ensureHelper() })
            await refresh(forceDevices: true)
        } catch { serviceError = error.localizedDescription }
    }

    private func helperIsCurrent() async throws -> Bool {
        let encoded = try await client.helperVersion()
        let version = HelperConstants.decodeVersion(encoded)
        return version.revision == HelperConstants.protocolRevision
            && version.version == HelperConstants.buildVersion
    }

    func refresh(forceDevices: Bool = false) async {
        guard !quitting, !refreshing else { return }
        refreshing = true
        defer { refreshing = false }
        do {
            guard try await helperIsCurrent() else {
                helperReady = false
                return
            }
            helperReady = true
            hasUsedHelper = true
            serviceError = nil
            let snapshots = try await client.sessionStatuses()
            let next = Dictionary(uniqueKeysWithValues: snapshots.map { ($0.id, $0) })
            let now = Date()
            for snapshot in snapshots {
                let id = snapshot.id, current = snapshot.status
                rates[id] = Self.rate(previous: previous[id], current: current)
                previous[id] = current
                if current.runState == .running {
                    if !current.systemInterface.isEmpty,
                       let network = try? await client.queryNetwork(interface: current.systemInterface) {
                        networks[id] = network
                        if network.hasUsableIPv4Lease { errors.removeValue(forKey: id) }
                    }
                } else {
                    rates[id] = .zero
                    networks.removeValue(forKey: id)
                }
            }
            sessions = next
            rates = rates.filter { next[$0.key] != nil }
            previous = previous.filter { next[$0.key] != nil }
            networks = networks.filter { next[$0.key] != nil }
            if forceDevices || now.timeIntervalSince(lastEnumeration) >= 2 {
                let fresh = try await client.listDevices()
                let freshIDs = Set(fresh.map(\.id))
                for device in fresh { knownDevices[device.id] = device }
                // A vanished phone's failed row remains reachable until disconnected.
                for snapshot in snapshots where knownDevices[snapshot.id] == nil {
                    let c = snapshot.configuration
                    knownDevices[snapshot.id] = DeviceDescriptor(vendorID: c.vendorID, productID: c.productID,
                        busNumber: c.busNumber, deviceAddress: c.deviceAddress, manufacturer: "",
                        product: snapshot.status.deviceDescription)
                }
                let retained = next.keys.sorted().filter { !freshIDs.contains($0) }.compactMap { knownDevices[$0] }
                devices = fresh + retained
                let visibleIDs = freshIDs.union(next.keys)
                knownDevices = knownDevices.filter { visibleIDs.contains($0.key) }
                errors = errors.filter { visibleIDs.contains($0.key) }
                lastEnumeration = now
            }
        } catch {
            if helperReady { serviceError = error.localizedDescription }
            helperReady = false
            rates.removeAll()
        }
    }

    private static func rate(previous: SessionStatus?, current: SessionStatus) -> PhoneRate {
        guard let previous, previous.runState == .running, current.runState == .running,
              current.systemInterface == previous.systemInterface,
              current.monotonicNanos > previous.monotonicNanos,
              current.rxBytes >= previous.rxBytes, current.txBytes >= previous.txBytes else { return .zero }
        let seconds = Double(current.monotonicNanos - previous.monotonicNanos) / 1_000_000_000
        return PhoneRate(download: Double(current.rxBytes - previous.rxBytes) * 8 / seconds,
                         upload: Double(current.txBytes - previous.txBytes) * 8 / seconds)
    }

    func isConnected(_ id: String) -> Bool {
        helperReady && sessions[id]?.status.runState == .running && networks[id]?.hasUsableIPv4Lease == true
    }
    func statusText(_ id: String) -> String {
        if let operation = operations[id] {
            switch operation {
            case .connecting: return L(.mtConnecting)
            case .configuring: return L(.mtConfiguring)
            case .disconnecting: return L(.mtDisconnecting)
            }
        }
        if errors[id] != nil || sessions[id]?.status.runState == .failed { return L(.mtConnectionFailed) }
        if isConnected(id) { return L(.mtConnected) }
        if sessions[id]?.status.runState == .running { return L(.mtNeedsSetup) }
        return L(.mtReady)
    }
    func errorText(_ id: String) -> String? {
        let message = errors[id] ?? sessions[id]?.status.fatalMessage
        guard let message, !message.isEmpty else { return nil }
        if message.contains("LIBUSB_ERROR_ACCESS") || message.contains("claim") {
            return L(.mtUSBConflict) + "\n" + message
        }
        return message
    }
    func warningText(_ id: String) -> String? {
        guard let network = networks[id] else { return nil }
        if network.address.hasPrefix("169.254.") { return L(.mtLinkLocalOnly) }
        for otherID in networks.keys.sorted() where otherID != id {
            guard let other = networks[otherID], let conflict = NetworkOverlap.conflict(network, other) else { continue }
            let name = devices.first { $0.id == otherID }?.displayName ?? otherID
            return L(conflict == .duplicateAddress ? .mtDuplicateIP : .mtOverlappingSubnet, name)
        }
        return nil
    }

    func connect(_ device: DeviceDescriptor) async {
        guard !quitting, actionTasks[device.id] == nil else { return }
        let task = Task { await self.performConnect(device) }
        actionTasks[device.id] = task
        await task.value
        actionTasks.removeValue(forKey: device.id)
    }

    private func performConnect(_ device: DeviceDescriptor) async {
        let id = device.id
        guard operations[id] == nil, helperReady, !starting, !updatingHelper else { return }
        operations[id] = .connecting
        errors.removeValue(forKey: id)
        defer { operations.removeValue(forKey: id) }
        let configuration = SessionConfiguration(vendorID: device.vendorID, productID: device.productID,
                                                 busNumber: device.busNumber, deviceAddress: device.deviceAddress)
        do {
            let backend = DeviceBackend(client: client)
            let network = try await PhoneConnectionWorkflow.connect(configuration: configuration,
                backend: backend, progress: { [weak self] stage in
                    await MainActor.run {
                        self?.operations[id] = stage == .connecting ? .connecting : .configuring
                    }
                })
            networks[id] = network
        } catch { errors[id] = error.localizedDescription }
        await refresh(forceDevices: true)
    }

    func disconnect(_ device: DeviceDescriptor) async {
        guard !quitting, actionTasks[device.id] == nil else { return }
        let task = Task { await self.performDisconnect(device) }
        actionTasks[device.id] = task
        await task.value
        actionTasks.removeValue(forKey: device.id)
    }

    private func performDisconnect(_ device: DeviceDescriptor) async {
        let id = device.id
        guard operations[id] == nil, helperReady, !starting, !updatingHelper else { return }
        operations[id] = .disconnecting
        defer { operations.removeValue(forKey: id) }
        do {
            try await client.stopSession(deviceID: id)
            errors.removeValue(forKey: id)
            networks.removeValue(forKey: id)
            rates.removeValue(forKey: id)
        } catch { errors[id] = error.localizedDescription }
        await refresh(forceDevices: true)
    }

    // Only Install Helper requests installation; launch and polling merely probe readiness.
    // The administrator-approved signature pin remains enforced on all XPC messages.
    private func ensureHelper() async throws {
        if helperReady { return }
        guard !updatingHelper else {
            throw HelperClient.Failure.helper(L(.mtHelperUpdating))
        }
        updatingHelper = true
        defer { updatingHelper = false }
        if let error = HelperInstaller.preflightError() { throw error }
        let authorization = try AuthorizationBroker.requestAuthorization(prompt: L(.authPromptInstall))
        defer { withExtendedLifetime(authorization) {} }
        try Task.checkCancellation()
        let output = try await HelperInstaller.run(.install, with: authorization)
        hasUsedHelper = true
        try Task.checkCancellation()
        for _ in 0..<12 {
            try Task.checkCancellation()
            await refresh(forceDevices: true)
            if helperReady { return }
            try await Task.sleep(for: .milliseconds(500))
        }
        throw HelperClient.Failure.helper(L(.mtHelperUnreachable) + "\n" + Self.cleanOutput(output))
    }

    func removeHelper() async {
        guard !quitting, actionTasks["helper"] == nil else { return }
        let task = Task { await self.performRemoveHelper() }
        actionTasks["helper"] = task
        await task.value
        actionTasks.removeValue(forKey: "helper")
    }

    private func performRemoveHelper() async {
        guard !updatingHelper, !anyOperation else { return }
        updatingHelper = true
        defer { updatingHelper = false }
        do {
            try Task.checkCancellation()
            let authorization = try AuthorizationBroker.requestAuthorization(prompt: L(.authPromptUninstall))
            defer { withExtendedLifetime(authorization) {} }
            let output = try await HelperInstaller.run(.uninstall, with: authorization)
            hasUsedHelper = false
            helperReady = false
            sessions.removeAll(); networks.removeAll(); rates.removeAll(); previous.removeAll()
            await refresh()
            if helperReady { serviceError = Self.cleanOutput(output) }
        } catch AuthorizationBroker.Failure.userCancelled { }
        catch HelperInstaller.Failure.userCancelled { }
        catch { serviceError = error.localizedDescription }
    }

    func prepareForQuit() async throws {
        quitting = true
        polling?.cancel()
        let pending = Array(actionTasks.values)
        pending.forEach { $0.cancel() }
        // Drain accepted starts/DHCP work before sending the helper's final lifecycle barrier.
        for task in pending { await task.value }
        await polling?.value
        if hasUsedHelper {
            try await client.disconnectAll()
            guard try await client.sessionStatuses().isEmpty else {
                throw HelperClient.Failure.helper(L(.mtQuitStillConnected))
            }
        }
        sessions.removeAll()
        networks.removeAll()
        rates.removeAll()
    }

    func cancelQuit() {
        quitting = false
        starting = false
        polling = nil
        start(prepareNetworking: false)
    }

    private static func cleanOutput(_ output: String) -> String {
        output.replacingOccurrences(of: "\u{1B}\\[[0-9;]*m", with: "", options: .regularExpression)
            .split(separator: "\n").suffix(12).joined(separator: "\n")
    }
}
