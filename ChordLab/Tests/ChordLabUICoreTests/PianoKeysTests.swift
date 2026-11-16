import CoreGraphics
import XCTest
@testable import ChordLabUICore

/// The roll's keyboard column (UI/Core/PianoKeys.swift, tracks design D14): the key under a point, the click velocity.
final class PianoKeysTests: XCTestCase {
  func testTheKeyUnderAKeyboardPoint() {
    let geometry = RollGeometry(pxPerBeat: 48, rowHeight: 14, ticksPerBeat: 960)
    // Row 0 (pitch 127) spans content y 0..<14; at a scroll of 1026 the view's top is content y 1026 (row 73, pitch 54).
    XCTAssertEqual(PianoKeys.pitch(atViewY: 0, geometry: geometry, scrollY: 0), 127)
    XCTAssertEqual(PianoKeys.pitch(atViewY: 13.9, geometry: geometry, scrollY: 0), 127)
    XCTAssertEqual(PianoKeys.pitch(atViewY: 14, geometry: geometry, scrollY: 0), 126)
    XCTAssertEqual(PianoKeys.pitch(atViewY: 5, geometry: geometry, scrollY: 1026), 54)
    XCTAssertEqual(PianoKeys.pitch(atViewY: 127 * 14 + 13, geometry: geometry, scrollY: 0), 0)
    XCTAssertNil(PianoKeys.pitch(atViewY: 128 * 14, geometry: geometry, scrollY: 0), "below pitch 0's row")
    XCTAssertNil(PianoKeys.pitch(atViewY: -1, geometry: geometry, scrollY: 0), "above pitch 127's row")
    XCTAssertNil(PianoKeys.pitch(atViewY: .nan, geometry: geometry, scrollY: 0))
    XCTAssertEqual(PianoKeys.velocity, 100)
  }
}
