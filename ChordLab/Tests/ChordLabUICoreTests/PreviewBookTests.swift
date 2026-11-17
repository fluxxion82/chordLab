import CoreGraphics
import XCTest
@testable import ChordLabUICore

/// The editor's note previews (UI/Core/PreviewBook.swift, tracks design D15 "Preview", plan Task 19): ids, the one
/// held preview whoever holds it (a palette cell, the roll, a keyboard key), the roll's effects turned into outputs,
/// how a hold ends when another owner takes over (a roll hold: stopped by the book AND its gesture cancelled, in the
/// same outputs), and the wire form (`kMsgTagPreview`). `ChordLabState` only routes the outputs; the random test
/// below drives the book with the real gesture machine (`RollEditLogic`) and the same routing.
final class PreviewBookTests: XCTestCase {
  /// Every Start is followed by exactly one Stop with its id, never two, never a Stop without a Start.
  private func assertPaired(_ messages: [PreviewMessage], open: Set<Int> = [], file: StaticString = #filePath,
                            line: UInt = #line) {
    var sounding = Set<Int>()
    var started = Set<Int>()
    for message in messages {
      switch message {
      case .start(let id, _, _):
        XCTAssertFalse(started.contains(id), "id \(id) started twice", file: file, line: line)
        started.insert(id)
        sounding.insert(id)
      case .stop(let id):
        XCTAssertTrue(sounding.contains(id), "a stop for \(id), which is not sounding", file: file, line: line)
        sounding.remove(id)
      case .extend(let id):
        XCTAssertTrue(sounding.contains(id), "an extend for \(id), which is not sounding", file: file, line: line)
      }
    }
    XCTAssertEqual(sounding, open, "sounding at the end", file: file, line: line)
  }

  private func previews(_ outputs: [PreviewOutput]) -> [PreviewMessage] {
    outputs.compactMap { if case .preview(let message) = $0 { return message } else { return nil } }
  }

  private let select = RollCommand.selectNotes(trackId: 1, ids: [2])
  private let update = RollCommand.updateNotes(trackId: 1, edits: [RollNoteEdit(id: 2, pitch: 46)], baseRevision: 15)

  // MARK: - Wire form

  func testTheWireFormIsTheSchedulersPreviewMessage() throws {
    func json(_ message: PreviewMessage) throws -> String {
      String(decoding: try JSONSerialization.data(withJSONObject: message.json, options: [.sortedKeys]), as: UTF8.self)
    }
    XCTAssertEqual(try json(.start(id: 3, pitches: [45], velocity: 90)), #"{"id":3,"on":true,"pitches":[45],"velocity":90}"#)
    XCTAssertEqual(try json(.stop(id: 3)), #"{"id":3,"on":false}"#)
    XCTAssertEqual(try json(.extend(id: 3)), #"{"extend":true,"id":3}"#)
  }

  // MARK: - Holds a view starts (a cell, a key)

  func testAHoldTakesTheNextIdAndOnlyItsOwnEndStopsIt() {
    var book = PreviewBook()
    let cell = book.hold(.cell("c"), pitches: [60, 64], velocity: 100)
    XCTAssertEqual(cell.id, 1)
    XCTAssertEqual(cell.outputs, [.preview(.start(id: 1, pitches: [60, 64], velocity: 100))])
    XCTAssertEqual(book.held, PreviewHold(id: 1, owner: .cell("c")))
    XCTAssertEqual(book.heldCellId, "c")
    XCTAssertEqual(book.endHold(), [.preview(.stop(id: 1))])
    XCTAssertNil(book.held)
    XCTAssertEqual(book.endHold(), [], "nothing held, nothing stopped")
    let key = book.hold(.key(pitch: 48), pitches: [48], velocity: 100)
    XCTAssertEqual(key.id, 2)
    XCTAssertNil(book.heldCellId)
    XCTAssertEqual(book.endHold(id: 1), [], "an older press's end does not stop this hold")
    XCTAssertEqual(book.endHold(id: 2), [.preview(.stop(id: 2))])
    XCTAssertEqual(book.endHold(id: 2), [], "its end is sent once")
  }

  func testAHoldOverACellOrKeyHoldStopsItFirst() {
    var book = PreviewBook()
    _ = book.hold(.cell("a"), pitches: [60], velocity: 100)
    XCTAssertEqual(book.hold(.key(pitch: 50), pitches: [50], velocity: 100).outputs,
                   [.preview(.stop(id: 1)), .preview(.start(id: 2, pitches: [50], velocity: 100))])
  }

  func testAHoldOverARollHoldStopsItAndCancelsThatGesture() {
    // Never stopped behind the machine's back: the stop and the gesture's cancel come in the same outputs, so the
    // cancelled machine's own stop finds no hold.
    var book = PreviewBook()
    _ = book.perform([.previewStart(pitch: 45, velocity: 90)], trackId: 3)
    XCTAssertEqual(book.hold(.key(pitch: 50), pitches: [50], velocity: 100).outputs,
                   [.preview(.stop(id: 1)), .cancelRoll(trackId: 3), .preview(.start(id: 2, pitches: [50], velocity: 100))])
    XCTAssertEqual(book.perform([.previewStop], trackId: 3), [], "the cancelled machine's stop sends nothing")
    _ = book.endHold()
    _ = book.perform([.previewStart(pitch: 45, velocity: 90)], trackId: 3)
    XCTAssertEqual(book.endHold(), [.preview(.stop(id: 3)), .cancelRoll(trackId: 3)],
                   "the keep-alive's lost release, the editor closing, panic")
    XCTAssertEqual(book.perform([.previewStop], trackId: 3), [])
  }

  // MARK: - The roll's effects

  func testAPressMoveAndUpBecomeCommandsAndPairedPreviews() {
    var book = PreviewBook()
    var out = book.perform([.send(select), .previewStart(pitch: 45, velocity: 90)], trackId: 1)
    XCTAssertEqual(out, [.command(select), .preview(.start(id: 1, pitches: [45], velocity: 90))])
    XCTAssertEqual(book.held, PreviewHold(id: 1, owner: .roll(trackId: 1)))
    out += book.perform([.previewStop, .previewStart(pitch: 46, velocity: 90)], trackId: 1)
    out += book.perform([.previewStop, .send(update)], trackId: 1)
    XCTAssertEqual(Array(out.suffix(4)), [.preview(.stop(id: 1)), .preview(.start(id: 2, pitches: [46], velocity: 90)),
                                          .preview(.stop(id: 2)), .command(update)])
    XCTAssertNil(book.held)
    assertPaired(previews(out))
  }

  func testARollStartOverAnotherOwnersHold() {
    var book = PreviewBook()
    _ = book.hold(.cell("a"), pitches: [60], velocity: 100)
    XCTAssertEqual(book.perform([.previewStart(pitch: 40, velocity: 100)], trackId: 1),
                   [.preview(.stop(id: 1)), .preview(.start(id: 2, pitches: [40], velocity: 100))])
    XCTAssertEqual(book.perform([.previewStart(pitch: 41, velocity: 100)], trackId: 2),
                   [.preview(.stop(id: 2)), .cancelRoll(trackId: 1), .preview(.start(id: 3, pitches: [41], velocity: 100))],
                   "another track's roll hold: stopped and its gesture cancelled")
    // The same track starting over its own hold breaks the machine's contract (it always stops first): the old id is
    // stopped so the pairing holds, and its gesture is NOT cancelled (that would drop the press just made).
    XCTAssertEqual(book.perform([.previewStart(pitch: 42, velocity: 100)], trackId: 2),
                   [.preview(.stop(id: 3)), .preview(.start(id: 4, pitches: [42], velocity: 100))])
  }

  func testAStopWithNothingHeldOrAnotherOwnersHoldSendsNothing() {
    var book = PreviewBook()
    XCTAssertEqual(book.perform([.previewStop], trackId: 1), [], "a second stop is never sent")
    _ = book.hold(.cell("a"), pitches: [60], velocity: 100)
    XCTAssertEqual(book.perform([.previewStop], trackId: 1), [], "the palette's hold is not the roll's")
    _ = book.endHold()
    _ = book.perform([.previewStart(pitch: 40, velocity: 100)], trackId: 2)
    XCTAssertEqual(book.perform([.previewStop], trackId: 1), [], "another track's roll hold is not this one's")
    XCTAssertEqual(book.perform([.previewStop], trackId: 2), [.preview(.stop(id: 2))])
  }

  func testARollPressEndsAKeyOrCellHoldButNotItsOwn() {
    // A press that starts no preview (an empty click, a rubber band, a Velocity-mode press) must not leave a key or a
    // cell sounding behind it while the button stays down.
    var book = PreviewBook()
    XCTAssertEqual(book.rollPressed(trackId: 1), [])
    _ = book.hold(.key(pitch: 50), pitches: [50], velocity: 100)
    XCTAssertEqual(book.rollPressed(trackId: 1), [.preview(.stop(id: 1))])
    _ = book.hold(.cell("c"), pitches: [60], velocity: 100)
    XCTAssertEqual(book.rollPressed(trackId: 1), [.preview(.stop(id: 2))])
    _ = book.perform([.previewStart(pitch: 40, velocity: 100)], trackId: 2)
    XCTAssertEqual(book.rollPressed(trackId: 1), [.preview(.stop(id: 3)), .cancelRoll(trackId: 2)])
    _ = book.perform([.previewStart(pitch: 40, velocity: 100)], trackId: 1)
    XCTAssertEqual(book.rollPressed(trackId: 1), [], "this track's own hold is its machine's to stop")
    XCTAssertEqual(book.held?.owner, .roll(trackId: 1))
  }

  /// While the instance follows another ChordLab (`LinkGate.allowsHold` false) the roll starts nothing: no start, no
  /// hold, no one-shot; its commands still go to the state's sender (which gates them), and the stop of a hold begun
  /// before still goes out.
  func testWithoutStartsTheRollNeitherHoldsNorSoundsButStillStops() {
    var book = PreviewBook()
    _ = book.perform([.previewStart(pitch: 40, velocity: 100)], trackId: 1)                       // begun before
    XCTAssertEqual(book.perform([.send(select), .previewStart(pitch: 41, velocity: 100),
                                 .previewOneShot(pitch: 42, velocity: 100)], trackId: 2, startsAllowed: false),
                   [.command(select)])
    XCTAssertEqual(book.held, PreviewHold(id: 1, owner: .roll(trackId: 1)), "the earlier hold is untouched")
    XCTAssertEqual(book.oneShots, [])
    XCTAssertEqual(book.perform([.previewStop], trackId: 1, startsAllowed: false), [.preview(.stop(id: 1))])
    XCTAssertNil(book.held)
    XCTAssertEqual(book.perform([.previewStart(pitch: 43, velocity: 100)], trackId: 1, startsAllowed: false), [])
    XCTAssertNil(book.held)
  }

  func testAOneShotStartsAtOnceAndStopsOnceWhenItEnds() {
    var book = PreviewBook()
    let insert = RollCommand.insertNotes(trackId: 1, notes: [RollNoteSpec(pitch: 48, start: 4800, length: 480, velocity: 100)],
                                         baseRevision: 15)
    var out = book.perform([.send(insert), .previewOneShot(pitch: 48, velocity: 100)], trackId: 1)
    XCTAssertEqual(out, [.command(insert), .preview(.start(id: 1, pitches: [48], velocity: 100)), .scheduleOneShotEnd(id: 1)])
    XCTAssertNil(book.held, "a one-shot is not the held preview")
    XCTAssertEqual(book.oneShots, [1])
    out += book.perform([.previewStart(pitch: 50, velocity: 80)], trackId: 1)
    var messages = previews(out)
    messages += previews(book.oneShotEnded(id: 1))
    XCTAssertEqual(messages.last, .stop(id: 1))
    XCTAssertEqual(book.oneShotEnded(id: 1), [], "its end is sent once")
    XCTAssertEqual(book.oneShotEnded(id: 99), [], "an id that is no one-shot")
    messages += previews(book.endHold())
    assertPaired(messages)
  }

  // MARK: - The real gesture machine and the book together

  /// Two tracks' real gesture machines (`RollEditLogic`), the book, and the routing `ChordLabState` does: a preview
  /// is sent, a one-shot end is scheduled, a `.cancelRoll` cancels that track's machine and feeds its effects back.
  private struct Rig {
    var book = PreviewBook()
    var machines: [Int: RollEditState] = [1: RollEditState(), 2: RollEditState()]
    var pointer: [Int: CGPoint] = [:]
    var messages: [PreviewMessage] = []
    var pendingOneShots: [Int] = []
    var keyIds: [Int] = []
    var cancelledStopsThatSent: [String] = []
    let contexts: [Int: RollContext]

    mutating func route(_ outputs: [PreviewOutput]) {
      for output in outputs {
        switch output {
        case .command: break
        case .preview(let message): messages.append(message)
        case .scheduleOneShotEnd(let id): pendingOneShots.append(id)
        case .cancelRoll(let trackId):
          let (next, effects) = RollEditLogic.cancel(machines[trackId]!)
          machines[trackId] = next
          let more = book.perform(effects, trackId: trackId)
          if !more.isEmpty { cancelledStopsThatSent.append("track \(trackId): \(more)") }
          route(more)
        }
      }
    }

    mutating func roll(_ event: RollPointerEvent, _ trackId: Int) {
      if event.phase == .down { route(book.rollPressed(trackId: trackId)) }
      let (next, effects) = RollEditLogic.handle(event, state: machines[trackId]!, context: contexts[trackId]!)
      machines[trackId] = next
      route(book.perform(effects, trackId: trackId))
    }

    /// The machine believes it holds a pitch exactly when the book holds for that track.
    func consistent() -> Bool {
      machines.allSatisfy { trackId, state in (state.press?.heldPitch != nil) == (book.held?.owner == .roll(trackId: trackId)) }
    }
  }

  private func context(trackId: Int, noteId: Int, pitch: Int) -> RollContext {
    var event = EventJSON(id: 1, root: 0, type: "maj", symbol: "C", roman: "I", family: "Major", startBeat: 0,
                          lengthBeats: 16, velocity: 100, voicing: [60], voicingNames: nil, outOfKey: nil,
                          outOfKeyNames: nil, function: nil, tension: nil, inversion: nil, octave: nil,
                          manualVoicing: nil, why: nil)
    event.playStart = 0
    event.playEnd = 16
    let notes = TrackNotesJSON(id: [noteId], pitch: [pitch], start: [0], length: [960], velocity: [100], anchor: [1],
                               offset: [0], active: [true])
    return RollContext(trackId: trackId, notes: notes, selectedIds: [], revision: 5, events: [event],
                       timing: RollTiming(ticksPerBeat: 960, barTicks: 3840, endTick: 15_360, minNoteTicks: 30),
                       geometry: RollGeometry(pxPerBeat: 48, rowHeight: 14, ticksPerBeat: 960))
  }

  func testRandomSequencesWithTheRealMachineStayPairedAndConsistent() {
    let geometry = RollGeometry(pxPerBeat: 48, rowHeight: 14, ticksPerBeat: 960)
    let pitches = [1: 60, 2: 64]
    func onNote(_ trackId: Int) -> CGPoint { CGPoint(x: geometry.x(tick: 480), y: geometry.y(pitch: pitches[trackId]!) + 7) }
    func onEmpty() -> CGPoint { CGPoint(x: geometry.x(tick: 7000), y: geometry.y(pitch: 90) + 7) }
    var generator = SystemRandomNumberGenerator()
    for round in 0..<300 {
      var rig = Rig(contexts: [1: context(trackId: 1, noteId: 10, pitch: 60), 2: context(trackId: 2, noteId: 20, pitch: 64)])
      for _ in 0..<80 {
        let t = Int.random(in: 1...2, using: &generator)
        switch Int.random(in: 0..<12, using: &generator) {
        case 0, 1:
          let p = onNote(t)
          rig.pointer[t] = p
          rig.roll(RollPointerEvent(phase: .down, location: p), t)
        case 2:
          let p = onEmpty()
          rig.pointer[t] = p
          rig.roll(RollPointerEvent(phase: .down, location: p), t)
        case 3, 4:
          guard let at = rig.pointer[t] else { continue }
          let p = CGPoint(x: at.x + CGFloat(Int.random(in: -20...20, using: &generator)),
                          y: at.y + 14 * CGFloat(Int.random(in: -2...2, using: &generator)))
          rig.pointer[t] = p
          rig.roll(RollPointerEvent(phase: .drag, location: p), t)
        case 5:
          rig.roll(RollPointerEvent(phase: .up, location: rig.pointer[t] ?? onEmpty()), t)
        case 6:
          rig.roll(RollPointerEvent(phase: .cancel, location: .zero), t)   // resign key, view gone, lane switch
        case 7:
          let (next, effects) = RollEditLogic.snapshotChanged(revision: 6 + UInt64(round), state: rig.machines[t]!)
          rig.machines[t] = next
          rig.route(rig.book.perform(effects, trackId: t))
        case 8:
          let held = rig.book.hold(.key(pitch: 40 + t), pitches: [40 + t], velocity: 100)
          rig.keyIds.append(held.id)
          rig.route(held.outputs)
        case 9:
          if let id = rig.keyIds.randomElement(using: &generator) { rig.route(rig.book.endHold(id: id)) }
        case 10:
          rig.route(Bool.random(using: &generator) ? rig.book.hold(.cell("c"), pitches: [60], velocity: 100).outputs
                                                    : rig.book.endHold())   // a cell press; or keep-alive / panic / close
        default:
          if let id = rig.pendingOneShots.randomElement(using: &generator) {
            rig.pendingOneShots.removeAll { $0 == id }
            rig.route(rig.book.oneShotEnded(id: id))
          }
        }
        XCTAssertTrue(rig.consistent(), "a machine and the book disagree about the held pitch (round \(round))")
        if !rig.consistent() { return }
      }
      rig.route(rig.book.endHold())
      for t in [1, 2] { rig.roll(RollPointerEvent(phase: .cancel, location: .zero), t) }
      for id in rig.pendingOneShots { rig.route(rig.book.oneShotEnded(id: id)) }
      assertPaired(rig.messages)
      XCTAssertEqual(rig.cancelledStopsThatSent, [], "a cancelled gesture's stop never sends a second Stop")
      XCTAssertNil(rig.book.held)
      XCTAssertTrue(rig.book.oneShots.isEmpty)
    }
  }
}
