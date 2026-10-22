// swift-tools-version: 5.9
// Unit tests for the SwiftUI layer's pure logic (`swift test` from this directory). The library compiles
// only the UI files listed below, which depend on nothing but Foundation, CoreGraphics and SwiftUI value
// types (Color, Transferable); the views, the state object and the iPlug2 bridge stay out of SPM and are
// built by the Xcode project, which picks up every file in UI/ on its own. A new pure file in UI/ that
// wants tests goes into `coreSources` here.
import Foundation
import PackageDescription

/// The pure UI files the library compiles.
let coreSources = [
  "ChordRef.swift",
  "ChordSymbol.swift",
  "FunctionStyle.swift",
  "HarmonyText.swift",
  "KeyNames.swift",
  "LabelMode.swift",
  "SectionStyle.swift",
  "SequenceGeometry.swift",
  "Snapshot.swift",
]

/// Everything else in UI/ (the views, the state object, the bridge glue), excluded so that SPM neither
/// compiles it nor warns about unhandled files.
let uiExcludes: [String] = {
  let ui = URL(fileURLWithPath: #filePath).deletingLastPathComponent().appendingPathComponent("UI").path
  let all = (try? FileManager.default.contentsOfDirectory(atPath: ui)) ?? []
  return all.filter { !coreSources.contains($0) }.sorted()
}()

let package = Package(
  name: "ChordLabUI",
  platforms: [.macOS("15.0")],   // `.v15` needs tools 6.0; the string form is the same deployment target
  products: [
    .library(name: "ChordLabUICore", targets: ["ChordLabUICore"]),
  ],
  targets: [
    .target(
      name: "ChordLabUICore",
      path: "UI",
      exclude: uiExcludes,
      sources: coreSources
    ),
    .testTarget(
      name: "ChordLabUICoreTests",
      dependencies: ["ChordLabUICore"],
      path: "Tests/ChordLabUICoreTests"
    ),
  ]
)
