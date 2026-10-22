import XCTest
@testable import ChordLabUICore

/// The palette label mode (UI/Core/LabelMode.swift). Its raw values and storage key are persisted in
/// UserDefaults, so changing either would silently reset every user's choice: they are pinned here.
final class LabelModeTests: XCTestCase {
  func testDefaultModeIsBoth() {
    XCTAssertEqual(LabelMode.defaultMode, .both)
  }

  func testRawValuesArePinned() {
    XCTAssertEqual(LabelMode.names.rawValue, "names")
    XCTAssertEqual(LabelMode.numerals.rawValue, "numerals")
    XCTAssertEqual(LabelMode.both.rawValue, "both")
    XCTAssertEqual(LabelMode(rawValue: "both"), .both)
    XCTAssertNil(LabelMode(rawValue: "Both"))
  }

  func testStorageKeyIsPinned() {
    XCTAssertEqual(LabelMode.storageKey, "chordlab.labelMode")
  }

  func testCasesInPickerOrderWithTitlesAndIds() {
    XCTAssertEqual(LabelMode.allCases, [.names, .numerals, .both])
    XCTAssertEqual(LabelMode.allCases.map(\.title), ["Names", "Numerals", "Both"])
    XCTAssertEqual(LabelMode.allCases.map(\.id), LabelMode.allCases.map(\.rawValue))
  }
}
