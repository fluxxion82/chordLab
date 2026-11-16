import CoreGraphics
import XCTest
@testable import ChordLabUICore

/// A track's roll view state (UI/Core/RollViewState.swift, tracks design D14): the first-open scroll, the toolbar
/// zoom, scroll clamping when the viewport or the content changes, and the per-track store `RollViewStates`.
final class RollViewStateTests: XCTestCase {
  /// 15 beats (14400 ticks) in 4/4 at 960: the snapshot-tracks fixture's timing.
  private let timing = RollTiming(ticksPerBeat: 960, barTicks: 3840, endTick: 14400, minNoteTicks: 30)
  private let viewport = CGSize(width: 600, height: 300)

  private func content(_ state: RollViewState, _ timing: RollTiming, _ viewport: CGSize) -> CGSize {
    RollViewState.contentSize(geometry: state.geometry(ticksPerBeat: timing.ticksPerBeat), timing: timing,
                              viewport: viewport)
  }

  // MARK: - Defaults

  func testDefaultsAreDesignD14s() {
    let state = RollViewState()
    XCTAssertEqual(state.scroll, .zero)
    XCTAssertEqual(state.pxPerBeat, 48)
    XCTAssertEqual(state.rowHeight, 14)
    XCTAssertEqual(state.grid, .eighth)
    XCTAssertEqual(state.snap, .off)
    XCTAssertEqual(state.mode, .notes)
    XCTAssertTrue(state.showsOtherTracks)
    XCTAssertEqual(state.editState, RollEditState())
    XCTAssertFalse(state.initialScrollApplied)
    XCTAssertEqual(state.geometry(ticksPerBeat: 960), RollGeometry())
  }

  func testTheContentIsTheProgressionPlusOneBarAndAllRows() {
    let state = RollViewState()
    // (14400 + 3840) ticks at 48 pt per beat = 912 pt; 128 rows of 14 pt.
    XCTAssertEqual(content(state, timing, viewport), CGSize(width: 912, height: 1792))
    XCTAssertEqual(content(state, timing, CGSize(width: 1000, height: 300)).width, 1000, "never narrower than the viewport")
  }

  func testTheContentReachesANotePastTheEnd() {
    // An inactive note left two bars past the end (14400 + 2 × 3840 = 22080): the content runs to its end plus one
    // bar, (22080 + 3840) ticks at 48 pt per beat = 1296 pt, while the progression's end (the hatch) stays put.
    let past = RollTiming(ticksPerBeat: 960, barTicks: 3840, endTick: 14400, minNoteTicks: 30, contentEndTick: 22080)
    XCTAssertEqual(past.endTick, 14400, "the hatch and the insert rule keep the progression's end")
    XCTAssertEqual(content(RollViewState(), past, viewport), CGSize(width: 1296, height: 1792))
    var state = RollViewState()
    state.fit(viewport: viewport, timing: past, role: .bass)
    state.setScroll(CGPoint(x: 1e6, y: 0), viewport: viewport, timing: past)
    XCTAssertEqual(state.scroll.x, 696, "1296 − 600")
    let noteEnd = state.geometry(ticksPerBeat: 960).x(tick: 22080)
    XCTAssertLessThanOrEqual(noteEnd, state.scroll.x + viewport.width, "the note's end (its resize edge) is on screen")
    XCTAssertGreaterThan(noteEnd, state.geometry(ticksPerBeat: 960).x(tick: 14400 + 3840),
                         "beyond the end plus one bar, where the old content stopped")
    // Without such a note the content stops one bar after the end, as before; a smaller content end is the end.
    XCTAssertEqual(timing.contentEndTick, 14400)
    XCTAssertEqual(RollTiming(ticksPerBeat: 960, barTicks: 3840, endTick: 14400, minNoteTicks: 30,
                              contentEndTick: 100).contentEndTick, 14400)
  }

  func testASnapshotWhoseLastNoteMovesInClampsBackToTheEnd() {
    let past = RollTiming(ticksPerBeat: 960, barTicks: 3840, endTick: 14400, minNoteTicks: 30, contentEndTick: 22080)
    var state = RollViewState()
    state.setScroll(CGPoint(x: 1e6, y: 0), viewport: viewport, timing: past)
    XCTAssertEqual(state.scroll.x, 696)
    // The note is deleted: the content shrinks back to the end plus one bar.
    state.fit(viewport: viewport, timing: timing, role: .bass)
    XCTAssertEqual(state.scroll.x, 312, "912 − 600")
  }

  // MARK: - First open

  func testFirstOpenScrollsToTheRolesRegisterAtBarOne() {
    for role in TrackRole.allCases {
      var state = RollViewState()
      state.fit(viewport: viewport, timing: timing, role: role)
      let expected = RollGeometry().initialScroll(register: role.register, viewport: viewport)
      XCTAssertEqual(state.scroll, expected, "\(role)")
      XCTAssertEqual(state.scroll.x, 0, "bar 1")
      XCTAssertTrue(state.initialScrollApplied)
      // The register's middle is in the middle of the view; a viewport taller than the register shows all of it.
      let visible = RollGeometry().visiblePitches(viewport: viewport, scroll: state.scroll)
      let middle = (role.register.lowerBound + role.register.upperBound) / 2
      XCTAssertLessThanOrEqual(abs((visible.lowerBound + visible.upperBound) / 2 - middle), 1, "\(role): \(visible)")
      let tall = CGSize(width: 600, height: 450)
      var tallState = RollViewState()
      tallState.fit(viewport: tall, timing: timing, role: role)
      let all = RollGeometry().visiblePitches(viewport: tall, scroll: tallState.scroll)
      XCTAssertTrue(all.contains(role.register.lowerBound) && all.contains(role.register.upperBound), "\(role): \(all)")
    }
  }

  func testTheFirstOpenHappensOnceAndWaitsForARealViewport() {
    var state = RollViewState()
    state.fit(viewport: .zero, timing: timing, role: .bass)
    XCTAssertFalse(state.initialScrollApplied, "no viewport yet: nothing to centre in")
    XCTAssertEqual(state.scroll, .zero)
    state.fit(viewport: CGSize(width: 600, height: 0), timing: timing, role: .bass)
    XCTAssertFalse(state.initialScrollApplied)

    state.fit(viewport: viewport, timing: timing, role: .bass)
    let first = state.scroll
    XCTAssertTrue(state.initialScrollApplied)
    state.setScroll(CGPoint(x: 100, y: 200), viewport: viewport, timing: timing)
    state.fit(viewport: viewport, timing: timing, role: .bass)
    XCTAssertEqual(state.scroll, CGPoint(x: 100, y: 200), "a later fit keeps the scroll the user chose")
    XCTAssertNotEqual(first, state.scroll)
  }

  func testARoleChangeOfAnOpenTrackDoesNotScroll() {
    var state = RollViewState()
    state.fit(viewport: viewport, timing: timing, role: .bass)
    let bassScroll = state.scroll
    state.fit(viewport: viewport, timing: timing, role: .lead)
    XCTAssertEqual(state.scroll, bassScroll)
  }

  func testAScrollBeforeTheFirstFitCountsAsTheFirstOpen() {
    var state = RollViewState()
    state.setScroll(CGPoint(x: 0, y: 500), viewport: viewport, timing: timing)
    XCTAssertTrue(state.initialScrollApplied)
    state.fit(viewport: viewport, timing: timing, role: .lead)
    XCTAssertEqual(state.scroll, CGPoint(x: 0, y: 500))
  }

  // MARK: - Scroll clamping

  func testSetScrollClampsToTheContent() {
    var state = RollViewState()
    state.setScroll(CGPoint(x: -50, y: -10), viewport: viewport, timing: timing)
    XCTAssertEqual(state.scroll, .zero)
    state.setScroll(CGPoint(x: 5000, y: 5000), viewport: viewport, timing: timing)
    XCTAssertEqual(state.scroll, CGPoint(x: 312, y: 1492), "912 − 600 and 1792 − 300")
    state.scroll(by: CGSize(width: -12, height: -92), viewport: viewport, timing: timing)
    XCTAssertEqual(state.scroll, CGPoint(x: 300, y: 1400))
  }

  func testAResizeOrAShorterProgressionClampsTheScroll() {
    var state = RollViewState()
    state.fit(viewport: viewport, timing: timing, role: .lead)
    state.setScroll(CGPoint(x: 312, y: 1492), viewport: viewport, timing: timing)
    // The window grows: less content left to scroll.
    let taller = CGSize(width: 700, height: 500)
    state.fit(viewport: taller, timing: timing, role: .lead)
    XCTAssertEqual(state.scroll, CGPoint(x: 212, y: 1292))
    // The progression shortens to three bars: 4 bars of content (768 pt) in a 700-pt viewport.
    let short = RollTiming(ticksPerBeat: 960, barTicks: 3840, endTick: 11520, minNoteTicks: 30)
    state.fit(viewport: taller, timing: short, role: .lead)
    XCTAssertEqual(state.scroll, CGPoint(x: 68, y: 1292))
    // An empty progression: one bar of slack fits, the scroll pins to 0 horizontally.
    let empty = RollTiming(ticksPerBeat: 960, barTicks: 3840, endTick: 0, minNoteTicks: 30)
    state.fit(viewport: taller, timing: empty, role: .lead)
    XCTAssertEqual(state.scroll.x, 0)
  }

  // MARK: - Zoom

  func testToolbarZoomDoublesAndHalvesTimeAboutTheCentreThenClamps() {
    var state = RollViewState()
    state.fit(viewport: viewport, timing: timing, role: .lead)
    let y = state.scroll.y
    state.zoomIn(viewport: viewport, timing: timing)
    XCTAssertEqual(state.pxPerBeat, 96)
    XCTAssertEqual(state.rowHeight, 14, "rows untouched")
    // About the centre x 300 from scroll 0: (0 + 300) × 2 − 300 = 300, inside the 1824-pt content.
    XCTAssertEqual(state.scroll, CGPoint(x: 300, y: y))
    state.zoomIn(viewport: viewport, timing: timing)
    XCTAssertEqual(state.pxPerBeat, 192)
    XCTAssertEqual(state.scroll.x, 900)
    state.zoomIn(viewport: viewport, timing: timing)
    XCTAssertEqual(state.pxPerBeat, 192, "at the limit nothing changes")
    XCTAssertEqual(state.scroll.x, 900)
    state.zoomOut(viewport: viewport, timing: timing)
    XCTAssertEqual(state.pxPerBeat, 96)
    XCTAssertEqual(state.scroll.x, 300)
    state.zoomOut(viewport: viewport, timing: timing)
    state.zoomOut(viewport: viewport, timing: timing)
    XCTAssertEqual(state.pxPerBeat, 24)
    // (0 + 300) / 2 − 300 is negative and the content (456 pt) fits the viewport: clamped to 0.
    XCTAssertEqual(state.scroll.x, 0)
    state.zoomOut(viewport: viewport, timing: timing)
    state.zoomOut(viewport: viewport, timing: timing)
    XCTAssertEqual(state.pxPerBeat, 12)
    XCTAssertFalse(state.geometry(ticksPerBeat: 960).canZoomOut)
  }

  func testZoomAboutAPointKeepsItsTickAndRowWhereTheContentAllows() {
    var state = RollViewState()
    state.setScroll(CGPoint(x: 100, y: 700), viewport: viewport, timing: timing)
    state.zoom(horizontally: 1.5, aboutViewX: 200, viewport: viewport, timing: timing)
    XCTAssertEqual(state.pxPerBeat, 72)
    XCTAssertEqual(state.scroll.x, 250, accuracy: 1e-9, "(100 + 200) × 1.5 − 200")
    state.zoom(vertically: 1.5, aboutViewY: 100, viewport: viewport, timing: timing)
    XCTAssertEqual(state.rowHeight, 21)
    XCTAssertEqual(state.scroll.y, 1100, accuracy: 1e-9, "(700 + 100) × 1.5 − 100")
    state.zoom(vertically: 100, aboutViewY: 0, viewport: viewport, timing: timing)
    XCTAssertEqual(state.rowHeight, 24, "clamped to the range")
    XCTAssertLessThanOrEqual(state.scroll.y, 128 * 24 - 300)
  }

  // MARK: - The per-track store

  private func track(_ id: Int, _ role: TrackRole) -> TrackJSON {
    TrackJSON(id: id, name: "T\(id)", role: role, mute: false, solo: false, audible: true,
              notes: TrackNotesJSON(id: [], pitch: [], start: [], length: [], velocity: [], anchor: [], offset: [],
                                    active: []))
  }

  func testTheStoreRemembersEachTracksStateAndItsFirstOpen() {
    var store = RollViewStates()
    XCTAssertEqual(store[7], RollViewState(), "an unknown track reads the defaults")
    store.setViewport(viewport, trackId: 1, role: .bass, timing: timing)
    store.setViewport(viewport, trackId: 2, role: .lead, timing: timing)
    XCTAssertEqual(store.viewport, viewport)
    XCTAssertEqual(store[1].scroll, RollGeometry().initialScroll(register: TrackRole.bass.register, viewport: viewport))
    XCTAssertEqual(store[2].scroll, RollGeometry().initialScroll(register: TrackRole.lead.register, viewport: viewport))
    store.edit(trackId: 1, timing: timing) { state, viewport, timing in
      state.grid = .sixteenth
      state.setScroll(CGPoint(x: 40, y: 900), viewport: viewport, timing: timing)
    }
    XCTAssertEqual(store[1].grid, .sixteenth)
    XCTAssertEqual(store[1].scroll, CGPoint(x: 40, y: 900))
    XCTAssertEqual(store[2].grid, .eighth, "per track")
    // Reopening track 1 (a fresh roll view measures the same viewport) keeps its scroll.
    store.setViewport(viewport, trackId: 1, role: .bass, timing: timing)
    XCTAssertEqual(store[1].scroll, CGPoint(x: 40, y: 900))
  }

  func testASnapshotDropsRemovedTracksAndClampsTheOpenOne() {
    var store = RollViewStates()
    store.setViewport(viewport, trackId: 1, role: .bass, timing: timing)
    store.setViewport(viewport, trackId: 2, role: .lead, timing: timing)
    store.edit(trackId: 2, timing: timing) { state, viewport, timing in
      state.setScroll(CGPoint(x: 312, y: 600), viewport: viewport, timing: timing)
    }
    let short = RollTiming(ticksPerBeat: 960, barTicks: 3840, endTick: 11520, minNoteTicks: 30)
    store.snapshotApplied(tracks: [track(2, .bass)], timing: short, openTrackId: 2)
    XCTAssertEqual(store.trackIds, [2], "track 1 is gone")
    XCTAssertEqual(store[2].scroll, CGPoint(x: 168, y: 600), "768 − 600; the role change did not re-scroll")
    store.snapshotApplied(tracks: [], timing: short, openTrackId: nil)
    XCTAssertEqual(store.trackIds, [])
  }

  // MARK: - The insert marker against a shorter progression (Task 20 review 2, finding 3)

  /// Tracks 1 (open) and 2 (parked) measured at the 14400-tick timing, each with an insert marker.
  private func storeWithMarkers(open: Int, parked: Int) -> RollViewStates {
    var store = RollViewStates()
    store.setViewport(viewport, trackId: 1, role: .bass, timing: timing)
    store.setViewport(viewport, trackId: 2, role: .lead, timing: timing)
    store.edit(trackId: 1, timing: timing) { state, _, _ in state.editState.insertMarker = open }
    store.edit(trackId: 2, timing: timing) { state, _, _ in state.editState.insertMarker = parked }
    return store
  }

  func testAnInsertMarkerBeyondAShorterProgressionsEndIsCleared() {
    var store = storeWithMarkers(open: 11520, parked: 11520)
    let short = RollTiming(ticksPerBeat: 960, barTicks: 3840, endTick: 7680, minNoteTicks: 30)
    store.snapshotApplied(tracks: [track(1, .bass), track(2, .lead)], timing: short, openTrackId: 1)
    XCTAssertNil(store[1].editState.insertMarker, "a paste there would start where no chord owns it")
    XCTAssertNil(store[2].editState.insertMarker, "the parked track's marker too: it pastes there once reopened")
  }

  func testAnInsertMarkerInsideTheProgressionStays() {
    var store = storeWithMarkers(open: 3840, parked: 7679)
    let short = RollTiming(ticksPerBeat: 960, barTicks: 3840, endTick: 7680, minNoteTicks: 30)
    store.snapshotApplied(tracks: [track(1, .bass), track(2, .lead)], timing: short, openTrackId: 1)
    XCTAssertEqual(store[1].editState.insertMarker, 3840)
    XCTAssertEqual(store[2].editState.insertMarker, 7679)
  }

  func testAnInsertMarkerExactlyAtTheEndIsCleared() {
    // The end is not a paste position: no chord owns it (half-open spans), and a click there sets no marker.
    var store = storeWithMarkers(open: 7680, parked: 0)
    let short = RollTiming(ticksPerBeat: 960, barTicks: 3840, endTick: 7680, minNoteTicks: 30)
    store.snapshotApplied(tracks: [track(1, .bass), track(2, .lead)], timing: short, openTrackId: 1)
    XCTAssertNil(store[1].editState.insertMarker)
    XCTAssertEqual(store[2].editState.insertMarker, 0)
  }

  func testASnapshotForATrackNotYetMeasuredCreatesNothing() {
    var store = RollViewStates()
    store.snapshotApplied(tracks: [track(3, .other)], timing: timing, openTrackId: 3)
    XCTAssertEqual(store.trackIds, [], "the roll's own measurement makes the entry and its first open")
  }

  // MARK: - Revealing notes (after Generate, and the first open of a track with notes)

  /// Notes from columns: (pitch, start, anchor, active), 240 ticks long.
  private func notes(_ rows: [(Int, Int, Int, Bool)]) -> TrackNotesJSON {
    TrackNotesJSON(id: Array(1...max(1, rows.count)).prefix(rows.count).map { $0 }, pitch: rows.map(\.0),
                   start: rows.map(\.1), length: rows.map { _ in 240 }, velocity: rows.map { _ in 100 },
                   anchor: rows.map(\.2), offset: rows.map { _ in 0 }, active: rows.map(\.3))
  }

  func testTheSpanOfNotesIsTheirActiveNotesInTheGivenChords() throws {
    let track = notes([(40, 960, 1, true), (52, 0, 2, true), (28, 3840, 2, true), (90, 100, 1, false), (35, 7680, 3, true)])
    XCTAssertEqual(RollNoteSpan(notes: track), RollNoteSpan(pitches: 28...52, firstStart: 0), "every active note")
    XCTAssertEqual(RollNoteSpan(notes: track, anchors: [1]), RollNoteSpan(pitches: 40...40, firstStart: 960),
                   "the inactive note of chord 1 does not count")
    XCTAssertEqual(RollNoteSpan(notes: track, anchors: [2, 3]), RollNoteSpan(pitches: 28...52, firstStart: 0))
    XCTAssertNil(RollNoteSpan(notes: track, anchors: [4]), "no notes in that chord")
    XCTAssertNil(RollNoteSpan(notes: notes([(60, 0, 1, false)])), "inactive notes only")
    XCTAssertNil(RollNoteSpan(notes: notes([])))
  }

  /// A Lead roll after its first open (C4–C6 centred), scrolled to bar 1.
  private func leadView(viewport: CGSize? = nil) -> RollViewState {
    var state = RollViewState()
    state.fit(viewport: viewport ?? self.viewport, timing: timing, role: .lead)
    return state
  }

  func testRevealLeavesTheScrollAloneWhenTheNotesAreInView() {
    var state = leadView()
    let before = state.scroll
    let visible = RollGeometry().visiblePitches(viewport: viewport, scroll: before)
    // Rows fully inside the viewport (the partly shown top and bottom rows excluded) and a start on screen.
    let inside = (visible.lowerBound + 1)...(visible.upperBound - 1)
    state.reveal(RollNoteSpan(pitches: inside, firstStart: 960 * 4), viewport: viewport, timing: timing)
    XCTAssertEqual(state.scroll, before)
    state.reveal(RollNoteSpan(pitches: 72...72, firstStart: 0), viewport: viewport, timing: timing)
    XCTAssertEqual(state.scroll, before)
  }

  func testRevealCentresRowsThatFitTheViewport() {
    var state = leadView()
    // E2–E3: 13 rows of 14 pt = 182 pt in a 300-pt viewport. Top of E3's row y(52) = 75 × 14 = 1050, bottom of E2's
    // row 87 × 14 + 14 = 1232: centred, the scroll is 1141 − 150 = 991.
    state.reveal(RollNoteSpan(pitches: 40...52, firstStart: 0), viewport: viewport, timing: timing)
    XCTAssertEqual(state.scroll, CGPoint(x: 0, y: 991))
    let visible = RollGeometry().visiblePitches(viewport: viewport, scroll: state.scroll)
    XCTAssertTrue(visible.contains(40) && visible.contains(52), "\(visible)")
    XCTAssertTrue(state.initialScrollApplied)
  }

  func testRevealPutsTheLowestRowOfATallRangeOneRowAboveTheBottom() {
    var state = leadView()
    // E1–E3, 25 rows = 350 pt in 300: the bottom of E1's row (99 × 14 + 14 = 1400) one row (14 pt) above the
    // viewport's bottom edge: 1400 + 14 − 300 = 1114.
    state.reveal(RollNoteSpan(pitches: 28...52, firstStart: 0), viewport: viewport, timing: timing)
    XCTAssertEqual(state.scroll.y, 1114)
    let visible = RollGeometry().visiblePitches(viewport: viewport, scroll: state.scroll)
    XCTAssertEqual(visible.lowerBound, 27, "one row under the lowest note shows: \(visible)")
    XCTAssertTrue(visible.contains(28))
  }

  func testRevealScrollsTimeOnlyWhenTheFirstStartIsOffScreen() {
    let narrow = CGSize(width: 300, height: 300)
    var state = leadView(viewport: narrow)
    let y = state.scroll.y
    // Bar 3 beat 2 (9120 ticks, x 456) is right of a 300-pt viewport at x 0: half a beat (24 pt) in from the left.
    state.reveal(RollNoteSpan(pitches: 72...72, firstStart: 9120), viewport: narrow, timing: timing)
    XCTAssertEqual(state.scroll, CGPoint(x: 432, y: y))
    // Now on screen: no change.
    state.reveal(RollNoteSpan(pitches: 72...72, firstStart: 9120 + 960), viewport: narrow, timing: timing)
    XCTAssertEqual(state.scroll.x, 432)
    // Left of the viewport: back to half a beat before it, clamped at 0 for tick 0.
    state.reveal(RollNoteSpan(pitches: 72...72, firstStart: 3840), viewport: narrow, timing: timing)
    XCTAssertEqual(state.scroll.x, 168, "192 − 24")
    state.reveal(RollNoteSpan(pitches: 72...72, firstStart: 0), viewport: narrow, timing: timing)
    XCTAssertEqual(state.scroll.x, 0, "−24 clamps to 0")
  }

  func testRevealIsClampedToTheContent() {
    var state = leadView()
    // The top octave cannot be centred: clamped to the first row.
    state.reveal(RollNoteSpan(pitches: 124...127, firstStart: 0), viewport: viewport, timing: timing)
    XCTAssertEqual(state.scroll.y, 0)
    state.reveal(RollNoteSpan(pitches: 0...2, firstStart: 14000), viewport: viewport, timing: timing)
    XCTAssertEqual(state.scroll, CGPoint(x: 312, y: 1792 - 300), "the bottom rows and the content's right end")
  }

  func testRevealCountsAsTheFirstOpenAndNeedsAViewport() {
    var fresh = RollViewState()
    fresh.reveal(RollNoteSpan(pitches: 40...52, firstStart: 0), viewport: .zero, timing: timing)
    XCTAssertEqual(fresh, RollViewState(), "no viewport: nothing to reveal in")
    fresh.reveal(RollNoteSpan(pitches: 40...52, firstStart: 0), viewport: viewport, timing: timing)
    XCTAssertEqual(fresh.scroll, CGPoint(x: 0, y: 991))
    fresh.fit(viewport: viewport, timing: timing, role: .lead)
    XCTAssertEqual(fresh.scroll, CGPoint(x: 0, y: 991), "the first open's register scroll does not undo it")
  }

  func testTheFirstOpenOfATrackWithNotesCentresOnItsNotes() {
    var state = RollViewState()
    state.fit(viewport: viewport, timing: timing, role: .lead, notes: RollNoteSpan(pitches: 40...52, firstStart: 3840))
    XCTAssertEqual(state.scroll, CGPoint(x: 0, y: 991), "the notes' rows centred by the reveal's rule, at bar 1")
    XCTAssertTrue(state.initialScrollApplied)
    var tall = RollViewState()
    tall.fit(viewport: viewport, timing: timing, role: .lead, notes: RollNoteSpan(pitches: 28...52, firstStart: 0))
    XCTAssertEqual(tall.scroll, CGPoint(x: 0, y: 1114), "taller than the viewport: its lowest row one row above the bottom")
    var empty = RollViewState()
    empty.fit(viewport: viewport, timing: timing, role: .lead, notes: nil)
    XCTAssertEqual(empty.scroll, RollGeometry().initialScroll(register: TrackRole.lead.register, viewport: viewport),
                   "no notes: the role's register as before")
    // Only the first open: a later fit with notes elsewhere keeps the scroll.
    state.fit(viewport: viewport, timing: timing, role: .lead, notes: RollNoteSpan(pitches: 100...110, firstStart: 0))
    XCTAssertEqual(state.scroll, CGPoint(x: 0, y: 991))
  }

  func testTheStoreOpensATrackWithNotesOnItsNotes() {
    var store = RollViewStates()
    store.setViewport(viewport, trackId: 1, role: .lead, timing: timing, notes: RollNoteSpan(pitches: 40...52, firstStart: 0))
    store.setViewport(viewport, trackId: 2, role: .lead, timing: timing, notes: nil)
    XCTAssertEqual(store[1].scroll, CGPoint(x: 0, y: 991))
    XCTAssertEqual(store[2].scroll, RollGeometry().initialScroll(register: TrackRole.lead.register, viewport: viewport))
    // The snapshot's re-fit of the open track passes its notes too, but the first open is done: nothing moves.
    store.snapshotApplied(tracks: [TrackJSON(id: 1, name: "Bass", role: .lead, mute: false, solo: false, audible: true,
                                             notes: notes([(100, 0, 1, true)]))], timing: timing, openTrackId: 1)
    XCTAssertEqual(store[1].scroll, CGPoint(x: 0, y: 991))
  }
}
