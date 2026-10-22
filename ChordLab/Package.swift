// swift-tools-version: 5.9
// Unit tests for the SwiftUI layer's pure logic (`swift test` from this directory). The library is
// everything in UI/Core: files that depend on nothing but Foundation, CoreGraphics and SwiftUI value
// types (Color, Transferable). The views, the state object and the iPlug2 bridge stay in UI/ outside
// Core and are built only by the Xcode project, whose synchronized UI group compiles UI/ and its
// subfolders (Core included). A new pure file that wants tests goes into UI/Core.
import PackageDescription

let package = Package(
  name: "ChordLabUI",
  platforms: [.macOS("15.0")],   // `.v15` needs tools 6.0; the string form is the same deployment target
  products: [
    .library(name: "ChordLabUICore", targets: ["ChordLabUICore"]),
  ],
  targets: [
    .target(
      name: "ChordLabUICore",
      path: "UI/Core"
    ),
    .testTarget(
      name: "ChordLabUICoreTests",
      dependencies: ["ChordLabUICore"],
      path: "Tests/ChordLabUICoreTests"
    ),
  ]
)
