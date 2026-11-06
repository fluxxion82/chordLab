import Foundation
import XCTest
@testable import ChordLabUICore

/// The heard message, its display lines and the palette match (UI/Core/Heard.swift, HeardText.swift,
/// PaletteMatch.swift), plus the snapshot's `midiThru` field.
final class HeardTests: XCTestCase {
  /// The engine's `heardJson` payload with a chord and a key (design D14).
  static let full = """
    {"schema":1,"active":true,"held":[57,60,64,67],"heldNames":["A","C","E","G"],
     "chord":{"bass":57,"inversion":0,"roman":"vi7","root":9,"symbol":"Am7","type":"m7"},
     "chordHeld":true,
     "key":{"tonic":{"letter":"A","alter":0},"mode":"Aeolian","name":"A Aeolian","percent":87},"onsets":12}
    """

  /// The same message before anything is identified: `chord` and `key` are null, `active` is false.
  static let empty = """
    {"schema":1,"active":false,"held":[],"heldNames":[],"chord":null,"chordHeld":false,"key":null,"onsets":0}
    """

  private func decode(_ json: String) throws -> HeardJSON {
    try JSONDecoder().decode(HeardJSON.self, from: Data(json.utf8))
  }

  private func heard(onsets: Int = 0, names: [String] = [], chord: HeardChordJSON? = nil,
                     key: HeardKeyJSON? = nil) -> HeardJSON {
    HeardJSON(schema: 1, active: true, held: [], heldNames: names, chord: chord, chordHeld: chord != nil,
              key: key, onsets: onsets)
  }

  private func chord(_ symbol: String, _ roman: String, root: Int = 0, type: String = "maj") -> HeardChordJSON {
    HeardChordJSON(root: root, type: type, symbol: symbol, roman: roman, inversion: 0, bass: 60 + root)
  }

  private func key(_ letter: String, _ alter: Int, _ mode: String, _ percent: Int) -> HeardKeyJSON {
    HeardKeyJSON(tonic: TonicJSON(letter: letter, alter: alter), mode: mode, name: "", percent: percent)
  }

  // MARK: - Decoding

  func testDecodesTheFullPayload() throws {
    let h = try decode(Self.full)
    XCTAssertEqual(h.schema, 1)
    XCTAssertTrue(h.active)
    XCTAssertEqual(h.held, [57, 60, 64, 67])
    XCTAssertEqual(h.heldNames, ["A", "C", "E", "G"])
    XCTAssertEqual(h.chord, HeardChordJSON(root: 9, type: "m7", symbol: "Am7", roman: "vi7", inversion: 0, bass: 57))
    XCTAssertTrue(h.chordHeld)
    XCTAssertEqual(h.key, HeardKeyJSON(tonic: TonicJSON(letter: "A", alter: 0), mode: "Aeolian",
                                       name: "A Aeolian", percent: 87))
    XCTAssertEqual(h.onsets, 12)
  }

  func testDecodesNullChordAndKey() throws {
    let h = try decode(Self.empty)
    XCTAssertFalse(h.active)
    XCTAssertNil(h.chord)
    XCTAssertNil(h.key)
    XCTAssertFalse(h.chordHeld)
    XCTAssertEqual(h.held, [])
    XCTAssertEqual(h.onsets, 0)
  }

  // MARK: - keyLine

  func testKeyLineOfAMinor() throws {
    XCTAssertEqual(HeardText.keyLine(try decode(Self.full)), "A Minor · 87%")
  }

  func testKeyLineSpellsFlatTonicsWithGlyphs() {
    XCTAssertEqual(HeardText.keyLine(heard(key: key("E", -1, "Ionian", 71))), "E♭ Major · 71%")
  }

  func testKeyLineIsNilWithoutAKey() throws {
    XCTAssertNil(HeardText.keyLine(try decode(Self.empty)))
  }

  // MARK: - listeningLine

  func testListeningLineWithNoOnsets() {
    XCTAssertEqual(HeardText.listeningLine(heard(onsets: 0)), "listening")
  }

  func testListeningLineWithOneNote() {
    XCTAssertEqual(HeardText.listeningLine(heard(onsets: 1)), "listening · 1 note")
  }

  func testListeningLineWithSeveralNotes() {
    XCTAssertEqual(HeardText.listeningLine(heard(onsets: 5)), "listening · 5 notes")
  }

  // MARK: - chordLine

  func testChordLineOfAm7() throws {
    XCTAssertEqual(HeardText.chordLine(try decode(Self.full)), "Am7 · vi7")
  }

  func testChordLineHasGlyphsOnBothParts() {
    let c = chord("F#m7b5", "#ivø7", root: 6, type: "m7b5")
    XCTAssertEqual(HeardText.chordLine(heard(chord: c)), "F♯m7♭5 · ♯ivø7")
  }

  func testChordLineIsNilWithoutAChord() throws {
    XCTAssertNil(HeardText.chordLine(try decode(Self.empty)))
  }

  // MARK: - heldLine

  func testHeldLineJoinsNamesWithGlyphs() {
    XCTAssertEqual(HeardText.heldLine(heard(names: ["C", "E", "G#"])), "C, E, G♯")
  }

  func testHeldLineFlat() {
    XCTAssertEqual(HeardText.heldLine(heard(names: ["Ab"])), "A♭")
  }

  func testHeldLineIsEmptyWithNoNames() {
    XCTAssertEqual(HeardText.heldLine(heard(names: [])), "")
  }

  // MARK: - PaletteMatch

  private func fixtureCells() throws -> [CellJSON] {
    let url = try SnapshotTests.fixturesDirectory().appendingPathComponent("snapshot-home-empty.json")
    return try JSONDecoder().decode(Snapshot.self, from: Data(contentsOf: url)).palette.cells
  }

  func testHeardIndexFindsAm7InTheFixturePalette() throws {
    let cells = try fixtureCells()
    let am7 = HeardChordJSON(root: 9, type: "m7", symbol: "Am7", roman: "vi7", inversion: 0, bass: 57)
    let index = try XCTUnwrap(PaletteMatch.heardIndex(cells, heard: am7))
    XCTAssertEqual(index, 18)                                   // tier 2, vi7 of C Ionian
    XCTAssertEqual(cells[index].symbol, "Am7")
  }

  func testHeardIndexMatchesRootAndTypeNotTheInversion() throws {
    let cells = try fixtureCells()
    let inverted = HeardChordJSON(root: 9, type: "m7", symbol: "Am7", roman: "vi7", inversion: 1, bass: 60)
    XCTAssertEqual(PaletteMatch.heardIndex(cells, heard: inverted), 18)
  }

  func testHeardIndexIsNilForAChordNotInTheCells() throws {
    let cells = try fixtureCells()
    // The root is in the palette (Am, Am7...), the type is not.
    let odd = HeardChordJSON(root: 9, type: "dim7", symbol: "Adim7", roman: "vi°7", inversion: 0, bass: 57)
    XCTAssertNil(PaletteMatch.heardIndex(cells, heard: odd))
  }

  func testHeardIndexIsNilWithoutAChord() throws {
    XCTAssertNil(PaletteMatch.heardIndex(try fixtureCells(), heard: nil))
  }

  func testHeardIndexTakesTheFirstOfTwoMatches() {
    let cell = { (tier: Int, degree: Int) in
      CellJSON(degree: degree, tier: tier, root: 9, type: "m7", symbol: "Am7", roman: "vi7", family: "Minor",
               filled: true, voicing: [], voicingNames: nil, outOfKey: nil, outOfKeyNames: nil, function: nil,
               tension: nil, engineId: nil, relation: nil, relationText: nil, label: nil, target: nil)
    }
    let am7 = HeardChordJSON(root: 9, type: "m7", symbol: "Am7", roman: "vi7", inversion: 0, bass: 57)
    XCTAssertEqual(PaletteMatch.heardIndex([cell(0, 5), cell(1, 5)], heard: am7), 0)
  }

  // MARK: - Snapshot.midiThru

  func testSnapshotMidiThruDecodesTrueAndFalse() throws {
    let on = SnapshotTests.phase1.replacingOccurrences(of: "\"followHost\":true,", with: "\"followHost\":true,\"midiThru\":true,")
    let off = SnapshotTests.phase1.replacingOccurrences(of: "\"followHost\":true,", with: "\"followHost\":true,\"midiThru\":false,")
    XCTAssertEqual(try SnapshotTests.decode(Snapshot.self, on).midiThru, true)
    XCTAssertEqual(try SnapshotTests.decode(Snapshot.self, off).midiThru, false)
  }

  func testSnapshotMidiThruIsNilFromAnOlderPlugin() throws {
    XCTAssertNil(try SnapshotTests.decode(Snapshot.self, SnapshotTests.phase1).midiThru)
    let url = try SnapshotTests.fixturesDirectory().appendingPathComponent("snapshot-legacy-phase1.json")
    XCTAssertNil(try JSONDecoder().decode(Snapshot.self, from: Data(contentsOf: url)).midiThru)
  }

  func testEngineFixtureCarriesMidiThru() throws {
    let url = try SnapshotTests.fixturesDirectory().appendingPathComponent("snapshot-home-empty.json")
    XCTAssertEqual(try JSONDecoder().decode(Snapshot.self, from: Data(contentsOf: url)).midiThru, true)
  }
}
