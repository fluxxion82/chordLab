import CoreGraphics
import XCTest
@testable import ChordLabUICore

/// The pure half of the roll's input view (plan Task 18): the scroll wheel's mapping (UI/Core/RollWheel.swift), the
/// cursor per hit zone, and the store's seam the input view feeds (`RollViewStates.handle`, `cancelGesture`,
/// `snapshotChanged`, in UI/Core/RollViewState.swift) on the snapshot-tracks fixture.
final class RollInputTests: XCTestCase {
  // MARK: - Wheel

  private func wheel(_ dx: CGFloat, _ dy: CGFloat, precise: Bool = true, inverted: Bool = false, momentum: Bool = false,
                     shift: Bool = false, option: Bool = false, command: Bool = false) -> RollWheel.Action {
    RollWheel.action(deltaX: dx, deltaY: dy, precise: precise, invertedFromDevice: inverted, momentum: momentum,
                     modifiers: RollModifiers(shift: shift, option: option, command: command))
  }

  func testAPlainWheelScrollsBothAxesAgainstTheDelta() {
    // AppKit's scrolling deltas say where the content moves: +y shows what is above, so the scroll goes up.
    XCTAssertEqual(wheel(3, -5), .scroll(CGSize(width: -3, height: 5)))
    XCTAssertEqual(wheel(0, 7, inverted: true), .scroll(CGSize(width: 0, height: -7)),
                   "natural scrolling is already in the deltas")
    XCTAssertEqual(wheel(2, 0, shift: true), .scroll(CGSize(width: -2, height: 0)),
                   "⇧ alone scrolls (macOS turns a ⇧ wheel into a horizontal delta itself)")
    XCTAssertEqual(wheel(0, 2, command: true), .scroll(CGSize(width: 0, height: -2)), "⌘ changes nothing")
  }

  func testALineWheelIsScaledToPoints() {
    XCTAssertEqual(wheel(0, 1, precise: false), .scroll(CGSize(width: 0, height: -RollWheel.lineStep)))
    XCTAssertEqual(wheel(-2, 0, precise: false), .scroll(CGSize(width: 2 * RollWheel.lineStep, height: 0)))
  }

  func testNoDeltaDoesNothing() {
    XCTAssertEqual(wheel(0, 0), .none)
    XCTAssertEqual(wheel(0, 0, option: true), .none)
    XCTAssertEqual(wheel(0, 0, shift: true, option: true), .none)
  }

  func testOptionZoomsRowsExponentiallyInTheDeviceDirection() {
    guard case .zoomRows(let up) = wheel(0, 10, option: true) else { return XCTFail("rows") }
    XCTAssertEqual(up, pow(RollWheel.zoomBase, 10), accuracy: 1e-9, "wheel up (away) zooms in")
    guard case .zoomRows(let natural) = wheel(0, -10, inverted: true, option: true) else { return XCTFail("rows") }
    XCTAssertEqual(natural, up, accuracy: 1e-9, "the same physical motion under natural scrolling zooms the same way")
    guard case .zoomRows(let down) = wheel(0, -10, option: true) else { return XCTFail("rows") }
    XCTAssertEqual(down * up, 1, accuracy: 1e-9, "the opposite motion undoes it")
    guard case .zoomRows(let line) = wheel(0, 1, precise: false, option: true) else { return XCTFail("rows") }
    XCTAssertEqual(line, pow(RollWheel.zoomBase, RollWheel.lineStep), accuracy: 1e-9, "a wheel notch")
  }

  func testShiftOptionZoomsTimeOnTheDominantAxis() {
    // A ⇧ mouse wheel arrives as a horizontal delta (macOS moves it), a trackpad as either: the larger one counts.
    guard case .zoomTime(let fromX) = wheel(6, 0, shift: true, option: true) else { return XCTFail("time") }
    XCTAssertEqual(fromX, pow(RollWheel.zoomBase, 6), accuracy: 1e-9)
    guard case .zoomTime(let fromY) = wheel(1, -8, shift: true, option: true) else { return XCTFail("time") }
    XCTAssertEqual(fromY, pow(RollWheel.zoomBase, -8), accuracy: 1e-9)
    guard case .zoomRows = wheel(9, 1, option: true) else { return XCTFail("⌥ alone stays rows, whatever the axis") }
  }

  func testMomentumNeverZoomsButStillScrolls() {
    // A flick's momentum events keep coming after the fingers lift; ⌥ pressed meanwhile must not turn them into a zoom.
    XCTAssertEqual(wheel(0, 10, momentum: true, option: true), .none)
    XCTAssertEqual(wheel(6, 0, momentum: true, shift: true, option: true), .none)
    XCTAssertEqual(wheel(3, -5, momentum: true), .scroll(CGSize(width: -3, height: 5)), "plain scrolling keeps momentum")
    XCTAssertEqual(wheel(2, 0, momentum: true, shift: true), .scroll(CGSize(width: -2, height: 0)))
    guard case .zoomRows = wheel(0, 10, option: true) else { return XCTFail("the fingers' own events still zoom") }
  }

  // MARK: - Cursor

  func testTheCursorIsAResizeArrowOnlyOverANotesEdgeInNotesMode() {
    XCTAssertEqual(RollCursor.cursor(zone: .startEdge, mode: .notes), .resizeLeftRight)
    XCTAssertEqual(RollCursor.cursor(zone: .endEdge, mode: .notes), .resizeLeftRight)
    XCTAssertEqual(RollCursor.cursor(zone: .body, mode: .notes), .arrow)
    XCTAssertEqual(RollCursor.cursor(zone: nil, mode: .notes), .arrow)
    XCTAssertEqual(RollCursor.cursor(zone: .endEdge, mode: .velocity), .arrow, "velocity mode has no edges")
  }

  // MARK: - The store's seam

  private let viewport = CGSize(width: 600, height: 300)

  private func fixture() throws -> Snapshot {
    let url = try SnapshotTests.fixturesDirectory().appendingPathComponent("snapshot-tracks.json")
    return try JSONDecoder().decode(Snapshot.self, from: Data(contentsOf: url))
  }

  /// The fixture's tracks measured (Bass 1, Lead 2), each scrolled to the content origin.
  private func measured(_ snapshot: Snapshot) -> RollViewStates {
    var store = RollViewStates()
    let timing = RollTiming(snapshot: snapshot)
    for track in snapshot.tracks ?? [] {
      store.setViewport(viewport, trackId: track.id, role: track.role, timing: timing)
      store.edit(trackId: track.id, timing: timing) { $0.setScroll(.zero, viewport: $1, timing: $2) }
    }
    return store
  }

  /// Bass note 1 (pitch 36, ticks 0..<960): the middle of its body in content coordinates at the default zoom.
  private func bassNoteBody(_ g: RollGeometry = RollGeometry()) -> CGPoint {
    CGPoint(x: g.x(tick: 480), y: g.y(pitch: 36) + g.rowHeight / 2)
  }

  func testAViewPointIsMappedThroughTheTracksScrollAndZoom() throws {
    let snapshot = try fixture()
    var store = measured(snapshot)
    let timing = RollTiming(snapshot: snapshot)
    store.edit(trackId: 1, timing: timing) { state, viewport, timing in
      state.zoom(vertically: 1.5, aboutViewY: 0, viewport: viewport, timing: timing)
      state.setScroll(CGPoint(x: 100, y: 900), viewport: viewport, timing: timing)
    }
    let state = store[1]
    let point = store.contentPoint(viewPoint: CGPoint(x: 10, y: 20), trackId: 1, ticksPerBeat: 960)
    XCTAssertEqual(point, state.geometry(ticksPerBeat: 960).contentPoint(viewPoint: CGPoint(x: 10, y: 20),
                                                                         scroll: state.scroll))
    XCTAssertEqual(point, CGPoint(x: 110, y: 920))
  }

  func testHandleRunsTheMachineWithTheTracksOptionsAndStoresItsState() throws {
    let snapshot = try fixture()
    var store = measured(snapshot)
    let timing = RollTiming(snapshot: snapshot)
    let body = bassNoteBody()
    let effects = store.handle(RollPointerEvent(phase: .down, location: body), trackId: 1, snapshot: snapshot,
                               timing: timing)
    XCTAssertEqual(effects, [.send(.selectNotes(trackId: 1, ids: [1])), .previewStart(pitch: 36, velocity: 100)])
    XCTAssertEqual(store[1].editState.press?.target, .note(RollHit(index: 0, id: 1, zone: .body)))
    XCTAssertEqual(store[1].editState.press?.baseRevision, 15)
    XCTAssertTrue(store[2].editState.isIdle, "only the track the event names")
    let up = store.handle(RollPointerEvent(phase: .up, location: body), trackId: 1, snapshot: snapshot, timing: timing)
    XCTAssertEqual(up, [.previewStop])
    XCTAssertTrue(store[1].editState.isIdle)

    // The track's mode reaches the machine: in velocity mode an empty double-click inserts nothing.
    store.edit(trackId: 1, timing: timing) { state, _, _ in state.mode = .velocity }
    let empty = CGPoint(x: RollGeometry().x(tick: 1920), y: RollGeometry().y(pitch: 50) + 1)
    var clicks: [RollEffect] = []
    for (phase, count) in [(RollPointerEvent.Phase.down, 1), (.up, 1), (.down, 2), (.up, 2)] {
      clicks += store.handle(RollPointerEvent(phase: phase, location: empty, clickCount: count), trackId: 1,
                             snapshot: snapshot, timing: timing)
    }
    XCTAssertEqual(clicks, [])
    XCTAssertEqual(store[1].editState.insertMarker, 1920, "the plain click still sets the marker")
  }

  func testHandleForATrackTheSnapshotLacksCancelsItsGesture() throws {
    let snapshot = try fixture()
    var store = measured(snapshot)
    let timing = RollTiming(snapshot: snapshot)
    _ = store.handle(RollPointerEvent(phase: .down, location: bassNoteBody()), trackId: 1, snapshot: snapshot,
                     timing: timing)
    var json = try JSONSerialization.jsonObject(with: Data(contentsOf: SnapshotTests.fixturesDirectory()
      .appendingPathComponent("snapshot-tracks.json"))) as! [String: Any]
    json["tracks"] = (json["tracks"] as! [[String: Any]]).filter { ($0["id"] as? Int) != 1 }
    let without = try JSONDecoder().decode(Snapshot.self, from: JSONSerialization.data(withJSONObject: json))
    let effects = store.handle(RollPointerEvent(phase: .drag, location: .zero), trackId: 1, snapshot: without,
                               timing: RollTiming(snapshot: without))
    XCTAssertEqual(effects, [.previewStop])
    XCTAssertTrue(store[1].editState.isIdle)
  }

  func testHandleWithACancelPhaseStopsAHeldPreviewAndLeavesIdleAlone() throws {
    let snapshot = try fixture()
    var store = measured(snapshot)
    let timing = RollTiming(snapshot: snapshot)
    let cancel = RollPointerEvent(phase: .cancel, location: .zero)
    XCTAssertEqual(store.handle(cancel, trackId: 1, snapshot: snapshot, timing: timing), [], "idle: nothing")
    let empty = CGPoint(x: RollGeometry().x(tick: 1920), y: RollGeometry().y(pitch: 50) + 1)
    _ = store.handle(RollPointerEvent(phase: .down, location: empty), trackId: 1, snapshot: snapshot, timing: timing)
    _ = store.handle(RollPointerEvent(phase: .up, location: empty), trackId: 1, snapshot: snapshot, timing: timing)
    let idle = store
    XCTAssertEqual(store.handle(cancel, trackId: 1, snapshot: snapshot, timing: timing), [])
    XCTAssertEqual(store, idle, "an idle cancel changes nothing (the marker stays)")

    // A press on an empty space holds no preview: its cancel drops the gesture with no stop.
    _ = store.handle(RollPointerEvent(phase: .down, location: empty), trackId: 1, snapshot: snapshot, timing: timing)
    XCTAssertFalse(store[1].editState.isIdle)
    XCTAssertEqual(store.handle(cancel, trackId: 1, snapshot: snapshot, timing: timing), [])
    XCTAssertTrue(store[1].editState.isIdle)

    // A press on a note holds its preview: the cancel stops it once, and a second cancel gives nothing.
    _ = store.handle(RollPointerEvent(phase: .down, location: bassNoteBody()), trackId: 1, snapshot: snapshot,
                     timing: timing)
    XCTAssertEqual(store[1].editState.press?.heldPitch, 36)
    XCTAssertEqual(store.handle(cancel, trackId: 1, snapshot: snapshot, timing: timing), [.previewStop])
    XCTAssertTrue(store[1].editState.isIdle)
    XCTAssertEqual(store[1].editState.insertMarker, nil, "the note press had cleared the marker")
    XCTAssertEqual(store.handle(cancel, trackId: 1, snapshot: snapshot, timing: timing), [])

    // Mid-drag the same: one stop, the preview and hidden notes gone.
    _ = store.handle(RollPointerEvent(phase: .down, location: bassNoteBody()), trackId: 1, snapshot: snapshot,
                     timing: timing)
    var moved = bassNoteBody()
    moved.x += 96
    moved.y -= RollGeometry().rowHeight
    _ = store.handle(RollPointerEvent(phase: .drag, location: moved), trackId: 1, snapshot: snapshot, timing: timing)
    XCTAssertFalse(store[1].editState.previewNotes.isEmpty)
    XCTAssertEqual(store.handle(cancel, trackId: 1, snapshot: snapshot, timing: timing), [.previewStop])
    XCTAssertTrue(store[1].editState.previewNotes.isEmpty && store[1].editState.hiddenIds.isEmpty)
    XCTAssertTrue(store[2].editState.isIdle, "only the track the event names")
  }

  func testCancelGestureStopsTheHeldPreviewOnceAndKeepsTheMarker() throws {
    let snapshot = try fixture()
    var store = measured(snapshot)
    let timing = RollTiming(snapshot: snapshot)
    let empty = CGPoint(x: RollGeometry().x(tick: 1920), y: RollGeometry().y(pitch: 50) + 1)
    _ = store.handle(RollPointerEvent(phase: .down, location: empty), trackId: 1, snapshot: snapshot, timing: timing)
    _ = store.handle(RollPointerEvent(phase: .up, location: empty), trackId: 1, snapshot: snapshot, timing: timing)
    XCTAssertEqual(store[1].editState.insertMarker, 1920)
    _ = store.handle(RollPointerEvent(phase: .down, location: bassNoteBody()), trackId: 1, snapshot: snapshot,
                     timing: timing)
    XCTAssertEqual(store.cancelGesture(trackId: 1), [.previewStop])
    XCTAssertTrue(store[1].editState.isIdle)
    XCTAssertEqual(store.cancelGesture(trackId: 1), [], "a cancel while idle does nothing")
    XCTAssertEqual(store.cancelGesture(trackId: 9), [], "nor for a track without a state")
    XCTAssertEqual(store.trackIds, [1, 2], "and makes no entry")
    // RollEditLogic.cancel is the machine's cancel phase.
    var state = RollEditState()
    (state, _) = RollEditLogic.handle(RollPointerEvent(phase: .down, location: bassNoteBody()), state: state,
                                      context: try XCTUnwrap(RollContext(snapshot: snapshot, trackId: 1,
                                                                         geometry: RollGeometry())))
    let byPhase = RollEditLogic.handle(RollPointerEvent(phase: .cancel, location: .zero), state: state,
                                       context: try XCTUnwrap(RollContext(snapshot: snapshot, trackId: 1,
                                                                          geometry: RollGeometry())))
    let direct = RollEditLogic.cancel(state)
    XCTAssertEqual(direct.0, byPhase.0)
    XCTAssertEqual(direct.1, byPhase.1)
  }

  func testASnapshotWithAnotherRevisionCancelsEveryTracksGesture() throws {
    let snapshot = try fixture()
    var store = measured(snapshot)
    let timing = RollTiming(snapshot: snapshot)
    _ = store.handle(RollPointerEvent(phase: .down, location: bassNoteBody()), trackId: 1, snapshot: snapshot,
                     timing: timing)
    // Lead's note 4 (pitch 72, ticks 960..<1440), pressed while Bass's press is parked (the lane switched).
    let g = RollGeometry()
    let lead = CGPoint(x: g.x(tick: 1200), y: g.y(pitch: 72) + 2)
    var parked = store
    _ = parked.handle(RollPointerEvent(phase: .down, location: lead), trackId: 2, snapshot: snapshot, timing: timing)
    XCTAssertTrue(parked[1].editState.isIdle == false, "another track's event leaves Bass's stored state alone")

    XCTAssertTrue(parked.snapshotChanged(revision: 15).isEmpty, "the same revision (a selection echo) cancels nothing")
    XCTAssertFalse(parked[1].editState.isIdle)
    let cancelled = parked.snapshotChanged(revision: 16)
    XCTAssertEqual(cancelled.map(\.trackId), [1, 2])
    XCTAssertEqual(cancelled.map(\.effects), [[.previewStop], [.previewStop]])
    XCTAssertTrue(parked[1].editState.isIdle && parked[2].editState.isIdle)
    XCTAssertTrue(parked.snapshotChanged(revision: 17).isEmpty, "idle states give nothing")
  }

  func testTheCursorOfAViewPointReadsTheTracksNotesScrollAndMode() throws {
    let snapshot = try fixture()
    var store = measured(snapshot)
    let timing = RollTiming(snapshot: snapshot)
    let g = RollGeometry()
    let row = g.y(pitch: 36) + 3
    // Note 1 is 48 pt wide: edges of 4 pt at both ends.
    XCTAssertEqual(store.cursor(atView: CGPoint(x: 1, y: row), trackId: 1, snapshot: snapshot, timing: timing), .resizeLeftRight)
    XCTAssertEqual(store.cursor(atView: CGPoint(x: 46, y: row), trackId: 1, snapshot: snapshot, timing: timing), .resizeLeftRight)
    XCTAssertEqual(store.cursor(atView: CGPoint(x: 24, y: row), trackId: 1, snapshot: snapshot, timing: timing), .arrow)
    XCTAssertEqual(store.cursor(atView: CGPoint(x: 60, y: row), trackId: 1, snapshot: snapshot, timing: timing), .arrow)
    store.edit(trackId: 1, timing: timing) { $0.setScroll(CGPoint(x: 20, y: 0), viewport: $1, timing: $2) }
    XCTAssertEqual(store.cursor(atView: CGPoint(x: 26, y: row), trackId: 1, snapshot: snapshot, timing: timing),
                   .resizeLeftRight, "content x 46 under a 20-pt scroll")
    store.edit(trackId: 1, timing: timing) { state, _, _ in state.mode = .velocity }
    XCTAssertEqual(store.cursor(atView: CGPoint(x: 26, y: row), trackId: 1, snapshot: snapshot, timing: timing), .arrow)
    XCTAssertEqual(store.cursor(atView: CGPoint(x: 26, y: row), trackId: 9, snapshot: snapshot, timing: timing), .arrow)
  }
}
