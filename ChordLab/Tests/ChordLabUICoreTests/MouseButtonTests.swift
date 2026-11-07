import XCTest
@testable import ChordLabUICore

/// The window's left-button state the palette cells and the hover tips read (UI/Core/MouseButton.swift).
final class MouseButtonTests: XCTestCase {
  func testPressAndRelease() {
    var button = MouseButton()
    XCTAssertFalse(button.isDown)
    XCTAssertNil(button.pressLocation)
    button.down(at: CGPoint(x: 10, y: 10))
    XCTAssertTrue(button.isDown)
    XCTAssertEqual(button.presses, 1)
    XCTAssertFalse(button.pressDragged)
    XCTAssertEqual(button.pressLocation, CGPoint(x: 10, y: 10))
    button.up()
    XCTAssertFalse(button.isDown)
    XCTAssertEqual(button.presses, 1)
    XCTAssertFalse(button.pressDragged)
    XCTAssertEqual(button.pressLocation, CGPoint(x: 10, y: 10), "kept until the next press: a down and its up may arrive together")
  }

  func testAPressIsADragPastTheThreshold() {
    var button = MouseButton()
    button.down(at: CGPoint(x: 10, y: 10))
    button.dragged(to: CGPoint(x: 12, y: 11))                       // within the threshold: a hold with a wobble
    XCTAssertFalse(button.pressDragged)
    button.dragged(to: CGPoint(x: 10, y: 10 + MouseButton.dragThreshold))   // exactly the threshold: still a hold
    XCTAssertFalse(button.pressDragged)
    button.dragged(to: CGPoint(x: 14, y: 10))
    XCTAssertTrue(button.pressDragged)
    button.dragged(to: CGPoint(x: 40, y: 40))
    XCTAssertTrue(button.pressDragged)
    XCTAssertTrue(button.isDown)
    button.up()
    XCTAssertTrue(button.pressDragged, "describes the latest press until the next down")
    button.down(at: CGPoint(x: 40, y: 40))
    XCTAssertFalse(button.pressDragged, "a new press starts undragged")
  }

  func testMovesWithoutAPressAreNotDrags() {
    var button = MouseButton()
    button.dragged(to: CGPoint(x: 100, y: 100))
    XCTAssertFalse(button.pressDragged)
    button.up()                                                     // an up with no down: harmless
    XCTAssertFalse(button.isDown)
    XCTAssertEqual(button.presses, 0)
  }

  func testADownWhileDownIsANewPress() {
    var button = MouseButton()
    button.down(at: CGPoint(x: 0, y: 0))
    button.dragged(to: CGPoint(x: 10, y: 0))
    XCTAssertTrue(button.pressDragged)
    button.down(at: CGPoint(x: 50, y: 50))                          // the up was missed (a drag session ate it)
    XCTAssertEqual(button.presses, 2)
    XCTAssertFalse(button.pressDragged)
    XCTAssertEqual(button.pressLocation, CGPoint(x: 50, y: 50))
    button.dragged(to: CGPoint(x: 51, y: 51))
    XCTAssertFalse(button.pressDragged, "the new press measures from its own origin")
    button.dragged(to: CGPoint(x: 60, y: 60))
    XCTAssertTrue(button.pressDragged)
  }

  func testStatesCompare() {
    var a = MouseButton()
    var b = MouseButton()
    XCTAssertEqual(a, b)
    a.down(at: .zero)
    XCTAssertNotEqual(a, b)
    b.down(at: CGPoint(x: 5, y: 5))
    XCTAssertNotEqual(a, b, "a press somewhere else is another value")
    a.up()
    b.up()
    XCTAssertNotEqual(a, b, "the press location stays part of the value after the up")
    var c = MouseButton()
    c.down(at: CGPoint(x: 5, y: 5))
    c.up()
    XCTAssertEqual(b, c)
  }
}
