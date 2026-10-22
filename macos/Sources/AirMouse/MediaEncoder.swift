import AppKit
import ImageIO
import UniformTypeIdentifiers

enum ImageFit: String, CaseIterable, Identifiable {
    case fit = "Fit", fill = "Fill / crop", stretch = "Resize"
    var id: String { rawValue }
}
struct PreparedMedia {
    let data: Data
    let preview: CGImage
    let frames: Int
    let duration: Double
    let sourceName: String
}
enum MediaEncoder {
    static let width = 205, height = 251, maxBytes = 3 * 1024 * 1024
    static func rectangle(source: CGSize, fit: ImageFit, cropX: Double, cropY: Double) -> CGRect {
        let w = Double(width), h = Double(height)
        if fit == .stretch { return CGRect(x:0,y:0,width:w,height:h) }
        let scale = fit == .fit ? min(w/source.width,h/source.height) : max(w/source.width,h/source.height)
        let sw = source.width*scale, sh = source.height*scale
        return CGRect(x:(w-sw)*cropX,y:(h-sh)*cropY,width:sw,height:sh)
    }
    static func rle(_ pixels: [UInt16]) -> Data {
        var data = Data(); var i = 0
        while i < pixels.count {
            let color = pixels[i]; var count = 1
            while i+count < pixels.count && pixels[i+count] == color && count < 65535 { count += 1 }
            Wire.put16(UInt16(count),into:&data); Wire.put16(color,into:&data); i += count
        }
        return data
    }
    static func prepare(url: URL, fit: ImageFit, cropX: Double, cropY: Double, animated: Bool = true) throws -> PreparedMedia {
        guard let source = CGImageSourceCreateWithURL(url as CFURL, nil), CGImageSourceGetCount(source) > 0 else { throw WatchError.message("Cannot read this image. Choose PNG, JPEG, HEIC, or GIF.") }
        let count = animated ? CGImageSourceGetCount(source) : 1
        guard count <= 1000 else { throw WatchError.message("This animation has more than 1,000 source frames. Trim it before uploading.") }
        var delays = [Double]()
        for i in 0..<count {
            let properties = CGImageSourceCopyPropertiesAtIndex(source,i,nil) as? [CFString:Any]
            let gif = properties?[kCGImagePropertyGIFDictionary] as? [CFString:Any]
            let delay = (gif?[kCGImagePropertyGIFUnclampedDelayTime] as? Double) ?? (gif?[kCGImagePropertyGIFDelayTime] as? Double) ?? 0.1
            delays.append(max(0.02,min(60,delay)))
        }
        let total = delays.reduce(0,+)
        guard count == 1 || total <= 60 else { throw WatchError.message("Trim the GIF to 60 seconds or less.") }
        // Sample the original timeline, retaining duration while limiting flash size and display work.
        let targetCount = count == 1 ? 1 : min(120,max(1,Int(floor(total*15))))
        let duration = count == 1 ? 60 : total / Double(targetCount)
        var data = Data("AMF1".utf8)
        Wire.put16(UInt16(width),into:&data); Wire.put16(UInt16(height),into:&data)
        Wire.put16(UInt16(targetCount),into:&data); Wire.put16(0,into:&data)
        var preview: CGImage?; var sourceIndex = 0; var end = delays[0]
        var cachedIndex = -1; var cachedFrame = Data()
        for frame in 0..<targetCount {
            let time = Double(frame)*duration
            while time >= end && sourceIndex < count-1 { sourceIndex += 1; end += delays[sourceIndex] }
            if cachedIndex != sourceIndex {
                let result: (Data, CGImage) = try autoreleasepool {
                    guard let image = CGImageSourceCreateImageAtIndex(source,sourceIndex,[kCGImageSourceShouldCache:false] as CFDictionary) else { throw WatchError.message("Could not decode frame \(sourceIndex+1).") }
                    guard image.width <= 16384, image.height <= 16384 else { throw WatchError.message("Resize images larger than 16,384 pixels first.") }
                    var rgba = [UInt8](repeating:0,count:width*height*4)
                    return try rgba.withUnsafeMutableBytes { bytes in
                        guard let ctx = CGContext(data:bytes.baseAddress,width:width,height:height,bitsPerComponent:8,bytesPerRow:width*4,space:CGColorSpaceCreateDeviceRGB(),bitmapInfo:CGImageAlphaInfo.premultipliedLast.rawValue | CGBitmapInfo.byteOrder32Big.rawValue) else { throw WatchError.message("Image memory allocation failed.") }
                        ctx.setFillColor(NSColor.black.cgColor); ctx.fill(CGRect(x:0,y:0,width:width,height:height))
                        ctx.interpolationQuality = .high
                        // A bitmap CGContext's CGImage draw already produces top-to-bottom storage.
                        // Adding an AppKit view-style Y flip would invert the uploaded pixels.
                        ctx.draw(image,in:rectangle(source:CGSize(width:image.width,height:image.height),fit:fit,cropX:cropX,cropY:cropY))
                        let raw = bytes.bindMemory(to:UInt8.self)
                        var rgb565 = [UInt16](); rgb565.reserveCapacity(width*height)
                        for pixel in 0..<width*height {
                            let i=pixel*4
                            rgb565.append(UInt16(raw[i] & 0xf8)<<8 | UInt16(raw[i+1] & 0xfc)<<3 | UInt16(raw[i+2])>>3)
                        }
                        guard let rendered = ctx.makeImage() else { throw WatchError.message("Could not render image preview.") }
                        return (rle(rgb565),rendered)
                    }
                }
                cachedIndex=sourceIndex; cachedFrame=result.0; if preview == nil { preview=result.1 }
            }
            Wire.put16(UInt16(max(67,min(60000,(duration*1000).rounded()))),into:&data)
            Wire.put32(UInt32(cachedFrame.count),into:&data); data.append(cachedFrame)
            guard data.count <= maxBytes else { throw WatchError.message("This animation exceeds the watch's 3 MiB limit. Use a shorter GIF, a simpler image, or turn off animation to send its first frame.") }
        }
        return PreparedMedia(data:data,preview:preview!,frames:targetCount,duration:count == 1 ? 0 : total,sourceName:url.lastPathComponent)
    }
}
