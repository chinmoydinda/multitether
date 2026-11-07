import XCTest
@testable import MultiTetherIPC

final class NetworkOverlapTests: XCTestCase {
    private func network(_ ip: String, _ mask: String = "255.255.255.0") -> NetworkState {
        NetworkState(hasAddress: true, address: ip, netmask: mask, router: "", hasDefaultRoute: true)
    }
    func testDuplicateAndSameSubnetAreDistinguished() {
        XCTAssertEqual(NetworkOverlap.conflict(network("192.168.42.2"), network("192.168.42.2")), .duplicateAddress)
        XCTAssertEqual(NetworkOverlap.conflict(network("192.168.42.2"), network("192.168.42.3")), .overlappingSubnet)
    }
    func testDifferentSubnetsAndNestedMasks() {
        XCTAssertEqual(NetworkOverlap.conflict(network("192.168.42.2"), network("10.0.0.2")), nil)
        XCTAssertEqual(NetworkOverlap.conflict(network("10.2.1.2", "255.0.0.0"), network("10.3.4.2")), .overlappingSubnet)
    }
    func testMissingAndInvalidAddressesAreIgnored() {
        XCTAssertEqual(NetworkOverlap.conflict(.empty, network("10.0.0.2")), nil)
        XCTAssertEqual(NetworkOverlap.conflict(network("999.0.0.2"), network("10.0.0.2")), nil)
    }
}
