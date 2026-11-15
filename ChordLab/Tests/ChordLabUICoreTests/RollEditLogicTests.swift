import CoreGraphics
import XCTest
@testable import ChordLabUICore

/// The piano roll's gesture machine (UI/Core/RollEditLogic.swift, tracks design D15), on whole event sequences.
///
/// The synthetic roll: 48 pt per beat and 14 pt rows at 960 ticks per beat, so 1 pt is 20 ticks and the 3 pt drag
/// threshold is 60 ticks; 4/4, four one-bar chords C, E7, C, C (playback spans 0-4-8-12-16 beats, so the progression
/// ends at tick 15,360); track 7 at revision 5 with notes 10 (C4 at 0), 11 (E4 at 960), 12 (G4 at 1920, a beat long)
/// and 13 (D4 at 5760, inactive).
final class RollEditLogicTests: XCTestCase {
  private let trackId = 7
  private let revision: UInt64 = 5
  private let geometry = RollGeometry(pxPerBeat: 48, rowHeight: 14, ticksPerBeat: 960)
  private let timing = RollTiming(ticksPerBeat: 960, barTicks: 3840, endTick: 15_360, minNoteTicks: 30)

  // MARK: - Fixtures

  /// C major: C root; E, G chord tones; D, A, B tensions; F avoid; the rest outside; the C major scale (2741).
  private let cGuide = GuideJSON(roles: [.root, .outside, .tension, .outside, .chord, .avoid, .outside, .chord, .outside,
                                         .tension, .outside, .tension],
                                 scale: 2741, degrees: Array(repeating: "", count: 12), names: Array(repeating: "", count: 12))
  /// E7 in C (as PitchSnapTests): its chord scale E F G# A B C D (2869) has no G.
  private let e7Guide = GuideJSON(roles: [.tension, .outside, .chord, .outside, .root, .tension, .outside, .tension, .chord,
                                          .avoid, .outside, .chord],
                                  scale: 2869, degrees: Array(repeating: "", count: 12), names: Array(repeating: "", count: 12))

  private func event(_ id: Int, _ symbol: String, beat: Double, guide: GuideJSON) -> EventJSON {
    var event = EventJSON(id: id, root: 0, type: "maj", symbol: symbol, roman: "I", family: "Major", startBeat: beat,
                          lengthBeats: 4, velocity: 100, voicing: [60], voicingNames: nil, outOfKey: nil,
                          outOfKeyNames: nil, function: nil, tension: nil, inversion: nil, octave: nil,
                          manualVoicing: nil, why: nil)
    event.playStart = beat
    event.playEnd = beat + 4
    event.guide = guide
    return event
  }

  private var events: [EventJSON] {
    [event(1, "C", beat: 0, guide: cGuide), event(2, "E7", beat: 4, guide: e7Guide), event(3, "C", beat: 8, guide: cGuide),
     event(4, "C", beat: 12, guide: cGuide)]
  }

  /// (id, pitch, start, length, velocity, active), in note order.
  private typealias NoteRow = (id: Int, pitch: Int, start: Int, length: Int, velocity: Int, active: Bool)

  private let standardNotes: [NoteRow] = [
    (10, 60, 0, 480, 100, true), (11, 64, 960, 480, 90, true), (12, 67, 1920, 960, 80, true), (13, 62, 5760, 480, 70, false),
  ]

  private func columns(_ rows: [NoteRow]) -> TrackNotesJSON {
    TrackNotesJSON(id: rows.map(\.id), pitch: rows.map(\.pitch), start: rows.map(\.start), length: rows.map(\.length),
                   velocity: rows.map(\.velocity), anchor: rows.map { _ in 1 }, offset: rows.map(\.start),
                   active: rows.map(\.active))
  }

  private func context(_ rows: [NoteRow]? = nil, selected: Set<Int> = [], revision: UInt64? = nil,
                       mode: RollMode = .notes, grid: RollGrid = .eighth, snap: PitchSnap = .off, track: Int? = nil,
                       geometry: RollGeometry? = nil) -> RollContext {
    RollContext(trackId: track ?? trackId, notes: columns(rows ?? standardNotes), selectedIds: selected,
                revision: revision ?? self.revision, events: events, timing: timing, geometry: geometry ?? self.geometry,
                mode: mode, grid: grid, snap: snap)
  }

  /// The middle of the row of `pitch`, at `tick`.
  private func point(_ tick: Int, _ pitch: Int) -> CGPoint {
    CGPoint(x: geometry.x(tick: tick), y: geometry.y(pitch: pitch) + 7)
  }

  private func offset(_ p: CGPoint, x: CGFloat = 0, y: CGFloat = 0) -> CGPoint { CGPoint(x: p.x + x, y: p.y + y) }

  private func down(_ p: CGPoint, shift: Bool = false, option: Bool = false, clicks: Int = 1) -> RollPointerEvent {
    RollPointerEvent(phase: .down, location: p, modifiers: RollModifiers(shift: shift, option: option), clickCount: clicks,
                     timestamp: 1)
  }

  private func drag(_ p: CGPoint, option: Bool = false) -> RollPointerEvent {
    RollPointerEvent(phase: .drag, location: p, modifiers: RollModifiers(option: option), clickCount: 1, timestamp: 1.1)
  }

  private func up(_ p: CGPoint, clicks: Int = 1) -> RollPointerEvent {
    RollPointerEvent(phase: .up, location: p, modifiers: RollModifiers(), clickCount: clicks, timestamp: 1.2)
  }

  private var cancel: RollPointerEvent {
    RollPointerEvent(phase: .cancel, location: .zero, modifiers: RollModifiers(), clickCount: 0, timestamp: 1.3)
  }

  /// Feeds `events` through the machine from `state`, collecting every effect.
  private func run(_ events: [RollPointerEvent], _ context: RollContext, from state: RollEditState = RollEditState())
    -> (state: RollEditState, effects: [RollEffect]) {
    var state = state
    var effects: [RollEffect] = []
    for event in events {
      let (next, produced) = RollEditLogic.handle(event, state: state, context: context)
      state = next
      effects += produced
    }
    return (state, effects)
  }

  private func sends(_ effects: [RollEffect]) -> [RollCommand] {
    effects.compactMap { if case .send(let command) = $0 { return command } else { return nil } }
  }

  /// Every held preview is stopped exactly once, and never started over a held one.
  private func assertPaired(_ effects: [RollEffect], file: StaticString = #filePath, line: UInt = #line) {
    var held = false
    for effect in effects {
      switch effect {
      case .previewStart:
        XCTAssertFalse(held, "a preview started over a held one", file: file, line: line)
        held = true
      case .previewStop:
        XCTAssertTrue(held, "a stop without a held preview", file: file, line: line)
        held = false
      default:
        break
      }
    }
    XCTAssertFalse(held, "a held preview was never stopped", file: file, line: line)
  }

  private func json(_ command: RollCommand) throws -> String {
    String(decoding: try JSONSerialization.data(withJSONObject: command.json, options: [.sortedKeys]), as: UTF8.self)
  }

  // MARK: - Hit-testing

  func testHitZonesOfANote() {
    let roll = context()
    // Note 10 spans x 0..<24 (480 ticks); each edge is min(4 pt, width / 4) = 4 pt.
    XCTAssertEqual(RollHit.hit(at: point(240, 60), context: roll), RollHit(index: 0, id: 10, zone: .body))
    XCTAssertEqual(RollHit.hit(at: CGPoint(x: 1, y: point(0, 60).y), context: roll)?.zone, .startEdge)
    XCTAssertEqual(RollHit.hit(at: CGPoint(x: 3.9, y: point(0, 60).y), context: roll)?.zone, .startEdge)
    XCTAssertEqual(RollHit.hit(at: CGPoint(x: 4, y: point(0, 60).y), context: roll)?.zone, .body)
    XCTAssertEqual(RollHit.hit(at: CGPoint(x: 20, y: point(0, 60).y), context: roll)?.zone, .endEdge)
    XCTAssertEqual(RollHit.hit(at: CGPoint(x: 23.9, y: point(0, 60).y), context: roll)?.zone, .endEdge)
    XCTAssertNil(RollHit.hit(at: CGPoint(x: 24, y: point(0, 60).y), context: roll), "the rectangle is half-open")
    XCTAssertNil(RollHit.hit(at: point(240, 61), context: roll), "another row")
    XCTAssertEqual(RollHit.hit(at: CGPoint(x: 12, y: geometry.y(pitch: 60)), context: roll)?.id, 10,
                   "the row's top boundary belongs to it (RollGeometry)")
    XCTAssertNil(RollHit.hit(at: CGPoint(x: 12, y: geometry.y(pitch: 59)), context: roll), "its bottom boundary is the next row's")
    XCTAssertNil(RollHit.hit(at: CGPoint(x: -1, y: point(0, 60).y), context: roll))
    XCTAssertNil(RollHit.hit(at: CGPoint(x: 12, y: -1), context: roll), "above the keyboard")
    XCTAssertNil(RollHit.hit(at: CGPoint(x: 12, y: 128 * 14), context: roll), "below the keyboard")
  }

  func testANarrowNoteKeepsABody() {
    // 40 ticks = 2 pt: each edge is 0.5 pt.
    let roll = context([(1, 60, 0, 40, 100, true)])
    let y = point(0, 60).y
    XCTAssertEqual(RollHit.hit(at: CGPoint(x: 0.2, y: y), context: roll)?.zone, .startEdge)
    XCTAssertEqual(RollHit.hit(at: CGPoint(x: 1, y: y), context: roll)?.zone, .body)
    XCTAssertEqual(RollHit.hit(at: CGPoint(x: 1.8, y: y), context: roll)?.zone, .endEdge)
  }

  func testOverlappingNotesHitTheTopmostTheLastInNoteOrder() {
    let roll = context([(1, 60, 0, 960, 100, true), (2, 60, 480, 960, 100, true)])
    XCTAssertEqual(RollHit.hit(at: point(300, 60), context: roll)?.id, 1)
    XCTAssertEqual(RollHit.hit(at: point(700, 60), context: roll), RollHit(index: 1, id: 2, zone: .body))
    XCTAssertEqual(RollHit.hit(at: point(1200, 60), context: roll)?.id, 2)
  }

  func testInactiveNotesAreHitTestable() {
    XCTAssertEqual(RollHit.hit(at: point(6000, 62), context: context())?.id, 13)
  }

  // MARK: - Empty space: insert and deselect

  func testAnEmptyClickWithoutASelectionInsertsOneNoteAtTheGridCell() {
    let p = point(7700, 50)
    let (state, effects) = run([down(p), up(p)], context())
    XCTAssertEqual(effects, [
      .send(.insertNotes(trackId: 7, notes: [RollNoteSpec(pitch: 50, start: 7680, length: 480, velocity: 100)], baseRevision: 5)),
      .previewOneShot(pitch: 50, velocity: 100),
    ])
    XCTAssertEqual(state.lastLength, 480)
    XCTAssertEqual(state.insertMarker, 7680)
    XCTAssertEqual(state.lastInsert, RollEditState.Insert(pitch: 50, start: 7680))
    XCTAssertTrue(state.isIdle)
  }

  func testTheInsertLengthIsTheLastLengthElseOneGridStepAtLeastTheMinimum() {
    let p = point(7700, 50)
    XCTAssertEqual(sends(run([down(p), up(p)], context(grid: .off)).effects),
                   [.insertNotes(trackId: 7, notes: [RollNoteSpec(pitch: 50, start: 7700, length: 30, velocity: 100)],
                                 baseRevision: 5)], "Off: a 10-tick grid, the 30-tick minimum")
    XCTAssertEqual(sends(run([down(p), up(p)], context(grid: .quarter)).effects),
                   [.insertNotes(trackId: 7, notes: [RollNoteSpec(pitch: 50, start: 7680, length: 960, velocity: 100)],
                                 baseRevision: 5)])
    var used = RollEditState()
    used.lastLength = 720
    XCTAssertEqual(sends(run([down(p), up(p)], context(grid: .quarter), from: used).effects),
                   [.insertNotes(trackId: 7, notes: [RollNoteSpec(pitch: 50, start: 7680, length: 720, velocity: 100)],
                                 baseRevision: 5)])
  }

  func testAnInsertSnapsItsPitchByTheGuideOfTheChordAtItsStart() {
    let p = point(4000, 67)   // E7's bar; G is not in its scale
    XCTAssertEqual(sends(run([down(p), up(p)], context(snap: .scale)).effects),
                   [.insertNotes(trackId: 7, notes: [RollNoteSpec(pitch: 68, start: 3840, length: 480, velocity: 100)],
                                 baseRevision: 5)])
    XCTAssertEqual(sends(run([down(p), up(p)], context(snap: .off)).effects),
                   [.insertNotes(trackId: 7, notes: [RollNoteSpec(pitch: 67, start: 3840, length: 480, velocity: 100)],
                                 baseRevision: 5)])
    let c = point(2900, 67)   // C's bar: G is a chord tone
    XCTAssertEqual(sends(run([down(c), up(c)], context(snap: .chordTones)).effects),
                   [.insertNotes(trackId: 7, notes: [RollNoteSpec(pitch: 67, start: 2880, length: 480, velocity: 100)],
                                 baseRevision: 5)])
  }

  func testAnEmptyClickWithASelectionDeselects() throws {
    let p = point(7700, 50)
    let (state, effects) = run([down(p), up(p)], context(selected: [11]))
    XCTAssertEqual(effects, [.send(.selectNotes(trackId: 7, ids: []))])
    XCTAssertEqual(try json(.selectNotes(trackId: 7, ids: [])), #"{"cmd":"selectNotes","ids":[],"trackId":null}"#)
    XCTAssertNil(state.lastLength)
  }

  func testNoInsertAtOrAfterTheProgressionsEnd() {
    for tick in [15_360, 15_400, 40_000] {
      let p = point(tick, 50)
      let (state, effects) = run([down(p), up(p)], context())
      XCTAssertEqual(effects, [], "tick \(tick)")
      XCTAssertNil(state.insertMarker, "the hatched area holds no marker")
    }
    let last = point(15_359, 50)
    XCTAssertEqual(sends(run([down(last), up(last)], context()).effects),
                   [.insertNotes(trackId: 7, notes: [RollNoteSpec(pitch: 50, start: 14_880, length: 480, velocity: 100)],
                                 baseRevision: 5)])
  }

  func testTheInsertMarkerIsSetOnlyByAPlainEmptyClick() {
    let (pressed, _) = run([down(point(7700, 50))], context(selected: [10]))
    XCTAssertNil(pressed.insertMarker, "the down alone sets nothing")
    let (marked, _) = run([up(point(7700, 50))], context(selected: [10]), from: pressed)
    XCTAssertEqual(marked.insertMarker, 7680, "the deselect click sets it to its grid tick")
    XCTAssertEqual(NoteClipboard.pastePosition(marker: marked.insertMarker, notes: columns(standardNotes), selectedIds: [11]),
                   7680, "a paste lands at the marker")
    let (inserted, _) = run([down(point(9700, 50)), up(point(9700, 50))], context())
    XCTAssertEqual(inserted.insertMarker, 9600, "so does the insert click")
    let (velocityClick, _) = run([down(point(9700, 50)), up(point(9700, 50))], context(mode: .velocity))
    XCTAssertEqual(velocityClick.insertMarker, 9600, "in either mode")
  }

  func testARubberBandClearsTheInsertMarkerSoAPasteFollowsTheSelection() {
    let (marked, _) = run([down(point(7700, 50)), up(point(7700, 50))], context(selected: [10]))
    let (banded, effects) = run([down(point(3000, 70)), drag(point(100, 63)), up(point(100, 63))], context(), from: marked)
    XCTAssertEqual(sends(effects), [.selectNotes(trackId: 7, ids: [11, 12])])
    XCTAssertNil(banded.insertMarker, "a committed rubber band clears it")
    XCTAssertEqual(NoteClipboard.pastePosition(marker: banded.insertMarker, notes: columns(standardNotes), selectedIds: [11, 12]),
                   2880, "⌘C ⌘V after a band pastes after the selection's end")
    let (cancelled, _) = run([down(point(3000, 70)), drag(point(100, 63)), cancel], context(), from: marked)
    XCTAssertEqual(cancelled.insertMarker, 7680, "a cancelled band changes nothing")
  }

  func testANotePressClearsTheInsertMarker() {
    let (marked, _) = run([down(point(7700, 50)), up(point(7700, 50))], context(selected: [10]))
    XCTAssertEqual(marked.insertMarker, 7680)
    let (onNote, _) = run([down(point(1200, 64))], context(), from: marked)
    XCTAssertNil(onNote.insertMarker, "a body press clears it at the down")
    let (onEdge, _) = run([down(CGPoint(x: 143, y: point(0, 67).y)), up(CGPoint(x: 143, y: point(0, 67).y))], context(),
                          from: marked)
    XCTAssertNil(onEdge.insertMarker, "an edge press too")
    let (inVelocity, _) = run([down(point(1200, 64)), up(point(1200, 64))], context(mode: .velocity), from: marked)
    XCTAssertNil(inVelocity.insertMarker, "and a press in velocity mode")
  }

  func testAClickPastTheEndClearsTheInsertMarker() {
    let (marked, _) = run([down(point(7700, 50)), up(point(7700, 50))], context(selected: [10]))
    let (cleared, _) = run([down(point(20_000, 50)), up(point(20_000, 50))], context(selected: [10]), from: marked)
    XCTAssertNil(cleared.insertMarker, "a click in the hatched area clears it")
    let (atEnd, _) = run([down(point(15_360, 50)), up(point(15_360, 50))], context(), from: marked)
    XCTAssertNil(atEnd.insertMarker, "so does one exactly at the end")
  }

  // MARK: - Double-clicks

  func testADoubleClickOnEmptySpaceInsertsOnceBeforeTheSnapshotArrives() {
    let p = point(7700, 50)
    let (_, effects) = run([down(p), up(p), down(p, clicks: 2), up(p, clicks: 2)], context())
    XCTAssertEqual(sends(effects).count, 1)
    XCTAssertEqual(effects, [
      .send(.insertNotes(trackId: 7, notes: [RollNoteSpec(pitch: 50, start: 7680, length: 480, velocity: 100)], baseRevision: 5)),
      .previewOneShot(pitch: 50, velocity: 100),
    ])
  }

  func testADoubleClickOnEmptySpaceDoesNotDeleteTheNoteItsFirstClickInserted() {
    let p = point(7700, 50)
    let (first, _) = run([down(p), up(p)], context())
    let (echoed, echo) = RollEditLogic.snapshotChanged(revision: 6, state: first)
    XCTAssertEqual(echo, [], "idle: a new revision cancels nothing")
    let arrived = context(standardNotes + [(20, 50, 7680, 480, 100, true)], selected: [20], revision: 6)
    let (_, effects) = run([down(p, clicks: 2), up(p, clicks: 2)], arrived, from: echoed)
    XCTAssertEqual(effects, [.previewStart(pitch: 50, velocity: 100), .previewStop], "no removeNotes")
  }

  func testADoubleClickOnAnExistingNoteDeletesIt() {
    let p = point(1200, 64)
    let (pressed, firstClick) = run([down(p), up(p)], context())
    XCTAssertEqual(firstClick, [.send(.selectNotes(trackId: 7, ids: [11])), .previewStart(pitch: 64, velocity: 90), .previewStop])
    let (_, secondClick) = run([down(p, clicks: 2), up(p, clicks: 2)], context(selected: [11]), from: pressed)
    XCTAssertEqual(secondClick, [.previewStart(pitch: 64, velocity: 90), .previewStop,
                                 .send(.removeNotes(trackId: 7, ids: [11], baseRevision: 5))])
  }

  func testAnOlderInsertDoesNotProtectItsNoteFromALaterDoubleClick() {
    let p = point(7700, 50)
    let (inserted, _) = run([down(p), up(p)], context())
    let arrived = context(standardNotes + [(20, 50, 7680, 480, 100, true)], selected: [20], revision: 6)
    let (_, effects) = run([down(p), up(p), down(p, clicks: 2), up(p, clicks: 2)], arrived, from: inserted)
    XCTAssertEqual(sends(effects), [.removeNotes(trackId: 7, ids: [20], baseRevision: 6)],
                   "a new click sequence forgets the insert")
    assertPaired(effects)
  }

  func testInactiveNotesAreSelectableAndDeletable() {
    let p = point(6000, 62)
    let (pressed, first) = run([down(p), up(p)], context())
    XCTAssertEqual(sends(first), [.selectNotes(trackId: 7, ids: [13])])
    let (_, second) = run([down(p, clicks: 2), up(p, clicks: 2)], context(selected: [13]), from: pressed)
    XCTAssertEqual(sends(second), [.removeNotes(trackId: 7, ids: [13], baseRevision: 5)])
    assertPaired(first + second)
  }

  // MARK: - Rubber band

  func testARubberBandSelectsEveryNoteItTouches() throws {
    let from = point(3000, 70)
    let to = point(100, 63)
    let (dragging, _) = run([down(from), drag(to)], context(selected: [10]))
    XCTAssertEqual(dragging.rubberBand, CGRect(x: to.x, y: from.y, width: from.x - to.x, height: to.y - from.y))
    XCTAssertEqual(dragging.previewNotes, [])
    let (state, effects) = run([up(to)], context(selected: [10]), from: dragging)
    XCTAssertEqual(effects, [.send(.selectNotes(trackId: 7, ids: [11, 12]))], "the band replaces the selection")
    XCTAssertNil(state.rubberBand)
    XCTAssertNil(state.insertMarker)
  }

  func testAShiftRubberBandAddsToTheSelection() {
    let (_, effects) = run([down(point(3000, 70), shift: true), drag(point(100, 63)), up(point(100, 63))],
                           context(selected: [10]))
    XCTAssertEqual(effects, [.send(.selectNotes(trackId: 7, ids: [10, 11, 12]))])
  }

  func testARubberBandEndingOnTheSelectionItStartedWithSendsNothing() {
    let band = [down(point(3000, 70)), drag(point(100, 63)), up(point(100, 63))]
    XCTAssertEqual(run(band, context(selected: [11, 12])).effects, [], "the same notes: nothing changes")
    let shiftBand = [down(point(3000, 70), shift: true), drag(point(100, 63)), up(point(100, 63))]
    XCTAssertEqual(run(shiftBand, context(selected: [10, 11, 12])).effects, [], "⇧ adding nothing new")
    XCTAssertEqual(run([down(point(9000, 100)), drag(point(12_000, 90)), up(point(12_000, 90))], context()).effects, [],
                   "nothing selected before or after")
  }

  func testARubberBandOverNothingClearsTheSelection() {
    let (_, effects) = run([down(point(9000, 100)), drag(point(12_000, 90)), up(point(12_000, 90))], context(selected: [10]))
    XCTAssertEqual(effects, [.send(.selectNotes(trackId: 7, ids: []))])
  }

  // MARK: - Pressing notes

  func testABodyDownSelectsAtOnce() {
    let p = point(1200, 64)
    XCTAssertEqual(run([down(p)], context(selected: [10])).effects,
                   [.send(.selectNotes(trackId: 7, ids: [11])), .previewStart(pitch: 64, velocity: 90)])
    XCTAssertEqual(run([down(p, shift: true)], context(selected: [10])).effects,
                   [.send(.selectNotes(trackId: 7, ids: [10, 11])), .previewStart(pitch: 64, velocity: 90)], "⇧ toggles in")
    XCTAssertEqual(run([down(point(240, 60))], context(selected: [10, 11])).effects, [.previewStart(pitch: 60, velocity: 100)],
                   "an already selected note keeps the selection and sends nothing")
  }

  func testAShiftDownThatTogglesANoteOutNeitherPreviewsNorDrags() {
    let p = point(240, 60)
    let (state, effects) = run([down(p, shift: true), drag(offset(p, x: 48, y: -28))], context(selected: [10, 11]))
    XCTAssertEqual(effects, [.send(.selectNotes(trackId: 7, ids: [11]))])
    XCTAssertEqual(state.previewNotes, [])
    XCTAssertEqual(run([up(offset(p, x: 48, y: -28))], context(selected: [11]), from: state).effects, [])
  }

  func testASmallJiggleIsStillAClick() {
    let p = point(240, 60)
    let (_, effects) = run([down(p), drag(offset(p, x: 2, y: 2)), up(offset(p, x: 2, y: 2))], context(selected: [10]))
    XCTAssertEqual(effects, [.previewStart(pitch: 60, velocity: 100), .previewStop])
  }

  // MARK: - Move and duplicate

  func testAMoveSendsOneUpdateWithTheDownsRevision() {
    let p = point(240, 60)
    let target = offset(point(240, 62), x: 48)   // two rows up, one beat later
    let roll = context(selected: [10, 11])
    let (dragging, moving) = run([down(p), drag(target)], roll)
    XCTAssertEqual(moving, [.previewStart(pitch: 60, velocity: 100), .previewStop, .previewStart(pitch: 62, velocity: 100)])
    XCTAssertEqual(dragging.previewNotes.map(\.id), [10, 11])
    XCTAssertEqual(dragging.previewNotes.map(\.pitch), [62, 66])
    XCTAssertEqual(dragging.previewNotes.map(\.start), [960, 1920])
    XCTAssertEqual(dragging.hiddenIds, [10, 11])
    let (echoed, echo) = RollEditLogic.snapshotChanged(revision: 5, state: dragging)
    XCTAssertEqual(echo, [], "a selection echo keeps the drag")
    XCTAssertEqual(echoed, dragging)
    let (state, ending) = run([up(target)], context(selected: [10, 11], revision: 5), from: echoed)
    XCTAssertEqual(ending, [.previewStop, .send(.updateNotes(trackId: 7, edits: [
      RollNoteEdit(id: 10, pitch: 62, start: 960), RollNoteEdit(id: 11, pitch: 66, start: 1920),
    ], baseRevision: 5))])
    XCTAssertEqual(state.previewNotes, [], "the up drops the preview")
    XCTAssertEqual(state.hiddenIds, [])
    assertPaired(moving + ending)
  }

  func testAMoveSnapsTheAnchorsStartToTheNearestGridPoint() {
    let p = point(1200, 64)
    let (_, effects) = run([down(p), drag(offset(p, x: 15)), up(offset(p, x: 15))], context())   // +300 ticks
    XCTAssertEqual(sends(effects).last, .updateNotes(trackId: 7, edits: [RollNoteEdit(id: 11, start: 1440)], baseRevision: 5))
  }

  func testOptionAtTheDownDuplicates() {
    let p = point(1200, 64)
    let target = offset(p, x: 24)   // +480 ticks
    let (dragging, _) = run([down(p, option: true), drag(target)], context(selected: [11]))
    XCTAssertEqual(dragging.previewNotes.map(\.start), [1440])
    XCTAssertEqual(dragging.hiddenIds, [], "the originals stay drawn")
    let (_, effects) = run([up(target)], context(selected: [11]), from: dragging)
    XCTAssertEqual(effects, [.previewStop, .send(.insertNotes(trackId: 7, notes: [
      RollNoteSpec(pitch: 64, start: 1440, length: 480, velocity: 90),
    ], baseRevision: 5))])
  }

  func testOptionPressedOnlyDuringTheDragMoves() {
    let p = point(1200, 64)
    let target = offset(p, x: 24)
    let (_, effects) = run([down(p), drag(target, option: true), up(target)], context(selected: [11]))
    XCTAssertEqual(sends(effects), [.updateNotes(trackId: 7, edits: [RollNoteEdit(id: 11, start: 1440)], baseRevision: 5)])
  }

  func testADragBackToItsOriginSendsNothing() {
    let p = point(240, 60)
    let (_, effects) = run([down(p), drag(offset(point(240, 65), x: 96)), drag(p), up(p)], context(selected: [10]))
    XCTAssertEqual(sends(effects), [])
    assertPaired(effects)
    let (_, duplicate) = run([down(p, option: true), drag(offset(p, x: 50)), drag(p), up(p)], context(selected: [10]))
    XCTAssertEqual(sends(duplicate), [], "a duplicate dropped on its original sends nothing")
  }

  func testAGroupMoveCannotPushAStartToTheEnd() {
    let rows = standardNotes + [(30, 60, 14_880, 480, 100, true)]
    let p = point(240, 60)
    let target = offset(p, x: 1000)   // 20,000 ticks right
    let (_, effects) = run([down(p), drag(target), up(target)], context(rows, selected: [10, 30], grid: .off))
    XCTAssertEqual(sends(effects), [.updateNotes(trackId: 7, edits: [
      RollNoteEdit(id: 10, start: 479), RollNoteEdit(id: 30, start: 15_359),
    ], baseRevision: 5)], "grid Off: the raw clamp, the last tick before the end")
    XCTAssertEqual(sends(run([down(p), drag(target), up(target)], context(rows, selected: [10, 30])).effects), [],
                   "on a grid the anchor's last allowed grid point is where it is")
  }

  func testAGroupStoppedByTheEndPutsTheAnchorOnTheLastGridPoint() {
    let rows = standardNotes + [(30, 60, 14_600, 480, 100, true)]
    let p = point(240, 60)
    let target = offset(p, x: 1000)   // 20,000 ticks right; the clamp allows 759
    XCTAssertEqual(sends(run([down(p), drag(target), up(target)], context(rows, selected: [10, 30])).effects), [
      .updateNotes(trackId: 7, edits: [RollNoteEdit(id: 10, start: 480), RollNoteEdit(id: 30, start: 15_080)], baseRevision: 5),
    ], "the anchor lands on 480, the last eighth the clamp allows")
    XCTAssertEqual(sends(run([down(p), drag(target), up(target)], context(rows, selected: [10, 30], grid: .off)).effects), [
      .updateNotes(trackId: 7, edits: [RollNoteEdit(id: 10, start: 759), RollNoteEdit(id: 30, start: 15_359)], baseRevision: 5),
    ])
    let alone = point(14_700, 60)
    XCTAssertEqual(sends(run([down(alone), drag(offset(alone, x: 1000)), up(offset(alone, x: 1000))],
                             context(rows, selected: [30])).effects),
                   [.updateNotes(trackId: 7, edits: [RollNoteEdit(id: 30, start: 14_880)], baseRevision: 5)],
                   "a single note stops on the last eighth, not on 15,359")
    // An off-grid anchor whose last grid point lies behind it: the raw clamp, so a drag right never moves left.
    let offGrid = standardNotes + [(31, 72, 100, 480, 100, true), (32, 60, 15_200, 120, 100, true)]
    let a = point(200, 72)
    XCTAssertEqual(sends(run([down(a), drag(offset(a, x: 1000)), up(offset(a, x: 1000))],
                             context(offGrid, selected: [31, 32])).effects), [
      .updateNotes(trackId: 7, edits: [RollNoteEdit(id: 31, start: 259), RollNoteEdit(id: 32, start: 15_359)], baseRevision: 5),
    ])
  }

  func testAGroupAtTheTopRowKeepsItsPitches() {
    let rows: [NoteRow] = [(1, 126, 0, 480, 100, true), (2, 127, 480, 480, 100, true)]
    let p = point(240, 126)
    let target = offset(point(240, 127), x: 0, y: -14)   // two rows up (the second is above the keyboard)
    let (dragging, effects) = run([down(p), drag(target), up(target)], context(rows, selected: [1, 2]))
    XCTAssertEqual(sends(effects), [])
    XCTAssertTrue(dragging.isIdle)
  }

  func testASingleNoteMoveSnapsItsPitchAndAGroupMoveDoesNot() {
    let p = point(1200, 64)
    let twoUp = point(1200, 66)
    let (_, single) = run([down(p), drag(twoUp), up(twoUp)], context(snap: .chordTones))
    XCTAssertEqual(sends(single).last, .updateNotes(trackId: 7, edits: [RollNoteEdit(id: 11, pitch: 67)], baseRevision: 5),
                   "F♯ over C snaps to G")
    XCTAssertTrue(single.contains(.previewStart(pitch: 67, velocity: 90)))
    assertPaired(single)
    let oneUp = point(1200, 65)
    XCTAssertEqual(sends(run([down(p), drag(oneUp), up(oneUp)], context(snap: .chordTones)).effects),
                   [.selectNotes(trackId: 7, ids: [11])], "F snaps back to E: nothing moved")
    let (_, group) = run([down(p), drag(twoUp), up(twoUp)], context(selected: [10, 11], snap: .chordTones))
    XCTAssertEqual(sends(group), [.updateNotes(trackId: 7, edits: [
      RollNoteEdit(id: 10, pitch: 62), RollNoteEdit(id: 11, pitch: 66),
    ], baseRevision: 5)], "a group moves by exact rows")
  }

  func testASnappedSingleNoteDraggedBackToItsOriginSendsNothing() {
    let rows = standardNotes + [(40, 66, 2400, 480, 100, true)]   // F♯4 over C: not a chord tone
    let p = point(2600, 66)
    let roll = context(rows, selected: [40], snap: .chordTones)
    let (_, back) = run([down(p), drag(point(2600, 69)), drag(p), up(p)], roll)
    XCTAssertEqual(sends(back), [], "back at its origin the note is not snapped")
    XCTAssertEqual(back.last, .previewStop)
    assertPaired(back)
    let (_, still) = run([down(p), drag(offset(p, x: 3)), drag(offset(p, x: 4)), up(offset(p, x: 4))], roll)
    XCTAssertEqual(sends(still), [], "a drag that changes neither row nor tick does not snap")
    let right = offset(p, x: 24)   // +480 ticks on the same row
    XCTAssertEqual(sends(run([down(p), drag(right), up(right)], roll).effects),
                   [.updateNotes(trackId: 7, edits: [RollNoteEdit(id: 40, pitch: 67, start: 2880)], baseRevision: 5)],
                   "once it actually moves, a horizontal move snaps it too")
  }

  // MARK: - Return to the origin, off the grid (D15)

  private let offGridNotes: [NoteRow] = [(10, 60, 100, 500, 100, true), (11, 64, 960, 480, 90, true),
                                         (12, 67, 2010, 470, 80, true)]

  func testAnOffGridNoteDraggedBackToItsOriginSendsNothing() {
    // The review's reproducer: eighth grid, content coordinates, down/drag/drag/up.
    XCTAssertEqual(geometry.pitch(atY: 945), 60)
    let roll = context(offGridNotes, selected: [10])
    let events = [down(CGPoint(x: 17.5, y: 945)), drag(CGPoint(x: 65.5, y: 875)), drag(CGPoint(x: 17.5, y: 945)),
                  up(CGPoint(x: 17.5, y: 945))]
    let (state, effects) = run(events, roll)
    XCTAssertEqual(sends(effects), [])
    assertPaired(effects)
    XCTAssertEqual(state.previewNotes, [])
    // The preview drawn at the origin agrees with the (empty) commit: the note stays where it is.
    let (dragging, _) = run(Array(events.prefix(3)), roll)
    XCTAssertEqual(dragging.previewNotes.map(\.start), [100])
    XCTAssertEqual(dragging.previewNotes.map(\.pitch), [60])
  }

  func testAnOffGridDuplicateDroppedOnItsOriginInsertsNothing() {
    let p = CGPoint(x: 17.5, y: 945)
    let roll = context(offGridNotes, selected: [10])
    let (dragging, moving) = run([down(p, option: true), drag(CGPoint(x: 65.5, y: 875)), drag(p)], roll)
    XCTAssertEqual(dragging.previewNotes.map(\.start), [100], "the preview is the origin, not a grid point")
    let (_, effects) = run([up(p)], roll, from: dragging)
    XCTAssertEqual(sends(effects), [])
    assertPaired(moving + effects)
  }

  func testAnOffGridStartEdgeResizeBackToItsOriginSendsNothing() {
    let p = CGPoint(x: 6, y: point(0, 60).y)   // note 10 starts at x 5
    XCTAssertEqual(RollHit.hit(at: p, context: context(offGridNotes))?.zone, .startEdge)
    let roll = context(offGridNotes, selected: [10])
    let (dragging, _) = run([down(p), drag(offset(p, x: 20)), drag(p)], roll)
    XCTAssertEqual(dragging.previewNotes.map(\.start), [100])
    XCTAssertEqual(dragging.previewNotes.map(\.length), [500])
    let (state, effects) = run([up(p)], roll, from: dragging)
    XCTAssertEqual(sends(effects), [])
    XCTAssertNil(state.lastLength, "nothing was resized")
  }

  func testAnOffGridEndEdgeResizeBackToItsOriginSendsNothing() {
    let p = CGPoint(x: 29, y: point(0, 60).y)   // note 10 ends at x 30
    XCTAssertEqual(RollHit.hit(at: p, context: context(offGridNotes))?.zone, .endEdge)
    let roll = context(offGridNotes, selected: [10])
    let (dragging, _) = run([down(p), drag(offset(p, x: -12)), drag(p)], roll)
    XCTAssertEqual(dragging.previewNotes.map(\.length), [500])
    let (state, effects) = run([up(p)], roll, from: dragging)
    XCTAssertEqual(sends(effects), [])
    XCTAssertNil(state.lastLength)
  }

  func testAMixedGroupDraggedBackToItsOriginSendsNothing() {
    let roll = context(offGridNotes, selected: [10, 11, 12])
    for pressed in [10, 11, 12] {
      let note = offGridNotes.first { $0.id == pressed }!
      let p = CGPoint(x: geometry.x(tick: note.start) + 8, y: geometry.y(pitch: note.pitch) + 7)
      let (dragging, moving) = run([down(p), drag(offset(p, x: 37, y: -28)), drag(p)], roll)
      XCTAssertEqual(dragging.previewNotes.map(\.start), [100, 960, 2010], "pressing note \(pressed)")
      let (_, effects) = run([up(p)], roll, from: dragging)
      XCTAssertEqual(sends(effects), [], "pressing note \(pressed)")
      assertPaired(moving + effects)
    }
    // A group resize back to the origin too (note 10's end edge, the others follow).
    let p = CGPoint(x: 29, y: point(0, 60).y)
    let (_, resize) = run([down(p), drag(offset(p, x: 11)), drag(p), up(p)], roll)
    XCTAssertEqual(sends(resize), [])
  }

  func testAnOffGridNoteBackAtItsXOnAnotherRowChangesOnlyItsPitch() {
    let p = CGPoint(x: 17.5, y: 945)
    let back = CGPoint(x: 17.5, y: 945 - 28)   // two rows up
    let roll = context(offGridNotes, selected: [10])
    let (_, effects) = run([down(p), drag(CGPoint(x: 65.5, y: 875)), drag(back), up(back)], roll)
    XCTAssertEqual(sends(effects), [.updateNotes(trackId: 7, edits: [RollNoteEdit(id: 10, pitch: 62)], baseRevision: 5)],
                   "a pitch-only edit: no start")
    let (_, copy) = run([down(p, option: true), drag(back), up(back)], roll)
    XCTAssertEqual(sends(copy), [.insertNotes(trackId: 7, notes: [
      RollNoteSpec(pitch: 62, start: 100, length: 500, velocity: 100),
    ], baseRevision: 5)], "the copy keeps the origin's start")
  }

  func testAnUpPastTheThresholdWithNoDragEventCommitsTheMove() {
    let p = point(1200, 64)
    XCTAssertEqual(run([down(p), up(offset(p, x: 24))], context()).effects, [
      .send(.selectNotes(trackId: 7, ids: [11])), .previewStart(pitch: 64, velocity: 90), .previewStop,
      .send(.updateNotes(trackId: 7, edits: [RollNoteEdit(id: 11, start: 1440)], baseRevision: 5)),
    ])
  }

  func testAnUpOnAnotherRowThanTheLastDragCommitsTheUpsRow() {
    let p = point(1200, 64)
    let (_, effects) = run([down(p), drag(point(1200, 66)), up(point(1200, 68))], context(selected: [11]))
    XCTAssertEqual(effects, [
      .previewStart(pitch: 64, velocity: 90), .previewStop, .previewStart(pitch: 66, velocity: 90), .previewStop,
      .send(.updateNotes(trackId: 7, edits: [RollNoteEdit(id: 11, pitch: 68)], baseRevision: 5)),
    ], "the up stops the held pitch once and starts nothing")
  }

  func testASingleNoteSnapsByTheGuideAtItsNewStart() {
    let p = point(1200, 64)
    let target = offset(point(1200, 67), x: 192)   // into E7's bar (+3840 ticks), onto G
    let (_, effects) = run([down(p), drag(target), up(target)], context(snap: .scale))
    XCTAssertEqual(sends(effects).last,
                   .updateNotes(trackId: 7, edits: [RollNoteEdit(id: 11, pitch: 68, start: 4800)], baseRevision: 5))
  }

  // MARK: - Resize

  func testAnEndEdgeResizeMovesTheEndToTheGrid() {
    let p = CGPoint(x: 143, y: point(0, 67).y)   // note 12 ends at x 144
    let target = offset(p, x: 20)   // +400 ticks: the end 3280 snaps to 3360
    let (dragging, _) = run([down(p), drag(target)], context())
    XCTAssertEqual(dragging.previewNotes, [RollNote(id: 12, pitch: 67, start: 1920, length: 1440, velocity: 80, anchor: 1,
                                                    offset: 1920, active: true)])
    let (state, effects) = run([up(target)], context(selected: [12]), from: dragging)
    XCTAssertEqual(effects, [.previewStop, .send(.updateNotes(trackId: 7, edits: [RollNoteEdit(id: 12, length: 1440)],
                                                              baseRevision: 5))])
    XCTAssertEqual(state.lastLength, 1440, "the next insert takes the resized length")
    let empty = point(7700, 50)
    XCTAssertEqual(sends(run([down(empty), up(empty)], context(), from: state).effects),
                   [.insertNotes(trackId: 7, notes: [RollNoteSpec(pitch: 50, start: 7680, length: 1440, velocity: 100)],
                                 baseRevision: 5)])
  }

  func testAStartEdgeResizeKeepsTheEnd() {
    let p = CGPoint(x: 97, y: point(0, 67).y)   // note 12 starts at x 96
    let left = offset(p, x: -24)
    let (earlier, effects) = run([down(p), drag(left), up(left)], context(selected: [12]))
    XCTAssertEqual(sends(effects), [.updateNotes(trackId: 7, edits: [RollNoteEdit(id: 12, start: 1440, length: 1440)],
                                                 baseRevision: 5)])
    XCTAssertEqual(earlier.lastLength, 1440)
    let right = offset(p, x: 30)   // +600: 2520 snaps to 2400
    XCTAssertEqual(sends(run([down(p), drag(right), up(right)], context(selected: [12])).effects),
                   [.updateNotes(trackId: 7, edits: [RollNoteEdit(id: 12, start: 2400, length: 480)], baseRevision: 5)])
  }

  func testAGroupResizeStopsAtTheMinimumLength() {
    let rows: [NoteRow] = [(1, 60, 0, 480, 100, true), (2, 62, 960, 60, 100, true)]
    let p = CGPoint(x: 23, y: point(0, 60).y)   // note 1's end edge
    let target = offset(p, x: -30)   // -600 ticks
    let (_, effects) = run([down(p), drag(target), up(target)], context(rows, selected: [1, 2], grid: .off))
    XCTAssertEqual(sends(effects), [.updateNotes(trackId: 7, edits: [
      RollNoteEdit(id: 1, length: 450), RollNoteEdit(id: 2, length: 30),
    ], baseRevision: 5)])
  }

  // MARK: - Velocity mode

  func testAVelocityDragChangesTheGroupAndPreviewsOnce() {
    let p = point(1200, 64)
    let roll = context(selected: [10, 11], mode: .velocity)
    let (pressed, atDown) = run([down(p)], roll)
    XCTAssertEqual(atDown, [], "no held preview, the note was already selected")
    let (dragging, _) = run([drag(offset(p, y: -20))], roll, from: pressed)
    XCTAssertEqual(dragging.previewNotes.map(\.velocity), [120, 110])
    let (_, effects) = run([up(offset(p, y: -20))], roll, from: dragging)
    XCTAssertEqual(effects, [.send(.updateNotes(trackId: 7, edits: [
      RollNoteEdit(id: 10, velocity: 120), RollNoteEdit(id: 11, velocity: 110),
    ], baseRevision: 5)), .previewOneShot(pitch: 64, velocity: 110)])
  }

  func testAVelocityDragKeepsEveryNoteIn1To127() {
    let p = point(1200, 64)
    let roll = context(selected: [10, 11], mode: .velocity)
    XCTAssertEqual(run([down(p), drag(offset(p, y: -50)), up(offset(p, y: -50))], roll).effects, [
      .send(.updateNotes(trackId: 7, edits: [RollNoteEdit(id: 10, velocity: 127), RollNoteEdit(id: 11, velocity: 117)],
                         baseRevision: 5)),
      .previewOneShot(pitch: 64, velocity: 117),
    ])
    XCTAssertEqual(run([down(p), drag(offset(p, y: 200)), up(offset(p, y: 200))], roll).effects, [
      .send(.updateNotes(trackId: 7, edits: [RollNoteEdit(id: 10, velocity: 11), RollNoteEdit(id: 11, velocity: 1)],
                         baseRevision: 5)),
      .previewOneShot(pitch: 64, velocity: 1),
    ])
  }

  func testVelocityModeSelectsWithoutPreviewAndNeverInserts() {
    let p = point(1200, 64)
    XCTAssertEqual(run([down(p), up(p)], context(mode: .velocity)).effects, [.send(.selectNotes(trackId: 7, ids: [11]))])
    XCTAssertEqual(run([down(p), drag(offset(p, x: 30)), up(offset(p, x: 30))], context(selected: [11], mode: .velocity)).effects,
                   [], "a horizontal drag is no velocity drag")
    let empty = point(7700, 50)
    XCTAssertEqual(run([down(empty), up(empty)], context(mode: .velocity)).effects, [])
    XCTAssertEqual(run([down(empty), up(empty)], context(selected: [11], mode: .velocity)).effects,
                   [.send(.selectNotes(trackId: 7, ids: []))])
    XCTAssertEqual(run([down(point(3000, 70)), drag(point(100, 63)), up(point(100, 63))], context(mode: .velocity)).effects,
                   [.send(.selectNotes(trackId: 7, ids: [11, 12]))], "the rubber band works as in notes mode")
  }

  // MARK: - Cancellation

  func testANewRevisionMidDragCancelsTheGestureAndItsPreview() {
    let p = point(240, 60)
    let target = offset(point(240, 62), x: 48)
    let (dragging, started) = run([down(p), drag(target)], context(selected: [10]))
    let (cancelled, effects) = RollEditLogic.snapshotChanged(revision: 6, state: dragging)
    XCTAssertEqual(effects, [.previewStop])
    XCTAssertTrue(cancelled.isIdle)
    XCTAssertEqual(cancelled.previewNotes, [])
    let (_, late) = run([drag(target), up(target)], context(selected: [10], revision: 6), from: cancelled)
    XCTAssertEqual(late, [], "the rest of the gesture is ignored")
    assertPaired(started + effects + late)
  }

  func testANewRevisionWhilePressedCancelsToo() {
    let p = point(1200, 64)
    let (pressed, started) = run([down(p)], context())
    let (cancelled, effects) = RollEditLogic.snapshotChanged(revision: 9, state: pressed)
    XCTAssertEqual(effects, [.previewStop])
    XCTAssertEqual(run([up(p, clicks: 2)], context(), from: cancelled).effects, [])
    assertPaired(started + effects)
    let empty = point(7700, 50)
    let (emptyPress, _) = run([down(empty)], context())
    let (dropped, none) = RollEditLogic.snapshotChanged(revision: 9, state: emptyPress)
    XCTAssertEqual(none, [], "nothing was held")
    XCTAssertEqual(run([up(empty)], context(revision: 9), from: dropped).effects, [], "the click inserts nothing")
  }

  func testTheCancelPhaseStopsThePreviewAndSendsNothing() {
    let p = point(240, 60)
    let target = offset(p, x: 48)
    let (_, effects) = run([down(p), drag(target), cancel, up(target)], context(selected: [10]))
    XCTAssertEqual(sends(effects), [])
    XCTAssertEqual(effects.last, .previewStop)
    assertPaired(effects)
    let (_, band) = run([down(point(3000, 70)), drag(point(100, 63)), cancel, up(point(100, 63))], context())
    XCTAssertEqual(band, [])
  }

  func testADownWhileDraggingCancelsTheLostGestureFirst() {
    let p = point(240, 60)
    let (_, effects) = run([down(p), drag(offset(p, x: 48)), down(point(1200, 64))], context(selected: [10]))
    XCTAssertEqual(effects, [.previewStart(pitch: 60, velocity: 100), .previewStop,
                             .send(.selectNotes(trackId: 7, ids: [11])), .previewStart(pitch: 64, velocity: 90)])
  }

  func testAGestureFromAnotherTrackIsCancelledBeforeTheEvent() {
    let p = point(240, 60)
    let (dragging, started) = run([down(p), drag(offset(p, x: 48))], context(selected: [10]))
    XCTAssertEqual(started.first, .previewStart(pitch: 60, velocity: 100))
    let other = context(selected: [], track: 8)
    let (switched, effects) = run([drag(offset(p, x: 96))], other, from: dragging)
    XCTAssertEqual(effects, [.previewStop], "the parked drag stops its preview and sends nothing")
    XCTAssertTrue(switched.isIdle)
    XCTAssertEqual(run([up(offset(p, x: 96))], other, from: dragging).effects, [.previewStop], "an up commits nothing")
    let (fresh, downEffects) = run([down(point(1200, 64))], other, from: dragging)
    XCTAssertEqual(downEffects, [.previewStop, .send(.selectNotes(trackId: 8, ids: [11])), .previewStart(pitch: 64, velocity: 90)])
    XCTAssertEqual(fresh.press?.trackId, 8)
    let (emptyPress, _) = run([down(point(7700, 50))], context())
    let (idle, none) = run([up(point(7700, 50))], other, from: emptyPress)
    XCTAssertEqual(none, [], "a parked empty click inserts nothing on the new track")
    XCTAssertTrue(idle.isIdle)
    assertPaired(started + effects)
  }

  func testAZoomMidDragKeepsTheDelta() {
    let p = point(1200, 64)
    let zoomed = RollGeometry(pxPerBeat: 96, rowHeight: 20, ticksPerBeat: 960)
    func zoomedPoint(_ tick: Int, _ pitch: Int) -> CGPoint {
      CGPoint(x: zoomed.x(tick: tick), y: zoomed.y(pitch: pitch) + 10)
    }
    let (pressed, _) = run([down(p)], context(selected: [11]))
    let after = context(selected: [11], geometry: zoomed)
    let (stillPressed, none) = run([drag(zoomedPoint(1200, 64))], after, from: pressed)
    XCTAssertEqual(none, [], "the same tick and row after the zoom: no travel")
    XCTAssertNil(stillPressed.drag)
    let target = zoomedPoint(1680, 66)   // +480 ticks, two rows up
    let (dragging, _) = run([drag(target)], after, from: stillPressed)
    XCTAssertEqual(dragging.previewNotes.map(\.start), [1440])
    XCTAssertEqual(dragging.previewNotes.map(\.pitch), [66])
    XCTAssertEqual(sends(run([up(target)], after, from: dragging).effects),
                   [.updateNotes(trackId: 7, edits: [RollNoteEdit(id: 11, pitch: 66, start: 1440)], baseRevision: 5)])
    // A rubber band keeps its corner on the down's tick and pitch.
    let (band, _) = run([down(point(3000, 70))], context())
    let (banding, _) = run([drag(zoomedPoint(100, 63))], context(geometry: zoomed), from: band)
    let corner = zoomedPoint(3000, 70)
    let end = zoomedPoint(100, 63)
    XCTAssertEqual(banding.rubberBand, CGRect(x: end.x, y: corner.y, width: corner.x - end.x, height: end.y - corner.y))
    XCTAssertEqual(sends(run([up(end)], context(geometry: zoomed), from: banding).effects),
                   [.selectNotes(trackId: 7, ids: [11, 12])])
  }

  func testEdgeDownsPreviewAndPairInEverySequence() {
    let end = CGPoint(x: 143, y: point(0, 67).y)
    let sequences: [[RollPointerEvent]] = [
      [down(end), up(end)],
      [down(end), drag(offset(end, x: 20)), up(offset(end, x: 20))],
      [down(end), drag(offset(end, x: 20)), cancel],
      [down(point(240, 60)), drag(point(240, 70)), drag(point(240, 50)), drag(point(240, 60)), up(point(240, 60))],
      [down(point(240, 60), option: true), drag(point(240, 70)), up(point(240, 70))],
    ]
    for events in sequences {
      let (state, effects) = run(events, context())
      assertPaired(effects)
      XCTAssertEqual(state.isIdle, events.last?.phase != .drag)
    }
  }

  // MARK: - Context

  func testTheContextReadsTheSnapshot() throws {
    let url = try SnapshotTests.fixturesDirectory().appendingPathComponent("snapshot-tracks.json")
    let snapshot = try JSONDecoder().decode(Snapshot.self, from: Data(contentsOf: url))
    let lead = try XCTUnwrap(RollContext(snapshot: snapshot, trackId: 2, geometry: geometry))
    XCTAssertEqual(lead.selectedIds, [4, 5])
    XCTAssertEqual(lead.revision, 15)
    XCTAssertEqual(lead.notes.id, [4, 5])
    XCTAssertEqual(lead.timing, RollTiming(snapshot: snapshot))
    XCTAssertEqual(lead.mode, .notes)
    XCTAssertEqual(lead.grid, .default)
    XCTAssertEqual(lead.snap, .default)
    let bass = try XCTUnwrap(RollContext(snapshot: snapshot, trackId: 1, geometry: geometry, mode: .velocity))
    XCTAssertEqual(bass.selectedIds, [], "the selection lives in the other track")
    XCTAssertEqual(bass.mode, .velocity)
    XCTAssertNil(RollContext(snapshot: snapshot, trackId: 9, geometry: geometry))
  }

  // MARK: - Commands

  func testTheCommandsAreTheEnginesDictionaries() throws {
    XCTAssertEqual(try json(.selectNotes(trackId: 7, ids: [10, 11])), #"{"cmd":"selectNotes","ids":[10,11],"trackId":7}"#)
    XCTAssertEqual(try json(.insertNotes(trackId: 7, notes: [RollNoteSpec(pitch: 50, start: 7680, length: 480, velocity: 100)],
                                         baseRevision: 5)),
                   #"{"baseRevision":5,"cmd":"insertNotes","notes":[{"length":480,"pitch":50,"start":7680,"velocity":100}],"trackId":7}"#)
    XCTAssertEqual(try json(.removeNotes(trackId: 7, ids: [11], baseRevision: nil)), #"{"cmd":"removeNotes","ids":[11],"trackId":7}"#)
    XCTAssertEqual(try json(.updateNotes(trackId: 7, edits: [RollNoteEdit(id: 10, pitch: 62, start: 960),
                                                             RollNoteEdit(id: 11, length: 30, velocity: 3)], baseRevision: 5)),
                   #"{"baseRevision":5,"cmd":"updateNotes","notes":[{"id":10,"pitch":62,"start":960},{"id":11,"length":30,"velocity":3}],"trackId":7}"#)
    XCTAssertEqual(try json(.removeNotes(trackId: 7, ids: [1], baseRevision: UInt64.max)),
                   #"{"baseRevision":18446744073709551615,"cmd":"removeNotes","ids":[1],"trackId":7}"#)
  }
}
