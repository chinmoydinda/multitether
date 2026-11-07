import CMultiTether
import Foundation
import MultiTetherIPC

public final class MultiTetherSession: ManagedDeviceSession {
    private let handle: OpaquePointer

    public init(configuration: SessionConfiguration) throws {
        var raw = tk_session_config_t()

        tk_session_config_init(&raw)
        raw.vendor_id = configuration.vendorID
        raw.product_id = configuration.productID
        raw.bus_number = configuration.busNumber
        raw.device_address = configuration.deviceAddress

        var error = tk_error_t()
        guard let created = tk_session_create(&raw, &error) else {
            throw MultiTetherError(result: TK_ERR_FAILED.rawValue, error: error)
        }
        handle = created
    }

    deinit {
        tk_session_destroy(handle)
    }

    public func start() throws {
        var error = tk_error_t()
        let result = tk_session_start(handle, &error)
        try check(result, error)
    }

    public func stop() {
        _ = tk_session_stop(handle)
    }

    public func status() -> SessionStatus {
        var raw = tk_session_status_t()
        guard tk_session_status_get(handle, &raw) == TK_OK else {
            return .idle
        }
        return SessionStatus(
            runState: RunState(rawValue: raw.run_state) ?? .idle,
            systemInterface: String(fixedCArray: raw.system_interface),
            driverInterface: String(fixedCArray: raw.driver_interface),
            deviceDescription: String(fixedCArray: raw.device_description),
            rxBytes: raw.rx_bytes,
            txBytes: raw.tx_bytes,
            monotonicNanos: raw.monotonic_nanos,
            fatalMessage: String(fixedCArray: raw.fatal))
    }
}
