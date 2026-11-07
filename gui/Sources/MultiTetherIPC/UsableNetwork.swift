import Foundation

public extension NetworkState {
    /// A self-assigned/link-local IP is not a DHCP uplink; never treat it as Connected.
    var hasUsableIPv4Lease: Bool {
        hasAddress && hasDefaultRoute && Self.isUsable(address) && Self.isUsable(router)
    }

    private static func isUsable(_ value: String) -> Bool {
        let parts = value.split(separator: ".", omittingEmptySubsequences: false)
        guard parts.count == 4 else { return false }
        let bytes = parts.compactMap { UInt8($0) }
        guard bytes.count == 4 else { return false }
        return bytes[0] != 0 && bytes[0] != 127 && bytes[0] < 224
            && !(bytes[0] == 169 && bytes[1] == 254)
    }
}
