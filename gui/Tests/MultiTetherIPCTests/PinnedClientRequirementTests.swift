import Foundation
import Security
import XCTest
@testable import MultiTetherIPC

final class PinnedClientRequirementTests: XCTestCase {
    func testValidAppPinIsAParseableCodeRequirement() {
        let value = PinnedClientRequirement.make(cdhash: String(repeating: "ab", count: 20))
        XCTAssertTrue(value != nil)
        var requirement: SecRequirement?
        let result = SecRequirementCreateWithString((value ?? "") as CFString, [], &requirement)
        XCTAssertEqual(result, errSecSuccess)
        XCTAssertTrue(requirement != nil)
    }
    func testRequirementInjectionAndMalformedHashesAreRejected() {
        for hash in ["", "abc", String(repeating: "g", count: 40), String(repeating: "a", count: 39),
                     String(repeating: "a", count: 41), "abc\" or true", String(repeating: " ", count: 40)] {
            XCTAssertEqual(PinnedClientRequirement.make(cdhash: hash), nil)
        }
    }
    func testMissingOrUserOwnedPinCannotAuthorizeAClient() throws {
        let path = FileManager.default.temporaryDirectory.appendingPathComponent(UUID().uuidString)
        XCTAssertEqual(PinnedClientRequirement.loadTrustedFile(path: path.path), nil)
        defer { try? FileManager.default.removeItem(at: path) }
        let value = PinnedClientRequirement.make(cdhash: String(repeating: "ab", count: 20)) ?? ""
        try value.write(to: path, atomically: true, encoding: .utf8)
        XCTAssertEqual(PinnedClientRequirement.loadTrustedFile(path: path.path), nil)
    }
}
