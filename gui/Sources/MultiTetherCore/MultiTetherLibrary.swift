import CMultiTether
import Foundation
import MultiTetherIPC

public enum MultiTetherLibrary {
    private static let deviceCapacity = 16

    public static func listDevices(readStrings: Bool = true) throws -> [DeviceDescriptor] {
        var buffer = [tk_device_info_t](repeating: tk_device_info_t(), count: deviceCapacity)
        var count = 0
        var error = tk_error_t()

        let result = tk_list_devices(&buffer, deviceCapacity, &count, readStrings, &error)
        try check(result, error)

        return buffer.prefix(min(count, deviceCapacity)).map { raw in
            DeviceDescriptor(
                vendorID: raw.vendor_id,
                productID: raw.product_id,
                busNumber: raw.bus_number,
                deviceAddress: raw.device_address,
                manufacturer: String(fixedCArray: raw.manufacturer),
                product: String(fixedCArray: raw.product))
        }
    }

    @discardableResult
    public static func cleanupOrphanInterfaces() throws -> Int {
        var removed = 0
        var error = tk_error_t()
        let result = tk_cleanup_orphan_interfaces(&removed, &error)
        try check(result, error)
        return removed
    }
}
