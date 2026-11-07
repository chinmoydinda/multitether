import Foundation
import XCTest
@testable import MultiTetherIPC

final class PhoneConnectionWorkflowTests: XCTestCase {
    private enum TestFailure: Error { case start, dhcp }
    private final class Backend: PhoneConnectionBackend {
        var states: [SessionStatus?] = [nil]
        var reads = 0
        var starts: [SessionConfiguration] = []
        var applied: [String] = []
        var stopped: [String] = []
        var networkReads = 0
        var networkResults: [NetworkState]?
        var failStart = false
        var failDHCP = false
        var result = NetworkState(hasAddress: true, address: "10.0.0.2", netmask: "255.255.255.0",
            router: "10.0.0.1", hasDefaultRoute: true)
        func start(_ configuration: SessionConfiguration) async throws {
            if failStart { throw TestFailure.start }; starts.append(configuration)
        }
        func status(deviceID: String) async throws -> SessionStatus? {
            defer { reads += 1 }; return states[min(reads, states.count - 1)]
        }
        func applyDHCP(interface: String) async throws {
            applied.append(interface); if failDHCP { throw TestFailure.dhcp }
        }
        func network(interface: String) async throws -> NetworkState {
            defer { networkReads += 1 }
            if let results = networkResults { return results[min(networkReads, results.count - 1)] }
            return result
        }
        func stop(deviceID: String) async throws { stopped.append(deviceID) }
    }
    private let config = SessionConfiguration(vendorID: 0x22d9, productID: 0x276a,
                                               busNumber: 0, deviceAddress: 6)
    private var running: SessionStatus {
        var s = SessionStatus.idle; s.runState = .running; s.systemInterface = "feth2"; return s
    }
    private func connect(_ b: Backend, polls: Int = 5, leases: Int = 5,
                         pause: () async throws -> Void = {}) async throws -> NetworkState {
        try await PhoneConnectionWorkflow.connect(configuration: config, backend: b,
            maxPolls: polls, maxLeasePolls: leases, pause: pause, progress: { _ in })
    }
    func testConnectAutomaticallyAppliesDHCPToTargetPhone() async throws {
        let b = Backend(); b.states = [nil, .idle, running]
        let network = try await connect(b)
        XCTAssertTrue(network.hasAddress)
        XCTAssertEqual(b.starts, [config]); XCTAssertEqual(b.applied, ["feth2"])
        XCTAssertTrue(b.stopped.isEmpty)
    }
    func testRetryReusesExistingConnection() async throws {
        let b = Backend(); b.states = [running]
        _ = try await connect(b)
        XCTAssertTrue(b.starts.isEmpty); XCTAssertEqual(b.applied, ["feth2"])
        XCTAssertTrue(b.stopped.isEmpty)
    }
    func testRejectedStartDoesNotStopAnUnownedSession() async {
        let b = Backend(); b.failStart = true
        do { _ = try await connect(b); XCTFail("Expected rejection") } catch { }
        XCTAssertTrue(b.stopped.isEmpty); XCTAssertTrue(b.applied.isEmpty)
    }
    func testUSBFailureCleansUpOnlyTargetPhone() async {
        let b = Backend(); var failed = SessionStatus.idle
        failed.runState = .failed; failed.fatalMessage = "LIBUSB_ERROR_ACCESS"
        b.states = [nil, failed]
        do { _ = try await connect(b); XCTFail("Expected USB failure") }
        catch { XCTAssertTrue(error.localizedDescription.contains("LIBUSB_ERROR_ACCESS")) }
        XCTAssertEqual(b.stopped, ["0.6"]); XCTAssertTrue(b.applied.isEmpty)
    }
    func testDHCPFailureKeepsNewSessionForRetry() async {
        let b = Backend(); b.states = [nil, running]; b.failDHCP = true
        do { _ = try await connect(b); XCTFail("Expected DHCP failure") } catch { }
        XCTAssertTrue(b.stopped.isEmpty)
    }
    func testDHCPFailureKeepsExistingSession() async {
        let b = Backend(); b.states = [running]; b.failDHCP = true
        do { _ = try await connect(b); XCTFail("Expected DHCP failure") } catch { }
        XCTAssertTrue(b.stopped.isEmpty)
    }
    func testMissingIPIsNotReportedAsConnected() async {
        let b = Backend(); b.states = [nil, running]; b.result = .empty
        do { _ = try await connect(b); XCTFail("Expected no address") } catch { }
        XCTAssertTrue(b.stopped.isEmpty)
        XCTAssertEqual(b.networkReads, 5)
    }
    func testTimeoutIsBoundedAndCleansUpTarget() async {
        let b = Backend(); b.states = [nil, .idle]
        do { _ = try await connect(b, polls: 3); XCTFail("Expected timeout") } catch { }
        XCTAssertEqual(b.reads, 4); XCTAssertEqual(b.stopped, ["0.6"])
        XCTAssertTrue(b.applied.isEmpty)
    }
    func testCancellationCleansUpTarget() async {
        let b = Backend(); b.states = [nil, .idle]
        do {
            _ = try await connect(b, pause: { throw CancellationError() })
            XCTFail("Expected cancellation")
        } catch { XCTAssertTrue(error is CancellationError) }
        XCTAssertEqual(b.stopped, ["0.6"]); XCTAssertTrue(b.applied.isEmpty)
    }
    func testLateDHCPLeaseSucceedsWithoutRestartingThePhone() async throws {
        let b = Backend(); b.states = [nil, running]
        b.networkResults = [.empty, .empty, b.result]
        let result = try await connect(b)
        XCTAssertTrue(result.hasAddress)
        XCTAssertEqual(b.networkReads, 3)
        XCTAssertEqual(b.starts.count, 1)
        XCTAssertTrue(b.stopped.isEmpty)
    }
}
