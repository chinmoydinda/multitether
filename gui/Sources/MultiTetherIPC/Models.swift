import Foundation

public struct SessionConfiguration: Codable, Hashable, Sendable {
    public var vendorID: UInt16
    public var productID: UInt16

    public var busNumber: UInt8
    public var deviceAddress: UInt8

    public init(vendorID: UInt16 = 0,
                productID: UInt16 = 0,
                busNumber: UInt8 = 0,
                deviceAddress: UInt8 = 0) {
        self.vendorID = vendorID
        self.productID = productID
        self.busNumber = busNumber
        self.deviceAddress = deviceAddress
    }
}

public enum RunState: Int32, Codable, Sendable {
    case idle = 0
    case starting = 1
    case running = 2
    case stopping = 3
    case stopped = 4
    case failed = 5

    public var isTransitional: Bool { self == .starting || self == .stopping }
}

public struct SessionStatus: Codable, Hashable, Sendable {
    public var runState: RunState

    public var systemInterface: String

    public var driverInterface: String

    public var deviceDescription: String

    public var rxBytes: UInt64
    public var txBytes: UInt64

    public var monotonicNanos: Int64

    public var fatalMessage: String

    public static let idle = SessionStatus(
        runState: .idle,
        systemInterface: "", driverInterface: "",
        deviceDescription: "",
        rxBytes: 0, txBytes: 0,
        monotonicNanos: 0, fatalMessage: "")

    public init(runState: RunState,
                systemInterface: String, driverInterface: String,
                deviceDescription: String, rxBytes: UInt64, txBytes: UInt64,
                monotonicNanos: Int64, fatalMessage: String) {
        self.runState = runState
        self.systemInterface = systemInterface
        self.driverInterface = driverInterface
        self.deviceDescription = deviceDescription
        self.rxBytes = rxBytes
        self.txBytes = txBytes
        self.monotonicNanos = monotonicNanos
        self.fatalMessage = fatalMessage
    }
}

public struct DeviceDescriptor: Codable, Hashable, Identifiable, Sendable {
    public var vendorID: UInt16
    public var productID: UInt16
    public var busNumber: UInt8
    public var deviceAddress: UInt8

    public var manufacturer: String
    public var product: String

    public var id: String { "\(busNumber).\(deviceAddress)" }

    public var displayName: String {
        if !product.isEmpty {
            return manufacturer.isEmpty ? product : "\(manufacturer) \(product)"
        }
        return L(.usbDeviceFallbackName, Int(vendorID), Int(productID))
    }

    public init(vendorID: UInt16, productID: UInt16, busNumber: UInt8, deviceAddress: UInt8,
                manufacturer: String, product: String) {
        self.vendorID = vendorID
        self.productID = productID
        self.busNumber = busNumber
        self.deviceAddress = deviceAddress
        self.manufacturer = manufacturer
        self.product = product
    }
}

public struct NetworkState: Codable, Hashable, Sendable {
    public var hasAddress: Bool
    public var address: String
    public var netmask: String
    public var router: String
    public var hasDefaultRoute: Bool

    public static let empty = NetworkState(
        hasAddress: false, address: "", netmask: "", router: "", hasDefaultRoute: false)

    public init(hasAddress: Bool, address: String, netmask: String, router: String,
                hasDefaultRoute: Bool) {
        self.hasAddress = hasAddress
        self.address = address
        self.netmask = netmask
        self.router = router
        self.hasDefaultRoute = hasDefaultRoute
    }
}

