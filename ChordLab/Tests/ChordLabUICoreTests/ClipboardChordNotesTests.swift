import XCTest
@testable import ChordLabUICore

/// The chord clipboard and ⌘D carrying the chords' notes (UI/Core/ClipboardChords.swift, tracks design D21, plan
/// Task 20): each copied chord entry carries the notes anchored to it with their lane `{trackId, name, role}`, the
/// payload carries the copying instance's token, and a paste from another instance drops every `trackId` so the engine
/// matches lanes by name and role or creates tracks.
final class ClipboardChordNotesTests: XCTestCase {
  private func event(_ id: Int, _ root: Int, _ type: String, _ symbol: String, length: Double = 4) -> EventJSON {
    EventJSON(id: id, root: root, type: type, symbol: symbol, roman: "I", family: "Major", startBeat: 0,
              lengthBeats: length, velocity: 100, voicing: [60], voicingNames: nil, outOfKey: nil,
              outOfKeyNames: nil, function: nil, tension: nil, inversion: nil, octave: nil,
              manualVoicing: nil, why: nil)
  }

  private var events: [EventJSON] {
    [event(1, 0, "maj", "C"), event(2, 9, "min", "Am"), event(3, 5, "maj", "F", length: 1)]
  }

  /// Bass (1): notes 10 (chord 1), 11 (chord 2, velocity 100), 12 (chord 3, inactive: offset past the 1-beat chord);
  /// Lead (2): notes 20 (chord 1, velocity 127) and 21 (chord 2).
  private var tracks: [TrackJSON] {
    [TrackJSON(id: 1, name: "Bass", role: .bass, mute: false, solo: false, audible: true,
               notes: TrackNotesJSON(id: [10, 11, 12], pitch: [36, 45, 41], start: [0, 3840, 9120],
                                     length: [960, 1920, 480], velocity: [90, 100, 80], anchor: [1, 2, 3],
                                     offset: [0, 0, 1440], active: [true, true, false])),
     TrackJSON(id: 2, name: "Lead", role: .lead, mute: false, solo: false, audible: true,
               notes: TrackNotesJSON(id: [20, 21], pitch: [72, 76], start: [960, 4320], length: [480, 240],
                                     velocity: [127, 100], anchor: [1, 2], offset: [960, 480], active: [true, true]))]
  }

  private let bass = ClipLane(trackId: 1, name: "Bass", role: .bass)
  private let lead = ClipLane(trackId: 2, name: "Lead", role: .lead)

  private func json(_ object: Any) throws -> String {
    String(decoding: try JSONSerialization.data(withJSONObject: object, options: [.sortedKeys]), as: UTF8.self)
  }

  // MARK: - Copying

  func testEachChordCarriesTheNotesAnchoredToItInTrackThenNoteOrder() {
    let chords = ClipboardChords.chords(of: events, ids: [2, 1], tracks: tracks)
    XCTAssertEqual(chords.map(\.notes), [
      [ClipChordNote(lane: bass, offset: 0, length: 960, pitch: 36, velocity: 90),
       ClipChordNote(lane: lead, offset: 960, length: 480, pitch: 72, velocity: 127)],
      [ClipChordNote(lane: bass, offset: 0, length: 1920, pitch: 45, velocity: nil),
       ClipChordNote(lane: lead, offset: 480, length: 240, pitch: 76, velocity: nil)],
    ], "strip order; velocity 100 (the engine's default) left out")
  }

  func testInactiveNotesAreCarriedWithTheirOffset() {
    let chords = ClipboardChords.chords(of: events, ids: [3], tracks: tracks)
    XCTAssertEqual(chords.first?.notes, [ClipChordNote(lane: bass, offset: 1440, length: 480, pitch: 41, velocity: 80)])
  }

  func testAChordWithoutNotesOrNoTracksCarriesNoNotesKey() throws {
    XCTAssertNil(ClipboardChords.chords(of: events, ids: [1], tracks: nil).first?.notes)
    XCTAssertNil(ClipboardChords.chords(of: events, ids: [1], tracks: []).first?.notes)
    let data = try XCTUnwrap(ClipboardChords.encode(ClipboardChords.chords(of: events, ids: [1], tracks: nil), token: nil))
    XCTAssertFalse(String(decoding: data, as: UTF8.self).contains("notes"), "an entry without notes is the old shape")
  }

  // MARK: - Payload

  func testThePayloadRoundTripsWithNotesAndTheToken() throws {
    let chords = ClipboardChords.chords(of: events, ids: [1, 2, 3], tracks: tracks)
    let data = try XCTUnwrap(ClipboardChords.encode(chords, token: "A"))
    let contents = try XCTUnwrap(ClipboardChords.decodeContents(data))
    XCTAssertEqual(contents.chords, chords)
    XCTAssertEqual(contents.token, "A")
    XCTAssertEqual(ClipboardChords.decode(data), chords)
    let object = try XCTUnwrap(JSONSerialization.jsonObject(with: data) as? [String: Any])
    XCTAssertEqual(object["token"] as? String, "A")
    let note = try XCTUnwrap(((object["chords"] as? [[String: Any]])?.first?["notes"] as? [[String: Any]])?.first)
    XCTAssertEqual(try json(note), #"{"lane":{"name":"Bass","role":"bass","trackId":1},"length":960,"offset":0,"pitch":36,"velocity":90}"#)
  }

  func testAnOlderPayloadWithoutNotesOrTokenStillDecodesAndPastes() throws {
    let older = Data(#"{"schema":1,"chords":[{"root":0,"type":"maj","lengthBeats":4,"velocity":100}]}"#.utf8)
    let contents = try XCTUnwrap(ClipboardChords.decodeContents(older))
    XCTAssertNil(contents.token)
    XCTAssertEqual(contents.chords, [ClipChord(root: 0, type: "maj", lengthBeats: 4, velocity: 100)])
    let board = FakeChordBoard()
    board.raw = older
    XCTAssertEqual(ClipboardChords.paste(from: board, ownToken: "B"), contents.chords)
  }

  func testAnUnknownRoleDecodesAsOtherAndAMistypedNoteRefusesThePayload() throws {
    let entry = #"{"root":0,"type":"maj","lengthBeats":4,"velocity":100,"notes":[{"lane":{"name":"Pad","role":"pad"},"offset":0,"length":480,"pitch":60}]}"#
    let decoded = try XCTUnwrap(ClipboardChords.decode(Data(#"{"schema":1,"chords":[\#(entry)]}"#.utf8)))
    XCTAssertEqual(decoded.first?.notes, [ClipChordNote(lane: ClipLane(trackId: nil, name: "Pad", role: .other), offset: 0,
                                                        length: 480, pitch: 60, velocity: nil)])
    let bad = #"{"root":0,"type":"maj","lengthBeats":4,"velocity":100,"notes":[{"lane":{"name":"Pad","role":"bass"},"offset":"0","length":480,"pitch":60}]}"#
    XCTAssertNil(ClipboardChords.decode(Data(#"{"schema":1,"chords":[\#(bad)]}"#.utf8)), "a mistyped offset")
  }

  // MARK: - Pasting: own instance keeps the track ids, another instance drops them

  func testAPasteFromThisInstanceKeepsTheTrackIds() throws {
    let board = FakeChordBoard()
    XCTAssertTrue(ClipboardChords.copy([1], from: events, tracks: tracks, token: "A", to: board))
    let pasted = try XCTUnwrap(ClipboardChords.paste(from: board, ownToken: "A"))
    XCTAssertEqual(pasted.first?.notes?.map(\.lane), [bass, lead])
  }

  func testAPasteFromAnotherInstanceDropsEveryTrackId() throws {
    let board = FakeChordBoard()
    XCTAssertTrue(ClipboardChords.copy([1, 2], from: events, tracks: tracks, token: "A", to: board))
    let pasted = try XCTUnwrap(ClipboardChords.paste(from: board, ownToken: "B"))
    XCTAssertEqual(pasted.flatMap { $0.notes ?? [] }.map(\.lane),
                   [ClipLane(trackId: nil, name: "Bass", role: .bass), ClipLane(trackId: nil, name: "Lead", role: .lead),
                    ClipLane(trackId: nil, name: "Bass", role: .bass), ClipLane(trackId: nil, name: "Lead", role: .lead)])
    XCTAssertEqual(pasted.map(\.root), [0, 9], "the chords themselves unchanged")
  }

  func testAPayloadWithNotesButNoTokenIsTreatedAsForeign() throws {
    let entry = #"{"root":0,"type":"maj","lengthBeats":4,"velocity":100,"notes":[{"lane":{"trackId":1,"name":"Bass","role":"bass"},"offset":0,"length":480,"pitch":36}]}"#
    let board = FakeChordBoard()
    board.raw = Data(#"{"schema":1,"chords":[\#(entry)]}"#.utf8)
    XCTAssertEqual(ClipboardChords.paste(from: board, ownToken: "A")?.first?.notes?.first?.lane,
                   ClipLane(trackId: nil, name: "Bass", role: .bass))
  }

  func testTheCopyWritesOneItemWithTheTokenAndTheSymbolLine() throws {
    let board = FakeChordBoard()
    XCTAssertTrue(ClipboardChords.copy([2, 1], from: events, tracks: tracks, token: "A", to: board))
    XCTAssertEqual(board.items.count, 1)
    XCTAssertEqual(board.items.first?.text, "C – Am")
    XCTAssertEqual(try ClipboardChords.decodeContents(XCTUnwrap(board.items.first?.data))?.token, "A")
  }

  func testACutRemovesOnlyAfterACheckedWrite() throws {
    let failing = FakeChordBoard()
    failing.accepts = false
    XCTAssertNil(ClipboardChords.cut([1], from: events, tracks: tracks, token: "A", to: failing))
    let board = FakeChordBoard()
    XCTAssertEqual(try json(XCTUnwrap(ClipboardChords.cut([1], from: events, tracks: tracks, token: "A", to: board))),
                   #"{"cmd":"removeChords","ids":[1]}"#)
  }

  // MARK: - The insertChords entries (paste and ⌘D)

  func testTheInsertCommandCarriesTheNotesAsTheEngineTakesThem() throws {
    let chords = ClipboardChords.chords(of: events, ids: [3], tracks: tracks)
    XCTAssertEqual(try json(ClipboardChords.insertCommand(chords, at: 3)),
                   #"{"chords":[{"lengthBeats":1,"notes":[{"lane":{"name":"Bass","role":"bass","trackId":1},"length":480,"offset":1440,"pitch":41,"velocity":80}],"root":5,"type":"maj","velocity":100}],"cmd":"insertChords","index":3}"#)
    let foreign = chords.map { $0.droppingTrackIds() }
    XCTAssertEqual(try json(ClipboardChords.insertCommand(foreign, at: 0)),
                   #"{"chords":[{"lengthBeats":1,"notes":[{"lane":{"name":"Bass","role":"bass"},"length":480,"offset":1440,"pitch":41,"velocity":80}],"root":5,"type":"maj","velocity":100}],"cmd":"insertChords","index":0}"#,
                   "no trackId key: the engine matches by name and role")
  }

  func testDuplicateCarriesTheTwoChordsBassNotesWithTheirTrackIds() throws {
    let command = ClipboardChords.duplicateCommand([1, 2], events: events, tracks: tracks)
    let entries = try XCTUnwrap(command?["chords"] as? [[String: Any]])
    XCTAssertEqual(command?["index"] as? Int, 2, "right after the last of them")
    XCTAssertEqual(entries.count, 2)
    let bassNotes = entries.map { ($0["notes"] as? [[String: Any]] ?? []).filter { ($0["lane"] as? [String: Any])?["trackId"] as? Int == 1 } }
    XCTAssertEqual(bassNotes.map { $0.map { $0["pitch"] as? Int } }, [[36], [45]])
    XCTAssertNil(ClipboardChords.duplicateCommand([99], events: events, tracks: tracks), "nothing to duplicate")
  }
}

/// A pasteboard that keeps what it is given for ChordLab's chord type; `accepts` false makes every write fail;
/// `raw` stands in for bytes left under the type by another build.
private final class FakeChordBoard: ChordPasteboard {
  var accepts = true
  var raw: Data?
  private(set) var items: [ClipboardItem] = []

  func clear() {
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
    return raw ?? items.last { $0.type == type }?.data
  }
}
