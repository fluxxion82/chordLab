import Foundation
import XCTest
@testable import ChordLabUICore

/// The tracks types (UI/Core/Tracks.swift) and the tracks half of the snapshot mirror (UI/Core/Snapshot.swift):
/// roles, the column-wise notes, the guide, the selection helpers and the worst-case decode. The fixture's
/// own values are asserted in SnapshotTests (`testTracksFixture…`).
final class TracksTests: XCTestCase {
  // MARK: - Roles

  func testRolesDecodeFromTheirWireNamesAndAnUnknownOneIsOther() throws {
    func role(_ text: String) throws -> TrackRole {
      try JSONDecoder().decode([TrackRole].self, from: Data("[\"\(text)\"]".utf8))[0]
    }
    XCTAssertEqual(try role("bass"), .bass)
    XCTAssertEqual(try role("lead"), .lead)
    XCTAssertEqual(try role("other"), .other)
    XCTAssertEqual(try role("keys"), .other, "a role this UI does not know never loses the snapshot")
    XCTAssertEqual(try role(""), .other)
  }

  func testARoleEncodesItsRawStringForTheCommands() throws {
    for (role, text) in [(TrackRole.bass, "bass"), (.lead, "lead"), (.other, "other")] {
      let data = try JSONEncoder().encode([role])
      XCTAssertEqual(String(decoding: data, as: UTF8.self), "[\"\(text)\"]")
    }
  }

  func testRoleRegistersAndNamesAreDesignD2sConstants() {
    XCTAssertEqual(TrackRole.bass.register, 28...55)
    XCTAssertEqual(TrackRole.lead.register, 60...84)
    XCTAssertEqual(TrackRole.other.register, 48...72)
    XCTAssertEqual(TrackRole.bass.displayName, "Bass")
    XCTAssertEqual(TrackRole.lead.displayName, "Lead")
    XCTAssertEqual(TrackRole.other.displayName, "Other")
  }

  func testLaneIsHashableAndDistinguishesTracks() {
    XCTAssertEqual(Set([Lane.chords, .track(1), .track(2), .track(1)]).count, 3)
    XCTAssertNotEqual(Lane.chords, .track(0))
  }

  // MARK: - Notes, column-wise

  /// A notes object with `n` notes: id i+1, pitch 40+i, start 100*i, length 50, velocity 100, anchor 1,
  /// offset 100*i, active for all but the last.
  private func notesJSON(_ n: Int, drop column: String? = nil, short: (String, Int)? = nil) -> String {
    func ints(_ f: (Int) -> Int) -> [Int] { (0..<n).map(f) }
    var columns: [(String, [String])] = [
      ("id", ints { $0 + 1 }.map(String.init)), ("pitch", ints { 40 + $0 }.map(String.init)),
      ("start", ints { 100 * $0 }.map(String.init)), ("length", ints { _ in 50 }.map(String.init)),
      ("velocity", ints { _ in 100 }.map(String.init)), ("anchor", ints { _ in 1 }.map(String.init)),
      ("offset", ints { 100 * $0 }.map(String.init)),
      ("active", (0..<n).map { $0 == n - 1 ? "false" : "true" }),
    ]
    if let (name, count) = short {
      columns = columns.map { $0.0 == name ? ($0.0, Array($0.1.prefix(count))) : $0 }
    }
    return "{" + columns.filter { $0.0 != column }.map { "\"\($0.0)\":[\($0.1.joined(separator: ","))]" }.joined(separator: ",") + "}"
  }

  func testNotesDecodeAsColumnsAndBuildANoteOnDemand() throws {
    let notes = try SnapshotTests.decode(TrackNotesJSON.self, notesJSON(3))
    XCTAssertEqual(notes.count, 3)
    XCTAssertEqual(Array(notes.indices), [0, 1, 2])
    XCTAssertEqual(notes.pitch, [40, 41, 42])
    XCTAssertEqual(notes.active, [true, true, false])
    let note = notes[1]
    XCTAssertEqual(note, RollNote(id: 2, pitch: 41, start: 100, length: 50, velocity: 100, anchor: 1, offset: 100, active: true))
    XCTAssertEqual(note.end, 150)
    XCTAssertFalse(notes[2].active)
    XCTAssertEqual(notes.index(ofId: 3), 2)
    XCTAssertNil(notes.index(ofId: 99))
  }

  func testAnEmptyTrackHasNoNotes() throws {
    let notes = try SnapshotTests.decode(TrackNotesJSON.self, notesJSON(0))
    XCTAssertEqual(notes.count, 0)
    XCTAssertTrue(notes.indices.isEmpty)
    XCTAssertNil(notes.index(ofId: 1))
  }

  func testRaggedColumnsCountTheShortestAndNeverTrap() throws {
    for column in ["id", "pitch", "start", "length", "velocity", "anchor", "offset", "active"] {
      let notes = try SnapshotTests.decode(TrackNotesJSON.self, notesJSON(5, short: (column, 2)))
      XCTAssertEqual(notes.count, 2, column)
      XCTAssertEqual(notes[1].id, 2, column)
      XCTAssertEqual(notes.indices.count, 2, column)
    }
  }

  // MARK: - Guide

  private func guide(roles: [String]? = nil, scale: Int = 2741) throws -> GuideJSON {
    let names = ["C", "Db", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"]
    let roles = roles ?? ["root", "outside", "tension", "outside", "chord", "avoid", "outside", "chord", "outside", "tension",
                          "outside", "tension"]
    func array(_ items: [String]) -> String { "[" + items.map { "\"\($0)\"" }.joined(separator: ",") + "]" }
    let json = "{\"roles\":\(array(roles)),\"scale\":\(scale),\"degrees\":\(array(names)),\"names\":\(array(names))}"
    return try SnapshotTests.decode(GuideJSON.self, json)
  }

  func testGuideRolesDecodeAndAnUnknownOneIsOutside() throws {
    let g = try guide(roles: ["root", "chord", "tension", "avoid", "outside", "wobble", "", "root", "chord", "tension", "avoid",
                              "outside"])
    XCTAssertEqual(g.roles, [.root, .chord, .tension, .avoid, .outside, .outside, .outside, .root, .chord, .tension, .avoid,
                             .outside])
  }

  func testGuideLookupsTakePitchClassModTwelveAndAShortArrayIsOutside() throws {
    let g = try guide()
    XCTAssertEqual(g.role(pc: 0), .root)
    XCTAssertEqual(g.role(pc: 12), .root)
    XCTAssertEqual(g.role(pc: 60), .root, "a MIDI note number works as its pitch class")
    XCTAssertEqual(g.role(pc: -1), .tension, "negative values wrap: -1 is B")
    XCTAssertEqual(g.role(pc: 5), .avoid)
    XCTAssertTrue(g.inScale(pc: 0))
    XCTAssertTrue(g.inScale(pc: 2))
    XCTAssertFalse(g.inScale(pc: 1))
    XCTAssertTrue(g.inScale(pc: 59), "59 is B, bit 11")
    XCTAssertFalse(g.inScale(pc: 61))
    XCTAssertFalse(g.inScale(pc: -2), "-2 is Bb")
    let short = try guide(roles: ["root", "chord"])
    XCTAssertEqual(short.role(pc: 1), .chord)
    XCTAssertEqual(short.role(pc: 2), .outside)
    XCTAssertEqual(short.role(pc: 11), .outside)
  }

  func testGuideNamesAndDegreesShowGlyphsInTheDisplayHelperOnly() throws {
    let g = try guide()
    XCTAssertEqual(g.names[1], "Db")
    XCTAssertEqual(g.names[6], "F#")
    XCTAssertEqual(g.displayNames[1], "D♭")
    XCTAssertEqual(g.displayNames[6], "F♯")
    XCTAssertEqual(g.displayNames[0], "C")
    XCTAssertEqual(g.displayName(pc: 13), "D♭")
    XCTAssertEqual(g.displayName(pc: 6), "F♯")
    XCTAssertEqual(g.displayDegrees.count, 12)
    let degrees = try SnapshotTests.decode(GuideJSON.self, """
      {"roles":[],"scale":0,"degrees":["R","b9","#11","bb7"],"names":[]}
      """).displayDegrees
    XCTAssertEqual(degrees, ["R", "♭9", "♯11", "♭♭7"])
  }

  // MARK: - Snapshot helpers

  private func snapshot(tracks: String? = nil, selection: String? = nil, ticks: String? = nil) throws -> Snapshot {
    var json = SnapshotTests.phase2
    var extra: [String] = []
    if let tracks { extra.append("\"tracks\":\(tracks)") }
    if let selection { extra.append("\"noteSelection\":\(selection)") }
    if let ticks { extra.append("\"ticksPerBeat\":\(ticks)") }
    if !extra.isEmpty {
      json = json.replacingOccurrences(of: "\"palette\":", with: extra.joined(separator: ",") + ",\"palette\":")
    }
    return try SnapshotTests.decode(Snapshot.self, json)
  }

  private func trackJSON(_ id: Int, role: String = "bass", noteCount: Int = 2) -> String {
    "{\"id\":\(id),\"name\":\"T\(id)\",\"role\":\"\(role)\",\"mute\":false,\"solo\":false,\"audible\":true,\"notes\":\(notesJSON(noteCount))}"
  }

  func testTrackLookupAndSelectedIds() throws {
    let lead = trackJSON(2, role: "lead")
    let s = try snapshot(tracks: "[\(trackJSON(1)),\(lead)]", selection: "{\"trackId\":2,\"ids\":[1,2]}")
    XCTAssertEqual(s.track(id: 2)?.role, .lead)
    XCTAssertEqual(s.track(id: 1)?.name, "T1")
    XCTAssertNil(s.track(id: 3))
    let one = try XCTUnwrap(s.track(id: 1)), two = try XCTUnwrap(s.track(id: 2))
    XCTAssertEqual(one.selectedIds(in: s.noteSelection), [], "the selection is of another track")
    XCTAssertEqual(two.selectedIds(in: s.noteSelection), [1, 2])
    XCTAssertEqual(two.selectedIds(in: nil), [])
  }

  func testNullAndAbsentSelectionAreNil() throws {
    XCTAssertNil(try snapshot(tracks: "[]", selection: "null").noteSelection)
    XCTAssertNil(try snapshot(tracks: "[]").noteSelection)
  }

  func testTrackLookupWithoutTracks() throws {
    XCTAssertNil(try snapshot().track(id: 1))
    XCTAssertNil(try snapshot().tracks)
    XCTAssertEqual(try snapshot(tracks: "[]").tracks?.count, 0)
  }

  func testTicksPerBeatDefaultsTo960() throws {
    XCTAssertEqual(try snapshot().ticksPerBeatOrDefault, 960)
    XCTAssertEqual(try snapshot(ticks: "480").ticksPerBeatOrDefault, 480)
  }

  func testAnUnknownRoleInATrackDecodesAsOther() throws {
    let s = try snapshot(tracks: "[\(trackJSON(1, role: "pad"))]")
    XCTAssertEqual(s.tracks?.first?.role, .other)
  }

  // MARK: - Notes as a collection

  private func notesOfTrack(ids: [Int]) -> TrackNotesJSON {
    TrackNotesJSON(id: ids, pitch: ids.map { 60 + $0 }, start: ids.map { $0 * 480 }, length: ids.map { _ in 240 },
                   velocity: ids.map { _ in 90 }, anchor: ids.map { _ in 1 }, offset: ids.map { $0 * 480 },
                   active: ids.map { _ in true })
  }

  func testNotesAreARandomAccessCollectionOfRollNotes() {
    let notes = notesOfTrack(ids: [7, 3, 9, 4])
    XCTAssertEqual(notes.startIndex, 0)
    XCTAssertEqual(notes.endIndex, 4)
    XCTAssertEqual(notes.map(\.id), [7, 3, 9, 4])
    XCTAssertEqual(notes.reversed().map(\.id), [4, 9, 3, 7])
    XCTAssertEqual(notes.first(where: { $0.pitch > 62 })?.id, 7)
    XCTAssertEqual(notes.last?.id, 4)
    XCTAssertEqual(notes.index(notes.startIndex, offsetBy: 2), 2)
    XCTAssertEqual(notes[1].pitch, 63)
  }

  func testARaggedColumnSetShortensTheCollection() {
    var notes = notesOfTrack(ids: [1, 2, 3])
    notes = TrackNotesJSON(id: notes.id, pitch: Array(notes.pitch.prefix(2)), start: notes.start, length: notes.length,
                           velocity: notes.velocity, anchor: notes.anchor, offset: notes.offset, active: notes.active)
    XCTAssertEqual(Array(notes).map(\.id), [1, 2])
    XCTAssertEqual(notes.endIndex, 2)
  }

  func testIndicesOfIdsAreAscendingNoteOrderAndSkipUnknownIds() {
    let notes = notesOfTrack(ids: [7, 3, 9, 4])
    XCTAssertEqual(notes.indices(ofIds: [4, 7, 100]), [0, 3])
    XCTAssertEqual(notes.indices(ofIds: [9, 3, 7, 4]), [0, 1, 2, 3])
    XCTAssertEqual(notes.indices(ofIds: []), [])
    XCTAssertEqual(notes.indices(ofIds: [55]), [])
    XCTAssertEqual(notes.index(ofId: 9), 2)
  }

  // MARK: - Lane fallback

  func testALaneResolvesToItselfWhileItsTrackExists() throws {
    let s = try snapshot(tracks: "[\(trackJSON(1)), \(trackJSON(4))]")
    XCTAssertEqual(Lane.chords.resolved(in: s.tracks), .chords)
    XCTAssertEqual(Lane.track(4).resolved(in: s.tracks), .track(4))
    XCTAssertEqual(Lane.track(1).resolved(in: s.tracks), .track(1))
  }

  func testALaneOfARemovedTrackFallsBackToChords() throws {
    let s = try snapshot(tracks: "[\(trackJSON(1))]")
    XCTAssertEqual(Lane.track(2).resolved(in: s.tracks), .chords)
    XCTAssertEqual(Lane.track(2).resolved(in: []), .chords)
  }

  func testALaneWithoutTracksFallsBackToChords() {
    XCTAssertEqual(Lane.track(1).resolved(in: nil), .chords)
    XCTAssertEqual(Lane.chords.resolved(in: nil), .chords)
  }

  func testTrackRolesAreCaseIterableInMenuOrder() {
    XCTAssertEqual(TrackRole.allCases, [.bass, .lead, .other])
  }

  // MARK: - Worst case

  /// The decode the roll's note edits wait on (design D6): 256 events each with a guide and a play span, and four
  /// tracks of 1024 notes, column-wise. The first decode is a plain `try`, so a failure shows the decode error and
  /// the counts are the assertion; `measure` is informational timing only. This synthetic payload (~321 KB)
  /// underestimates the real worst snapshot (longer names, ids and notes), so the number the design records for D6
  /// comes from `engine/tools/latency_bench/decode_bench.sh` on the bench's dump, not from here.
  func testTheWorstCaseSnapshotDecodes() throws {
    let url = try SnapshotTests.fixturesDirectory().appendingPathComponent("snapshot-tracks.json")
    var object = try XCTUnwrap(JSONSerialization.jsonObject(with: Data(contentsOf: url)) as? [String: Any])
    let events = try XCTUnwrap(object["events"] as? [[String: Any]])
    let template = events[0]
    object["events"] = (0..<256).map { i -> [String: Any] in
      var event = template
      event["id"] = i + 1
      event["startBeat"] = Double(i) * 4
      event["playStart"] = Double(i) * 4
      event["playEnd"] = Double(i) * 4 + 4
      return event
    }
    let notesPerTrack = 1024
    func column(_ f: (Int) -> Int) -> [Int] { (0..<notesPerTrack).map(f) }
    object["tracks"] = (1...4).map { t -> [String: Any] in
      [
        "id": t, "name": "Track \(t)", "role": ["bass", "lead", "other", "bass"][t - 1], "mute": t == 2, "solo": false,
        "audible": t != 2,
        "notes": [
          "id": column { t * 10_000 + $0 }, "pitch": column { 36 + $0 % 48 }, "start": column { $0 * 960 },
          "length": column { _ in 480 }, "velocity": column { 60 + $0 % 60 }, "anchor": column { $0 % 256 + 1 },
          "offset": column { $0 % 4 * 240 }, "active": (0..<notesPerTrack).map { $0 % 17 != 0 },
        ] as [String: Any],
      ]
    }
    let data = try JSONSerialization.data(withJSONObject: object)
    print("worst-case snapshot JSON: \(data.count) bytes")

    let decoded = try JSONDecoder().decode(Snapshot.self, from: data)
    measure {
      _ = try? JSONDecoder().decode(Snapshot.self, from: data)
    }
    XCTAssertEqual(decoded.events.count, 256)
    XCTAssertTrue(decoded.events.allSatisfy { $0.guide?.roles.count == 12 && $0.playEnd == $0.playStart.map { $0 + 4 } })
    XCTAssertEqual(decoded.tracks?.count, 4)
    for track in decoded.tracks ?? [] {
      XCTAssertEqual(track.notes.count, notesPerTrack, track.name)
      XCTAssertEqual(track.notes.id.count, notesPerTrack)
      XCTAssertEqual(track.notes.active.count, notesPerTrack)
    }
    XCTAssertEqual(decoded.tracks?[2].notes[5].pitch, 41)
    XCTAssertEqual(decoded.tracks?[3].notes.index(ofId: 40_000 + 1023), 1023)
  }
}
