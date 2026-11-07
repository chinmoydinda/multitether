import XCTest
@testable import MultiTetherIPC

final class HelperStartupTests: XCTestCase {
    private enum Failure: Error { case unavailable }

    func testCurrentHelperDoesNotRequestInstallation() async throws {
        var installations = 0
        let ready = try await HelperStartup.prepare(isReady: { true }, install: { installations += 1 })
        XCTAssertTrue(ready)
        XCTAssertEqual(installations, 0)
    }
    func testMissingHelperAtLaunchDoesNotRequestInstallation() async throws {
        var installations = 0
        let ready = try await HelperStartup.prepare(isReady: { false }, install: { installations += 1 })
        XCTAssertFalse(ready)
        XCTAssertEqual(installations, 0)
    }
    func testUnreachableHelperAtLaunchDoesNotRequestInstallation() async throws {
        var installations = 0
        let ready = try await HelperStartup.prepare(isReady: { throw Failure.unavailable },
                                                    install: { installations += 1 })
        XCTAssertFalse(ready)
        XCTAssertEqual(installations, 0)
    }
    func testInstallButtonPreparesMissingHelper() async throws {
        var installations = 0
        let ready = try await HelperStartup.prepare(userRequestedInstallation: true,
            isReady: { false }, install: { installations += 1 })
        XCTAssertTrue(ready)
        XCTAssertEqual(installations, 1)
    }
    func testInstallButtonReusesCurrentHelperWithoutAuthorization() async throws {
        var installations = 0
        let ready = try await HelperStartup.prepare(userRequestedInstallation: true,
            isReady: { true }, install: { installations += 1 })
        XCTAssertTrue(ready)
        XCTAssertEqual(installations, 0)
    }
    func testCancelledApprovalDoesNotLoopOrRetryAutomatically() async {
        var installations = 0
        do {
            try await HelperStartup.prepare(userRequestedInstallation: true, isReady: { false }, install: {
                installations += 1
                throw CancellationError()
            })
            XCTFail("Expected cancelled approval")
        } catch { XCTAssertTrue(error is CancellationError) }
        XCTAssertEqual(installations, 1)
    }
}
