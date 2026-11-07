import Foundation

public enum HelperConstants {
    public static let requestTimeout: TimeInterval = 30
    public static let clientRequirementPath = "/Library/PrivilegedHelperTools/com.multitether/client-requirement.txt"
    public static let buildVersion = "MultiTether 0.2.6"
    public static let machServiceName = "com.multitether.helper"
    public static let privilegedRightName = "system.privilege.admin"

    // Revision 8 trims unused TetherKit telemetry/config fields from XPC payloads.
    public static let protocolRevision = 8

    public static func encodeVersion(_ version: String) -> String {
        "\(protocolRevision)|\(version)"
    }

    public static func decodeVersion(_ encoded: String) -> (revision: Int, version: String) {
        guard let separator = encoded.firstIndex(of: "|"),
              let revision = Int(encoded[encoded.startIndex..<separator]) else {
            return (0, encoded)
        }
        return (revision, String(encoded[encoded.index(after: separator)...]))
    }
}
