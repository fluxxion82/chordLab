import XCTest
@testable import ChordLabUICore

/// The clipboard's payload and text (UI/Core/ClipboardChords.swift, strip editing design D5).
final class ClipboardChordsTests: XCTestCase {
  /// A snapshot event with the fields the clipboard reads; the rest are placeholders.
  private func event(_ id: Int, _ root: Int, _ type: String, _ symbol: String, length: Double = 4,
                     velocity: Int = 100, inversion: Int? = 0, octave: Int? = 0, manual: Bool? = false) -> EventJSON {
    EventJSON(id: id, root: root, type: type, symbol: symbol, roman: "I", family: "Major", startBeat: 0,
              lengthBeats: length, velocity: velocity, voicing: [60], voicingNames: nil, outOfKey: nil,
              outOfKeyNames: nil, function: nil, tension: nil, inversion: inversion, octave: octave,
              manualVoicing: manual, why: nil)
  }

  private var events: [EventJSON] {
    [event(1, 0, "maj", "C"), event(2, 9, "min", "Am", length: 2.5, inversion: 1, manual: true),
     event(3, 10, "maj", "Bb", length: 0.5, velocity: 80, octave: -1, manual: true), event(4, 7, "7", "G7")]
  }

  func testChordsComeInStripOrderWithEveryField() {
    XCTAssertEqual(ClipboardChords.chords(of: events, ids: [3, 2]), [
      ClipChord(root: 9, type: "min", lengthBeats: 2.5, velocity: 100, inversion: 1, octave: 0, manualVoicing: true),
      ClipChord(root: 10, type: "maj", lengthBeats: 0.5, velocity: 80, inversion: 0, octave: -1, manualVoicing: true),
    ])
    XCTAssertEqual(ClipboardChords.chords(of: events, ids: [99]), [])
  }

  func testTheCustomTypeRoundTrips() throws {
    let chords = ClipboardChords.chords(of: events, ids: [1, 2, 3, 4])
    let data = try XCTUnwrap(ClipboardChords.encode(chords))
    XCTAssertEqual(ClipboardChords.decode(data), chords)
    let object = try XCTUnwrap(JSONSerialization.jsonObject(with: data) as? [String: Any])
    XCTAssertEqual(object["schema"] as? Int, 1)
    XCTAssertEqual((object["chords"] as? [Any])?.count, 4)
    XCTAssertNil(ClipboardChords.encode([]))
  }

  func testAnEventWithoutVoicingFieldsCopiesWithoutThem() throws {
    let older = [event(5, 2, "min", "Dm", length: 1.5, inversion: nil, octave: nil, manual: nil)]
    let chords = ClipboardChords.chords(of: older, ids: [5])
    XCTAssertEqual(chords, [ClipChord(root: 2, type: "min", lengthBeats: 1.5, velocity: 100)])
    let data = try XCTUnwrap(ClipboardChords.encode(chords))
    XCTAssertEqual(String(data: data, encoding: .utf8)?.contains("inversion"), false)
    XCTAssertEqual(ClipboardChords.decode(data), chords)
  }

  func testDecodeRefusesAnythingButChordLabsPayload() {
    let entry = #"{"root":0,"type":"maj","lengthBeats":4,"velocity":100}"#
    XCTAssertNotNil(ClipboardChords.decode(Data(#"{"schema":1,"chords":[\#(entry)]}"#.utf8)))
    XCTAssertNil(ClipboardChords.decode(Data(#"{"schema":2,"chords":[\#(entry)]}"#.utf8)), "another schema")
    XCTAssertNil(ClipboardChords.decode(Data(#"{"chords":[\#(entry)]}"#.utf8)), "no schema")
    XCTAssertNil(ClipboardChords.decode(Data(#"{"schema":1,"chords":[]}"#.utf8)), "no chords")
    XCTAssertNil(ClipboardChords.decode(Data(#"{"schema":1,"chords":[{"root":0,"lengthBeats":4,"velocity":100}]}"#.utf8)),
                 "no type")
    XCTAssertNil(ClipboardChords.decode(Data(#"{"schema":1,"chords":[{"root":"0","type":"maj","lengthBeats":4,"velocity":100}]}"#.utf8)),
                 "a mistyped root")
    XCTAssertNil(ClipboardChords.decode(Data("C – Am".utf8)), "plain text")
  }

  func testTheSymbolLineUsesTheStripsGlyphsInStripOrder() {
    XCTAssertEqual(ClipboardChords.symbolLine(of: events, ids: [4, 1, 3, 2]), "C – Am – B♭ – G7")
    XCTAssertEqual(ClipboardChords.symbolLine(of: events, ids: [2]), "Am")
  }

  func testTheInsertCommandCarriesEachEntryAsTheEngineTakesIt() throws {
    let chords = [ClipChord(root: 9, type: "min", lengthBeats: 2.5, velocity: 100, inversion: 1, octave: 0, manualVoicing: true),
                  ClipChord(root: 2, type: "min", lengthBeats: 1.5, velocity: 90)]
    let data = try JSONSerialization.data(withJSONObject: ClipboardChords.insertCommand(chords, at: 3), options: [.sortedKeys])
    XCTAssertEqual(String(data: data, encoding: .utf8),
                   #"{"chords":[{"inversion":1,"lengthBeats":2.5,"manualVoicing":true,"octave":0,"root":9,"type":"min","velocity":100},{"lengthBeats":1.5,"root":2,"type":"min","velocity":90}],"cmd":"insertChords","index":3}"#)
  }

  func testTheRemoveCommand() throws {
    let data = try JSONSerialization.data(withJSONObject: ClipboardChords.removeCommand([3, 1]), options: [.sortedKeys])
    XCTAssertEqual(String(data: data, encoding: .utf8), #"{"cmd":"removeChords","ids":[3,1]}"#)
  }

  // MARK: - Through a pasteboard

  func testACopyClearsAndWritesOneItemWithBothForms() {
    let board = FakePasteboard()
    XCTAssertTrue(ClipboardChords.copy([2, 1], from: events, to: board))
    XCTAssertEqual(board.cleared, 1)
    XCTAssertEqual(board.items.count, 1, "both forms on one item: published together or not at all")
    XCTAssertEqual(board.items.first?.text, "C – Am")
    XCTAssertEqual(ClipboardChords.paste(from: board), ClipboardChords.chords(of: events, ids: [1, 2]))
  }

  func testACopyOfNothingLeavesThePasteboardAlone() {
    let board = FakePasteboard()
    XCTAssertFalse(ClipboardChords.copy([99], from: events, to: board))
    XCTAssertEqual(board.cleared, 0)
    XCTAssertTrue(board.items.isEmpty)
  }

  func testACutRemovesOnlyAfterAPublishedCopy() throws {
    let failing = FakePasteboard()
    failing.accepts = false
    XCTAssertFalse(ClipboardChords.copy([1, 3], from: events, to: failing))
    XCTAssertNil(ClipboardChords.cut([1, 3], from: events, to: failing), "the write failed: no removal is sent")
    let board = FakePasteboard()
    let command = try XCTUnwrap(ClipboardChords.cut([1, 3], from: events, to: board))
    let data = try JSONSerialization.data(withJSONObject: command, options: [.sortedKeys])
    XCTAssertEqual(String(data: data, encoding: .utf8), #"{"cmd":"removeChords","ids":[1,3]}"#)
    XCTAssertEqual(ClipboardChords.paste(from: board), ClipboardChords.chords(of: events, ids: [1, 3]))
    XCTAssertNil(ClipboardChords.cut([99], from: events, to: board), "nothing to copy: nothing removed")
  }

  func testPasteReadsOnlyChordLabsChords() {
    let board = FakePasteboard()
    XCTAssertNil(ClipboardChords.paste(from: board), "an empty pasteboard")
    board.raw = Data("C – Am".utf8)
    XCTAssertNil(ClipboardChords.paste(from: board), "text under our type is not a payload")
  }
}

/// A pasteboard that keeps what it is given; `accepts` false makes every write fail, as a refused
/// `writeObjects` would. `raw` stands in for bytes another app left under ChordLab's type.
private final class FakePasteboard: ChordPasteboard {
  var accepts = true
  var raw: Data?
  private(set) var cleared = 0
  private(set) var items: [ClipboardItem] = []

  func clear() {
    cleared += 1
    items = []
    raw = nil
  }

  func write(_ item: ClipboardItem) -> Bool {
    guard accepts else { return false }
    items.append(item)
    return true
  }

  func data(forType type: String) -> Data? {
    guard type == ClipboardChords.pasteboardType else { return nil }
    return raw ?? items.last?.chords
  }
}
