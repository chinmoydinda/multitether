import Foundation
import Security

public enum PinnedClientRequirement {
    public static func make(cdhash: String) -> String? {
        guard cdhash.count == 40,
              cdhash.utf8.allSatisfy({ (48...57).contains($0) || (65...70).contains($0) || (97...102).contains($0) })
        else { return nil }
        return "identifier \"com.multitether\" and cdhash H\"\(cdhash)\""
    }

    public static func loadTrustedFile(path: String) -> String? {
        guard let attributes = try? FileManager.default.attributesOfItem(atPath: path),
              attributes[.type] as? FileAttributeType == .typeRegular,
              (attributes[.ownerAccountID] as? NSNumber)?.intValue == 0,
              let permissions = attributes[.posixPermissions] as? NSNumber,
              permissions.intValue & 0o022 == 0,
              let text = try? String(contentsOfFile: path, encoding: .utf8) else { return nil }
        let value = text.trimmingCharacters(in: .whitespacesAndNewlines)
        // Accept exactly our generated pin format, not an arbitrary permissive
        // expression even if a malformed file was accidentally installed.
        let prefix = "identifier \"com.multitether\" and cdhash H\""
        guard value.hasPrefix(prefix), value.hasSuffix("\"") else { return nil }
        let hash = String(value.dropFirst(prefix.count).dropLast())
        guard make(cdhash: hash) == value else { return nil }
        var requirement: SecRequirement?
        guard SecRequirementCreateWithString(value as CFString, [], &requirement) == errSecSuccess else { return nil }
        return value
    }
}
