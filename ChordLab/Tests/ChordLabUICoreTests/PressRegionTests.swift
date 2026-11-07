import XCTest
@testable import ChordLabUICore

/// Where a pressable view takes a press (UI/Core/PressRegion.swift).
final class PressRegionTests: XCTestCase {
  private let frame = CGRect(x: 100, y: 100, width: 40, height: 40)

  func testRectangleTakesItsWholeVisibleFrame() {
    let region = PressRegion(frame: frame, visible: frame, circular: false)
    XCTAssertTrue(region.contains(CGPoint(x: 101, y: 101)), "a corner")
    XCTAssertTrue(region.contains(CGPoint(x: 120, y: 120)))
    XCTAssertFalse(region.contains(CGPoint(x: 99, y: 120)))
    XCTAssertFalse(region.contains(CGPoint(x: 120, y: 141)))
  }

  func testCircleLeavesTheCornersOut() {
    let region = PressRegion(frame: frame, visible: frame, circular: true)
    XCTAssertFalse(region.contains(CGPoint(x: 101, y: 101)), "a corner outside the inscribed circle")
    XCTAssertTrue(region.contains(CGPoint(x: 120, y: 120)), "the centre")
    XCTAssertTrue(region.contains(CGPoint(x: 120, y: 101)), "the top of the circle")
    XCTAssertTrue(region.contains(CGPoint(x: 134, y: 134)), "just inside the rim")
    XCTAssertFalse(region.contains(CGPoint(x: 135, y: 135)), "just outside the rim")
  }

  func testOnlyTheVisiblePartTakesAPress() {
    let visible = CGRect(x: 100, y: 100, width: 40, height: 20)   // the lower half is under the viewport's edge
    let region = PressRegion(frame: frame, visible: visible, circular: true)
    XCTAssertTrue(region.contains(CGPoint(x: 120, y: 110)))
    XCTAssertFalse(region.contains(CGPoint(x: 120, y: 130)), "inside the circle, outside the viewport")
    XCTAssertFalse(PressRegion(frame: frame, visible: .zero, circular: true).contains(CGPoint(x: 120, y: 120)))
    XCTAssertFalse(PressRegion.nowhere.contains(.zero))
  }
}
