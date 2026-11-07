import Foundation

public enum NetworkOverlap {
    public enum Conflict: Equatable { case duplicateAddress, overlappingSubnet }

    public static func conflict(_ first: NetworkState, _ second: NetworkState) -> Conflict? {
        guard first.hasAddress, second.hasAddress,
              let a = address(first.address), let b = address(second.address),
              let am = address(first.netmask), let bm = address(second.netmask) else { return nil }
        if a == b { return .duplicateAddress }
        let aStart = a & am, aEnd = aStart | ~am
        let bStart = b & bm, bEnd = bStart | ~bm
        return aStart <= bEnd && bStart <= aEnd ? .overlappingSubnet : nil
    }

    private static func address(_ text: String) -> UInt32? {
        let parts = text.split(separator: ".", omittingEmptySubsequences: false)
        guard parts.count == 4 else { return nil }
        var result: UInt32 = 0
        for part in parts {
            guard !part.isEmpty, part.allSatisfy({ $0.isASCII && $0.isNumber }),
                  let value = UInt32(part), value <= 255 else { return nil }
            result = result << 8 | value
        }
        return result
    }
}
