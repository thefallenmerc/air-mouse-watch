import XCTest
import AppKit
import ImageIO
import UniformTypeIdentifiers
@testable import AirMouse

final class CodecTests: XCTestCase {
    func testSettingsWireFormat() throws {
        var settings=MouseSettings(); let data=settings.encoded()
        XCTAssertEqual(data.count,36); XCTAssertEqual(try MouseSettings(data:data),settings)
        XCTAssertEqual(Array(data.prefix(8)),[1,0,2,0,0,0,96,65]) // 14.0 float, little endian
        settings.axisX=0; settings.axisY=3; XCTAssertFalse(settings.valid)
        settings=MouseSettings(); settings.gyroGain = .nan
        XCTAssertThrowsError(try MouseSettings(data:settings.encoded()))
        for n in 0..<36 { XCTAssertThrowsError(try MouseSettings(data:data.prefix(n))) }
        XCTAssertEqual(Wire.crc32(Data("123456789".utf8)),0xcbf43926)
    }
    func testImageGeometry() {
        let source=CGSize(width:1000,height:500)
        let fit=MediaEncoder.rectangle(source:source,fit:.fit,cropX:0.5,cropY:0.5)
        XCTAssertEqual(fit.width,205); XCTAssertEqual(fit.height,102.5)
        let fill=MediaEncoder.rectangle(source:source,fit:.fill,cropX:0.5,cropY:0.5)
        XCTAssertEqual(fill.height,251); XCTAssertLessThan(fill.minX,0)
        let crop=MediaEncoder.rectangle(source:source,fit:.fill,cropX:0,cropY:1)
        XCTAssertEqual(crop.minX,0)
        XCTAssertEqual(MediaEncoder.rectangle(source:source,fit:.stretch,cropX:0.5,cropY:0.5).size,CGSize(width:205,height:251))
    }
    func testRLE() {
        let encoded=MediaEncoder.rle([0xf800,0xf800,0x07e0])
        XCTAssertEqual(Array(encoded),[2,0,0,248,1,0,224,7])
        XCTAssertTrue(MediaEncoder.rle([]).isEmpty)
        let big=MediaEncoder.rle(Array(repeating:UInt16(3),count:70000))
        XCTAssertEqual(big.count,8); XCTAssertEqual(Wire.u16(big,0),65535); XCTAssertEqual(Wire.u16(big,4),4465)
    }
    func image(_ color: NSColor) -> CGImage {
        let ctx=CGContext(data:nil,width:20,height:20,bitsPerComponent:8,bytesPerRow:80,space:CGColorSpaceCreateDeviceRGB(),bitmapInfo:CGImageAlphaInfo.premultipliedLast.rawValue)!
        ctx.setFillColor(color.cgColor); ctx.fill(CGRect(x:0,y:0,width:20,height:20)); return ctx.makeImage()!
    }
    func testGIFTimelineAndCrossLanguageArtifact() throws {
        let folder=FileManager.default.temporaryDirectory.appendingPathComponent(UUID().uuidString)
        try FileManager.default.createDirectory(at:folder,withIntermediateDirectories:true); defer {try? FileManager.default.removeItem(at:folder)}
        let url=folder.appendingPathComponent("test.gif")
        let dest=CGImageDestinationCreateWithURL(url as CFURL,UTType.gif.identifier as CFString,2,nil)!
        let properties=[kCGImagePropertyGIFDictionary:[kCGImagePropertyGIFDelayTime:0.2]] as CFDictionary
        CGImageDestinationAddImage(dest,image(.red),properties); CGImageDestinationAddImage(dest,image(.blue),properties)
        XCTAssertTrue(CGImageDestinationFinalize(dest))
        let result=try MediaEncoder.prepare(url:url,fit:.stretch,cropX:0.5,cropY:0.5)
        XCTAssertEqual(result.frames,6); XCTAssertEqual(result.duration,0.4,accuracy:0.01)
        XCTAssertEqual(String(data:result.data.prefix(4),encoding:.utf8),"AMF1")
        XCTAssertEqual(Wire.u16(result.data,4),205); XCTAssertEqual(Wire.u16(result.data,6),251)
        let still=try MediaEncoder.prepare(url:url,fit:.stretch,cropX:0.5,cropY:0.5,animated:false)
        XCTAssertEqual(still.frames,1)
        if let path=ProcessInfo.processInfo.environment["AIR_MOUSE_TEST_MEDIA"] { try result.data.write(to:URL(fileURLWithPath:path)) }
    }
    func testRejectUnreadableMedia() {
        XCTAssertThrowsError(try MediaEncoder.prepare(url:URL(fileURLWithPath:"/nonexistent/image.gif"),fit:.fit,cropX:0.5,cropY:0.5))
    }
    func testTopRowOrientation() throws {
        // A PNG whose first stored row is red and last row is blue must keep that orientation.
        let pixels=Data([255,0,0,255,255,0,0,255,0,0,255,255,0,0,255,255])
        let image=CGImage(width:2,height:2,bitsPerComponent:8,bitsPerPixel:32,bytesPerRow:8,space:CGColorSpaceCreateDeviceRGB(),bitmapInfo:CGBitmapInfo(rawValue:CGImageAlphaInfo.premultipliedLast.rawValue),provider:CGDataProvider(data:pixels as CFData)!,decode:nil,shouldInterpolate:false,intent:.defaultIntent)!
        let url=FileManager.default.temporaryDirectory.appendingPathComponent(UUID().uuidString+".png")
        defer {try? FileManager.default.removeItem(at:url)}
        let dest=CGImageDestinationCreateWithURL(url as CFURL,UTType.png.identifier as CFString,1,nil)!
        CGImageDestinationAddImage(dest,image,nil); XCTAssertTrue(CGImageDestinationFinalize(dest))
        let result=try MediaEncoder.prepare(url:url,fit:.stretch,cropX:0.5,cropY:0.5)
        XCTAssertEqual(Wire.u16(result.data,20),0xf800,"Top row should remain red")
    }
}
