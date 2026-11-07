import XCTest
@testable import MultiTetherIPC

final class UsableNetworkTests: XCTestCase {
    private func network(_ address: String, router: String = "10.122.64.60", route: Bool = true) -> NetworkState {
        NetworkState(hasAddress: true, address: address, netmask: "255.255.255.0", router: router,
                     hasDefaultRoute: route)
    }
    func testRealPhoneLeaseIsUsable() {
        XCTAssertTrue(network("10.122.64.184").hasUsableIPv4Lease)
    }
    func testObservedSelfAssignedSamsungAddressIsNotConnected() {
        XCTAssertFalse(network("169.254.143.41").hasUsableIPv4Lease)
        XCTAssertFalse(network("169.254.143.41", router: "").hasUsableIPv4Lease)
    }
    func testMissingGatewayOrRouteIsNotReady() {
        XCTAssertFalse(network("10.122.64.184", router: "").hasUsableIPv4Lease)
        XCTAssertFalse(network("10.122.64.184", route: false).hasUsableIPv4Lease)
    }
    func testInvalidLoopbackAndMulticastAddressesAreNotUplinks() {
        for address in ["", "999.1.2.3", "0.0.0.0", "127.0.0.1", "224.1.2.3"] {
            XCTAssertFalse(network(address).hasUsableIPv4Lease)
        }
    }
}
