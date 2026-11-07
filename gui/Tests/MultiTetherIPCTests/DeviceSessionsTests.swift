import Foundation
import XCTest
@testable import MultiTetherIPC

final class DeviceSessionsTests: XCTestCase {
    private final class FakeSession: ManagedDeviceSession {
        var snapshot = SessionStatus.idle
        var starts = 0
        var stops = 0
        var failStart = false
        var onStart: (() -> Void)?

        func start() throws {
            starts += 1
            onStart?()
            if failStart { throw TestFailure.start }
            snapshot.runState = .running
        }
        func stop() { stops += 1; snapshot.runState = .stopped }
        func status() -> SessionStatus { snapshot }
    }
    private enum TestFailure: Error { case start }
    private let first = SessionConfiguration(vendorID: 0x04e8, productID: 0x6863,
                                              busNumber: 1, deviceAddress: 1)
    private let second = SessionConfiguration(vendorID: 0x22d9, productID: 0x276a,
                                               busNumber: 0, deviceAddress: 6)

    func testQuitAlsoStopsStartingAndFailedSessions() throws {
        let a = FakeSession(), b = FakeSession()
        let manager = DeviceSessionManager { $0.deviceID == self.first.deviceID ? a : b }
        try manager.start(configuration: first)
        try manager.start(configuration: second)
        a.snapshot.runState = .starting
        b.snapshot.runState = .failed
        manager.stopAll()
        XCTAssertEqual(a.stops, 1)
        XCTAssertEqual(b.stops, 1)
        XCTAssertTrue(manager.snapshots().isEmpty)
    }

    func testQuitStopsEveryPhoneAndAllowsNextAppLaunchToConnect() throws {
        let a = FakeSession(), b = FakeSession(), replacement = FakeSession()
        var constructions = 0
        let manager = DeviceSessionManager { _ in
            constructions += 1
            return constructions == 1 ? a : (constructions == 2 ? b : replacement)
        }
        try manager.start(configuration: first)
        try manager.start(configuration: second)
        manager.stopAll()
        XCTAssertEqual(a.stops, 1)
        XCTAssertEqual(b.stops, 1)
        XCTAssertTrue(manager.snapshots().isEmpty)
        XCTAssertFalse(manager.holdsDevices)
        manager.stopAll()
        XCTAssertEqual(a.stops, 1)
        try manager.start(configuration: first)
        XCTAssertEqual(replacement.starts, 1)
        manager.shutdown()
    }

    func testStoppingOnePhoneKeepsOtherSessionAndInterface() throws {
        let a = FakeSession(), b = FakeSession()
        a.snapshot.systemInterface = "feth0"
        b.snapshot.systemInterface = "feth2"
        let manager = DeviceSessionManager { $0.deviceID == self.first.deviceID ? a : b }
        try manager.start(configuration: first)
        try manager.start(configuration: second)
        XCTAssertEqual(manager.snapshots().count, 2)
        manager.stop(deviceID: first.deviceID)
        XCTAssertEqual(a.stops, 1)
        XCTAssertEqual(b.stops, 0)
        XCTAssertEqual(manager.snapshots().first?.status.systemInterface, "feth2")
        XCTAssertEqual(manager.snapshots().first?.status.runState, .running)
        manager.stop(deviceID: "missing")
        XCTAssertEqual(b.stops, 0)
        manager.shutdown()
        XCTAssertEqual(b.stops, 1)
    }

    func testDuplicateDeviceCannotStartAnotherRuntimeIncludingBeforeHandshake() throws {
        let session = FakeSession()
        var constructions = 0
        let manager = DeviceSessionManager { _ in constructions += 1; return session }
        try manager.start(configuration: first)
        session.snapshot.runState = .starting
        XCTAssertThrowsError(try manager.start(configuration: first))
        session.snapshot.runState = .idle // Start() accepted but thread not scheduled yet.
        XCTAssertThrowsError(try manager.start(configuration: first))
        XCTAssertTrue(manager.holdsDevices)
        XCTAssertEqual(constructions, 1)
        manager.shutdown()
    }

    func testFailedPhoneCanReconnectWithoutStoppingHealthyPhone() throws {
        let a = FakeSession(), replacement = FakeSession(), b = FakeSession()
        var count = 0
        let manager = DeviceSessionManager { configuration in
            if configuration.deviceID == self.second.deviceID { return b }
            count += 1
            return count == 1 ? a : replacement
        }
        try manager.start(configuration: first)
        try manager.start(configuration: second)
        a.snapshot.runState = .failed
        try manager.start(configuration: first)
        XCTAssertEqual(a.stops, 1)
        XCTAssertEqual(replacement.starts, 1)
        XCTAssertEqual(b.stops, 0)
        XCTAssertEqual(manager.snapshots().count, 2)
        manager.shutdown()
    }

    func testStartFailureIsCleanedUpWithoutLosingOtherPhone() throws {
        let a = FakeSession(), b = FakeSession()
        b.failStart = true
        let manager = DeviceSessionManager { $0.deviceID == self.first.deviceID ? a : b }
        try manager.start(configuration: first)
        XCTAssertThrowsError(try manager.start(configuration: second))
        XCTAssertEqual(b.stops, 1)
        XCTAssertEqual(a.stops, 0)
        XCTAssertEqual(manager.snapshots().map(\.id), [first.deviceID])
        manager.shutdown()
    }

    func testIdenticalModelsHaveDifferentSessionIdentitiesAndBusZeroIsValid() throws {
        let manager = DeviceSessionManager { _ in FakeSession() }
        var other = first
        other.busNumber = 0
        other.deviceAddress = 6
        try manager.start(configuration: first)
        try manager.start(configuration: other)
        let snapshots = manager.snapshots()
        XCTAssertEqual(Set(snapshots.map(\.id)), ["1.1", "0.6"])
        let encoded = try JSONEncoder().encode(snapshots)
        let decoded = try JSONDecoder().decode([DeviceSessionStatus].self, from: encoded)
        XCTAssertEqual(decoded.map(\.configuration), snapshots.map(\.configuration))
        manager.shutdown()
    }

    func testAmbiguousSelectionIsRejectedBeforeConstructingRuntime() {
        var constructions = 0
        let manager = DeviceSessionManager { _ in constructions += 1; return FakeSession() }
        XCTAssertThrowsError(try manager.start(configuration: SessionConfiguration()))
        XCTAssertEqual(constructions, 0)
    }

    func testShutdownStopsEverySessionAndRejectsLateQueuedStarts() throws {
        var created: [FakeSession] = []
        let manager = DeviceSessionManager { _ in
            let session = FakeSession(); created.append(session); return session
        }
        try manager.start(configuration: first)
        try manager.start(configuration: second)
        manager.shutdown()
        manager.shutdown()
        XCTAssertEqual(created.map(\.stops), [1, 1])
        XCTAssertFalse(manager.holdsDevices)
        XCTAssertTrue(manager.snapshots().isEmpty)
        XCTAssertThrowsError(try manager.start(configuration: first))
    }

    func testPollingAndHeldFlagRemainAvailableDuringConstruction() throws {
        var manager: DeviceSessionManager!
        manager = DeviceSessionManager { _ in
            // If construction held the polling lock this would deadlock.
            XCTAssertTrue(manager.holdsDevices)
            XCTAssertTrue(manager.snapshots().isEmpty)
            return FakeSession()
        }
        try manager.start(configuration: second)
        manager.shutdown()
    }
}
