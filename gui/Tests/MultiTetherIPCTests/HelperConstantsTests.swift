import XCTest

@testable import MultiTetherIPC

final class HelperConstantsTests: XCTestCase {
    func testEncodeDecodeRoundTrip() {
        let encoded = HelperConstants.encodeVersion("MultiTether 0.1.4 (C++23, macOS 13.3+)")
        let (revision, version) = HelperConstants.decodeVersion(encoded)

        XCTAssertEqual(revision, HelperConstants.protocolRevision)
        XCTAssertEqual(version, "MultiTether 0.1.4 (C++23, macOS 13.3+)")
    }

    func testDecodeLegacyVersionWithoutRevision() {
        let (revision, version) =
            HelperConstants.decodeVersion("MultiTether 0.1.2 (C++23, macOS 13.3+)")

        XCTAssertEqual(revision, 0)
        XCTAssertEqual(version, "MultiTether 0.1.2 (C++23, macOS 13.3+)")
        XCTAssertTrue(revision != HelperConstants.protocolRevision)
    }
}
