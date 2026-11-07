import AppKit
import CoreGraphics
import ImageIO
import UniformTypeIdentifiers

// One source for modern PNG and legacy Finder list-view representations.
let arguments = CommandLine.arguments
guard arguments.count == 3 else { fatalError("Usage: build-icon.swift source.png output.icns") }
let sourceURL = URL(fileURLWithPath: arguments[1])
let outputURL = URL(fileURLWithPath: arguments[2])
guard let source = CGImageSourceCreateWithURL(sourceURL as CFURL, nil),
      let artwork = CGImageSourceCreateImageAtIndex(source, 0, nil),
      let colorSpace = CGColorSpace(name: CGColorSpace.sRGB) else {
    fatalError("Cannot decode app icon")
}
func bigEndian(_ value: Int) -> Data {
    var number = UInt32(value).bigEndian
    return withUnsafeBytes(of: &number) { Data($0) }
}
func record(_ type: String, _ bytes: Data) -> Data {
    Data(type.utf8) + bigEndian(bytes.count + 8) + bytes
}
func raster(_ size: Int) -> (png: Data, rgba: [UInt8]) {
    guard let context = CGContext(data: nil, width: size, height: size,
        bitsPerComponent: 8, bytesPerRow: size * 4, space: colorSpace,
        bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue),
        let pixels = context.data else { fatalError("Cannot allocate icon image") }
    context.interpolationQuality = .high
    context.draw(artwork, in: CGRect(x: 0, y: 0, width: size, height: size))
    guard let image = context.makeImage() else { fatalError("Cannot render icon") }
    let png = NSMutableData()
    guard let destination = CGImageDestinationCreateWithData(png as CFMutableData, UTType.png.identifier as CFString, 1, nil)
        else { fatalError("Cannot encode icon PNG") }
    CGImageDestinationAddImage(destination, image, nil)
    guard CGImageDestinationFinalize(destination) else { fatalError("Cannot finish icon PNG") }
    var rgba = Array(UnsafeBufferPointer(start: pixels.assumingMemoryBound(to: UInt8.self), count: size * size * 4))
    // Legacy RGB planes use straight color; the alpha mask is stored separately.
    for offset in stride(from: 0, to: rgba.count, by: 4) {
        let alpha = Int(rgba[offset + 3])
        for channel in 0..<3 {
            rgba[offset + channel] = alpha == 0 ? 0 : UInt8(min(255, Int(rgba[offset + channel]) * 255 / alpha))
        }
    }
    return (png as Data, rgba)
}
var representations: [Int: (png: Data, rgba: [UInt8])] = [:]
for size in [16, 32, 48, 64, 128, 256, 512, 1024] { representations[size] = raster(size) }
var records = Data()
for (type, size) in [("icp4", 16), ("icp5", 32), ("icp6", 64), ("ic07", 128),
                     ("ic08", 256), ("ic09", 512), ("ic10", 1024),
                     ("ic11", 32), ("ic12", 64), ("ic13", 256), ("ic14", 512)] {
    records += record(type, representations[size]!.png)
}
for (rgbType, maskType, size) in [("is32", "s8mk", 16), ("il32", "l8mk", 32),
                                 ("ih32", "h8mk", 48), ("it32", "t8mk", 128)] {
    let rgba = representations[size]!.rgba
    var rgb = size == 128 ? Data(repeating: 0, count: 4) : Data()
    // Literal ICNS RLE blocks, one plane per color channel.
    for channel in 0..<3 {
        let plane = stride(from: channel, to: rgba.count, by: 4).map { rgba[$0] }
        var offset = 0
        while offset < plane.count {
            let end = min(offset + 128, plane.count)
            rgb.append(UInt8(end - offset - 1))
            rgb.append(contentsOf: plane[offset..<end])
            offset = end
        }
    }
    records += record(rgbType, rgb)
    records += record(maskType, Data(stride(from: 3, to: rgba.count, by: 4).map { rgba[$0] }))
}
try (Data("icns".utf8) + bigEndian(records.count + 8) + records).write(to: outputURL, options: .atomic)
print("Built app icon: 16–1024 pixels, Retina and legacy Finder representations")

// Precision geometric intertwined cable-M template PDF matching MultiTetherIcon.png.
// Uses circular tangent arcs, an over/under weave gap, and hollow ring terminals.
let menuURL = outputURL.deletingLastPathComponent().appendingPathComponent("MenuBarIcon.pdf")
var bounds = CGRect(x: 0, y: 0, width: 18, height: 18)
guard let consumer = CGDataConsumer(url: menuURL as CFURL),
      let menu = CGContext(consumer: consumer, mediaBox: &bounds, nil) else {
    fatalError("Cannot encode menu-bar icon")
}
menu.beginPDFPage(nil)
let black = CGColor(gray: 0, alpha: 1)
menu.setStrokeColor(black)
menu.setFillColor(black)
menu.setLineCap(.round)
menu.setLineJoin(.round)

let strokeWidth: CGFloat = 1.6
let leftFoot = CGPoint(x: 3.1, y: 3.9)
let rightFoot = CGPoint(x: 14.9, y: 3.9)
let footRadius: CGFloat = 1.4
let topRadius: CGFloat = 1.65
let bottomRadius: CGFloat = 1.8

let ringClip = CGMutablePath()
ringClip.addRect(bounds)
ringClip.addEllipse(in: CGRect(x: leftFoot.x - footRadius, y: leftFoot.y - footRadius, width: footRadius * 2, height: footRadius * 2))
ringClip.addEllipse(in: CGRect(x: rightFoot.x - footRadius, y: rightFoot.y - footRadius, width: footRadius * 2, height: footRadius * 2))
menu.addPath(ringClip)
menu.clip(using: .evenOdd)

menu.saveGState()
let underClip = CGMutablePath()
underClip.addRect(bounds)
let overMask = CGMutablePath()
overMask.move(to: CGPoint(x: 11.4, y: 12.6))
overMask.addLine(to: CGPoint(x: 6.7, y: 5.2))
underClip.addPath(overMask.copy(strokingWithWidth: strokeWidth + 1.4, lineCap: .round, lineJoin: .round, miterLimit: 4))
menu.addPath(underClip)
menu.clip(using: .evenOdd)

let underStrand = CGMutablePath()
underStrand.move(to: CGPoint(x: 3.25, y: 4.8))
underStrand.addArc(tangent1End: CGPoint(x: 5.25, y: 14.8), tangent2End: CGPoint(x: 11.3, y: 5.2), radius: topRadius)
underStrand.addArc(tangent1End: CGPoint(x: 11.3, y: 5.2), tangent2End: CGPoint(x: 16.0, y: 5.2), radius: bottomRadius)
underStrand.addLine(to: CGPoint(x: 14.6, y: 5.2))
menu.addPath(underStrand)
menu.setLineWidth(strokeWidth)
menu.strokePath()
menu.restoreGState()

let overStrand = CGMutablePath()
overStrand.move(to: CGPoint(x: 14.75, y: 4.8))
overStrand.addArc(tangent1End: CGPoint(x: 12.75, y: 14.8), tangent2End: CGPoint(x: 6.7, y: 5.2), radius: topRadius)
overStrand.addArc(tangent1End: CGPoint(x: 6.7, y: 5.2), tangent2End: CGPoint(x: 2.0, y: 5.2), radius: bottomRadius)
overStrand.addLine(to: CGPoint(x: 3.4, y: 5.2))
menu.addPath(overStrand)
menu.setLineWidth(strokeWidth)
menu.strokePath()

menu.resetClip()
menu.setLineWidth(1.35)
menu.strokeEllipse(in: CGRect(x: leftFoot.x - footRadius, y: leftFoot.y - footRadius, width: footRadius * 2, height: footRadius * 2))
menu.strokeEllipse(in: CGRect(x: rightFoot.x - footRadius, y: rightFoot.y - footRadius, width: footRadius * 2, height: footRadius * 2))
menu.endPDFPage()
menu.closePDF()
