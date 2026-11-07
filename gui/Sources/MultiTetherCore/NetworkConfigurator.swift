import CMultiTether
import Foundation
import MultiTetherIPC

public enum NetworkConfigurator {
    public static func query(interface: String) throws -> NetworkState {
        var raw = tk_net_state_t()
        var error = tk_error_t()
        let result = interface.withCString { tk_net_query($0, &raw, &error) }
        try check(result, error)

        return NetworkState(
            hasAddress: raw.has_address,
            address: String(fixedCArray: raw.address),
            netmask: String(fixedCArray: raw.netmask),
            router: String(fixedCArray: raw.router),
            hasDefaultRoute: raw.has_default_route)
    }
}

