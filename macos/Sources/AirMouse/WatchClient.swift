import Foundation
import CoreBluetooth
import Combine

// CoreBluetooth delegates are delivered on .main (set explicitly in init).
@MainActor final class WatchClient: NSObject, ObservableObject, @preconcurrency CBCentralManagerDelegate, @preconcurrency CBPeripheralDelegate {
    static let service = CBUUID(string:"b8571000-7f12-4c31-9638-37b2d28a6421")
    static let commandID = CBUUID(string:"b8571001-7f12-4c31-9638-37b2d28a6421")
    static let replyID = CBUUID(string:"b8571002-7f12-4c31-9638-37b2d28a6421")
    static let settingsID = CBUUID(string:"b8571003-7f12-4c31-9638-37b2d28a6421")
    @Published var status = "Starting Bluetooth…"
    @Published var error: String?
    @Published var devices: [CBPeripheral] = []
    @Published var ready = false
    @Published var busy = false
    @Published var progress: Double = 0
    @Published var settings = MouseSettings()
    @Published var savedSettings: MouseSettings?
    @Published var hidSubscribed = false
    @Published var paused = false
    @Published var touchReady = false
    @Published var statusReceived = false
    @Published var storageReady = false
    @Published var hasMedia = false
    @Published var openMouseCompatibility = false
    @Published var progressLabel = ""
    private var central: CBCentralManager!
    private var peripheral: CBPeripheral?
    var connecting: Bool { peripheral != nil && !ready }
    private var commandChar: CBCharacteristic?, replyChar: CBCharacteristic?, settingsChar: CBCharacteristic?
    private var sequence: UInt16 = 0
    private struct Pending { let id: UInt16; let op: UInt8; let continuation: CheckedContinuation<Data,Error> }
    private var pending: Pending?
    private var timeout: Task<Void,Never>?
    private var connectionTimeout: Task<Void,Never>?
    private var poll: Timer?
    private var cancelled = false
    private var reconnect = true
    init(preview: Bool = false) {
        super.init()
        if preview { status="Connect your watch"; return }
        central = CBCentralManager(delegate:self,queue:.main,options:[CBCentralManagerOptionShowPowerAlertKey:true])
        poll = Timer.scheduledTimer(withTimeInterval:2,repeats:true) { [weak self] _ in
            Task { @MainActor in
                guard let self, self.ready, !self.busy, self.pending == nil else { return }
                do { _ = try await self.request(1) } catch { self.error = error.localizedDescription }
            }
        }
    }
    func centralManagerDidUpdateState(_ central: CBCentralManager) {
        switch central.state {
        case .poweredOn: status="Looking for your watch…"; scan()
        case .poweredOff: status="Bluetooth is off"; disconnected()
        case .unauthorized: status="Allow Bluetooth for Air Mouse in System Settings → Privacy & Security"; disconnected()
        default: status="Bluetooth is unavailable"; disconnected()
        }
    }
    func scan() {
        guard central != nil else { return }
        guard central.state == .poweredOn else { return }
        guard !connecting, !ready else { return }
        central.stopScan(); devices=[]; error=nil
        reconnect=true
        for p in central.retrieveConnectedPeripherals(withServices:[Self.service]) { discovered(p) }
        // macOS can already hold the HID link while the peripheral stops advertising.
        for p in central.retrieveConnectedPeripherals(withServices:[CBUUID(string:"1812")]) where p.name == "Air Mouse Watch" { discovered(p) }
        // Only reconnect a remembered identity when it is actually observed/connected.
        // Cached peripherals survive a watch pairing reset and can otherwise block
        // discovery by starting a connection to the now-retired address.
        central.scanForPeripherals(withServices:[Self.service,CBUUID(string:"1812")],options:[CBCentralManagerScanOptionAllowDuplicatesKey:false])
    }
    private func discovered(_ p: CBPeripheral) {
        if !devices.contains(where:{$0.identifier==p.identifier}) { devices.append(p) }
        if peripheral == nil { status="Watch found — choose Connect" }
        if peripheral == nil && UserDefaults.standard.string(forKey:"watchID") == p.identifier.uuidString { connect(p) }
    }
    func centralManager(_ central: CBCentralManager, didDiscover p: CBPeripheral, advertisementData: [String:Any], rssi RSSI: NSNumber) {
        let services=advertisementData[CBAdvertisementDataServiceUUIDsKey] as? [CBUUID] ?? []
        let name=advertisementData[CBAdvertisementDataLocalNameKey] as? String ?? p.name
        guard services.contains(Self.service) || name == "Air Mouse Watch" else { return }
        discovered(p)
    }
    func connect(_ p: CBPeripheral) {
        guard peripheral == nil else { return }
        reconnect=true; error=nil; peripheral=p; p.delegate=self
        status="Connecting to \(p.name ?? "Air Mouse Watch")…"
        central.connect(p,options:nil)
        connectionTimeout?.cancel()
        connectionTimeout=Task { [weak self] in
            do { try await Task.sleep(nanoseconds:60_000_000_000) } catch { return }
            guard let self, self.peripheral == p, !self.ready else { return }
            self.reconnect=false; self.peripheral=nil
            self.central.cancelPeripheralConnection(p); self.disconnected()
            self.status="Connection timed out — scan and retry"
            self.error="Keep the watch nearby. If its firmware changed, select the newly discovered watch."
        }
    }
    func centralManager(_ central: CBCentralManager, didConnect p: CBPeripheral) {
        guard p == peripheral else { return }
        central.stopScan(); status="Reading settings — approve macOS pairing if asked"; p.discoverServices([Self.service])
    }
    func centralManager(_ central: CBCentralManager, didFailToConnect p: CBPeripheral, error: Error?) {
        guard p == peripheral else { return }
        status="Connection failed. Scan to retry."; self.error=error?.localizedDescription; peripheral=nil; disconnected()
    }
    func centralManager(_ central: CBCentralManager, didDisconnectPeripheral p: CBPeripheral, error: Error?) {
        guard p == peripheral else { return }
        disconnected(); peripheral=nil; status="Watch disconnected"
        if let error { self.error=error.localizedDescription }
        if reconnect && central.state == .poweredOn {
            Task { try? await Task.sleep(nanoseconds:2_000_000_000); if self.reconnect && self.peripheral == nil { self.scan() } }
        }
    }
    private func disconnected() {
        connectionTimeout?.cancel(); connectionTimeout=nil
        ready=false; savedSettings=nil; hidSubscribed=false; paused=false
        statusReceived=false; touchReady=false; storageReady=false; hasMedia=false; openMouseCompatibility=false
        commandChar=nil; replyChar=nil; settingsChar=nil
        finish(.failure(WatchError.message("The watch disconnected. Reconnect and retry.")))
    }
    func forget() {
        reconnect=false; UserDefaults.standard.removeObject(forKey:"watchID"); central.stopScan()
        if let p=peripheral { central.cancelPeripheralConnection(p) }
        disconnected(); peripheral=nil; devices=[]; status="Companion disconnected; macOS mouse pairing is unchanged"
    }
    func peripheral(_ p: CBPeripheral, didDiscoverServices error: Error?) {
        guard p == peripheral else { return }
        if let error { self.error=error.localizedDescription; return }
        guard let service=p.services?.first(where:{$0.uuid==Self.service}) else { self.error="Watch configuration service is missing."; return }
        p.discoverCharacteristics([Self.commandID,Self.replyID,Self.settingsID],for:service)
    }
    func peripheral(_ p: CBPeripheral, didDiscoverCharacteristicsFor service: CBService, error: Error?) {
        guard p == peripheral else { return }
        if let error { self.error=error.localizedDescription; return }
        for c in service.characteristics ?? [] {
            if c.uuid == Self.commandID { commandChar=c }
            if c.uuid == Self.replyID { replyChar=c }
            if c.uuid == Self.settingsID { settingsChar=c }
        }
        guard let replyChar, let settingsChar, commandChar != nil else { self.error="Firmware is missing required characteristics."; return }
        p.setNotifyValue(true,for:replyChar); p.readValue(for:settingsChar)
    }
    func peripheral(_ p: CBPeripheral, didUpdateNotificationStateFor c: CBCharacteristic, error: Error?) {
        guard p == peripheral else { return }
        if let error { self.error=error.localizedDescription; return }
        updateReady()
    }
    private func updateReady() {
        guard replyChar?.isNotifying == true, savedSettings != nil, commandChar != nil else { return }
        connectionTimeout?.cancel(); connectionTimeout=nil
        let firstConnection = !ready
        ready=true
        if firstConnection { status="Reading watch status…" }
        UserDefaults.standard.set(peripheral?.identifier.uuidString,forKey:"watchID")
        if firstConnection {
            Task { do { _ = try await self.request(1) } catch { self.error=error.localizedDescription } }
        }
    }
    func peripheral(_ p: CBPeripheral, didUpdateValueFor c: CBCharacteristic, error: Error?) {
        guard p == peripheral else { return }
        if let error { self.error=error.localizedDescription; if c.uuid == Self.replyID { finish(.failure(error)) }; return }
        guard let data=c.value else { return }
        if c.uuid == Self.settingsID {
            do {
                let value=try MouseSettings(data:data)
                if savedSettings == nil || settings == savedSettings { settings=value }
                savedSettings=value; updateReady()
            }
            catch { self.error=error.localizedDescription }
            return
        }
        guard c.uuid == Self.replyID, data.count == 17, let pending, data[0] == (pending.op | 0x80), Wire.u16(data,1) == pending.id else { return }
        guard data[11] == 1 else { finish(.failure(WatchError.message("Protocol version mismatch."))); return }
        statusReceived=true
        if status == "Reading watch status…" { status="Watch connected" }
        hidSubscribed=data[5] != 0; paused=data[6] != 0
        touchReady=data[8] != 0; storageReady=data[9] != 0; hasMedia=data[10] != 0
        openMouseCompatibility=data[16] != 0
        if pending.op == 1, let savedSettings, Wire.u32(data,12) != Wire.crc32(savedSettings.encoded()), let settingsChar { p.readValue(for:settingsChar) }
        if data[3] == 0 { finish(.success(data)) }
        else { finish(.failure(WatchError.message([1:"The watch rejected an invalid command.",2:"The watch could not save or validate the data.",3:"The watch is busy preparing storage."][data[3]] ?? "Unknown watch error."))) }
    }
    func peripheral(_ p: CBPeripheral, didWriteValueFor c: CBCharacteristic, error: Error?) {
        guard p == peripheral else { return }
        if let error, c.uuid == Self.commandID { finish(.failure(error)) }
    }
    private func finish(_ result: Result<Data,Error>) {
        guard let current=pending else { return }; pending=nil; timeout?.cancel(); timeout=nil; current.continuation.resume(with:result)
    }
    private func request(_ op: UInt8, _ body: Data = Data(), seconds: UInt64 = 10) async throws -> Data {
        guard ready, let p=peripheral, let c=commandChar else { throw WatchError.message("Connect the watch first.") }
        // The UI and status timer share a single ordered request stream.
        while pending != nil {
            try await Task.sleep(nanoseconds:20_000_000)
            guard ready else { throw WatchError.message("The watch disconnected.") }
        }
        guard ready, peripheral == p, commandChar === c else { throw WatchError.message("The connection changed. Please retry.") }
        sequence &+= 1; let id=sequence; var data=Data([op]); Wire.put16(id,into:&data); data.append(body)
        guard data.count <= p.maximumWriteValueLength(for:.withResponse) else { throw WatchError.message("The Bluetooth write limit is too small. Reconnect the watch.") }
        return try await withCheckedThrowingContinuation { continuation in
            pending=Pending(id:id,op:op,continuation:continuation)
            timeout=Task { [weak self] in
                // Notifications can be lost while HID shares the link. Recover the
                // cached reply by reading it; never repeat a flash-changing write.
                var waited: UInt64 = 0
                while waited < seconds {
                    let interval = min(UInt64(2), seconds-waited)
                    do { try await Task.sleep(nanoseconds:interval*1_000_000_000) } catch { return }
                    waited += interval
                    guard let self, self.pending?.id == id, self.peripheral == p else { return }
                    if waited < seconds, let reply = self.replyChar { p.readValue(for:reply) }
                }
                guard let self, self.pending?.id == id, self.peripheral == p else { return }
                let phase = [UInt8(4):"preparing image storage",5:"sending an image chunk",6:"saving the image"][op] ?? "reading or saving settings"
                self.finish(.failure(WatchError.message("The watch did not respond while \(phase). Reconnect and retry.")))
                self.ready=false
                self.central.cancelPeripheralConnection(p)
            }
            p.writeValue(data,for:c,type:.withResponse)
        }
    }
    private func run(_ action: @escaping () async throws -> Void) {
        guard !busy else { return }; busy=true; error=nil
        Task { defer { busy=false }; do { try await action() } catch { self.error=error.localizedDescription } }
    }
    func apply() {
        let candidate=settings
        guard candidate.valid else { error="One or more settings are outside the supported range."; return }
        run { _ = try await self.request(2,candidate.encoded()); self.savedSettings=candidate; self.status="Settings saved on the watch" }
    }
    func togglePause() { run { _ = try await self.request(8,Data([self.paused ? 0 : 1])) } }
    func cancelUpload() { cancelled=true }
    func upload(_ media: PreparedMedia) {
        run {
            self.cancelled=false; self.progress=0; self.progressLabel="Preparing watch storage…"
            defer { self.progressLabel="" }
            do {
                var begin=Data(); Wire.put32(UInt32(media.data.count),into:&begin); Wire.put32(Wire.crc32(media.data),into:&begin)
                _ = try await self.request(4,begin,seconds:90)
                let chunkSize=min(180,(self.peripheral?.maximumWriteValueLength(for:.withResponse) ?? 20)-7)
                var offset=0
                while offset < media.data.count {
                    if self.cancelled { _ = try await self.request(7); self.status="Upload cancelled; previous image kept"; return }
                    let end=min(offset+chunkSize,media.data.count); var body=Data(); Wire.put32(UInt32(offset),into:&body)
                    body.append(media.data.subdata(in:offset..<end)); _ = try await self.request(5,body)
                    offset=end; self.progress=Double(offset)/Double(media.data.count); self.progressLabel="Sending image · \(Int(self.progress*100))%"
                }
                self.progressLabel="Checking and saving image…"; _ = try await self.request(6,seconds:30)
                self.status="\(media.sourceName) saved on the watch"
            } catch {
                if self.ready { _ = try? await self.request(7) }
                throw error
            }
        }
    }
}
