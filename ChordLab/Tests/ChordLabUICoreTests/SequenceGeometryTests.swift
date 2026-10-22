import CoreGraphics
import XCTest
@testable import ChordLabUICore

/// The sequence bar's geometry (UI/Core/SequenceGeometry.swift): block layout, reorder targets, resize
/// snapping, section spans and section-edge snapping. All at the fixed zoom of 24 pt per beat.
final class SequenceGeometryTests: XCTestCase {
  private let px: CGFloat = 24
  private typealias Span = SequenceGeometry.SpanFrame

  /// 32 quarter-beat blocks: 6 pt each, 192 pt in all.
  private var quarters: SequenceGeometry.Layout {
    SequenceGeometry.layout(lengths: Array(repeating: 0.25, count: 32), pxPerBeat: px)
  }

  /// 4, 0.25 and 2 beats: x 0, 96, 102; widths 96, 6, 48; centres 48, 99, 126; endX 150.
  private var mixed: SequenceGeometry.Layout {
    SequenceGeometry.layout(lengths: [4, 0.25, 2], pxPerBeat: px)
  }

  /// Four one-bar blocks: starts 0, 96, 192, 288.
  private var fourBars: SequenceGeometry.Layout {
    SequenceGeometry.layout(lengths: [4, 4, 4, 4], pxPerBeat: px)
  }

  private var empty: SequenceGeometry.Layout {
    SequenceGeometry.layout(lengths: [], pxPerBeat: px)
  }

  // MARK: - Layout

  func testQuarterBeatBlocksAreLinearNotPaddedToAMinimumWidth() {
    let layout = quarters
    XCTAssertEqual(layout.frames.count, 32)
    XCTAssertEqual(layout.endX, 192, "32 quarter-beat blocks span 192 pt (not 320)")
    XCTAssertTrue(layout.frames.allSatisfy { $0.width == 6 }, "every quarter-beat block is 6 pt wide")
    XCTAssertTrue(layout.frames.enumerated().allSatisfy { CGFloat($0.offset) * 6 == $0.element.x },
                  "blocks abut at multiples of 6")
    XCTAssertEqual(layout.frames[0].centre, 3)
    XCTAssertEqual(layout.frames[1].centre, 9)
  }

  func testEndMarkerEqualsTheRulersBeat() {
    XCTAssertEqual(SequenceGeometry.x(ofBeat: 8, pxPerBeat: px), quarters.endX)
  }

  func testMixedLengthsLayOutAsPrefixSums() {
    let layout = mixed
    XCTAssertEqual(layout.frames.map(\.x), [0, 96, 102])
    XCTAssertEqual(layout.frames.map(\.width), [96, 6, 48])
    XCTAssertEqual(layout.endX, 150)
    XCTAssertEqual(layout.frames.map(\.centre), [48, 99, 126])
  }

  func testRulerBeatToXMatchesEveryBlockStart() {
    let starts: [Double] = [0, 4, 4.25]
    for (beat, frame) in zip(starts, mixed.frames) {
      XCTAssertEqual(SequenceGeometry.x(ofBeat: beat, pxPerBeat: px), frame.x, "beat \(beat)")
    }
  }

  func testEmptyLayout() {
    XCTAssertTrue(empty.frames.isEmpty)
    XCTAssertEqual(empty.endX, 0)
  }

  func testNonFiniteOrNegativeLengthsRenderZeroWide() {
    let odd = SequenceGeometry.layout(lengths: [1, .nan, -2, .infinity], pxPerBeat: px)
    XCTAssertEqual(odd.frames.map(\.width), [24, 0, 0, 0])
    XCTAssertEqual(odd.endX, 24)
  }

  // MARK: - Reorder target

  func testTargetIndexOfQuarterBeatBlocks() {
    let layout = quarters
    XCTAssertEqual(SequenceGeometry.targetIndex(from: 0, droppedBy: 2, in: layout), 0, "centre 5 < 9: stays")
    XCTAssertEqual(SequenceGeometry.targetIndex(from: 0, droppedBy: 5.9, in: layout), 0, "centre 8.9 < 9: stays")
    XCTAssertEqual(SequenceGeometry.targetIndex(from: 0, droppedBy: 7, in: layout), 1,
                   "7 pt right: past block 1's centre")
    XCTAssertEqual(SequenceGeometry.targetIndex(from: 0, droppedBy: 12.5, in: layout), 2, "centre 15.5 > 9, 15")
    XCTAssertEqual(SequenceGeometry.targetIndex(from: 31, droppedBy: -1000, in: layout), 0, "far left: first")
    XCTAssertEqual(SequenceGeometry.targetIndex(from: 5, droppedBy: 0, in: layout), 5, "no drag: itself")
    XCTAssertEqual(SequenceGeometry.targetIndex(from: 40, droppedBy: 10, in: layout), 40, "outside the layout: itself")
  }

  func testTargetIndexOfMixedLengths() {
    let layout = mixed
    XCTAssertEqual(SequenceGeometry.targetIndex(from: 2, droppedBy: -100, in: layout), 0, "block 2 at 26: first")
    XCTAssertEqual(SequenceGeometry.targetIndex(from: 2, droppedBy: -30, in: layout), 1,
                   "block 2 at 96: after block 0, before block 1 (centre 99)")
    XCTAssertEqual(SequenceGeometry.targetIndex(from: 0, droppedBy: 60, in: layout), 1,
                   "block 0 at 108: after block 1 (99), before block 2 (126)")
    XCTAssertEqual(SequenceGeometry.targetIndex(from: 0, droppedBy: 100, in: layout), 2, "block 0 at 148: last")
    XCTAssertEqual(SequenceGeometry.targetIndex(from: 1, droppedBy: -60, in: layout), 0, "quarter block at 39: first")
    XCTAssertEqual(SequenceGeometry.targetIndex(from: 1, droppedBy: -40, in: layout), 1,
                   "quarter block at 59: after block 0's centre, still 1")
  }

  // MARK: - Resize

  func testResizedLengthSnapsAndClamps() {
    let range = 0.25...64.0
    func resized(_ length: Double, by dx: CGFloat) -> Double {
      SequenceGeometry.resizedLength(length, by: dx, pxPerBeat: px, snap: 0.25, range: range)
    }
    XCTAssertEqual(resized(4, by: 0), 4, "no drag: unchanged")
    XCTAssertEqual(resized(4, by: 24), 5, "+24 pt: +1 beat")
    XCTAssertEqual(resized(4, by: 5), 4.25, "+5 pt (0.21 beat) snaps to 4.25")
    XCTAssertEqual(resized(4, by: 2), 4, "+2 pt (0.08 beat) snaps back to 4")
    XCTAssertEqual(resized(0.5, by: -1000), 0.25, "clamped to 0.25")
    XCTAssertEqual(resized(60, by: 1000), 64, "clamped to 64")
  }

  // MARK: - Section spans

  func testTwoSectionsSpanTheirBlocks() {
    XCTAssertEqual(SequenceGeometry.sectionSpans(starts: [0, 2], in: mixed),
                   [Span(x: 0, width: 102), Span(x: 102, width: 48)])
  }

  func testOneSectionSpansEverything() {
    XCTAssertEqual(SequenceGeometry.sectionSpans(starts: [0], in: mixed), [Span(x: 0, width: 150)])
  }

  func testNoSectionsNoSpans() {
    XCTAssertTrue(SequenceGeometry.sectionSpans(starts: [], in: mixed).isEmpty)
  }

  func testStartOutsideTheLayoutGetsAnEmptySpanAtTheEnd() {
    XCTAssertEqual(SequenceGeometry.sectionSpans(starts: [0, 7], in: mixed),
                   [Span(x: 0, width: 150), Span(x: 150, width: 0)],
                   "the earlier span runs to the end, the stale one is empty")
  }

  func testUnsortedStartGetsAnEmptySpan() {
    XCTAssertEqual(SequenceGeometry.sectionSpans(starts: [2, 1], in: mixed),
                   [Span(x: 102, width: 48), Span(x: 150, width: 0)])
  }

  func testSectionOnNoBlocksIsEmpty() {
    XCTAssertEqual(SequenceGeometry.sectionSpans(starts: [0], in: empty), [Span(x: 0, width: 0)])
  }

  // MARK: - Boundary snapping

  func testBoundaryIndexSnapsToTheNearestBlockStart() {
    let layout = mixed   // starts 0, 96, 102
    XCTAssertEqual(SequenceGeometry.boundaryIndex(nearestTo: 0, in: layout), 0)
    XCTAssertEqual(SequenceGeometry.boundaryIndex(nearestTo: 40, in: layout), 0)
    XCTAssertEqual(SequenceGeometry.boundaryIndex(nearestTo: 97, in: layout), 1)
    XCTAssertEqual(SequenceGeometry.boundaryIndex(nearestTo: 99, in: layout), 1, "a tie snaps to the lower index")
    XCTAssertEqual(SequenceGeometry.boundaryIndex(nearestTo: 100, in: layout), 2)
    XCTAssertEqual(SequenceGeometry.boundaryIndex(nearestTo: 1000, in: layout), 2, "far right: last")
    XCTAssertEqual(SequenceGeometry.boundaryIndex(nearestTo: -50, in: layout), 0, "far left: first")
  }

  func testBoundaryIndexWithoutBlocksIsZero() {
    XCTAssertEqual(SequenceGeometry.boundaryIndex(nearestTo: 10, in: empty), 0)
  }

  // MARK: - Section edge drag (origin boundary + translation, never the edge as drawn)

  func testDroppedBoundaryUsesTheOriginalEdgePlusTranslation() {
    XCTAssertEqual(SequenceGeometry.droppedBoundaryIndex(from: 1, draggedBy: 96, in: fourBars), 2,
                   "boundary at 96 dragged +96 lands on 2, not 3")
  }

  func testDroppedBoundarySnapsAtTheMidpoint() {
    XCTAssertEqual(SequenceGeometry.droppedBoundaryIndex(from: 1, draggedBy: 47, in: fourBars), 1)
    XCTAssertEqual(SequenceGeometry.droppedBoundaryIndex(from: 1, draggedBy: 49, in: fourBars), 2)
  }

  func testDroppedBoundaryDraggedLeftClampsToZero() {
    XCTAssertEqual(SequenceGeometry.droppedBoundaryIndex(from: 2, draggedBy: -96, in: fourBars), 1)
    XCTAssertEqual(SequenceGeometry.droppedBoundaryIndex(from: 2, draggedBy: -1000, in: fourBars), 0)
  }

  func testDroppedBoundaryEdgeCases() {
    XCTAssertEqual(SequenceGeometry.droppedBoundaryIndex(from: 1, draggedBy: 0, in: fourBars), 1, "no drag: itself")
    XCTAssertEqual(SequenceGeometry.droppedBoundaryIndex(from: 3, draggedBy: 1000, in: fourBars), 3, "far right: last")
    XCTAssertEqual(SequenceGeometry.droppedBoundaryIndex(from: 9, draggedBy: 50, in: fourBars), 9,
                   "a start outside the layout: itself")
  }

  func testDroppedBoundaryWithMixedLengths() {
    XCTAssertEqual(SequenceGeometry.droppedBoundaryIndex(from: 1, draggedBy: -30, in: mixed), 1, "96−30 stays at 1")
    XCTAssertEqual(SequenceGeometry.droppedBoundaryIndex(from: 1, draggedBy: -49, in: mixed), 0, "96−49 snaps to 0")
  }
}
