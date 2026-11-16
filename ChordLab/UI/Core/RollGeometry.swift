import CoreGraphics
import Foundation

/// The piano roll's geometry, pure and in one place so that drawing, hit-testing, scrolling and zoom cannot
/// disagree (tracks design D14).
///
/// **Content coordinates** are the roll's own space, before any scroll: `x = tick / ticksPerBeat × pxPerBeat`
/// from the progression's tick 0, and `y = (127 − pitch) × rowHeight` for the TOP of a pitch's row, so pitch
/// 127 is the top row and the 128 rows make the content `128 × rowHeight` tall. A row owns the half-open
/// interval `[y(pitch:), y(pitch:) + rowHeight)`, so a point exactly on a boundary belongs to the row below
/// it on screen (the lower pitch's row starts there) and `pitch(atY: y(pitch:)) == pitch`. **View
/// coordinates** are points inside the scrolling viewport; the scroll offset is the content point at the
/// viewport's top-left, so `content = view + scroll`. The functions that take a scroll never clamp it; the
/// caller runs `clampedScroll(_:viewport:contentSize:)` when it stores one.
///
/// `pxPerBeat` is 12...192 (default 48) and `rowHeight` 8...24 (default 14), both clamped by `init`. The
/// inverse mappings (`tick(atX:)`, `pitch(atY:)`) add a millionth of a tick (or row) before flooring, so a
/// value computed by the forward mapping always lands back on its own tick or row despite floating-point
/// error; no real pointer position is within that distance of a boundary on the wrong side.
///
/// Depends on nothing but CoreGraphics and Foundation; unit-tested by the `ChordLabUICore` package
/// (Tests/ChordLabUICoreTests/RollGeometryTests.swift).
struct RollGeometry: Equatable {
  /// Horizontal zoom: points per beat (design D14).
  static let pxPerBeatRange: ClosedRange<CGFloat> = 12...192
  /// Vertical zoom: points per pitch row (design D14).
  static let rowHeightRange: ClosedRange<CGFloat> = 8...24
  static let defaultPxPerBeat: CGFloat = 48
  static let defaultRowHeight: CGFloat = 14
  /// MIDI pitches 0...127.
  static let pitchCount = 128

  /// A millionth of a tick or row: what the inverse mappings add before flooring (see the type comment).
  private static let epsilon = 1e-6
  /// The largest tick or row count a pointer position is converted to, so an infinite or absurd coordinate
  /// cannot overflow `Int`; well above the engine's largest tick (15,728,640).
  private static let largestValue = 1e12

  let pxPerBeat: CGFloat
  let rowHeight: CGFloat
  let ticksPerBeat: Int

  /// Clamps `pxPerBeat` and `rowHeight` to their ranges (not a number takes the default) and `ticksPerBeat` to
  /// at least 1 (the engine's grid is 960; the snapshot's `ticksPerBeat` is passed through).
  init(pxPerBeat: CGFloat = RollGeometry.defaultPxPerBeat, rowHeight: CGFloat = RollGeometry.defaultRowHeight,
       ticksPerBeat: Int = 960) {
    self.pxPerBeat = Self.clamp(pxPerBeat, to: Self.pxPerBeatRange, default: Self.defaultPxPerBeat)
    self.rowHeight = Self.clamp(rowHeight, to: Self.rowHeightRange, default: Self.defaultRowHeight)
    self.ticksPerBeat = max(1, ticksPerBeat)
  }

  private static func clamp(_ value: CGFloat, to range: ClosedRange<CGFloat>, default fallback: CGFloat) -> CGFloat {
    value.isNaN ? fallback : min(max(value, range.lowerBound), range.upperBound)
  }

  // MARK: - Ticks and x

  /// The x of a tick in content coordinates: `tick / ticksPerBeat × pxPerBeat`.
  func x(tick: Int) -> CGFloat {
    CGFloat(tick) / CGFloat(ticksPerBeat) * pxPerBeat
  }

  /// The tick under content x: floored to an integer tick and never below 0 (a point left of the start is
  /// tick 0; not a number is 0).
  func tick(atX x: CGFloat) -> Int {
    Int(Self.floored(tickValue(atX: x) + Self.epsilon, lowest: 0))
  }

  /// The fractional tick at content x, finite and within ±`largestValue`.
  private func tickValue(atX x: CGFloat) -> Double {
    Self.finite(Double(x) / Double(pxPerBeat) * Double(ticksPerBeat))
  }

  // MARK: - Pitches and y

  /// The y of the TOP of a pitch's row in content coordinates: `(127 − pitch) × rowHeight`.
  func y(pitch: Int) -> CGFloat {
    CGFloat(Self.pitchCount - 1 - pitch) * rowHeight
  }

  /// The pitch whose row contains content y: 127 at the top, clamped to 0...127 (a point above the first row
  /// is 127, below the last 0; not a number is 127).
  func pitch(atY y: CGFloat) -> Int {
    Self.pitchCount - 1 - row(atY: y)
  }

  /// The row index from the top (0 = pitch 127) at content y, clamped to 0...127.
  private func row(atY y: CGFloat) -> Int {
    Int(Self.floored(rowValue(atY: y) + Self.epsilon, lowest: 0, highest: Double(Self.pitchCount - 1)))
  }

  /// The fractional row at content y, finite and within ±`largestValue`.
  private func rowValue(atY y: CGFloat) -> Double {
    Self.finite(Double(y) / Double(rowHeight))
  }

  private static func finite(_ value: Double) -> Double {
    value.isNaN ? 0 : min(max(value, -largestValue), largestValue)
  }

  private static func floored(_ value: Double, lowest: Double, highest: Double = largestValue) -> Double {
    min(max(value.rounded(.down), lowest), highest)
  }

  // MARK: - Rectangles

  /// A note's rectangle in content coordinates: from `x(tick: start)` for `length` ticks (never a negative
  /// width), one row tall at the pitch's row.
  func noteRect(start: Int, length: Int, pitch: Int) -> CGRect {
    CGRect(x: x(tick: start), y: y(pitch: pitch), width: max(0, x(tick: length)), height: rowHeight)
  }

  /// The narrowest a note shows on screen, in points: a note shorter than this at the current zoom (30 ticks at 12 pt
  /// per beat is 0.375 pt) is drawn and hit this wide.
  static let minDisplayedNoteWidth: CGFloat = 2

  /// A note's rectangle as the roll shows it, in content coordinates: `noteRect` widened to at least
  /// `minDisplayedNoteWidth` from the same start (its left edge and row never move). Drawing, hit-testing (`RollHit`:
  /// clicks, edge zones, the context menu and the cursor) and the rubber band all use it, so a short note is hit
  /// where it is drawn; the canvas then insets it for its own look (a point's gap, the row inset), never wider.
  func displayedNoteRect(start: Int, length: Int, pitch: Int) -> CGRect {
    let rect = noteRect(start: start, length: length, pitch: pitch)
    return CGRect(x: rect.minX, y: rect.minY, width: max(rect.width, Self.minDisplayedNoteWidth), height: rect.height)
  }

  /// `minDisplayedNoteWidth` in ticks at this zoom, rounded up (160 at 12 pt per beat): how far a shorter note's
  /// displayed rectangle reaches past its start. `RollVisibleNotes` takes every note as at least this long, so a short
  /// note whose widened tail crosses the viewport's left edge is still drawn where `RollHit` hits it.
  var minDisplayedNoteTicks: Int {
    Int((Double(Self.minDisplayedNoteWidth) / Double(pxPerBeat) * Double(ticksPerBeat)).rounded(.up))
  }

  /// A pitch's full-width row (the keyboard column, the guide bands, the row stripes).
  func rowRect(pitch: Int, width: CGFloat) -> CGRect {
    CGRect(x: 0, y: y(pitch: pitch), width: width, height: rowHeight)
  }

  // MARK: - View and content points

  /// The content point under a view point: `view + scroll` (the scroll is the content point at the viewport's
  /// top-left).
  func contentPoint(viewPoint: CGPoint, scroll: CGPoint) -> CGPoint {
    CGPoint(x: viewPoint.x + scroll.x, y: viewPoint.y + scroll.y)
  }

  /// The view point of a content point: `content − scroll`.
  func viewPoint(contentPoint: CGPoint, scroll: CGPoint) -> CGPoint {
    CGPoint(x: contentPoint.x - scroll.x, y: contentPoint.y - scroll.y)
  }

  // MARK: - Visible ranges

  /// The ticks the viewport shows, partially visible ones included: from the tick under the left edge (never
  /// below 0) up to, exclusive, the first tick that starts at or after the right edge. Empty for a viewport
  /// with no width, or one entirely left of tick 0.
  func visibleTicks(viewport: CGSize, scroll: CGPoint) -> Range<Int> {
    let first = tick(atX: scroll.x)
    guard viewport.width > 0 else { return first..<first }
    let right = tickValue(atX: scroll.x + viewport.width)
    let last = Int(Self.floored((right - Self.epsilon).rounded(.up), lowest: 0))
    return first..<max(first, last)
  }

  /// The pitches the viewport shows, partially visible rows included, clamped to 0...127: from the row under
  /// the top edge to the row containing the last visible point (a bottom edge exactly on a row boundary does
  /// not show the next row). An empty viewport shows just the row at its top edge; a scroll beyond either end
  /// shows the nearest row(s), never an empty or inverted range.
  func visiblePitches(viewport: CGSize, scroll: CGPoint) -> ClosedRange<Int> {
    let top = row(atY: scroll.y)
    var bottom = top
    if viewport.height > 0 {
      let edge = rowValue(atY: scroll.y + viewport.height)
      let lastRow = Int(Self.floored((edge - Self.epsilon).rounded(.up) - 1, lowest: 0, highest: Double(Self.pitchCount - 1)))
      bottom = max(top, lastRow)
    }
    return (Self.pitchCount - 1 - bottom)...(Self.pitchCount - 1 - top)
  }

  // MARK: - Content size and scroll

  /// The content size: the width of `endTick + slackTicks` (the caller passes the progression's end and one
  /// bar of the meter as slack, like the strip's trailing slack), never narrower than the viewport, and
  /// `128 × rowHeight` tall. A negative end or slack counts as 0.
  func contentSize(endTick: Int, slackTicks: Int, viewport: CGSize) -> CGSize {
    let ticks = max(0, endTick) + max(0, slackTicks)
    return CGSize(width: max(x(tick: ticks), viewport.width), height: CGFloat(Self.pitchCount) * rowHeight)
  }

  /// A scroll offset inside the content: x in `0...max(0, content.width − viewport.width)`, y likewise, so a
  /// content that fits the viewport pins to 0. Not a number is 0.
  func clampedScroll(_ scroll: CGPoint, viewport: CGSize, contentSize: CGSize) -> CGPoint {
    func clamp(_ value: CGFloat, content: CGFloat, viewport: CGFloat) -> CGFloat {
      guard !value.isNaN else { return 0 }
      return min(max(value, 0), max(0, content - viewport))
    }
    return CGPoint(x: clamp(scroll.x, content: contentSize.width, viewport: viewport.width),
                   y: clamp(scroll.y, content: contentSize.height, viewport: viewport.height))
  }

  // MARK: - Zoom

  /// Zooms time by `factor` keeping the tick under view x `aboutViewX` where it is: `pxPerBeat` becomes
  /// `pxPerBeat × factor` clamped to its range, and the returned scroll puts the same content point under that
  /// view x (to within floating error). When the range clamps, the point still stays fixed for the factor
  /// actually applied. A factor that is not positive and finite changes nothing. The scroll is not clamped
  /// (zooming out near the start asks for a negative x, which only `clampedScroll` can resolve, moving the
  /// point); the vertical scroll and the row height are untouched.
  func zoomedHorizontally(by factor: CGFloat, aboutViewX viewX: CGFloat, scroll: CGPoint)
    -> (geometry: RollGeometry, scroll: CGPoint) {
    guard factor.isFinite, factor > 0, viewX.isFinite, scroll.x.isFinite else { return (self, scroll) }
    let zoomed = RollGeometry(pxPerBeat: pxPerBeat * factor, rowHeight: rowHeight, ticksPerBeat: ticksPerBeat)
    let applied = zoomed.pxPerBeat / pxPerBeat
    return (zoomed, CGPoint(x: (scroll.x + viewX) * applied - viewX, y: scroll.y))
  }

  /// Zooms pitch rows by `factor` keeping the row under view y `aboutViewY` where it is: the same rule as
  /// `zoomedHorizontally`, on `rowHeight` (8...24) and the vertical scroll.
  func zoomedVertically(by factor: CGFloat, aboutViewY viewY: CGFloat, scroll: CGPoint)
    -> (geometry: RollGeometry, scroll: CGPoint) {
    guard factor.isFinite, factor > 0, viewY.isFinite, scroll.y.isFinite else { return (self, scroll) }
    let zoomed = RollGeometry(pxPerBeat: pxPerBeat, rowHeight: rowHeight * factor, ticksPerBeat: ticksPerBeat)
    let applied = zoomed.rowHeight / rowHeight
    return (zoomed, CGPoint(x: scroll.x, y: (scroll.y + viewY) * applied - viewY))
  }

  /// The toolbar's + (decision, not in the design): doubles `pxPerBeat` (12, 24, 48, 96, 192; a pinched-in value
  /// in between doubles too, up to 192) about the viewport's horizontal centre, leaving rows and the vertical
  /// scroll alone.
  func zoomIn(viewport: CGSize, scroll: CGPoint) -> (geometry: RollGeometry, scroll: CGPoint) {
    zoomedHorizontally(by: 2, aboutViewX: viewport.width / 2, scroll: scroll)
  }

  /// The toolbar's −: halves `pxPerBeat` about the viewport's horizontal centre (see `zoomIn`).
  func zoomOut(viewport: CGSize, scroll: CGPoint) -> (geometry: RollGeometry, scroll: CGPoint) {
    zoomedHorizontally(by: 0.5, aboutViewX: viewport.width / 2, scroll: scroll)
  }

  /// Whether the toolbar's + can still zoom (`pxPerBeat` below its maximum).
  var canZoomIn: Bool { pxPerBeat < Self.pxPerBeatRange.upperBound }

  /// Whether the toolbar's − can still zoom (`pxPerBeat` above its minimum).
  var canZoomOut: Bool { pxPerBeat > Self.pxPerBeatRange.lowerBound }

  // MARK: - First open

  /// Where a track's roll scrolls the first time it opens (design D14, D2): x 0 (bar 1) and the role's register
  /// (`TrackRole.register`; bounds clamped to 0...127) vertically centred in the viewport, the scroll clamped
  /// to the 128 rows (a register near either end of the keyboard cannot be centred and stops at the edge; a
  /// viewport taller than the keyboard pins to 0).
  func initialScroll(register: ClosedRange<Int>, viewport: CGSize) -> CGPoint {
    let top = min(max(register.upperBound, 0), Self.pitchCount - 1)
    let bottom = min(max(register.lowerBound, 0), Self.pitchCount - 1)
    let centre = (y(pitch: top) + y(pitch: bottom) + rowHeight) / 2
    let content = CGSize(width: viewport.width, height: CGFloat(Self.pitchCount) * rowHeight)
    return clampedScroll(CGPoint(x: 0, y: centre - viewport.height / 2), viewport: viewport, contentSize: content)
  }

  // MARK: - Keyboard column

  /// Whether a pitch is a black key (pitch class 1, 3, 6, 8 or 10); false outside 0...127.
  static func isBlackKey(pitch: Int) -> Bool {
    guard (0..<pitchCount).contains(pitch) else { return false }
    return [1, 3, 6, 8, 10].contains(pitch % 12)
  }

  /// The keyboard column's label for a C: "C4" for 60 (middle C is C4), "C-1" for 0, "C9" for 120; nil for every
  /// other pitch and for a pitch outside 0...127.
  static func cLabel(pitch: Int) -> String? {
    guard (0..<pitchCount).contains(pitch), pitch % 12 == 0 else { return nil }
    return "C\(pitch / 12 - 1)"
  }
}
