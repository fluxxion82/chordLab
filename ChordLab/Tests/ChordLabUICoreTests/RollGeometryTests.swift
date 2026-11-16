import CoreGraphics
import XCTest
@testable import ChordLabUICore

/// The piano roll's geometry (UI/Core/RollGeometry.swift, tracks design D14).
final class RollGeometryTests: XCTestCase {
  private let zoomLevels: [CGFloat] = [12, 24, 37.5, 48, 96, 100.3, 192]
  private let rowHeights: [CGFloat] = [8, 13.7, 14, 24]

  // MARK: - Construction and constants

  func testDefaultsAndRangesAreDesignD14sConstants() {
    XCTAssertEqual(RollGeometry.pxPerBeatRange, 12...192)
    XCTAssertEqual(RollGeometry.rowHeightRange, 8...24)
    XCTAssertEqual(RollGeometry.defaultPxPerBeat, 48)
    XCTAssertEqual(RollGeometry.defaultRowHeight, 14)
    let geometry = RollGeometry()
    XCTAssertEqual(geometry.pxPerBeat, 48)
    XCTAssertEqual(geometry.rowHeight, 14)
    XCTAssertEqual(geometry.ticksPerBeat, 960)
  }

  func testInitClampsToTheRanges() {
    XCTAssertEqual(RollGeometry(pxPerBeat: 1, rowHeight: 1).pxPerBeat, 12)
    XCTAssertEqual(RollGeometry(pxPerBeat: 1, rowHeight: 1).rowHeight, 8)
    XCTAssertEqual(RollGeometry(pxPerBeat: 9999, rowHeight: 9999).pxPerBeat, 192)
    XCTAssertEqual(RollGeometry(pxPerBeat: 9999, rowHeight: 9999).rowHeight, 24)
    XCTAssertEqual(RollGeometry(pxPerBeat: -5, rowHeight: -5).pxPerBeat, 12)
  }

  func testInitTakesTheDefaultForNotANumberAndAPositiveTicksPerBeat() {
    let geometry = RollGeometry(pxPerBeat: .nan, rowHeight: .nan, ticksPerBeat: 0)
    XCTAssertEqual(geometry.pxPerBeat, 48)
    XCTAssertEqual(geometry.rowHeight, 14)
    XCTAssertEqual(geometry.ticksPerBeat, 1)
  }

  // MARK: - Ticks and x

  func testXOfATickIsLinearFromTickZero() {
    let geometry = RollGeometry(pxPerBeat: 48)
    XCTAssertEqual(geometry.x(tick: 0), 0)
    XCTAssertEqual(geometry.x(tick: 960), 48, accuracy: 1e-9)
    XCTAssertEqual(geometry.x(tick: 480), 24, accuracy: 1e-9)
    XCTAssertEqual(geometry.x(tick: 3840), 192, accuracy: 1e-9)
    XCTAssertEqual(RollGeometry(pxPerBeat: 96, ticksPerBeat: 480).x(tick: 480), 96, accuracy: 1e-9)
  }

  func testTickToXToTickRoundTripsAtEveryZoom() {
    let ticks = [0, 1, 29, 30, 119, 120, 479, 480, 960, 1234, 12_345, 3_840_000, 15_728_640]
    for zoom in zoomLevels {
      let geometry = RollGeometry(pxPerBeat: zoom)
      for tick in ticks {
        XCTAssertEqual(geometry.tick(atX: geometry.x(tick: tick)), tick, "tick \(tick) at \(zoom) px/beat")
      }
    }
  }

  func testTickOfXFloorsToAnIntegerTick() {
    let geometry = RollGeometry(pxPerBeat: 48)   // 0.05 pt per tick
    XCTAssertEqual(geometry.tick(atX: 0.049), 0)
    XCTAssertEqual(geometry.tick(atX: 0.051), 1)
    XCTAssertEqual(geometry.tick(atX: 47.999), 959)
    XCTAssertEqual(geometry.tick(atX: 48), 960)
  }

  func testTickOfXIsClampedAtZeroAndSurvivesNonFiniteInput() {
    let geometry = RollGeometry()
    XCTAssertEqual(geometry.tick(atX: -0.01), 0)
    XCTAssertEqual(geometry.tick(atX: -500), 0)
    XCTAssertEqual(geometry.tick(atX: .nan), 0)
    XCTAssertEqual(geometry.tick(atX: .infinity), 1_000_000_000_000, "clamped to the largest value, never an overflow")
  }

  // MARK: - Pitches and y

  func testPitch127IsAtTheTopAndEachRowIsRowHeightTall() {
    let geometry = RollGeometry(rowHeight: 14)
    XCTAssertEqual(geometry.y(pitch: 127), 0)
    XCTAssertEqual(geometry.y(pitch: 126), 14)
    XCTAssertEqual(geometry.y(pitch: 60), 67 * 14)
    XCTAssertEqual(geometry.y(pitch: 0), 127 * 14)
  }

  func testPitchToYToPitchRoundTripsAtEveryRowHeight() {
    for height in rowHeights {
      let geometry = RollGeometry(rowHeight: height)
      for pitch in 0...127 {
        let top = geometry.y(pitch: pitch)
        XCTAssertEqual(geometry.pitch(atY: top), pitch, "top of \(pitch) at \(height)")
        XCTAssertEqual(geometry.pitch(atY: top + height / 2), pitch, "middle of \(pitch) at \(height)")
        XCTAssertEqual(geometry.pitch(atY: top + height - 0.001), pitch, "bottom of \(pitch) at \(height)")
      }
    }
  }

  func testPitchOfYIsClampedAndSurvivesNonFiniteInput() {
    let geometry = RollGeometry(rowHeight: 14)
    XCTAssertEqual(geometry.pitch(atY: -1), 127)
    XCTAssertEqual(geometry.pitch(atY: -9999), 127)
    XCTAssertEqual(geometry.pitch(atY: 128 * 14), 0)
    XCTAssertEqual(geometry.pitch(atY: 99999), 0)
    XCTAssertEqual(geometry.pitch(atY: .nan), 127)
    XCTAssertEqual(geometry.pitch(atY: .infinity), 0)
    XCTAssertEqual(geometry.pitch(atY: -.infinity), 127)
  }

  // MARK: - Rectangles

  func testNoteRectSitsOnItsRowFromItsStartForItsLength() {
    let geometry = RollGeometry(pxPerBeat: 48, rowHeight: 14)
    let rect = geometry.noteRect(start: 960, length: 480, pitch: 60)
    XCTAssertEqual(rect.minX, 48, accuracy: 1e-9)
    XCTAssertEqual(rect.width, 24, accuracy: 1e-9)
    XCTAssertEqual(rect.minY, 67 * 14)
    XCTAssertEqual(rect.height, 14)
  }

  func testNoteRectNeverHasANegativeWidth() {
    XCTAssertEqual(RollGeometry().noteRect(start: 0, length: -10, pitch: 60).width, 0)
  }

  func testTheDisplayedRectOfAShortNoteIsWidenedToTheMinimum() {
    // 30 ticks at 12 pt per beat: 0.375 pt logically, drawn and hit 2 pt wide from the same start and row.
    let geometry = RollGeometry(pxPerBeat: 12, rowHeight: 14)
    XCTAssertEqual(RollGeometry.minDisplayedNoteWidth, 2)
    let logical = geometry.noteRect(start: 960, length: 30, pitch: 60)
    XCTAssertEqual(logical.width, 0.375, accuracy: 1e-9)
    let displayed = geometry.displayedNoteRect(start: 960, length: 30, pitch: 60)
    XCTAssertEqual(displayed, CGRect(x: logical.minX, y: logical.minY, width: 2, height: logical.height))
    XCTAssertEqual(geometry.displayedNoteRect(start: 0, length: 0, pitch: 60).width, 2, "even an empty note shows")
  }

  func testTheDisplayedRectOfANoteWiderThanTheMinimumIsItsRect() {
    let geometry = RollGeometry(pxPerBeat: 48, rowHeight: 14)
    XCTAssertEqual(geometry.displayedNoteRect(start: 960, length: 480, pitch: 60),
                   geometry.noteRect(start: 960, length: 480, pitch: 60))
    XCTAssertEqual(geometry.displayedNoteRect(start: 0, length: 40, pitch: 60).width, 2, accuracy: 1e-9,
                   "exactly the minimum")
  }

  func testRowRectSpansTheGivenWidth() {
    let rect = RollGeometry(rowHeight: 20).rowRect(pitch: 100, width: 500)
    XCTAssertEqual(rect, CGRect(x: 0, y: 27 * 20, width: 500, height: 20))
  }

  // MARK: - View and content points

  func testViewAndContentPointsDifferByTheScroll() {
    let geometry = RollGeometry()
    let scroll = CGPoint(x: 120, y: 300)
    XCTAssertEqual(geometry.contentPoint(viewPoint: CGPoint(x: 10, y: 20), scroll: scroll), CGPoint(x: 130, y: 320))
    XCTAssertEqual(geometry.viewPoint(contentPoint: CGPoint(x: 130, y: 320), scroll: scroll), CGPoint(x: 10, y: 20))
  }

  func testAViewPointRoundTripsToATickAndPitchUnderScrollAndZoom() {
    for zoom in zoomLevels {
      for height in rowHeights {
        let geometry = RollGeometry(pxPerBeat: zoom, rowHeight: height)
        let scroll = CGPoint(x: 333.5, y: 612.25)
        for tick in [0, 77, 960, 20_000] {
          for pitch in [0, 33, 60, 127] {
            // The middle of the cell, so floating error cannot cross a boundary.
            let content = CGPoint(x: geometry.x(tick: tick) + 0.001, y: geometry.y(pitch: pitch) + height / 2)
            let view = geometry.viewPoint(contentPoint: content, scroll: scroll)
            let back = geometry.contentPoint(viewPoint: view, scroll: scroll)
            XCTAssertEqual(geometry.tick(atX: back.x), tick)
            XCTAssertEqual(geometry.pitch(atY: back.y), pitch)
          }
        }
      }
    }
  }

  // MARK: - Visible ranges

  func testVisibleTicksFromTheScrollToTheRightEdge() {
    let geometry = RollGeometry(pxPerBeat: 48)   // 0.05 pt per tick
    let viewport = CGSize(width: 200, height: 100)
    XCTAssertEqual(geometry.visibleTicks(viewport: viewport, scroll: .zero), 0..<4000)
    XCTAssertEqual(geometry.visibleTicks(viewport: viewport, scroll: CGPoint(x: 48, y: 0)), 960..<4960)
    XCTAssertEqual(geometry.visibleTicks(viewport: viewport, scroll: CGPoint(x: 10, y: 0)), 200..<4200)
  }

  func testVisibleTicksIncludePartiallyVisibleTicks() {
    let geometry = RollGeometry(pxPerBeat: 48)
    let viewport = CGSize(width: 200, height: 100)
    // Left edge inside tick 0, right edge inside tick 4000.
    XCTAssertEqual(geometry.visibleTicks(viewport: viewport, scroll: CGPoint(x: 0.03, y: 0)), 0..<4001)
  }

  func testVisibleTicksClampAtZeroAndEmptyViewportsAreEmpty() {
    let geometry = RollGeometry(pxPerBeat: 48)
    XCTAssertEqual(geometry.visibleTicks(viewport: CGSize(width: 200, height: 10), scroll: CGPoint(x: -50, y: 0)), 0..<3000)
    XCTAssertTrue(geometry.visibleTicks(viewport: CGSize(width: 0, height: 10), scroll: CGPoint(x: 48, y: 0)).isEmpty)
    XCTAssertTrue(geometry.visibleTicks(viewport: CGSize(width: 100, height: 10), scroll: CGPoint(x: -500, y: 0)).isEmpty)
  }

  func testVisibleTicksCoverEveryVisibleTickAtAnyZoom() {
    for zoom in zoomLevels {
      let geometry = RollGeometry(pxPerBeat: zoom)
      let viewport = CGSize(width: 731, height: 100)
      let scroll = CGPoint(x: 1234.5, y: 0)
      let range = geometry.visibleTicks(viewport: viewport, scroll: scroll)
      // The tick under the first and last visible pixel lie inside; the neighbours do not both lie inside.
      XCTAssertTrue(range.contains(geometry.tick(atX: scroll.x)))
      XCTAssertTrue(range.contains(geometry.tick(atX: scroll.x + viewport.width - 0.001)))
      XCTAssertEqual(range.lowerBound, geometry.tick(atX: scroll.x))
      XCTAssertFalse(range.contains(range.lowerBound - 1))
      XCTAssertGreaterThanOrEqual(geometry.x(tick: range.upperBound), scroll.x + viewport.width - 1e-6)
      XCTAssertLessThan(geometry.x(tick: range.upperBound - 1), scroll.x + viewport.width)
    }
  }

  func testVisiblePitchesAtTheTop() {
    let geometry = RollGeometry(rowHeight: 14)
    // 140 pt is exactly ten rows: the row starting at the bottom edge is not visible.
    XCTAssertEqual(geometry.visiblePitches(viewport: CGSize(width: 100, height: 140), scroll: .zero), 118...127)
  }

  func testVisiblePitchesIncludePartiallyVisibleRows() {
    let geometry = RollGeometry(rowHeight: 14)
    let viewport = CGSize(width: 100, height: 140)
    XCTAssertEqual(geometry.visiblePitches(viewport: viewport, scroll: CGPoint(x: 0, y: 7)), 117...127)
    XCTAssertEqual(geometry.visiblePitches(viewport: viewport, scroll: CGPoint(x: 0, y: 14)), 117...126)
    XCTAssertEqual(geometry.visiblePitches(viewport: viewport, scroll: CGPoint(x: 0, y: 14.5)), 116...126)
  }

  func testVisiblePitchesAtTheBottomAndBeyondTheEdges() {
    let geometry = RollGeometry(rowHeight: 14)
    let viewport = CGSize(width: 100, height: 140)
    XCTAssertEqual(geometry.visiblePitches(viewport: viewport, scroll: CGPoint(x: 0, y: 128 * 14 - 140)), 0...9)
    XCTAssertEqual(geometry.visiblePitches(viewport: viewport, scroll: CGPoint(x: 0, y: 5000)), 0...0)
    XCTAssertEqual(geometry.visiblePitches(viewport: viewport, scroll: CGPoint(x: 0, y: -50)), 121...127)
    XCTAssertEqual(geometry.visiblePitches(viewport: CGSize(width: 100, height: 99999), scroll: .zero), 0...127)
  }

  func testVisiblePitchesOfAnEmptyViewportIsTheRowAtTheTopEdge() {
    let geometry = RollGeometry(rowHeight: 14)
    XCTAssertEqual(geometry.visiblePitches(viewport: CGSize(width: 100, height: 0), scroll: CGPoint(x: 0, y: 28)), 125...125)
  }

  // MARK: - Content size and scroll clamping

  func testContentWidthCoversTheEndPlusSlackAndHeightIs128Rows() {
    let geometry = RollGeometry(pxPerBeat: 48, rowHeight: 14)
    // 4/4: one bar (3840 ticks) of slack after a 4-bar progression.
    let size = geometry.contentSize(endTick: 4 * 3840, slackTicks: 3840, viewport: CGSize(width: 100, height: 100))
    XCTAssertEqual(size.width, 5 * 4 * 48, accuracy: 1e-9)
    XCTAssertEqual(size.height, 128 * 14)
  }

  func testContentWidthIsNeverNarrowerThanTheViewport() {
    let geometry = RollGeometry(pxPerBeat: 12)
    let size = geometry.contentSize(endTick: 960, slackTicks: 3840, viewport: CGSize(width: 900, height: 100))
    XCTAssertEqual(size.width, 900)
    XCTAssertEqual(RollGeometry().contentSize(endTick: 0, slackTicks: 0, viewport: .zero).width, 0)
  }

  func testNegativeEndOrSlackCountsAsZero() {
    let geometry = RollGeometry(pxPerBeat: 48)
    let size = geometry.contentSize(endTick: -960, slackTicks: -960, viewport: .zero)
    XCTAssertEqual(size.width, 0)
  }

  func testClampedScrollStaysInsideTheContent() {
    let geometry = RollGeometry()
    let viewport = CGSize(width: 400, height: 300)
    let content = CGSize(width: 1000, height: 1792)
    XCTAssertEqual(geometry.clampedScroll(CGPoint(x: 100, y: 200), viewport: viewport, contentSize: content),
                   CGPoint(x: 100, y: 200))
    XCTAssertEqual(geometry.clampedScroll(CGPoint(x: -5, y: -5), viewport: viewport, contentSize: content), .zero)
    XCTAssertEqual(geometry.clampedScroll(CGPoint(x: 9999, y: 9999), viewport: viewport, contentSize: content),
                   CGPoint(x: 600, y: 1492))
  }

  func testClampedScrollIsZeroWhenTheContentFitsTheViewport() {
    let geometry = RollGeometry()
    XCTAssertEqual(geometry.clampedScroll(CGPoint(x: 50, y: 50), viewport: CGSize(width: 400, height: 3000),
                                          contentSize: CGSize(width: 400, height: 1792)), .zero)
  }

  func testClampedScrollTakesNonFiniteInputAsZero() {
    let clamped = RollGeometry().clampedScroll(CGPoint(x: CGFloat.nan, y: CGFloat.infinity), viewport: CGSize(width: 10, height: 10),
                                               contentSize: CGSize(width: 100, height: 100))
    XCTAssertEqual(clamped.x, 0)
    XCTAssertEqual(clamped.y, 90)
  }

  // MARK: - Zoom about a point

  /// The beat under view x `viewX`.
  private func beat(_ geometry: RollGeometry, viewX: CGFloat, scroll: CGPoint) -> CGFloat {
    (scroll.x + viewX) / geometry.pxPerBeat
  }

  /// The row (fractional, from the top) under view y `viewY`.
  private func row(_ geometry: RollGeometry, viewY: CGFloat, scroll: CGPoint) -> CGFloat {
    (scroll.y + viewY) / geometry.rowHeight
  }

  func testHorizontalZoomKeepsTheTickUnderThePoint() {
    let scroll = CGPoint(x: 700, y: 321)
    for start in [12, 48, 100.3, 192] as [CGFloat] {
      for factor in [0.5, 0.8, 1, 1.25, 2, 3.3] as [CGFloat] {
        for viewX in [0, 1, 250, 777] as [CGFloat] {
          let geometry = RollGeometry(pxPerBeat: start)
          let zoomed = geometry.zoomedHorizontally(by: factor, aboutViewX: viewX, scroll: scroll)
          XCTAssertEqual(beat(zoomed.geometry, viewX: viewX, scroll: zoomed.scroll),
                         beat(geometry, viewX: viewX, scroll: scroll), accuracy: 1e-9,
                         "from \(start) by \(factor) about \(viewX)")
          XCTAssertEqual(zoomed.scroll.y, scroll.y, "vertical scroll is untouched")
          XCTAssertEqual(zoomed.geometry.rowHeight, geometry.rowHeight)
          XCTAssertEqual(zoomed.geometry.ticksPerBeat, geometry.ticksPerBeat)
        }
      }
    }
  }

  func testHorizontalZoomScalesPxPerBeatByTheFactor() {
    let zoomed = RollGeometry(pxPerBeat: 48).zoomedHorizontally(by: 1.5, aboutViewX: 100, scroll: .zero)
    XCTAssertEqual(zoomed.geometry.pxPerBeat, 72, accuracy: 1e-9)
  }

  func testHorizontalZoomClampsAndStillKeepsThePointForTheClampedFactor() {
    let scroll = CGPoint(x: 700, y: 0)
    let viewX: CGFloat = 250
    // 96 × 4 would be 384: clamped to 192, an effective factor of 2.
    let geometry = RollGeometry(pxPerBeat: 96)
    let zoomedIn = geometry.zoomedHorizontally(by: 4, aboutViewX: viewX, scroll: scroll)
    XCTAssertEqual(zoomedIn.geometry.pxPerBeat, 192)
    XCTAssertEqual(beat(zoomedIn.geometry, viewX: viewX, scroll: zoomedIn.scroll), beat(geometry, viewX: viewX, scroll: scroll),
                   accuracy: 1e-9)
    XCTAssertEqual(zoomedIn.scroll.x, (700 + 250) * 2 - 250, accuracy: 1e-9)
    // 24 × 0.1 would be 2.4: clamped to 12, an effective factor of 0.5.
    let small = RollGeometry(pxPerBeat: 24)
    let zoomedOut = small.zoomedHorizontally(by: 0.1, aboutViewX: viewX, scroll: scroll)
    XCTAssertEqual(zoomedOut.geometry.pxPerBeat, 12)
    XCTAssertEqual(beat(zoomedOut.geometry, viewX: viewX, scroll: zoomedOut.scroll), beat(small, viewX: viewX, scroll: scroll),
                   accuracy: 1e-9)
  }

  func testHorizontalZoomAtTheLimitMovesNothing() {
    let scroll = CGPoint(x: 700, y: 40)
    let geometry = RollGeometry(pxPerBeat: 192)
    let zoomed = geometry.zoomedHorizontally(by: 2, aboutViewX: 250, scroll: scroll)
    XCTAssertEqual(zoomed.geometry, geometry)
    XCTAssertEqual(zoomed.scroll.x, scroll.x, accuracy: 1e-9)
  }

  func testHorizontalZoomIgnoresAFactorThatIsNotPositiveAndFinite() {
    let scroll = CGPoint(x: 700, y: 40)
    let geometry = RollGeometry(pxPerBeat: 60)
    for factor in [0, -2, CGFloat.nan, CGFloat.infinity] {
      let zoomed = geometry.zoomedHorizontally(by: factor, aboutViewX: 250, scroll: scroll)
      XCTAssertEqual(zoomed.geometry, geometry)
      XCTAssertEqual(zoomed.scroll, scroll)
    }
  }

  func testHorizontalZoomNearTheLeftEdgeLeavesTheClampToTheCaller() {
    // Zooming out about a point near the start asks for a negative scroll; clampedScroll is the caller's step.
    let geometry = RollGeometry(pxPerBeat: 48)
    let zoomed = geometry.zoomedHorizontally(by: 0.5, aboutViewX: 300, scroll: .zero)
    XCTAssertEqual(zoomed.scroll.x, -150, accuracy: 1e-9)
    let clamped = zoomed.geometry.clampedScroll(zoomed.scroll, viewport: CGSize(width: 600, height: 100),
                                                contentSize: CGSize(width: 2000, height: 1792))
    XCTAssertEqual(clamped.x, 0)
  }

  func testVerticalZoomKeepsThePitchRowUnderThePoint() {
    let scroll = CGPoint(x: 55, y: 480)
    for start in [8, 14, 13.7, 24] as [CGFloat] {
      for factor in [0.5, 0.8, 1, 1.25, 2, 3.3] as [CGFloat] {
        for viewY in [0, 1, 90, 399] as [CGFloat] {
          let geometry = RollGeometry(rowHeight: start)
          let zoomed = geometry.zoomedVertically(by: factor, aboutViewY: viewY, scroll: scroll)
          XCTAssertEqual(row(zoomed.geometry, viewY: viewY, scroll: zoomed.scroll),
                         row(geometry, viewY: viewY, scroll: scroll), accuracy: 1e-9, "from \(start) by \(factor) about \(viewY)")
          XCTAssertEqual(zoomed.scroll.x, scroll.x, "horizontal scroll is untouched")
          XCTAssertEqual(zoomed.geometry.pxPerBeat, geometry.pxPerBeat)
        }
      }
    }
  }

  func testVerticalZoomKeepsTheSamePitchUnderThePoint() {
    let geometry = RollGeometry(rowHeight: 14)
    let scroll = CGPoint(x: 0, y: 600)
    let viewY: CGFloat = 123.4
    let pitchBefore = geometry.pitch(atY: scroll.y + viewY)
    let zoomed = geometry.zoomedVertically(by: 1.5, aboutViewY: viewY, scroll: scroll)
    XCTAssertEqual(zoomed.geometry.rowHeight, 21, accuracy: 1e-9)
    XCTAssertEqual(zoomed.geometry.pitch(atY: zoomed.scroll.y + viewY), pitchBefore)
  }

  func testVerticalZoomClampsAndStillKeepsThePointForTheClampedFactor() {
    let scroll = CGPoint(x: 0, y: 480)
    let viewY: CGFloat = 90
    let geometry = RollGeometry(rowHeight: 16)
    let zoomedIn = geometry.zoomedVertically(by: 5, aboutViewY: viewY, scroll: scroll)
    XCTAssertEqual(zoomedIn.geometry.rowHeight, 24)
    XCTAssertEqual(row(zoomedIn.geometry, viewY: viewY, scroll: zoomedIn.scroll), row(geometry, viewY: viewY, scroll: scroll),
                   accuracy: 1e-9)
    let zoomedOut = geometry.zoomedVertically(by: 0.1, aboutViewY: viewY, scroll: scroll)
    XCTAssertEqual(zoomedOut.geometry.rowHeight, 8)
    XCTAssertEqual(row(zoomedOut.geometry, viewY: viewY, scroll: zoomedOut.scroll), row(geometry, viewY: viewY, scroll: scroll),
                   accuracy: 1e-9)
  }

  // MARK: - Toolbar steps

  func testZoomInAndOutStepThroughTheDocumentedLevels() {
    let viewport = CGSize(width: 600, height: 400)
    var geometry = RollGeometry(pxPerBeat: 12)
    var scroll = CGPoint(x: 0, y: 100)
    var seen: [CGFloat] = [geometry.pxPerBeat]
    for _ in 0..<5 {
      (geometry, scroll) = geometry.zoomIn(viewport: viewport, scroll: scroll)
      seen.append(geometry.pxPerBeat)
    }
    XCTAssertEqual(seen, [12, 24, 48, 96, 192, 192])
    for _ in 0..<5 {
      (geometry, scroll) = geometry.zoomOut(viewport: viewport, scroll: scroll)
      seen.append(geometry.pxPerBeat)
    }
    XCTAssertEqual(Array(seen.suffix(5)), [96, 48, 24, 12, 12])
  }

  func testToolbarZoomKeepsTheViewportCentreFixed() {
    let viewport = CGSize(width: 600, height: 400)
    let geometry = RollGeometry(pxPerBeat: 48)
    let scroll = CGPoint(x: 1000, y: 250)
    for zoomed in [geometry.zoomIn(viewport: viewport, scroll: scroll), geometry.zoomOut(viewport: viewport, scroll: scroll)] {
      XCTAssertEqual(beat(zoomed.geometry, viewX: 300, scroll: zoomed.scroll), beat(geometry, viewX: 300, scroll: scroll),
                     accuracy: 1e-9)
      XCTAssertEqual(zoomed.scroll.y, 250, "the toolbar zooms time only")
    }
  }

  func testCanZoomReflectsTheLimits() {
    XCTAssertFalse(RollGeometry(pxPerBeat: 192).canZoomIn)
    XCTAssertTrue(RollGeometry(pxPerBeat: 192).canZoomOut)
    XCTAssertFalse(RollGeometry(pxPerBeat: 12).canZoomOut)
    XCTAssertTrue(RollGeometry(pxPerBeat: 12).canZoomIn)
    XCTAssertTrue(RollGeometry().canZoomIn && RollGeometry().canZoomOut)
  }

  // MARK: - First open

  func testInitialScrollStartsAtBarOneAndCentresTheRegister() {
    let geometry = RollGeometry(rowHeight: 14)
    let viewport = CGSize(width: 700, height: 400)
    for role in TrackRole.allCases {
      let scroll = geometry.initialScroll(register: role.register, viewport: viewport)
      XCTAssertEqual(scroll.x, 0)
      let top = geometry.y(pitch: role.register.upperBound)
      let bottom = geometry.y(pitch: role.register.lowerBound) + geometry.rowHeight
      XCTAssertEqual(scroll.y + viewport.height / 2, (top + bottom) / 2, accuracy: 1e-9, "\(role)")
    }
    // Bass 28...55: rows from y(55) = 1008 to y(28) + 14 = 1400, centre 1204, less half the viewport (200).
    XCTAssertEqual(geometry.initialScroll(register: TrackRole.bass.register, viewport: viewport).y, 1004, accuracy: 1e-9)
    // Lead 60...84: 602 to 952, centre 777. Other 48...72: 770 to 1120, centre 945.
    XCTAssertEqual(geometry.initialScroll(register: TrackRole.lead.register, viewport: viewport).y, 577, accuracy: 1e-9)
    XCTAssertEqual(geometry.initialScroll(register: TrackRole.other.register, viewport: viewport).y, 745, accuracy: 1e-9)
  }

  func testInitialScrollIsClampedToTheContent() {
    let geometry = RollGeometry(rowHeight: 14)
    // A register at the top of the keyboard cannot be centred: scroll stops at 0.
    XCTAssertEqual(geometry.initialScroll(register: 120...127, viewport: CGSize(width: 700, height: 400)).y, 0)
    // At the bottom it stops at the last scroll position.
    XCTAssertEqual(geometry.initialScroll(register: 0...5, viewport: CGSize(width: 700, height: 400)).y, 128 * 14 - 400,
                   accuracy: 1e-9)
    // A viewport taller than the keyboard has nowhere to scroll.
    XCTAssertEqual(geometry.initialScroll(register: TrackRole.lead.register, viewport: CGSize(width: 700, height: 3000)).y, 0)
  }

  func testInitialScrollTakesAnOutOfRangeRegisterAsClamped() {
    let scroll = RollGeometry().initialScroll(register: -10...300, viewport: CGSize(width: 700, height: 400))
    XCTAssertEqual(scroll.x, 0)
    XCTAssertEqual(scroll.y, 696, accuracy: 1e-9, "0...127 centres at 896; less half the 400-point viewport")
  }

  // MARK: - Keyboard column

  func testBlackKeysAreThePitchClassesOfTheSharps() {
    let black = Set([1, 3, 6, 8, 10])
    for pitch in 0...127 {
      XCTAssertEqual(RollGeometry.isBlackKey(pitch: pitch), black.contains(pitch % 12), "\(pitch)")
    }
    XCTAssertFalse(RollGeometry.isBlackKey(pitch: -1))
    XCTAssertFalse(RollGeometry.isBlackKey(pitch: 128))
  }

  func testCLabelsFollowMiddleCIsC4() {
    XCTAssertEqual(RollGeometry.cLabel(pitch: 60), "C4")
    XCTAssertEqual(RollGeometry.cLabel(pitch: 0), "C-1")
    XCTAssertEqual(RollGeometry.cLabel(pitch: 12), "C0")
    XCTAssertEqual(RollGeometry.cLabel(pitch: 24), "C1")
    XCTAssertEqual(RollGeometry.cLabel(pitch: 72), "C5")
    XCTAssertEqual(RollGeometry.cLabel(pitch: 120), "C9")
  }

  func testOnlyCsAreLabelledAndOnlyInRange() {
    XCTAssertEqual((0...127).filter { RollGeometry.cLabel(pitch: $0) != nil }.count, 11)
    XCTAssertNil(RollGeometry.cLabel(pitch: 61))
    XCTAssertNil(RollGeometry.cLabel(pitch: 127))
    XCTAssertNil(RollGeometry.cLabel(pitch: -12))
    XCTAssertNil(RollGeometry.cLabel(pitch: 132))
  }
}
