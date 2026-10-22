import Foundation

enum Wire {
    static func u16(_ d: Data, _ i: Int) -> UInt16 { UInt16(d[i]) | UInt16(d[i+1]) << 8 }
    static func u32(_ d: Data, _ i: Int) -> UInt32 { UInt32(u16(d,i)) | UInt32(u16(d,i+2)) << 16 }
    static func put16(_ v: UInt16, into d: inout Data) { d.append(UInt8(truncatingIfNeeded:v)); d.append(UInt8(truncatingIfNeeded:v >> 8)) }
    static func put32(_ v: UInt32, into d: inout Data) { put16(UInt16(truncatingIfNeeded:v), into:&d); put16(UInt16(truncatingIfNeeded:v >> 16), into:&d) }
    static func crc32(_ data: Data) -> UInt32 {
        var crc = UInt32.max
        for byte in data { crc ^= UInt32(byte); for _ in 0..<8 { crc = (crc >> 1) ^ (crc & 1 == 1 ? 0xedb88320 : 0) } }
        return ~crc
    }
}
struct MouseSettings: Equatable {
    var flags: UInt16 = 2 // Legacy gyro fields retain wire offsets; firmware ignores them.
    var gyroGain: Float = 14, touchGain: Float = 1.4, deadzone: Float = 0.7, smoothingMs: Float = 24
    var holdMs: UInt16 = 550, swipeMs: UInt16 = 420
    var scrollSteps: UInt8 = 12, brightness: UInt8 = 100, axisX: UInt8 = 5, axisY: UInt8 = 3
    var edgePercent: UInt16 = 16, spanPercent: UInt16 = 68
    var dpiX: UInt16 = 1600, dpiY: UInt16 = 1600
    var touchEnabled: Bool { get { flags & 2 != 0 } set { flags = newValue ? flags | 2 : flags & ~2 } }
    var reverseScroll: Bool { get { flags & 4 != 0 } set { flags = newValue ? flags | 4 : flags & ~4 } }
    var valid: Bool {
        flags <= 7 && gyroGain.isFinite && (1...60).contains(gyroGain) && touchGain.isFinite && (0.2...5).contains(touchGain) &&
        deadzone.isFinite && (0...5).contains(deadzone) && smoothingMs.isFinite && (0...150).contains(smoothingMs) &&
        (250...1500).contains(holdMs) && (150...700).contains(swipeMs) && (1...80).contains(scrollSteps) && brightness >= 5 &&
        axisX < 6 && axisY < 6 && axisX % 3 != axisY % 3 && (5...25).contains(edgePercent) && (50...90).contains(spanPercent) &&
        (50...26000).contains(dpiX) && dpiX % 50 == 0 && (50...26000).contains(dpiY) && dpiY % 50 == 0
    }
    func encoded() -> Data {
        var d = Data(); Wire.put16(1,into:&d); Wire.put16(flags,into:&d)
        for f in [gyroGain,touchGain,deadzone,smoothingMs] { Wire.put32(f.bitPattern,into:&d) }
        Wire.put16(holdMs,into:&d); Wire.put16(swipeMs,into:&d)
        d.append(contentsOf:[scrollSteps,brightness,axisX,axisY]); Wire.put16(edgePercent,into:&d); Wire.put16(spanPercent,into:&d)
        Wire.put16(dpiX,into:&d); Wire.put16(dpiY,into:&d)
        return d
    }
    init() {}
    init(data: Data) throws {
        guard data.count == 36, Wire.u16(data,0) == 1 else { throw WatchError.message("Unsupported settings format. Update the client and firmware together.") }
        flags=Wire.u16(data,2); gyroGain=Float(bitPattern:Wire.u32(data,4)); touchGain=Float(bitPattern:Wire.u32(data,8))
        deadzone=Float(bitPattern:Wire.u32(data,12)); smoothingMs=Float(bitPattern:Wire.u32(data,16))
        holdMs=Wire.u16(data,20); swipeMs=Wire.u16(data,22); scrollSteps=data[24]; brightness=data[25]; axisX=data[26]; axisY=data[27]
        edgePercent=Wire.u16(data,28); spanPercent=Wire.u16(data,30)
        dpiX=Wire.u16(data,32); dpiY=Wire.u16(data,34)
        guard valid else { throw WatchError.message("The watch returned invalid settings.") }
    }
}
enum WatchError: LocalizedError {
    case message(String)
    var errorDescription: String? { if case let .message(text) = self { return text }; return nil }
}
