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
  func testATranslatedContainerPlacesAndClampsInItsOwnBounds() {
    let shifted = CGRect(x: 100, y: 50, width: 900, height: 620)
    let frame = TipGeometry.frame(anchor: CGRect(x: 500, y: 350, width: 100, height: 56), tipSize: tip,
                                  in: shifted, preferAbove: true, gap: 6)
    XCTAssertEqual(frame, CGRect(x: 450, y: 244, width: 200, height: 100))
    let left = TipGeometry.frame(anchor: CGRect(x: 100, y: 350, width: 40, height: 56), tipSize: tip,
                                 in: shifted, preferAbove: true, gap: 6)
    XCTAssertEqual(left.minX, 100 + TipGeometry.margin)
    let top = TipGeometry.frame(anchor: CGRect(x: 500, y: 60, width: 100, height: 56), tipSize: tip,
                                in: shifted, preferAbove: true, gap: 6)
    XCTAssertEqual(top.minY, 122, "no room above inside the container (its top is 50): below, 116 + 6")
  }

  func testATipLargerThanTheContainerIsPinnedToItsTopLeadingMargin() {
    let huge = CGSize(width: 1000, height: 700)
    let frame = TipGeometry.frame(anchor: CGRect(x: 400, y: 300, width: 100, height: 56), tipSize: huge,
                                  in: container, preferAbove: true, gap: 6)
    XCTAssertEqual(frame.origin, CGPoint(x: TipGeometry.margin, y: TipGeometry.margin),
                   "the start of the content stays in view; the overflow is at the trailing and bottom edges")
    XCTAssertEqual(frame.size, huge)
  }

  func testExactlyEnoughRoomAboveKeepsTheTipAboveWithTheFullGap() {
    // 100 high + 6 gap + 4 margin = 110 above the anchor: just fits.
    let anchor = CGRect(x: 400, y: 110, width: 100, height: 56)
    let frame = TipGeometry.frame(anchor: anchor, tipSize: tip, in: container, preferAbove: true, gap: 6)
    XCTAssertEqual(frame.maxY, 104)
    XCTAssertEqual(frame.minY, TipGeometry.margin)
    let tight = TipGeometry.frame(anchor: anchor.offsetBy(dx: 0, dy: -1), tipSize: tip, in: container,
                                  preferAbove: true, gap: 6)
    XCTAssertEqual(tight.minY, 109 + 56 + 6, "one point less: below")
  }

  func testExactlyEnoughRoomBelowKeepsTheTipBelow() {
    // Anchor bottom 510: 620 − 4 margin − 6 gap − 100 = 510.
    let anchor = CGRect(x: 400, y: 454, width: 100, height: 56)
    let frame = TipGeometry.frame(anchor: anchor, tipSize: tip, in: container, preferAbove: false, gap: 6)
    XCTAssertEqual(frame.minY, 516)
    XCTAssertEqual(frame.maxY, 620 - TipGeometry.margin)
  }

  func testWhenNeitherSideFitsATieGoesToThePreferredSide() {
    // 300 high; anchor 200…256 in 0…456: 190 above and 190 below once gap and margin are taken.
    let tall = CGSize(width: 200, height: 300)
    let box = CGRect(x: 0, y: 0, width: 900, height: 456)
    let anchor = CGRect(x: 400, y: 200, width: 100, height: 56)
    let above = TipGeometry.frame(anchor: anchor, tipSize: tall, in: box, preferAbove: true, gap: 6)
    XCTAssertEqual(above.minY, TipGeometry.margin, "above, pushed down to the margin")
    let below = TipGeometry.frame(anchor: anchor, tipSize: tall, in: box, preferAbove: false, gap: 6)
    XCTAssertEqual(below.maxY, 456 - TipGeometry.margin, "below, pushed up to the margin")
  }
}
