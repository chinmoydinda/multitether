import Foundation

/// Every message is checked against the administrator-installed app signature.
/// Administrator authorization is reserved for installing/removing the helper.
@objc public protocol MultiTetherHelperProtocol {
    func helperVersion(reply: @escaping (String) -> Void)
    func listDevices(reply: @escaping (Data?, String?) -> Void)
    func sessionStatuses(reply: @escaping (Data?, String?) -> Void)
    func queryNetwork(interface: String, reply: @escaping (Data?, String?) -> Void)
    func startSession(configuration: Data, reply: @escaping (String?) -> Void)
    func stopSession(deviceID: String, reply: @escaping (String?) -> Void)
    func disconnectAll(reply: @escaping (String?) -> Void)
    func applyDHCP(interface: String, reply: @escaping (String?) -> Void)
}
