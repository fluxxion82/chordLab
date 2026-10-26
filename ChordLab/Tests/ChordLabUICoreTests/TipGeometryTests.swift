import CoreGraphics
import XCTest
@testable import ChordLabUICore

/// Hover tip placement (UI/Core/TipGeometry.swift): centred on the anchor, on the preferred side when it
/// fits, flipped when it does not, the larger side when neither fits, clamped inside the editor.
final class TipGeometryTests: XCTestCase {
  private let container = CGRect(x: 0, y: 0, width: 900, height: 620)
  private let tip = CGSize(width: 200, height: 100)

  func testCentredAboveTheAnchorWithTheGap() {
    let anchor = CGRect(x: 400, y: 300, width: 100, height: 56)
    let frame = TipGeometry.frame(anchor: anchor, tipSize: tip, in: container, preferAbove: true, gap: 6)
    XCTAssertEqual(frame, CGRect(x: 350, y: 194, width: 200, height: 100))
  }

  func testFlipsBelowWhenThereIsNoRoomAbove() {
    let anchor = CGRect(x: 400, y: 40, width: 100, height: 56)
    let frame = TipGeometry.frame(anchor: anchor, tipSize: tip, in: container, preferAbove: true, gap: 6)
    XCTAssertEqual(frame.minY, 102, "anchor bottom 96 + gap 6")
  }

  func testPrefersBelowWhenAsked() {
    let anchor = CGRect(x: 400, y: 300, width: 100, height: 56)
    let frame = TipGeometry.frame(anchor: anchor, tipSize: tip, in: container, preferAbove: false, gap: 6)
    XCTAssertEqual(frame.minY, 362)
  }

  func testFlipsAboveWhenThereIsNoRoomBelow() {
    let anchor = CGRect(x: 400, y: 560, width: 100, height: 56)
    let frame = TipGeometry.frame(anchor: anchor, tipSize: tip, in: container, preferAbove: false, gap: 6)
    XCTAssertEqual(frame.maxY, 554, "anchor top 560 − gap 6")
  }

  func testClampsHorizontallyInsideTheContainer() {
    let left = TipGeometry.frame(anchor: CGRect(x: 0, y: 300, width: 56, height: 56), tipSize: tip,
                                 in: container, preferAbove: true, gap: 6)
    XCTAssertEqual(left.minX, TipGeometry.margin)
    let right = TipGeometry.frame(anchor: CGRect(x: 860, y: 300, width: 40, height: 56), tipSize: tip,
                                  in: container, preferAbove: true, gap: 6)
    XCTAssertEqual(right.maxX, 900 - TipGeometry.margin)
  }

  func testATipTallerThanEitherSideTakesTheLargerSideClampedToTheContainer() {
    let tall = CGSize(width: 200, height: 500)
    let anchor = CGRect(x: 400, y: 200, width: 100, height: 56)   // 200 above, 364 below
    let frame = TipGeometry.frame(anchor: anchor, tipSize: tall, in: container, preferAbove: true, gap: 6)
    XCTAssertEqual(frame.maxY, 620 - TipGeometry.margin, "below, pushed up to fit")
    XCTAssertGreaterThanOrEqual(frame.minY, TipGeometry.margin)
  }

  func testAZeroSizeTipStillGetsAFrame() {
    let frame = TipGeometry.frame(anchor: CGRect(x: 400, y: 300, width: 100, height: 56), tipSize: .zero,
                                  in: container, preferAbove: true, gap: 6)
    XCTAssertEqual(frame, CGRect(x: 450, y: 294, width: 0, height: 0), "before the tip is measured")
  }
}
