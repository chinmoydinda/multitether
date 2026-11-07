import CMultiTether
import MultiTetherIPC
import Foundation

public struct MultiTetherError: LocalizedError, Sendable {
    public let result: Int32
    public let message: String

    public var errorDescription: String? { message }

    init(result: Int32, error: tk_error_t) {
        self.result = result
        let text = String(fixedCArray: error.message)

        self.message = text.isEmpty ? L(.libraryGenericFailure, Int(result)) : text
    }
}

func check(_ result: tk_result_t, _ error: tk_error_t) throws {
    guard result != TK_OK else { return }
    throw MultiTetherError(result: result.rawValue, error: error)
}
