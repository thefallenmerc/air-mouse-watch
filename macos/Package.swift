// swift-tools-version: 6.0
import PackageDescription
let package = Package(name: "AirMouse", platforms: [.macOS(.v13)], products: [.executable(name: "AirMouse", targets: ["AirMouse"])], targets: [
    .executableTarget(name: "AirMouse"),
    .testTarget(name: "AirMouseTests", dependencies: ["AirMouse"])
], swiftLanguageModes: [.v5])
