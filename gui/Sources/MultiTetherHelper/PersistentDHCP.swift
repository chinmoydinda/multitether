import Darwin
import Foundation
import SystemConfiguration
import MultiTetherIPC

/// A process-owned IPConfiguration service per interface. Unlike `ipconfig set`,
/// these references survive other phones being connected and network changes.
/// API contract: apple-oss-distributions/bootp/IPConfiguration_framework/.
final class PersistentDHCP {
    private typealias Create = @convention(c) (CFString, CFDictionary) -> Unmanaged<CFTypeRef>?
    private typealias NotificationKey = @convention(c) (CFTypeRef) -> Unmanaged<CFString>?
    private typealias Refresh = @convention(c) (CFTypeRef) -> Void
    private struct API {
        let library: UnsafeMutableRawPointer
        let create: Create
        let refresh: Refresh
        let notificationKey: NotificationKey
        let ipv4Key: CFString
    }
    private struct Service {
        let handle: CFTypeRef
        let driverInterface: String
    }
    private var services: [String: Service] = [:]
    private let api: API?

    init() {
        guard let library = dlopen("/System/Library/PrivateFrameworks/IPConfiguration.framework/IPConfiguration", RTLD_NOW | RTLD_LOCAL),
              let create = dlsym(library, "IPConfigurationServiceCreate"),
              let refresh = dlsym(library, "IPConfigurationServiceRefreshConfiguration"),
              let notificationKey = dlsym(library, "IPConfigurationServiceGetNotificationKey"),
              let key = dlsym(library, "kIPConfigurationServiceOptionIPv4Entity") else {
            api = nil; return
        }
        api = API(library: library, create: unsafeBitCast(create, to: Create.self),
                  refresh: unsafeBitCast(refresh, to: Refresh.self),
                  notificationKey: unsafeBitCast(notificationKey, to: NotificationKey.self),
                  ipv4Key: key.assumingMemoryBound(to: CFString.self).pointee)
    }

    /// Called only on the helper lifecycle queue. No waiting for a lease here;
    /// polling stays responsive while both phones obtain leases independently.
    func start(interface: String, driverInterface: String) throws {
        guard let api else { throw Failure.message(L(.mtPersistentDHCPUnavailable)) }
        try ensureUp(driverInterface)
        try ensureUp(interface)
        // Recreate any existing service handle so Retry after a link-local
        // fallback or recreated feth interface starts a fresh DHCP DISCOVER burst.
        services.removeValue(forKey: interface)
        // NoPublish=false asks IPConfiguration itself to publish the service's
        // IPv4/DNS state. This is not a fabricated SCDynamicStore-only service.
        let options: [String: Any] = [
            api.ipv4Key as String: [kSCPropNetIPv4ConfigMethod as String: kSCValNetIPv4ConfigMethodDHCP],
            "NoPublish": false
        ]
        guard let handle = api.create(interface as CFString, options as CFDictionary)?.takeRetainedValue() else {
            throw Failure.message(L(.mtPersistentDHCPFailed, interface))
        }
        guard let key = api.notificationKey(handle)?.takeUnretainedValue(),
              (key as String).hasPrefix("State:/Network/Service/") else {
            throw Failure.message(L(.mtDHCPPublishFailed, interface))
        }
        services[interface] = Service(handle: handle, driverInterface: driverInterface)
        // The network agent can bring the system side down while setting up the
        // service. Raising it again lets the initial DHCP exchange actually flow.
        try ensureUp(driverInterface)
        try ensureUp(interface)
        api.refresh(handle)
    }

    func stop(interface: String) { services.removeValue(forKey: interface) }
    func stopAll() { services.removeAll() }

    func maintain(activeInterfaces: Set<String>) {
        for interface in Array(services.keys) {
            guard activeInterfaces.contains(interface) else { stop(interface: interface); continue }
            guard let service = services[interface] else { continue }
            try? ensureUp(service.driverInterface)
            try? ensureUp(interface)
        }
    }

    private func ensureUp(_ interface: String) throws {
        guard interface.range(of: "^feth[0-9]{1,8}$", options: .regularExpression) != nil else {
            throw Failure.message(L(.mtPersistentDHCPFailed, interface))
        }
        var addresses: UnsafeMutablePointer<ifaddrs>?
        if getifaddrs(&addresses) == 0, let first = addresses {
            defer { freeifaddrs(first) }
            var pointer: UnsafeMutablePointer<ifaddrs>? = first
            while let item = pointer {
                if String(cString: item.pointee.ifa_name) == interface,
                   item.pointee.ifa_flags & UInt32(IFF_UP) != 0 { return }
                pointer = item.pointee.ifa_next
            }
        }
        let process = Process()
        process.executableURL = URL(fileURLWithPath: "/sbin/ifconfig")
        process.arguments = [interface, "up"]
        let output = Pipe()
        process.standardOutput = output; process.standardError = output
        try process.run()
        let data = output.fileHandleForReading.readDataToEndOfFile()
        process.waitUntilExit()
        guard process.terminationStatus == 0 else {
            throw Failure.message(L(.mtInterfaceUpFailed, interface, String(decoding: data, as: UTF8.self)))
        }
    }

    enum Failure: LocalizedError {
        case message(String)
        var errorDescription: String? { switch self { case .message(let text): return text } }
    }
}
