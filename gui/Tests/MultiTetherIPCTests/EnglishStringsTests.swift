import XCTest
@testable import MultiTetherIPC
final class EnglishStringsTests: XCTestCase {
    func testEveryStringIsNonemptyAndEnglishOnly() {
        for key in L10nKey.allCases {
            XCTAssertFalse(key.text.isEmpty)
            XCTAssertFalse(key.text.unicodeScalars.contains { (0x3400...0x9fff).contains($0.value) })
        }
    }
    func testDeviceAndErrorFormatting() {
        XCTAssertTrue(L(.mtDuplicateIP, "Phone B").contains("Phone B"))
        XCTAssertTrue(L(.authorizationDenied, 42).contains("42"))
    }
}
