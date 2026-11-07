import Foundation
import Security

public final class AuthorizationToken: @unchecked Sendable {
    private let authorization: AuthorizationRef

    init(authorization: AuthorizationRef) {
        self.authorization = authorization
    }

    public func withReference<T>(_ body: (AuthorizationRef) throws -> T) rethrows -> T {
        try body(authorization)
    }

    deinit {
        AuthorizationFree(authorization, [.destroyRights])
    }
}

public enum AuthorizationBroker {
    public enum Failure: LocalizedError {
        case userCancelled
        case denied(OSStatus)
        case internalFailure(OSStatus)

        public var errorDescription: String? {
            switch self {
            case .userCancelled:
                return L(.authorizationCancelled)
            case .denied(let status):
                return L(.authorizationDenied, Int(status))
            case .internalFailure(let status):
                return L(.authorizationSessionFailed, Int(status))
            }
        }
    }

    public static func requestAuthorization(
        right: String = HelperConstants.privilegedRightName,
        prompt: String? = nil
    ) throws -> AuthorizationToken {
        var authorization: AuthorizationRef?
        let createStatus = AuthorizationCreate(nil, nil, [], &authorization)
        guard createStatus == errAuthorizationSuccess, let authorization else {
            throw Failure.internalFailure(createStatus)
        }

        var handedOff = false
        defer {
            if !handedOff {
                AuthorizationFree(authorization, [.destroyRights])
            }
        }

        let copyStatus = copyRights(authorization, right: right, prompt: prompt)
        guard copyStatus == errAuthorizationSuccess else {
            throw copyStatus == errAuthorizationCanceled
                ? Failure.userCancelled
                : Failure.denied(copyStatus)
        }

        handedOff = true
        return AuthorizationToken(authorization: authorization)
    }

    private static func copyRights(_ authorization: AuthorizationRef,
                                   right: String,
                                   prompt: String?) -> OSStatus {
        var name = Array(right.utf8CString)
        var promptKey = Array(kAuthorizationEnvironmentPrompt.utf8CString)
        var promptValue = Array(prompt?.utf8 ?? "".utf8)

        return name.withUnsafeMutableBufferPointer { nameBuffer in
            var rightItem = AuthorizationItem(name: nameBuffer.baseAddress!, valueLength: 0,
                                              value: nil, flags: 0)
            return withUnsafeMutablePointer(to: &rightItem) { rightPointer in
                var rights = AuthorizationRights(count: 1, items: rightPointer)

                let flags: AuthorizationFlags = [.extendRights, .interactionAllowed, .preAuthorize]

                guard prompt != nil else {
                    return AuthorizationCopyRights(authorization, &rights, nil, flags, nil)
                }
                return promptKey.withUnsafeMutableBufferPointer { keyBuffer in
                    promptValue.withUnsafeMutableBufferPointer { valueBuffer in
                        var promptItem = AuthorizationItem(
                            name: keyBuffer.baseAddress!,
                            valueLength: valueBuffer.count,
                            value: valueBuffer.baseAddress,
                            flags: 0)
                        return withUnsafeMutablePointer(to: &promptItem) { promptPointer in
                            var environment = AuthorizationEnvironment(count: 1,
                                                                       items: promptPointer)
                            return AuthorizationCopyRights(authorization, &rights, &environment,
                                                           flags, nil)
                        }
                    }
                }
            }
        }
    }
}

