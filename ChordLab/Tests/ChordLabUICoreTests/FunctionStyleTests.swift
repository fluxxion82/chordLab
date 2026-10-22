import SwiftUI
import XCTest
@testable import ChordLabUICore

/// The harmonic-function colours (UI/FunctionStyle.swift).
final class FunctionStyleTests: XCTestCase {
  func testEachFunctionCodeHasItsColour() {
    XCTAssertEqual(FunctionStyle.color("T"), FunctionStyle.tonic)
    XCTAssertEqual(FunctionStyle.color("PD"), FunctionStyle.predominant)
    XCTAssertEqual(FunctionStyle.color("D"), FunctionStyle.dominant)
    XCTAssertEqual(FunctionStyle.color("O"), FunctionStyle.other)
  }

  func testUnknownOrMissingCodeIsOther() {
    XCTAssertEqual(FunctionStyle.color("X"), FunctionStyle.other)
    XCTAssertEqual(FunctionStyle.color(""), FunctionStyle.other)
    XCTAssertEqual(FunctionStyle.color(nil), FunctionStyle.other)
  }

  func testColoursAreTheDocumentedHexValues() {
    XCTAssertEqual(hex(FunctionStyle.tonic), "#3DB38A")
    XCTAssertEqual(hex(FunctionStyle.predominant), "#E0A83A")
    XCTAssertEqual(hex(FunctionStyle.dominant), "#E05A5A")
    XCTAssertEqual(hex(FunctionStyle.other), "#6B7089")
  }
}
