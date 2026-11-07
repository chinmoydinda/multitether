import Foundation
import Security
import MultiTetherCore
import MultiTetherIPC

final class HelperService: NSObject, MultiTetherHelperProtocol {
    // Serialize lifecycle, enumeration and DHCP so teardown cannot race configuration.
    private let lifecycleQueue = DispatchQueue(label: "com.multitether.helper.lifecycle")
    private let sessions = DeviceSessionManager { try MultiTetherSession(configuration: $0) }
    private let dhcp = PersistentDHCP()
    private var maintenance: DispatchSourceTimer?

    override init() {
        super.init()
        let timer = DispatchSource.makeTimerSource(queue: lifecycleQueue)
        timer.schedule(deadline: .now() + 2, repeating: 2)
        timer.setEventHandler { [weak self] in
            guard let self else { return }
            let active = Set(self.sessions.snapshots().filter { $0.status.runState == .running }
                .map { $0.status.systemInterface })
            self.dhcp.maintain(activeInterfaces: active)
        }
        maintenance = timer
        timer.resume()
    }

    func helperVersion(reply: @escaping (String) -> Void) {
        reply(HelperConstants.encodeVersion(HelperConstants.buildVersion))
    }

    func listDevices(reply: @escaping (Data?, String?) -> Void) {
        lifecycleQueue.async {
            do {
                self.respond(try MultiTetherLibrary.listDevices(readStrings: !self.sessions.holdsDevices), reply)
            } catch { reply(nil, error.localizedDescription) }
        }
    }

    func sessionStatuses(reply: @escaping (Data?, String?) -> Void) {
        respond(sessions.snapshots(), reply)
    }

    func queryNetwork(interface: String, reply: @escaping (Data?, String?) -> Void) {
        do { respond(try NetworkConfigurator.query(interface: interface), reply) }
        catch { reply(nil, error.localizedDescription) }
    }

    func startSession(configuration: Data, reply: @escaping (String?) -> Void) {
        let decoded: SessionConfiguration
        do { decoded = try JSONDecoder().decode(SessionConfiguration.self, from: configuration) }
        catch { reply(L(.helperRequestDecodeFailed, error.localizedDescription)); return }
        lifecycleQueue.async {
            if let existing = self.sessions.snapshots().first(where: { $0.id == decoded.deviceID }),
               existing.status.runState == .failed || existing.status.runState == .stopped,
               !existing.status.systemInterface.isEmpty {
                self.dhcp.stop(interface: existing.status.systemInterface)
            }
            do { try self.sessions.start(configuration: decoded); reply(nil) }
            catch { reply(error.localizedDescription) }
        }
    }

    func stopSession(deviceID: String, reply: @escaping (String?) -> Void) {
        lifecycleQueue.async {
            if let snapshot = self.sessions.snapshots().first(where: { $0.id == deviceID }) {
                self.dhcp.stop(interface: snapshot.status.systemInterface)
            }
            self.sessions.stop(deviceID: deviceID)
            reply(nil)
        }
    }

    func disconnectAll(reply: @escaping (String?) -> Void) {
        // Reply only after DHCP services, USB runtimes and their feth pairs are gone.
        lifecycleQueue.async {
            self.dhcp.stopAll()
            self.sessions.stopAll()
            reply(nil)
        }
    }

    func applyDHCP(interface: String, reply: @escaping (String?) -> Void) {
        lifecycleQueue.async {
            guard let snapshot = self.sessions.snapshots().first(where: {
                $0.status.runState == .running && $0.status.systemInterface == interface
            }) else { reply(L(.helperInterfaceNotOwned)); return }
            do {
                try self.dhcp.start(interface: interface, driverInterface: snapshot.status.driverInterface)
                reply(nil)
            } catch { reply(error.localizedDescription) }
        }
    }

    func shutdown() {
        maintenance?.cancel()
        lifecycleQueue.sync { dhcp.stopAll(); sessions.shutdown() }
    }

    private func respond<T: Encodable>(_ value: T, _ reply: (Data?, String?) -> Void) {
        do { reply(try JSONEncoder().encode(value), nil) }
        catch { reply(nil, L(.helperReplyEncodeFailed, error.localizedDescription)) }
    }
}

final class HelperListenerDelegate: NSObject, NSXPCListenerDelegate {
    private let service: HelperService

    init(service: HelperService) {
        self.service = service
    }

    func listener(_ listener: NSXPCListener,
                  shouldAcceptNewConnection connection: NSXPCConnection) -> Bool {
        guard let requirement = PinnedClientRequirement.loadTrustedFile(
            path: HelperConstants.clientRequirementPath) else { return false }
        connection.setCodeSigningRequirement(requirement)
        connection.exportedInterface = NSXPCInterface(with: MultiTetherHelperProtocol.self)
        connection.exportedObject = service
        connection.resume()
        return true
    }
}
