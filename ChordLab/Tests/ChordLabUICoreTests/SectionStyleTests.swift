import SwiftUI
import XCTest
@testable import ChordLabUICore

/// The section colour palette (UI/SectionStyle.swift) that the engine's `colourIndex` indexes.
final class SectionStyleTests: XCTestCase {
  func testExactlyEightColoursAndNames() {
    XCTAssertEqual(SectionStyle.colors.count, 8, "kNumSectionColours in engine/include/chordlab/document.h")
    XCTAssertEqual(SectionStyle.names, ["Olive", "Blue", "Grey", "Magenta", "Teal", "Amber", "Red", "Violet"])
  }

  func testColoursAreTheDocumentedHexValues() {
    XCTAssertEqual(SectionStyle.colors.map(hex),
                   ["#8A8F3A", "#2E7EA6", "#6B7089", "#A64D8A", "#3DB38A", "#E0A83A", "#E05A5A", "#7B5CC9"])
  }

  func testColoursAreDistinct() {
    XCTAssertEqual(Set(SectionStyle.colors.compactMap(hex)).count, 8)
  }

  func testInRangeIndicesAreTheirOwnColour() {
    for index in 0..<8 {
      XCTAssertEqual(SectionStyle.color(index), SectionStyle.colors[index], "index \(index)")
    }
  }

  func testOutOfRangeIndicesWrap() {
    XCTAssertEqual(SectionStyle.color(8), SectionStyle.colors[0])
    XCTAssertEqual(SectionStyle.color(10), SectionStyle.colors[2])
    XCTAssertEqual(SectionStyle.color(-1), SectionStyle.colors[7])
    XCTAssertEqual(SectionStyle.color(-9), SectionStyle.colors[7])
  }
}
