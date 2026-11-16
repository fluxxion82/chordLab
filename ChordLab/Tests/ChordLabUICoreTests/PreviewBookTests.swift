import XCTest
@testable import ChordLabUICore

/// The one held preview and the short ones (UI/Core/PreviewBook.swift, tracks design D15 "Preview", plan Task 19): ids,
/// Start/Stop paired by id whoever holds (a palette cell, the roll, a keyboard key), the roll's effects turned into
/// messages, how a hold must end before another owner holds, and the wire form (`kMsgTagPreview`).
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

  private func previews(_ outputs: [RollSeamOutput]) -> [PreviewMessage] {
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

  // MARK: - Holds

  func testAHoldTakesTheNextIdAndItsEndStopsThatId() {
    var book = PreviewBook()
    XCTAssertEqual(book.startHold(.cell("c"), pitches: [60, 64], velocity: 100), [.start(id: 1, pitches: [60, 64], velocity: 100)])
    XCTAssertEqual(book.held, PreviewHold(id: 1, owner: .cell("c")))
    XCTAssertEqual(book.heldCellId, "c")
    XCTAssertEqual(book.endHold(), [.stop(id: 1)])
    XCTAssertNil(book.held)
    XCTAssertEqual(book.endHold(), [], "nothing held, nothing stopped")
    XCTAssertEqual(book.startHold(.key(pitch: 48), pitches: [48], velocity: 100), [.start(id: 2, pitches: [48], velocity: 100)])
    XCTAssertNil(book.heldCellId)
  }

  func testAHoldOverAHoldStopsTheOldOneFirst() {
    var book = PreviewBook()
    var messages = book.startHold(.cell("a"), pitches: [60], velocity: 100)
    messages += book.startHold(.key(pitch: 50), pitches: [50], velocity: 100)
    XCTAssertEqual(Array(messages.suffix(2)), [.stop(id: 1), .start(id: 2, pitches: [50], velocity: 100)])
    messages += book.endHold()
    assertPaired(messages)
  }

  func testAnOwnersEndStopsOnlyItsOwnHold() {
    var book = PreviewBook()
    _ = book.startHold(.key(pitch: 50), pitches: [50], velocity: 100)
    XCTAssertEqual(book.endHold(if: { if case .cell = $0 { return true } else { return false } }), [])
    XCTAssertEqual(book.held?.id, 1)
    XCTAssertEqual(book.endHold(if: { if case .key = $0 { return true } else { return false } }), [.stop(id: 1)])
  }

  // MARK: - Ending a hold before another owner holds

  func testHowTheHoldEndsBeforeANewOne() {
    var book = PreviewBook()
    XCTAssertEqual(book.release(before: .cell("x")), .none, "nothing held")
    _ = book.startHold(.cell("a"), pitches: [60], velocity: 100)
    XCTAssertEqual(book.release(before: .cell("b")), .stop)
    XCTAssertEqual(book.release(before: .roll(trackId: 1)), .stop)
    XCTAssertEqual(book.release(before: nil), .stop, "the editor closing, a lost release, panic")
    _ = book.startHold(.roll(trackId: 2), pitches: [45], velocity: 90)
    // A roll hold belongs to that track's gesture machine: it is ended by cancelling the gesture (whose stop comes back
    // through the seam), never stopped behind the machine's back.
    XCTAssertEqual(book.release(before: .cell("a")), .cancelRoll(trackId: 2))
    XCTAssertEqual(book.release(before: .key(pitch: 40)), .cancelRoll(trackId: 2))
    XCTAssertEqual(book.release(before: .roll(trackId: 1)), .cancelRoll(trackId: 2))
    XCTAssertEqual(book.release(before: nil), .cancelRoll(trackId: 2))
    // The same track's own next start (the machine stops before it starts, so this is only a guard).
    XCTAssertEqual(book.release(before: .roll(trackId: 2)), .stop)
  }

  // MARK: - The roll's effects (the seam)

  func testAPressMoveAndUpBecomeCommandsAndPairedPreviews() {
    var book = PreviewBook()
    var out = book.perform([.send(select), .previewStart(pitch: 45, velocity: 90)], trackId: 1)
    XCTAssertEqual(out, [.command(select), .preview(.start(id: 1, pitches: [45], velocity: 90))])
    XCTAssertEqual(book.held, PreviewHold(id: 1, owner: .roll(trackId: 1)))
    // A vertical move while held: the old pitch stops, the new one starts under a new id.
    out += book.perform([.previewStop, .previewStart(pitch: 46, velocity: 90)], trackId: 1)
    out += book.perform([.previewStop, .send(update)], trackId: 1)
    XCTAssertEqual(Array(out.suffix(4)), [.preview(.stop(id: 1)), .preview(.start(id: 2, pitches: [46], velocity: 90)),
                                          .preview(.stop(id: 2)), .command(update)])
    XCTAssertNil(book.held)
    assertPaired(previews(out))
  }

  func testAStopWithNothingHeldOrAnotherOwnersHoldSendsNothing() {
    var book = PreviewBook()
    XCTAssertEqual(book.perform([.previewStop], trackId: 1), [], "a second stop is never sent")
    _ = book.startHold(.cell("a"), pitches: [60], velocity: 100)
    XCTAssertEqual(book.perform([.previewStop], trackId: 1), [], "the palette's hold is not the roll's")
    _ = book.startHold(.roll(trackId: 2), pitches: [40], velocity: 100)
    XCTAssertEqual(book.perform([.previewStop], trackId: 1), [], "another track's roll hold is not this one's")
    XCTAssertEqual(book.perform([.previewStop], trackId: 2), [.preview(.stop(id: 2))], "the cell (1) was stopped when track 2 took the hold")
  }

  func testAOneShotStartsAtOnceAndStopsOnceWhenItEnds() {
    var book = PreviewBook()
    let insert = RollCommand.insertNotes(trackId: 1, notes: [RollNoteSpec(pitch: 48, start: 4800, length: 480, velocity: 100)],
                                         baseRevision: 15)
    var out = book.perform([.send(insert), .previewOneShot(pitch: 48, velocity: 100)], trackId: 1)
    XCTAssertEqual(out, [.command(insert), .preview(.start(id: 1, pitches: [48], velocity: 100)), .scheduleOneShotEnd(id: 1)])
    XCTAssertNil(book.held, "a one-shot is not the held preview")
    XCTAssertEqual(book.oneShots, [1])
    // A hold may start while the one-shot still sounds; each keeps its own id.
    out += book.perform([.previewStart(pitch: 50, velocity: 80)], trackId: 1)
    var messages = previews(out)
    messages += book.oneShotEnded(id: 1)
    XCTAssertEqual(messages.last, .stop(id: 1))
    XCTAssertEqual(book.oneShotEnded(id: 1), [], "its end is sent once")
    XCTAssertEqual(book.oneShotEnded(id: 99), [], "an id that is no one-shot")
    messages += book.endHold()
    assertPaired(messages)
  }

  func testAPanicClearsNothingItDidNotStop() {
    // The caller ends the hold properly before a panic (a roll hold through its gesture's cancel); the one-shots keep
    // their pending stop, so every Start still gets exactly one Stop.
    var book = PreviewBook()
    var messages = previews(book.perform([.previewOneShot(pitch: 48, velocity: 100)], trackId: 1))
    messages += book.startHold(.key(pitch: 40), pitches: [40], velocity: 100)
    XCTAssertEqual(book.release(before: nil), .stop)
    messages += book.endHold()
    messages += book.oneShotEnded(id: 1)
    assertPaired(messages)
  }

  /// A long random walk over every entry point: Start/Stop stay paired by id and the book never holds two.
  func testRandomSequencesStayPaired() {
    var generator = SystemRandomNumberGenerator()
    for _ in 0..<200 {
      var book = PreviewBook()
      var messages: [PreviewMessage] = []
      var machineHolds: [Int: Bool] = [1: false, 2: false]   // what each track's machine believes
      for _ in 0..<60 {
        let track = Int.random(in: 1...2, using: &generator)
        switch Int.random(in: 0..<7, using: &generator) {
        case 0:
          // The machine's down on a note: it stops its own hold first, then starts (its contract).
          if machineHolds[track] == true { messages += previews(book.perform([.previewStop], trackId: track)) }
          // A different owner's hold ends the way `release(before:)` says.
          if case .cancelRoll(let other) = book.release(before: .roll(trackId: track)) {
            messages += previews(book.perform([.previewStop], trackId: other))
            machineHolds[other] = false
          }
          messages += previews(book.perform([.previewStart(pitch: 40 + track, velocity: 100)], trackId: track))
          machineHolds[track] = true
        case 1:
          if machineHolds[track] == true {
            messages += previews(book.perform([.previewStop, .previewStart(pitch: 60, velocity: 90)], trackId: track))
          }
        case 2:
          if machineHolds[track] == true { messages += previews(book.perform([.previewStop], trackId: track)) }
          machineHolds[track] = false
        case 3:
          let outputs = book.perform([.previewOneShot(pitch: 50, velocity: 100)], trackId: track)
          messages += previews(outputs)
        case 4:
          for id in book.oneShots.shuffled(using: &generator).prefix(1) { messages += book.oneShotEnded(id: id) }
        case 5:
          switch book.release(before: .cell("c")) {
          case .cancelRoll(let other):
            messages += previews(book.perform([.previewStop], trackId: other))
            machineHolds[other] = false
          case .stop, .none:
            break
          }
          messages += book.startHold(.cell("c"), pitches: [60], velocity: 100)
        default:
          if case .cancelRoll(let other) = book.release(before: nil) {
            messages += previews(book.perform([.previewStop], trackId: other))
            machineHolds[other] = false
          } else {
            messages += book.endHold()
          }
        }
      }
      if case .cancelRoll(let other) = book.release(before: nil) {
        messages += previews(book.perform([.previewStop], trackId: other))
      } else {
        messages += book.endHold()
      }
      for id in book.oneShots { messages += book.oneShotEnded(id: id) }
      assertPaired(messages)
      XCTAssertNil(book.held)
      XCTAssertTrue(book.oneShots.isEmpty)
    }
  }

  // MARK: - The keyboard column

  func testTheKeyUnderAKeyboardPoint() {
    let geometry = RollGeometry(pxPerBeat: 48, rowHeight: 14, ticksPerBeat: 960)
    // Row 0 (pitch 127) spans content y 0..<14; at a scroll of 1026 the view's top is content y 1026 (row 73, pitch 54).
    XCTAssertEqual(PianoKeys.pitch(atViewY: 0, geometry: geometry, scrollY: 0), 127)
    XCTAssertEqual(PianoKeys.pitch(atViewY: 13.9, geometry: geometry, scrollY: 0), 127)
    XCTAssertEqual(PianoKeys.pitch(atViewY: 14, geometry: geometry, scrollY: 0), 126)
    XCTAssertEqual(PianoKeys.pitch(atViewY: 5, geometry: geometry, scrollY: 1026), 54)
    XCTAssertEqual(PianoKeys.pitch(atViewY: 127 * 14 + 13, geometry: geometry, scrollY: 0), 0)
    XCTAssertNil(PianoKeys.pitch(atViewY: 128 * 14, geometry: geometry, scrollY: 0), "below pitch 0's row")
    XCTAssertNil(PianoKeys.pitch(atViewY: -1, geometry: geometry, scrollY: 0), "above pitch 127's row")
    XCTAssertNil(PianoKeys.pitch(atViewY: .nan, geometry: geometry, scrollY: 0))
    XCTAssertEqual(PianoKeys.velocity, 100)
  }
}
