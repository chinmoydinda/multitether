import CMultiTether
import Foundation

extension String {
    init<T>(fixedCArray tuple: T) {
        var mutableCopy = tuple
        self = withUnsafeBytes(of: &mutableCopy) { raw in
            let bytes = raw.prefix { $0 != 0 }
            return String(decoding: bytes, as: UTF8.self)
        }
    }
}
