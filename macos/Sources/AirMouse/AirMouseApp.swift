import SwiftUI
import AppKit
import UniformTypeIdentifiers

@main struct AirMouseApp: App {
    @StateObject private var client = WatchClient(preview:CommandLine.arguments.contains("--preview"))
    var body: some Scene {
        MenuBarExtra("Air Mouse",systemImage:"computermouse") { TrayMenu(client:client) }
        Window("Air Mouse",id:"settings") {
            SettingsView(client:client)
                .frame(width:650,height:680)
        }.windowResizability(.contentSize)
    }
}
struct TrayMenu: View {
    @ObservedObject var client: WatchClient
    @Environment(\.openWindow) private var openWindow
    var body: some View {
        Text(client.status)
        if !client.ready {
            ForEach(client.devices,id:\.identifier) { watch in
                Button("Connect to \(watch.name ?? "Air Mouse Watch")") { client.connect(watch) }
                    .disabled(client.connecting)
            }
        }
        if client.ready { Text(client.hidSubscribed ? "Mouse connected to macOS" : "Pair in System Settings → Bluetooth") }
        Divider()
        Button("Settings…") { openWindow(id:"settings"); NSApp.activate(ignoringOtherApps:true) }.keyboardShortcut(",")
        Button(client.paused ? "Resume pointer" : "Pause pointer") { client.togglePause() }.disabled(!client.ready || client.busy)
        Divider()
        Button("Quit Air Mouse") { NSApp.terminate(nil) }.keyboardShortcut("q")
    }
}
struct SettingsView: View {
    @ObservedObject var client: WatchClient
    @State private var fit = ImageFit.fill
    @State private var cropX = 0.5
    @State private var cropY = 0.5
    @State private var animated = true
    @State private var selectedURL: URL?
    @State private var prepared: PreparedMedia?
    @State private var preparing = false
    @State private var mediaError: String?
    @State private var prepareTask: Task<Void,Never>?
    @State private var generation = 0
    var body: some View {
        VStack(alignment:.leading,spacing:14) {
            HStack {
                Image(systemName:"computermouse.fill").font(.largeTitle).foregroundStyle(.tint)
                VStack(alignment:.leading,spacing:3) { Text("Air Mouse").font(.title2.bold()); Text(client.status).font(.callout).foregroundStyle(.secondary) }
                Spacer()
                Circle().fill(client.ready ? Color.green : Color.orange).frame(width:9,height:9)
            }.padding(.horizontal,8)
            if let error=client.error { Text(error).foregroundStyle(.red).font(.callout).textSelection(.enabled) }
            TabView {
                connection.tabItem { Label("Watch",systemImage:"applewatch") }
                touch.tabItem { Label("Touch",systemImage:"hand.draw") }
                media.tabItem { Label("Display",systemImage:"photo") }
            }
            HStack {
                Text("Settings and images stay on your watch after quitting this app.").font(.caption).foregroundStyle(.secondary)
                Spacer()
                Button("Save to watch") { client.apply() }
                    .buttonStyle(.borderedProminent)
                    .disabled(!client.ready || client.busy || !client.settings.valid || client.settings == client.savedSettings)
            }
        }.padding(20)
    }
    var connection: some View {
        Form {
            Section("Connect") {
                Text("First pair Air Mouse Watch in System Settings → Bluetooth. Cursor movement, clicks, and scrolling then work without this app.")
                HStack {
                    Button("Open Bluetooth settings") { NSWorkspace.shared.open(URL(string:"x-apple.systempreferences:com.apple.BluetoothSettings")!) }
                    Button("Scan for watches") { client.scan() }.disabled(client.ready)
                }
                ForEach(client.devices,id:\.identifier) { watch in
                    HStack { Text(watch.name ?? "Air Mouse Watch"); Spacer(); Button("Connect") { client.connect(watch) }.disabled(client.ready || client.busy) }
                }
            }
            if client.ready && !client.statusReceived { Text("Reading watch status…").foregroundStyle(.secondary) }
            if client.ready && client.statusReceived {
                Section("Watch status") {
                    statusRow("Mouse reports",client.hidSubscribed ? "Subscribed by macOS" : "Waiting for macOS pairing")
                    statusRow("Touch",client.touchReady ? "Ready" : "Controller unavailable")
                    statusRow("Image storage",client.storageReady ? client.hasMedia ? "Image saved" : "Ready" : "Wrong firmware partition")
                    HStack {
                        Button(client.paused ? "Resume pointer" : "Pause pointer") { client.togglePause() }
                        Button("Disconnect companion") { client.forget() }
                    }.disabled(client.busy)
                }
            }
            Section("Pair with another device") {
                Text("With the watch running, hold BOOT for 3 seconds until it says ‘Release to reset pairing’, then release. The watch restarts with its old bonds cleared and a fresh Bluetooth identity. Select Air Mouse Watch in the new device’s Bluetooth settings; no companion is required.")
                Text("Your settings and image stay saved. Forget the old watch entry on previous devices if it remains listed. Do not hold BOOT while powering on; that enters firmware download mode.").font(.caption).foregroundStyle(.secondary)
            }
            Section("Mouse utilities") {
                Text(client.openMouseCompatibility ? "Experimental OpenMouse profile enabled. OpenMouse can use the moddoMOUSE DPI protocol if macOS exposes its Bluetooth feature reports. Only DPI scaling is supported; polling stays at 125 Hz. Hardware compatibility is not yet verified." : "This watch uses standard Bluetooth mouse reports. To try control.openmouse.app, install the separate experimental OpenMouse firmware build; the normal identity is not registered with OpenMouse.")
                    .foregroundStyle(.secondary)
                Link("Open OpenMouse",destination:URL(string:"https://control.openmouse.app/")!)
                Link("OpenMouse device integration guide",destination:URL(string:"https://docs.openmouse.app/add-a-device/overview/")!)
            }
        }.formStyle(.grouped)
    }
    var touch: some View {
        Form {
            Section("Touchpad") {
                Toggle("Enable touch input",isOn:$client.settings.touchEnabled)
                floatSlider("Pointer sensitivity",value:$client.settings.touchGain,range:0.2...5,suffix:"×")
                integerSlider("Right-click hold",value:$client.settings.holdMs,range:250...1500,suffix:"ms")
                Text("Tap for left click. Hold still for right click. Move a finger deliberately to move the pointer. This gesture does not hold the left mouse button.").font(.caption).foregroundStyle(.secondary)
            }
            Section("Pointer scale · shared with OpenMouse") {
                dpiSlider("Horizontal",value:$client.settings.dpiX)
                dpiSlider("Vertical",value:$client.settings.dpiY)
                Text("1600 means 1× touch sensitivity. Wrist movement does not move the cursor.").font(.caption).foregroundStyle(.secondary)
            }
            Section("Quick scrolling") {
                integerSlider("Scroll intensity",value:$client.settings.scrollSteps,range:1...80,suffix:"wheel steps")
                integerSlider("Maximum swipe time",value:$client.settings.swipeMs,range:150...700,suffix:"ms")
                integerSlider("Edge start zone",value:$client.settings.edgePercent,range:5...25,suffix:"%")
                integerSlider("Minimum swipe distance",value:$client.settings.spanPercent,range:50...90,suffix:"%")
                Toggle("Reverse scrolling direction",isOn:$client.settings.reverseScroll)
                Text("Swipe quickly from one edge to the opposite edge for vertical or horizontal scrolling. Movement that starts near an edge waits briefly to distinguish a swipe from pointer control.").font(.caption).foregroundStyle(.secondary)
            }
        }.formStyle(.grouped).disabled(client.busy)
    }
    var media: some View {
        VStack(spacing:10) {
            HStack(alignment:.top,spacing:22) {
                ZStack {
                    RoundedRectangle(cornerRadius:26).fill(.black)
                    if let prepared { Image(decorative:prepared.preview,scale:1).resizable().interpolation(.high).scaledToFit().clipShape(RoundedRectangle(cornerRadius:22)).padding(5) }
                    else { Image(systemName:"photo.on.rectangle.angled").font(.largeTitle).foregroundStyle(.gray) }
                }.frame(width:205,height:251)
                VStack(alignment:.leading,spacing:12) {
                    Button("Choose image or GIF…") { chooseImage() }
                    if let selectedURL { Text(selectedURL.lastPathComponent).font(.caption).lineLimit(2) }
                    Picker("Sizing",selection:$fit) { ForEach(ImageFit.allCases) { Text($0.rawValue).tag($0) } }
                    Toggle("Play animation",isOn:$animated)
                    if fit == .fill {
                        Text("Crop position").font(.caption).foregroundStyle(.secondary)
                        Slider(value:$cropX,in:0...1) { Text("Horizontal") }
                        Slider(value:$cropY,in:0...1) { Text("Vertical") }
                    }
                    if let prepared { Text("\(prepared.frames) frame\(prepared.frames == 1 ? "" : "s") · \(ByteCountFormatter.string(fromByteCount:Int64(prepared.data.count),countStyle:.binary))").font(.caption).foregroundStyle(.secondary) }
                    if preparing { ProgressView("Preparing preview…").controlSize(.small) }
                }.frame(maxWidth:.infinity)
            }.padding(.top,16)
            if let mediaError { Text(mediaError).foregroundStyle(.red).font(.caption).textSelection(.enabled) }
            Text("Fit keeps the full image with black bars. Fill crops to the screen; Resize stretches. GIFs loop on the watch. Up to 60 seconds, 120 frames, and 3 MiB; transfers can take several minutes.").font(.caption).foregroundStyle(.secondary)
            integerSlider("Screen brightness",value:$client.settings.brightness,range:5...255,suffix:"/ 255")
            if !client.progressLabel.isEmpty {
                ProgressView(client.progressLabel,value:client.progress)
                Button("Cancel upload") { client.cancelUpload() }
            } else {
                Button("Send to watch") { if let prepared { client.upload(prepared) } }
                    .buttonStyle(.borderedProminent).disabled(prepared == nil || preparing || !client.ready || client.busy || !client.storageReady)
            }
            Spacer(minLength:0)
        }.padding(.horizontal,18)
        .onChange(of:fit) { _ in prepare() }.onChange(of:cropX) { _ in prepare() }
        .onChange(of:cropY) { _ in prepare() }.onChange(of:animated) { _ in prepare() }
    }
    func statusRow(_ name:String,_ value:String) -> some View { HStack { Text(name); Spacer(); Text(value).foregroundStyle(.secondary) } }
    func floatSlider(_ title:String,value:Binding<Float>,range:ClosedRange<Float>,suffix:String) -> some View {
        VStack(alignment:.leading,spacing:4) { HStack { Text(title); Spacer(); Text(String(format:"%.1f %@",value.wrappedValue,suffix)).monospacedDigit().foregroundStyle(.secondary) }; Slider(value:value,in:range) }
    }
    func integerSlider<T: FixedWidthInteger>(_ title:String,value:Binding<T>,range:ClosedRange<Double>,suffix:String) -> some View {
        VStack(alignment:.leading,spacing:4) { HStack { Text(title); Spacer(); Text(String(value.wrappedValue) + " " + suffix).monospacedDigit().foregroundStyle(.secondary) }; Slider(value:Binding(get:{Double(value.wrappedValue)},set:{value.wrappedValue=T($0.rounded())}),in:range,step:1) }
    }
    func dpiSlider(_ title:String,value:Binding<UInt16>) -> some View {
        VStack(alignment:.leading,spacing:4) {
            HStack { Text(title); Spacer(); Text("\(value.wrappedValue) DPI scale").monospacedDigit().foregroundStyle(.secondary) }
            Slider(value:Binding(get:{Double(value.wrappedValue)},set:{value.wrappedValue=UInt16(($0/50).rounded()*50)}),in:50...26000,step:50)
        }
    }
    func chooseImage() {
        let panel=NSOpenPanel(); panel.allowedContentTypes=[.image]; panel.allowsMultipleSelection=false
        guard panel.runModal() == .OK, let url=panel.url else { return }
        selectedURL=url; prepare()
    }
    func prepare() {
        guard let selectedURL else { return }
        prepareTask?.cancel(); generation += 1; let current=generation
        let mode=fit, x=cropX, y=cropY, play=animated
        preparing=true; prepared=nil; mediaError=nil
        prepareTask=Task {
            do {
                try await Task.sleep(nanoseconds:250_000_000)
                let result=try await Task.detached(priority:.userInitiated) { try MediaEncoder.prepare(url:selectedURL,fit:mode,cropX:x,cropY:y,animated:play) }.value
                guard current == generation else { return }; prepared=result; preparing=false
            } catch is CancellationError { }
            catch { guard current == generation else { return }; mediaError=error.localizedDescription; preparing=false }
        }
    }
}
