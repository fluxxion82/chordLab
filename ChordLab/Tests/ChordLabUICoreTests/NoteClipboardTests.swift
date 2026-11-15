import XCTest
@testable import ChordLabUICore

/// The note clipboard (UI/Core/NoteClipboard.swift, tracks design D15 "Keyboard").
final class NoteClipboardTests: XCTestCase {
  private let limits = RollLimits(minNoteTicks: 30, endTick: 15_360)

  /// Four notes in note order (start, pitch, id); 13 is inactive.
  private let notes = TrackNotesJSON(id: [10, 11, 12, 13], pitch: [60, 64, 67, 45], start: [0, 960, 1920, 2160],
                                     length: [480, 480, 960, 240], velocity: [100, 90, 80, 70], anchor: [1, 1, 1, 1],
                                     offset: [0, 960, 1920, 2160], active: [true, true, true, false])

  private func json(_ command: [String: Any]) throws -> String {
    String(decoding: try JSONSerialization.data(withJSONObject: command, options: [.sortedKeys]), as: UTF8.self)
  }

  // MARK: - Payload

  func testCopiedNotesAreSortedByStartAndRelativeToTheFirst() {
    XCTAssertEqual(NoteClipboard.clipNotes(of: notes, ids: [12, 11]), [
      ClipNote(pitch: 64, start: 0, length: 480, velocity: 90),
      ClipNote(pitch: 67, start: 960, length: 960, velocity: 80),
    ])
    XCTAssertEqual(NoteClipboard.clipNotes(of: notes, ids: [13, 10]), [
      ClipNote(pitch: 60, start: 0, length: 480, velocity: 100),
      ClipNote(pitch: 45, start: 2160, length: 240, velocity: 70),
    ], "inactive notes are copied like any other")
    XCTAssertEqual(NoteClipboard.clipNotes(of: notes, ids: [99]), [])
  }

  func testThePayloadRoundTrips() throws {
    let clip = NoteClipboard.clipNotes(of: notes, ids: [10, 11, 12, 13])
    let data = try XCTUnwrap(NoteClipboard.encode(clip))
    XCTAssertEqual(NoteClipboard.decode(data), clip)
    XCTAssertEqual(String(decoding: data, as: UTF8.self),
                   #"{"notes":[{"length":480,"pitch":60,"start":0,"velocity":100},{"length":480,"pitch":64,"start":960,"velocity":90},{"length":960,"pitch":67,"start":1920,"velocity":80},{"length":240,"pitch":45,"start":2160,"velocity":70}],"schema":1}"#)
    XCTAssertNil(NoteClipboard.encode([]))
    XCTAssertEqual(NoteClipboard.pasteboardType, "com.SterlingAlbury.chordlab.notes")
  }

  func testDecodeRefusesAnythingButAValidPayload() {
    func decode(_ text: String) -> [ClipNote]? { NoteClipboard.decode(Data(text.utf8)) }
    let entry = #"{"pitch":60,"start":0,"length":480,"velocity":100}"#
    XCTAssertNotNil(decode(#"{"schema":1,"notes":[\#(entry)]}"#))
    XCTAssertNil(decode(#"{"schema":2,"notes":[\#(entry)]}"#), "another schema")
    XCTAssertNil(decode(#"{"notes":[\#(entry)]}"#), "no schema")
    XCTAssertNil(decode(#"{"schema":1,"notes":[]}"#), "no notes")
    XCTAssertNil(decode(#"{"schema":1,"notes":[{"pitch":60,"start":-1,"length":480,"velocity":100}]}"#), "a negative start")
    XCTAssertNil(decode(#"{"schema":1,"notes":[{"pitch":128,"start":0,"length":480,"velocity":100}]}"#), "pitch 128")
    XCTAssertNil(decode(#"{"schema":1,"notes":[{"pitch":-1,"start":0,"length":480,"velocity":100}]}"#), "pitch -1")
    XCTAssertNil(decode(#"{"schema":1,"notes":[{"pitch":60,"start":0,"length":480,"velocity":0}]}"#), "velocity 0")
    XCTAssertNil(decode(#"{"schema":1,"notes":[{"pitch":60,"start":0,"length":480,"velocity":128}]}"#), "velocity 128")
    XCTAssertNil(decode(#"{"schema":1,"notes":[{"pitch":60,"start":0,"length":29,"velocity":100}]}"#), "shorter than 30 ticks")
    XCTAssertNil(decode(#"{"schema":1,"notes":[{"pitch":60,"start":0,"length":15728641,"velocity":100}]}"#), "longer than the domain")
    XCTAssertNil(decode(#"{"schema":1,"notes":[{"pitch":60,"start":15728611,"length":30,"velocity":100}]}"#),
                 "a start past the domain")
    XCTAssertNil(decode(#"{"schema":1,"notes":[{"pitch":"60","start":0,"length":480,"velocity":100}]}"#), "a mistyped pitch")
    XCTAssertNil(decode(#"{"schema":1,"notes":[{"pitch":60,"start":0,"length":480}]}"#), "no velocity")
    XCTAssertNil(decode("C4 E4"), "plain text")
  }

  // MARK: - Text

  func testTheTextLineNamesTheNotesInNoteOrder() {
    XCTAssertEqual(NoteClipboard.textLine(of: notes, ids: [12, 10, 13]), "C4 G4 A2")
    XCTAssertEqual(NoteClipboard.noteName(pitch: 60), "C4")
    XCTAssertEqual(NoteClipboard.noteName(pitch: 61), "C#4")
    XCTAssertEqual(NoteClipboard.noteName(pitch: 0), "C-1")
    XCTAssertEqual(NoteClipboard.noteName(pitch: 127), "G9")
    XCTAssertEqual(NoteClipboard.noteName(pitch: 46), "A#2")
  }

  // MARK: - Through a pasteboard

  func testACopyClearsAndWritesOneItemWithBothForms() throws {
    let board = FakeNotePasteboard()
    XCTAssertTrue(NoteClipboard.copy([11, 10], from: notes, to: board))
    XCTAssertEqual(board.cleared, 1)
    XCTAssertEqual(board.items.count, 1, "both forms on one item: published together or not at all")
    let item = try XCTUnwrap(board.items.first)
    XCTAssertEqual(item.type, NoteClipboard.pasteboardType)
    XCTAssertEqual(item.text, "C4 E4")
    XCTAssertEqual(NoteClipboard.paste(from: board), NoteClipboard.clipNotes(of: notes, ids: [10, 11]))
    XCTAssertNil(ClipboardChords.paste(from: board), "notes are not chords")
  }

  func testACopyOfNothingLeavesThePasteboardAlone() {
    let board = FakeNotePasteboard()
    XCTAssertFalse(NoteClipboard.copy([99], from: notes, to: board))
    XCTAssertEqual(board.cleared, 0)
    XCTAssertTrue(board.items.isEmpty)
  }

  func testACutRemovesOnlyAfterAPublishedCopy() throws {
    let failing = FakeNotePasteboard()
    failing.accepts = false
    XCTAssertNil(NoteClipboard.cut([12, 10], trackId: 7, from: notes, baseRevision: 5, to: failing),
                 "the write failed: no removal is sent")
    let board = FakeNotePasteboard()
    let command = try XCTUnwrap(NoteClipboard.cut([12, 10], trackId: 7, from: notes, baseRevision: 5, to: board))
    XCTAssertEqual(command, .removeNotes(trackId: 7, ids: [10, 12], baseRevision: 5))
    XCTAssertEqual(NoteClipboard.paste(from: board), NoteClipboard.clipNotes(of: notes, ids: [10, 12]))
    XCTAssertNil(NoteClipboard.cut([99], trackId: 7, from: notes, baseRevision: 5, to: board), "nothing to copy: nothing removed")
  }

  func testPasteReadsOnlyChordLabsNotes() {
    let board = FakeNotePasteboard()
    XCTAssertNil(NoteClipboard.paste(from: board), "an empty pasteboard")
    board.raw[NoteClipboard.pasteboardType] = Data("C4 E4".utf8)
    XCTAssertNil(NoteClipboard.paste(from: board), "text under our type is not a payload")
    let chords = FakeNotePasteboard()
    let event = EventJSON(id: 1, root: 0, type: "maj", symbol: "C", roman: "I", family: "Major", startBeat: 0, lengthBeats: 4,
                          velocity: 100, voicing: [60], voicingNames: nil, outOfKey: nil, outOfKeyNames: nil, function: nil,
                          tension: nil, inversion: nil, octave: nil, manualVoicing: nil, why: nil)
    XCTAssertTrue(ClipboardChords.copy([1], from: [event], to: chords))
    XCTAssertEqual(chords.items.first?.type, ClipboardChords.pasteboardType)
    XCTAssertNil(NoteClipboard.paste(from: chords), "chords are not notes")
  }

  // MARK: - Paste placement

  func testPasteLandsAtTheMarkerElseAfterTheSelectionElseAtZero() {
    XCTAssertEqual(NoteClipboard.pastePosition(marker: 3840, notes: notes, selectedIds: [10]), 3840)
    XCTAssertEqual(NoteClipboard.pastePosition(marker: nil, notes: notes, selectedIds: [10, 13]), 2400,
                   "the selection's end: the latest end of the selected notes")
    XCTAssertEqual(NoteClipboard.pastePosition(marker: nil, notes: notes, selectedIds: [12, 13]), 2880)
    XCTAssertEqual(NoteClipboard.pastePosition(marker: nil, notes: notes, selectedIds: []), 0)
    XCTAssertEqual(NoteClipboard.pastePosition(marker: nil, notes: notes, selectedIds: [99]), 0, "a stale selection")
  }

  func testThePasteCommandInsertsAtTheBaseWithoutABaseRevision() throws {
    let clip = [ClipNote(pitch: 64, start: 0, length: 480, velocity: 90), ClipNote(pitch: 67, start: 960, length: 960, velocity: 80)]
    let command = try XCTUnwrap(NoteClipboard.pasteCommand(clip, trackId: 7, at: 3840, limits: limits))
    XCTAssertEqual(command, .insertNotes(trackId: 7, notes: [RollNoteSpec(pitch: 64, start: 3840, length: 480, velocity: 90),
                                                              RollNoteSpec(pitch: 67, start: 4800, length: 960, velocity: 80)],
                                         baseRevision: nil))
    XCTAssertEqual(try json(command.json),
                   #"{"cmd":"insertNotes","notes":[{"length":480,"pitch":64,"start":3840,"velocity":90},{"length":960,"pitch":67,"start":4800,"velocity":80}],"trackId":7}"#)
    XCTAssertNil(NoteClipboard.pasteCommand([], trackId: 7, at: 0, limits: limits))
  }

  func testPastedLengthsAreClampedIntoTheDomain() throws {
    let clip = [ClipNote(pitch: 60, start: 0, length: 9600, velocity: 100)]
    let base = RollLimits.engineMaxNoteTick - 960
    let command = try XCTUnwrap(NoteClipboard.pasteCommand(clip, trackId: 7, at: base, limits: limits))
    XCTAssertEqual(command, .insertNotes(trackId: 7, notes: [RollNoteSpec(pitch: 60, start: base, length: 960, velocity: 100)],
                                         baseRevision: nil))
  }
}

/// A pasteboard that keeps what it is given; `accepts` false makes every write fail, as a refused `writeObjects`
/// would. `raw` stands in for bytes another app left under a type.
private final class FakeNotePasteboard: ChordPasteboard {
  var accepts = true
  var raw: [String: Data] = [:]
  private(set) var cleared = 0
  private(set) var items: [ClipboardItem] = []

  func clear() {
    cleared += 1
    items = []
    raw = [:]
  }

  func write(_ item: ClipboardItem) -> Bool {
    guard accepts else { return false }
    items.append(item)
    return true
  }

  func data(forType type: String) -> Data? {
    raw[type] ?? items.last { $0.type == type }?.data
  }
}
